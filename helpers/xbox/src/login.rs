// Microsoft account sign-in, as xodus-cli's `login` (GPL-3.0, the Xodus authors): the Windows
// "TokenBroker" inline login page in a WebView2 window, whose host bridge hands over the DA token
// that is exchanged for the user's STS tokens. The WebView2 profile lives in <profile>\webview.

use std::path::Path;

use serde_json::json;
use tao::dpi::{LogicalSize, Size};
use tao::event::{Event, WindowEvent};
use tao::event_loop::{ControlFlow, EventLoop, EventLoopBuilder, EventLoopProxy, EventLoopWindowTarget};
use tao::platform::run_return::EventLoopExtRunReturn;
use tao::window::{Window, WindowBuilder};
use wry::http::{HeaderMap, HeaderValue};
use wry::{PageLoadEvent, WebContext, WebView, WebViewBuilder};
use xodus::models::live::{DAProperty, ExchangeUserTokenOutcome, HostBridgeMessage};
use xodus::models::secrets::{self, LegacyToken};
use xodus::models::soap;
use xodus::tokens::TokenManager;

use crate::output::{Failure, Outcome};

const CLIENT_ID: &str = "000000004424da1f";
const LOGIN_MARKET: &str = "en-US";
const USER_AUTH_SCOPE: &str = "scope=service::user.auth.xboxlive.com::MBI_SSL&api-version=2.0";
const TITLE: &str = "Launchme · Xbox sign-in";

enum UserEvent {
    Open(String, HeaderMap),
    HostGetContext(String),
    Finish,
    Token(DAProperty),
}

struct Session {
    window: Option<Window>,
    webview: Option<WebView>,
    finish: bool,
    result: Option<Result<soap::BodyContent, Failure>>,
}

