// Content license → the package's full AES key, as xodus-cli's license.rs (GPL-3.0, the Xodus
// authors). Asked for every install and every launch, so an expired Game Pass entitlement stops
// working like it does in the Xbox app.

use xodus::licensing::content::{LicenseContentError, get_license_content};
use xodus::licensing::splicense::SPLicense;
use xodus::tokens::TokenManager;

use crate::output::Failure;
use crate::tokens::store_tickets;

pub async fn content_key(client: &reqwest::Client, tokens: &TokenManager, content_id: &str, market: &str) -> Result<[u8; 32], Failure> {
    let tickets = store_tickets(client, tokens).await?;
    let (_content, license) = get_license_content(
        client,
        tickets.device,
        tickets.user,
        tickets.puid,
        content_id.to_string(),
        market.to_string(),
    )
    .await
    .map_err(|e| match e {
        LicenseContentError::NotEntitled { description } => Failure::new("not_entitled", description),
        other => Failure::network(other),
    })?;
    let game = SPLicense::parse_base64(&license.splicense_block)
        .map_err(|_| Failure::new("internal", "unreadable game license"))?;
    let device = tokens
        .get_device_license()
        .map_err(|e| Failure::new("storage", e.to_string()))?;
    let device = SPLicense::parse_base64(&device.splicense)
        .map_err(|_| Failure::new("internal", "unreadable device license"))?;
    let key = device
        .encrypted_device_key
        .ok_or_else(|| Failure::new("internal", "device license without a key"))?
        .derive_device_key();
    if game.content_keys.len() != 1 {
        return Err(Failure::new(
            "unsupported_package",
            format!("{} content keys", game.content_keys.len()),
        ));
    }
    let (_, content_key) = game.content_keys.into_iter().next().expect("one key");
    let full = content_key
        .unpack(&key)
        .map_err(|_| Failure::new("internal", "content key did not unpack"))?;
    Ok(*full)
}
