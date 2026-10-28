// Microsoft account sign-in on Linux, as xodus-cli's `login` there (GPL-3.0, the Xodus authors) and
// as login.rs on Windows: the Windows "TokenBroker" inline login page in a web view whose host
// bridge (window.external.notify) hands over the DA token, exchanged for the user's STS tokens.
// The web view is GTK 4 + WebKitGTK 6.0 (Xodus uses wry, i.e. WebKitGTK 4.1 with GTK 3; this is the
// WebKit Launchme's own browser window uses). Its cookies live in <profile>/webview.

use std::cell::RefCell;
use std::path::Path;
use std::rc::Rc;
use std::time::Duration;

use gtk4::glib;
use gtk4::prelude::*;
use serde_json::json;
use webkit6::prelude::*;
use xodus::models::live::{DAProperty, ExchangeUserTokenOutcome, HostBridgeMessage};
use xodus::models::secrets::{self, LegacyToken};
use xodus::models::soap;
use xodus::tokens::TokenManager;

use crate::output::{Failure, Outcome};

const CLIENT_ID: &str = "000000004424da1f";
const LOGIN_MARKET: &str = "en-US";
const USER_AUTH_SCOPE: &str = "scope=service::user.auth.xboxlive.com::MBI_SSL&api-version=2.0";
const TITLE: &str = "Launchme · Xbox sign-in";
const USER_AGENT: &str = "Mozilla/5.0 (Windows NT 10.0; Win64; x64; MSAppHost/3.0) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/70.0.3538.102 Safari/537.36 Edge/18.26100";
// The page talks to its host through window.external.notify; the helper's own scripts post through
// window.ipc. Both reach the "ipc" script message handler.
const BRIDGE: &str = "(() => { const post = (m) => window.webkit.messageHandlers.ipc.postMessage(String(m)); \
    window.external = { notify: post }; window.ipc = { postMessage: post }; })();";
const CAPABILITIES: &str = r#"{"PrivatePropertyBag":1,"PasswordlessConnect":1,"PreferAssociate":1,"ChromelessUI":0}"#;

enum Message {
    Script(String), // A message the page posted (window.external.notify / window.ipc.postMessage).
    Finish,
    Closed,
}

struct SignIn {
    window: gtk4::Window,
    view: webkit6::WebView,
    messages: Rc<RefCell<Vec<Message>>>,
}

// The window and its web view: the host bridge, the TokenBroker's user agent, cookies in
// <profile>/webview. GTK must be initialized on this thread.
fn open(profile: &Path) -> SignIn {
    let messages: Rc<RefCell<Vec<Message>>> = Rc::default();
    let data = profile.join("webview");
    let session = webkit6::NetworkSession::new(data.join("data").to_str(), data.join("cache").to_str());
    let content = webkit6::UserContentManager::new();
    content.add_script(&webkit6::UserScript::new(
        BRIDGE,
        webkit6::UserContentInjectedFrames::AllFrames,
        webkit6::UserScriptInjectionTime::Start,
        &[],
        &[],
    ));
    content.register_script_message_handler("ipc", None);
    {
        let messages = messages.clone();
        content.connect_script_message_received(Some("ipc"), move |_, value| {
            messages.borrow_mut().push(Message::Script(value.to_str().to_string()));
        });
    }
    let view = webkit6::WebView::builder()
        .network_session(&session)
        .user_content_manager(&content)
        .build();
    if let Some(settings) = WebViewExt::settings(&view) {
        settings.set_user_agent(Some(USER_AGENT));
    }
    {
        let messages = messages.clone();
        view.connect_load_changed(move |view, event| {
            if event == webkit6::LoadEvent::Finished
                && view.uri().is_some_and(|uri| uri.starts_with("https://login.live.com/ppsecure/post.srf"))
            {
                messages.borrow_mut().push(Message::Finish);
            }
        });
    }
    let window = gtk4::Window::builder()
        .title(TITLE)
        .default_width(500)
        .default_height(700)
        .resizable(false)
        .child(&view)
        .build();
    {
        let messages = messages.clone();
        window.connect_close_request(move |_| {
            messages.borrow_mut().push(Message::Closed);
            glib::Propagation::Proceed
        });
    }
    window.present();
    SignIn { window, view, messages }
}