fn login_headers() -> HeaderMap {
    let mut headers = HeaderMap::new();
    let pairs: [(&str, String); 9] = [
        ("cxh-capabilities", r#"{"PrivatePropertyBag":1,"PasswordlessConnect":1,"PreferAssociate":1,"ChromelessUI":0}"#.into()),
        ("cxh-correlationId", uuid::Uuid::new_v4().to_string()),
        ("cxh-msaBinaryVersion", "55".into()),
        ("cxh-identityClientBinaryVersion", "3".into()),
        ("cxh-osVersionInfo", r#"{"platformId":2,"majorVersion":10,"minorVersion":0,"buildNumber":26100}"#.into()),
        ("cxh-platform", "CloudExperienceHost.Platform.DESKTOP".into()),
        ("cxh-protocol", "TokenBroker".into()),
        ("cxh-source", "TokenBroker".into()),
        ("hostApp", "CloudExperienceHost".into()),
    ];
    for (name, value) in pairs {
        if let (Ok(name), Ok(value)) = (
            wry::http::header::HeaderName::from_bytes(name.as_bytes()),
            HeaderValue::from_str(&value),
        ) {
            headers.insert(name, value);
        }
    }
    headers
}

fn login_url() -> String {
    format!(
        "https://login.live.com/ppsecure/InlineLogin.srf?id=80604&scid=3&mkt={LOGIN_MARKET}&Platform=Windows10&clientid={CLIENT_ID}&hosted=1"
    )
}

fn open(
    target: &EventLoopWindowTarget<UserEvent>,
    proxy: EventLoopProxy<UserEvent>,
    session: &mut Session,
    context: &mut WebContext,
    url: String,
    headers: HeaderMap,
) -> Result<(), Failure> {
    if session.window.is_none() {
        let window = WindowBuilder::new()
            .with_resizable(false)
            .with_title(TITLE)
            .with_inner_size(Size::Logical(LogicalSize::new(500.0, 700.0)))
            // Above Launchme's full-screen window, which started it.
            .with_always_on_top(true)
            .with_focused(true)
            .build(target)
            .map_err(|e| Failure::new("internal", e.to_string()))?;
        session.window = Some(window);
    }
    session.webview = None;
    let window = session.window.as_ref().expect("window");
    let ipc = proxy.clone();
    let webview = WebViewBuilder::new_with_web_context(context)
        .with_url(&url)
        .with_user_agent("Mozilla/5.0 (Windows NT 10.0; Win64; x64; MSAppHost/3.0) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/70.0.3538.102 Safari/537.36 Edge/18.26100")
        .with_headers(headers)
        .with_initialization_script("window.external = {notify: window.ipc.postMessage }")
        .with_ipc_handler(move |request| {
            let body = request.body();
            if let Ok(data) = serde_json::from_str::<DAProperty>(body) {
                let _ = ipc.send_event(UserEvent::Token(data));
            } else if let Ok(message) = serde_json::from_str::<HostBridgeMessage>(body)
                && let Some(ctx) = message.get_context_invoke()
            {
                let _ = ipc.send_event(UserEvent::HostGetContext(ctx.to_string()));
            }
        })
        .with_on_page_load_handler(move |event, url| {
            if matches!(event, PageLoadEvent::Finished) && url.starts_with("https://login.live.com/ppsecure/post.srf") {
                let _ = proxy.send_event(UserEvent::Finish);
            }
        })
        .build(window)
        .map_err(|e| Failure::new("internal", format!("WebView2: {e}")))?;
    session.webview = Some(webview);
    Ok(())
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
    let mut context = WebContext::new(Some(profile.join("webview")));
    let mut event_loop: EventLoop<UserEvent> = EventLoopBuilder::with_user_event().build();
    let proxy = event_loop.create_proxy();
    let mut session = Session {
        window: None,
        webview: None,
        finish: false,
        result: None,
    };
    let _ = proxy.send_event(UserEvent::Open(login_url(), login_headers()));
    let mut user: Option<secrets::User> = None;

    event_loop.run_return(|event, target, control_flow| {
        *control_flow = ControlFlow::Wait;
        match event {
            Event::UserEvent(UserEvent::Open(url, headers)) => {
                if let Err(e) = open(target, proxy.clone(), &mut session, &mut context, url, headers) {
                    session.result = Some(Err(e));
                }
            }
            Event::UserEvent(UserEvent::Finish) => {
                if let Some(webview) = &session.webview {
                    let _ = webview.evaluate_script("window.ipc.postMessage(JSON.stringify(ServerData))");
                }
            }
            Event::UserEvent(UserEvent::HostGetContext(ctx)) => {
                if let Some(webview) = &session.webview {
                    let _ = webview.evaluate_script(&format!(r#"window["CloudExperienceHost.Bridge.dispatchMessage"](JSON.stringify({{"type": "callback", "value": {{ "name": "CloudExperienceHost.getContext", "args": ["CloudExperienceHost", "TokenBroker", "TokenBroker", "{{\"PrivatePropertyBag\":1,\"PasswordlessConnect\":1,\"PreferAssociate\":1,\"ChromelessUI\":0}}"], "context": "{ctx}"}}}}))"#));
                }
            }
            Event::UserEvent(UserEvent::Token(data)) => {
                let finish = session.finish;
                let outcome = tokio::task::block_in_place(|| {
                    tokio::runtime::Handle::current().block_on(exchange(client, device.clone(), &data, finish))
                });
                match outcome {
                    Err(e) => session.result = Some(Err(e)),
                    Ok(ExchangeUserTokenOutcome::Fault(fault)) => {
                        if let Some(url) = fault.and_then(|f| f.inline_auth_url) {
                            // Second step (consent / proofs): the page finishes the flow itself.
                            session.finish = true;
                            let _ = proxy.send_event(UserEvent::Open(url, HeaderMap::new()));
                        } else {
                            session.result = Some(Err(Failure::new("signed_out", "Microsoft refused the token exchange")));
                        }
                    }
                    Ok(ExchangeUserTokenOutcome::Issued(body)) => {
                        user = Some(secrets::User {
                            puid: data.puid.clone(),
                            username: data.username.clone(),
                        });
                        session.result = Some(Ok(body));
                    }
                }
            }
            Event::WindowEvent {
                event: WindowEvent::CloseRequested,
                ..
            } => {
                session.result = Some(Err(Failure::new("cancelled", "sign-in window closed")));
            }
            _ => {}
        }
        if session.result.is_some() {
            session.webview = None;
            session.window = None;
            *control_flow = ControlFlow::Exit;
        }
    });

    let body = session
        .result
        .unwrap_or_else(|| Err(Failure::new("cancelled", "sign-in ended")))?;
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
