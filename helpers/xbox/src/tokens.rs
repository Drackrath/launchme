// Account state and the Microsoft Store tickets every account service needs: the device and the
// user MSA compact tickets for www.microsoft.com (as the Gaming Services licensing client asks).

use serde_json::json;
use xodus::models::live::ExchangeUserTokenOutcome;
use xodus::models::secrets::{LegacyToken, Token};
use xodus::models::soap;
use xodus::tokens::TokenManager;

use crate::output::{Failure, Outcome};

pub const HOSTING_APP: &str = "{d6d5a677-0872-4ab0-9442-bb792fce85c5}";
pub const MSA_SCOPE: &str = "www.microsoft.com";

pub fn status(tokens: &TokenManager) -> Outcome {
    match (tokens.get_user(), tokens.get_user_sts_token()) {
        (Ok(user), Ok(_)) => Ok(json!({
            "signed_in": true,
            "username": user.username,
            "puid": user.puid,
            "device": tokens.get_device_license().is_ok(),
        })),
        _ => Ok(json!({ "signed_in": false, "device": tokens.get_device_license().is_ok() })),
    }
}

// Signs the user out; the device registration stays (the next sign-in reuses it).
pub fn logout(tokens: &TokenManager) -> Outcome {
    tokens
        .remove_persistent()
        .map_err(|e| Failure::new("storage", e.to_string()))?;
    Ok(json!({ "signed_in": false }))
}

// An STS token whose lifetime ends within five minutes (or cannot be read) is renewed first.
fn expiring(token: &LegacyToken) -> bool {
    chrono::DateTime::parse_from_rfc3339(&token.lifetime.expires)
        .map(|expires| expires.with_timezone(&chrono::Utc) < chrono::Utc::now() + chrono::Duration::minutes(5))
        .unwrap_or(false)
}

pub async fn ensure_device(client: &reqwest::Client, tokens: &TokenManager) -> Result<LegacyToken, Failure> {
    for attempt in 0..2 {
        xodus::tokens::device::ensure_device_credentials(client, tokens).await;
        match tokens.get_device_sts_token() {
            Ok(Token::Legacy(token)) if attempt == 0 && expiring(&token) => crate::store::forget_device_token(),
            Ok(Token::Legacy(token)) => return Ok(token),
            Ok(_) => return Err(Failure::new("internal", "unexpected device token type")),
            Err(e) => return Err(Failure::new("network", format!("device registration: {e}"))),
        }
    }
    Err(Failure::new("network", "device sign-in"))
}

pub struct Signed {
    pub device: LegacyToken,
    pub user: LegacyToken,
    pub username: String,
    pub puid: String,
}

pub async fn signed_in(client: &reqwest::Client, tokens: &TokenManager) -> Result<Signed, Failure> {
    let (Ok(user), Ok(sts)) = (tokens.get_user(), tokens.get_user_sts_token()) else {
        return Err(Failure::new("signed_out", "not signed in"));
    };
    let Token::Legacy(sts) = sts else {
        return Err(Failure::new("signed_out", "unsupported user token, sign in again"));
    };
    let device = ensure_device(client, tokens).await?;
    Ok(Signed {
        device,
        user: sts,
        username: user.username,
        puid: user.puid,
    })
}

pub struct StoreTickets {
    pub device: String,
    pub user: String,
    pub puid: String,
}

fn compact(token: Token) -> Result<String, Failure> {
    match token {
        Token::Compact(value) => Ok(value),
        Token::Legacy(_) => Err(Failure::new("internal", "expected a compact ticket")),
    }
}

pub async fn store_tickets(client: &reqwest::Client, tokens: &TokenManager) -> Result<StoreTickets, Failure> {
    let signed = signed_in(client, tokens).await?;
    let device = xodus::api::live::exchange_device_token(
        client,
        signed.device.clone(),
        HOSTING_APP.to_string(),
        MSA_SCOPE.to_string(),
        Some(soap::PolicyReference::mbi_ssl()),
    )
    .await
    .map_err(Failure::network)?;
    let device = compact(device.into())?;

    let outcome = xodus::api::live::exchange_user_token(
        client,
        signed.user,
        signed.username,
        signed.device,
        None,
        Some("Silent".to_string()),
        HOSTING_APP.to_string(),
        &[(MSA_SCOPE.to_string(), Some(soap::PolicyReference::mbi_ssl()))],
    )
    .await
    .map_err(Failure::network)?;
    let user: Token = match outcome {
        ExchangeUserTokenOutcome::Issued(soap::BodyContent::RequestSecurityTokenResponseCollection(mut c))
            if !c.security_tokens.is_empty() =>
        {
            c.security_tokens.remove(0).into()
        }
        ExchangeUserTokenOutcome::Issued(soap::BodyContent::RequestSecurityTokenResponse(t)) => (*t).into(),
        _ => return Err(Failure::new("signed_out", "the session expired, sign in again")),
    };
    Ok(StoreTickets {
        device,
        user: compact(user)?,
        puid: signed.puid,
    })
}