fn pump() {
    let context = glib::MainContext::default();
    while context.pending() {
        context.iteration(false);
    }
}

fn login_headers() -> Vec<(&'static str, String)> {
    vec![
        ("cxh-capabilities", CAPABILITIES.into()),
        ("cxh-correlationId", uuid::Uuid::new_v4().to_string()),
        ("cxh-msaBinaryVersion", "55".into()),
        ("cxh-identityClientBinaryVersion", "3".into()),
        ("cxh-osVersionInfo", r#"{"platformId":2,"majorVersion":10,"minorVersion":0,"buildNumber":26100}"#.into()),
        ("cxh-platform", "CloudExperienceHost.Platform.DESKTOP".into()),
        ("cxh-protocol", "TokenBroker".into()),
        ("cxh-source", "TokenBroker".into()),
        ("hostApp", "CloudExperienceHost".into()),
    ]
}

fn login_url() -> String {
    format!(
        "https://login.live.com/ppsecure/InlineLogin.srf?id=80604&scid=3&mkt={LOGIN_MARKET}&Platform=Windows10&clientid={CLIENT_ID}&hosted=1"
    )
}

fn load(view: &webkit6::WebView, url: &str, headers: &[(&'static str, String)]) {
    let request = webkit6::URIRequest::new(url);
    if let Some(fields) = request.http_headers() {
        for (name, value) in headers {
            fields.append(name, value);
        }
    }
    view.load_request(&request);
}

async fn exchange(
    client: &reqwest::Client,
    device: LegacyToken,
    data: &DAProperty,
    finish: bool,
) -> Result<ExchangeUserTokenOutcome, Failure> {
    let user_token = LegacyToken {
        key_name: None,
        token: data.da_token.clone(),
        binary_secret: None,
        tpm_key: None,
        lifetime: soap::Timestamp {
            id: None,
            created: data.da_start_time.clone(),
            expires: data.da_expires.clone(),
        },
    };
    let mut scopes = vec![(USER_AUTH_SCOPE.to_string(), Some(soap::PolicyReference::token_broker()))];
    if finish {
        scopes.push(("http://Passport.NET/tb".to_string(), None));
    }
    xodus::api::live::exchange_user_token(
        client,
        user_token,
        data.username.clone(),
        device,
        Some(data.sts_inline_flow_token.clone()),
        None,
        CLIENT_ID.to_string(),
        &scopes,
    )
    .await
    .map_err(Failure::network)
}

pub async fn run(client: &reqwest::Client, tokens: &TokenManager, profile: &Path) -> Outcome {
    let device = crate::tokens::ensure_device(client, tokens).await?;
    gtk4::init().map_err(|e| Failure::new("internal", format!("GTK: {e} (no display?)")))?;
    let SignIn { window, view, messages } = open(profile);
    load(&view, &login_url(), &login_headers());

    let evaluate = |script: &str| view.evaluate_javascript(script, None, None, None::<&gtk4::gio::Cancellable>, |_| {});
    let mut finish = false;
    let mut user: Option<secrets::User> = None;
    let mut result: Option<Result<soap::BodyContent, Failure>> = None;
    while result.is_none() {
        pump();
        let pending: Vec<Message> = std::mem::take(&mut *messages.borrow_mut());
        for message in pending {
            if result.is_some() {
                break;
            }
            match message {
                Message::Closed => result = Some(Err(Failure::new("cancelled", "sign-in window closed"))),
                Message::Finish => evaluate("window.ipc.postMessage(JSON.stringify(ServerData))"),
                Message::Script(body) => {
                    // As the Windows window's IPC handler: the DA token, else the bridge's getContext call.
                    let Ok(data) = serde_json::from_str::<DAProperty>(&body) else {
                        if let Ok(message) = serde_json::from_str::<HostBridgeMessage>(&body)
                            && let Some(ctx) = message.get_context_invoke()
                        {
                            evaluate(&format!(
                                r#"window["CloudExperienceHost.Bridge.dispatchMessage"](JSON.stringify({{"type": "callback", "value": {{ "name": "CloudExperienceHost.getContext", "args": ["CloudExperienceHost", "TokenBroker", "TokenBroker", "{{\"PrivatePropertyBag\":1,\"PasswordlessConnect\":1,\"PreferAssociate\":1,\"ChromelessUI\":0}}"], "context": "{ctx}"}}}}))"#
                            ));
                        }
                        continue;
                    };
                    match exchange(client, device.clone(), &data, finish).await {
                        Err(e) => result = Some(Err(e)),
                        Ok(ExchangeUserTokenOutcome::Fault(fault)) => {
                            if let Some(url) = fault.and_then(|f| f.inline_auth_url) {
                                // Second step (consent / proofs): the page finishes the flow itself.
                                finish = true;
                                load(&view, &url, &[]);
                            } else {
                                result = Some(Err(Failure::new("signed_out", "Microsoft refused the token exchange")));
                            }
                        }
                        Ok(ExchangeUserTokenOutcome::Issued(body)) => {
                            user = Some(secrets::User {
                                puid: data.puid.clone(),
                                username: data.username.clone(),
                            });
                            result = Some(Ok(body));
                        }
                    }
                }
            }
        }
        tokio::time::sleep(Duration::from_millis(16)).await;
    }
    window.destroy();
    pump();

    let body = result.unwrap_or_else(|| Err(Failure::new("cancelled", "sign-in ended")))?;
    let issued = match body {
        soap::BodyContent::RequestSecurityTokenResponseCollection(collection) => collection.security_tokens,
        soap::BodyContent::RequestSecurityTokenResponse(token) => vec![*token],
        _ => return Err(Failure::new("internal", "unexpected sign-in answer")),
    };
    let user = user.ok_or_else(|| Failure::new("internal", "no user"))?;
    for token in issued {
        let address = token.applies_to.endpoint_reference.address.clone();
        let token: secrets::Token = token.into();
        let address = match &token {
            secrets::Token::Legacy(legacy) => legacy.key_name.clone().unwrap_or(address),
            _ => address,
        };
        tokens
            .save_user_token(address, token)
            .map_err(|e| Failure::new("storage", e.to_string()))?;
    }
    tokens
        .save_user(&user)
        .map_err(|e| Failure::new("storage", e.to_string()))?;
    Ok(json!({ "signed_in": true, "username": user.username, "puid": user.puid }))
}

#[cfg(test)]
mod tests {
    use super::*;

    // The window and the page's host bridge, offline (a local page, no Microsoft service). Needs a
    // display: cargo test -- --ignored --test-threads=1
    #[test]
    #[ignore = "needs a display (X11 or Wayland)"]
    fn the_pages_bridge_reaches_the_helper() {
        gtk4::init().expect("GTK");
        let profile = std::env::temp_dir().join(format!("launchme-xbox-signin-{}", std::process::id()));
        let SignIn { window, view, messages } = open(&profile);
        view.load_html(
            "<html><body><script>window.external.notify(JSON.stringify({ping: navigator.userAgent}));</script></body></html>",
            Some("https://login.live.com/"),
        );
        let until = std::time::Instant::now() + Duration::from_secs(30);
        let mut received = None;
        while received.is_none() && std::time::Instant::now() < until {
            pump();
            for message in std::mem::take(&mut *messages.borrow_mut()) {
                if let Message::Script(body) = message {
                    received = Some(body);
                }
            }
            std::thread::sleep(Duration::from_millis(16));
        }
        window.destroy();
        pump();
        let body = received.expect("no message from the page");
        assert!(body.contains("MSAppHost/3.0"), "{body}");
        let _ = std::fs::remove_dir_all(profile);
    }
}
