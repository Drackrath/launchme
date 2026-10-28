// The account's games: Store collections (bought, Play Anywhere, claimed), the active Game Pass
// subscription and its PC catalog, then displaycatalog details. Endpoints as documented by Xodus
// (docs/xbox/gamepass.md, PR #199) and its owned-games work (PR #221).

use std::collections::{BTreeMap, HashMap};
use std::path::Path;
use std::time::{SystemTime, UNIX_EPOCH};

use serde::Deserialize;
use serde_json::{Value, json};
use xodus::tokens::TokenManager;
use xodus::xal::cvlib::CorrelationVector;

use crate::output::{Failure, Outcome};
use crate::tokens::{StoreTickets, store_tickets};

const COLLECTIONS_QUERY: &str = "https://collections.mp.microsoft.com/v7.0/collections/query";
const SUBSCRIPTIONS: &str = "https://beige.xboxservices.com/PCGAFD/entitlements/subscriptions";
const SIGLS: &str = "https://catalog.gamepass.com/sigls/v3/";
const SIGLS_V2: &str = "https://catalog.gamepass.com/sigls/v2";
const DISPLAY_CATALOG: &str = "https://displaycatalog.mp.microsoft.com/v7.0/products";
const ALL_PC_GAMES: &str = "fdd9e2a7-0fee-49f6-ad69-4354098401ff";
const ALL_PC_GAMES_EA: &str = "609d944c-d395-4c0a-9ea4-e9f39b52c1ad";
const PC_GAME_PASS: &str = "CFQ7TTC0KGQ8";
const ULTIMATE: &str = "CFQ7TTC0KHS0";
// Every Game Pass subscription id the Xbox app asks about.
const SUBSCRIPTION_IDS: [&str; 7] = [
    "CFQ7TTC0K6L8",
    "CFQ7TTC0K5DJ",
    "CFQ7TTC0KGQ8",
    "CFQ7TTC0P85B",
    "CFQ7TTC0KHS0",
    "CFQ7TTC10QFD",
    "CFQ7TTC0HXBN",
];
const BATCH: usize = 20;
const MAX_PAGES: usize = 100;

#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
enum Acquisition {
    GamePass = 1,
    Other = 2,
    Purchased = 3,
}

impl Acquisition {
    fn name(self) -> &'static str {
        match self {
            Acquisition::Purchased => "purchased",
            Acquisition::GamePass => "gamepass",
            Acquisition::Other => "other",
        }
    }
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
struct CollectionsPage {
    #[serde(default)]
    items: Vec<CollectionItem>,
    continuation_token: Option<String>,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
struct CollectionItem {
    product_id: String,
    #[serde(default)]
    product_kind: String,
    #[serde(default)]
    status: String,
    order_id: Option<String>,
    recurrence_id: Option<String>,
}

async fn owned(client: &reqwest::Client, tickets: &StoreTickets, market: &str) -> Result<BTreeMap<String, Acquisition>, Failure> {
    let mut result = BTreeMap::new();
    let mut continuation: Option<String> = None;
    for _ in 0..MAX_PAGES {
        let mut body = json!({
            "beneficiaries": [{ "identityType": "Msa", "identityValue": tickets.user, "localTicketReference": tickets.puid }],
            "market": market,
            "maxPageSize": 100,
            "validityType": "Valid",
            "productKinds": ["Game"],
        });
        if let Some(token) = &continuation {
            body["continuationToken"] = Value::String(token.clone());
        }
        let page: CollectionsPage = client
            .post(COLLECTIONS_QUERY)
            .header("Authorization", &tickets.device)
            .json(&body)
            .send()
            .await
            .and_then(|r| r.error_for_status())
            .map_err(Failure::network)?
            .json()
            .await
            .map_err(Failure::network)?;
        for item in page.items {
            if item.product_kind != "Game" || item.status != "Active" {
                continue;
            }
            let acquisition = if item.order_id.as_deref().is_some_and(|s| !s.is_empty()) {
                Acquisition::Purchased
            } else if item.recurrence_id.as_deref().is_some_and(|s| !s.is_empty()) {
                Acquisition::GamePass
            } else {
                Acquisition::Other
            };
            let slot = result.entry(item.product_id).or_insert(acquisition);
            *slot = (*slot).max(acquisition);
        }
        match page.continuation_token {
            Some(token) if !token.is_empty() => continuation = Some(token),
            _ => break,
        }
    }
    Ok(result)
}

// Active subscription ids; an error here only means "no Game Pass list", never a failed sync.
async fn subscriptions(client: &reqwest::Client, tickets: &StoreTickets, market: &str, language: &str) -> Vec<String> {
    let response = client
        .post(with(SUBSCRIPTIONS, &[("market", market), ("language", language)]))
        .header("x-ms-api-version", "1.1")
        .header("authorization", &tickets.user)
        .header("ms-cv", CorrelationVector::new().to_string())
        .json(&json!({ "productIds": SUBSCRIPTION_IDS }))
        .send()
        .await
        .and_then(|r| r.error_for_status());
    let Ok(response) = response else {
        crate::output::log("subscriptions: request failed");
        return Vec::new();
    };
    let Ok(value) = response.json::<Value>().await else {
        return Vec::new();
    };
    let mut active: Vec<String> = value["entitlements"]
        .as_object()
        .into_iter()
        .flatten()
        .filter(|(_, e)| e["status"].as_str() == Some("Active"))
        .map(|(id, _)| id.clone())
        .collect();
    active.sort();
    active
}

async fn gamepass_catalog(client: &reqwest::Client, subscription: &str, market: &str, language: &str) -> Vec<String> {
    let ids = |value: Value| -> Vec<String> {
        value
            .as_array()
            .into_iter()
            .flatten()
            .filter_map(|e| e["id"].as_str().map(str::to_owned))
            .collect()
    };
    let v3 = client
        .get(with(SIGLS, &[
            ("language", language),
            ("market", market),
            ("id", ALL_PC_GAMES),
            ("platformContext", "pc"),
            ("subscriptionContext", subscription),
        ]))
        .send()
        .await
        .and_then(|r| r.error_for_status());
    if let Ok(response) = v3
        && let Ok(value) = response.json::<Value>().await
    {
        let list = ids(value);
        if !list.is_empty() {
            return list;
        }
    }
    let v2 = client
        .get(with(SIGLS_V2, &[("id", ALL_PC_GAMES_EA), ("language", language), ("market", market)]))
        .send()
        .await
        .and_then(|r| r.error_for_status());
    match v2 {
        Ok(response) => response.json::<Value>().await.map(ids).unwrap_or_default(),
        Err(_) => Vec::new(),
    }
}

// reqwest 0.13 builds query strings only with its query feature; url does it here.
fn with(base: &str, params: &[(&str, &str)]) -> String {
    url::Url::parse_with_params(base, params)
        .map(String::from)
        .unwrap_or_else(|_| base.to_string())
}

fn https(uri: &str) -> String {
    if let Some(rest) = uri.strip_prefix("//") {
        format!("https://{rest}")
    } else {
        uri.to_string()
    }
}

fn image(images: &Value, purposes: &[&str]) -> Option<String> {
    let images = images.as_array()?;
    purposes.iter().find_map(|purpose| {
        images
            .iter()
            .filter(|i| i["ImagePurpose"].as_str() == Some(*purpose))
            .max_by_key(|i| i["Width"].as_u64().unwrap_or(0))
            .and_then(|i| i["Uri"].as_str())
            .map(https)
    })
}

// The Windows.Desktop package, if the product runs on PC (GDK MSIXVC, MSIXVC2 or an older UWP one).
fn pc_package(product: &Value) -> Option<(String, Option<String>, u64)> {
    product["DisplaySkuAvailabilities"]
        .as_array()?
        .iter()
        .flat_map(|dsa| dsa["Sku"]["Properties"]["Packages"].as_array().into_iter().flatten())
        .filter(|pkg| {
            pkg["PlatformDependencies"]
                .as_array()
                .into_iter()
                .flatten()
                .any(|d| d["PlatformName"].as_str() == Some("Windows.Desktop"))
        })
        .map(|pkg| {
            (
                pkg["PackageFormat"].as_str().unwrap_or("").to_string(),
                pkg["ContentId"].as_str().map(str::to_owned),
                pkg["MaxDownloadSizeInBytes"].as_u64().unwrap_or(0),
            )
        })
        .max_by_key(|(_, _, size)| *size)
}

fn describe(product: &Value) -> Option<(String, Value)> {
    let id = product["ProductId"].as_str()?.to_string();
    let local = &product["LocalizedProperties"][0];
    let (format, content_id, size) = pc_package(product)?;
    let images = &local["Images"];
    let title_id = product["AlternateIds"]
        .as_array()
        .into_iter()
        .flatten()
        .find(|a| a["IdType"].as_str() == Some("XboxTitleId"))
        .and_then(|a| a["Value"].as_str())
        .map(str::to_owned);
    let own_install = match format.as_str() {
        "MSIXVC" => "supported",
        "MSIXVC2" => "unsupported",
        _ => "not_gdk",
    };
    Some((
        id.clone(),
        json!({
            "product_id": id,
            "title": local["ProductTitle"].as_str().unwrap_or(""),
            "publisher": local["PublisherName"].as_str(),
            "developer": local["DeveloperName"].as_str(),
            "description": local["ShortDescription"].as_str(),
            "release_date": product["MarketProperties"][0]["OriginalReleaseDate"].as_str(),
            "pfn": product["Properties"]["PackageFamilyName"].as_str(),
            "xbox_title_id": title_id,
            "content_id": content_id,
            "package_format": format,
            "download_size": size,
            "own_install": own_install,
            "images": {
                "poster": image(images, &["Poster"]),
                "box": image(images, &["BoxArt", "Tile"]),
                "hero": image(images, &["SuperHeroArt", "TitledHeroArt"]),
                "logo": image(images, &["Logo"]),
            },
        }),
    ))
}

async fn details(client: &reqwest::Client, ids: &[String], market: &str, language: &str) -> Result<HashMap<String, Value>, Failure> {
    let mut result = HashMap::new();
    let languages = format!("{language},neutral");
    for (index, chunk) in ids.chunks(BATCH).enumerate() {
        crate::output::progress("details", (index * BATCH) as u64, ids.len() as u64);
        let value: Value = client
            .get(with(DISPLAY_CATALOG, &[("bigIds", chunk.join(",").as_str()), ("market", market), ("languages", languages.as_str())]))
            .send()
            .await
            .and_then(|r| r.error_for_status())
            .map_err(Failure::network)?
            .json()
            .await
            .map_err(Failure::network)?;
        for product in value["Products"].as_array().into_iter().flatten() {
            if let Some((id, game)) = describe(product) {
                result.insert(id, game);
            }
        }
    }
    Ok(result)
}

fn write_atomic(path: &Path, value: &Value) -> Result<(), Failure> {
    let storage = |e: std::io::Error| Failure::new("storage", format!("{}: {e}", path.display()));
    if let Some(parent) = path.parent() {
        std::fs::create_dir_all(parent).map_err(storage)?;
    }
    let part = path.with_extension("part");
    std::fs::write(&part, serde_json::to_vec_pretty(value).expect("json")).map_err(storage)?;
    std::fs::rename(&part, path).map_err(storage)
}

pub async fn run(client: &reqwest::Client, tokens: &TokenManager, market: &str, language: &str, out: &Path) -> Outcome {
    let tickets = store_tickets(client, tokens).await?;
    crate::output::progress("collections", 0, 0);
    let owned = owned(client, &tickets, market).await?;
    let subscriptions = subscriptions(client, &tickets, market, language).await;
    let pass = [PC_GAME_PASS, ULTIMATE]
        .into_iter()
        .find(|id| subscriptions.iter().any(|s| s == id));
    let catalog = match pass {
        Some(subscription) => gamepass_catalog(client, subscription, market, language).await,
        None => Vec::new(),
    };

    let mut acquisitions: BTreeMap<String, Acquisition> = owned;
    for id in &catalog {
        acquisitions.entry(id.clone()).or_insert(Acquisition::GamePass);
    }
    let ids: Vec<String> = acquisitions.keys().cloned().collect();
    let mut details = details(client, &ids, market, language).await?;

    let games: Vec<Value> = acquisitions
        .iter()
        .filter_map(|(id, acquisition)| {
            let mut game = details.remove(id)?;
            game["acquisition"] = json!(acquisition.name());
            game["in_game_pass"] = json!(catalog.iter().any(|c| c == id));
            Some(game)
        })
        .collect();
    let synced_at = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0);
    let username = tokens.get_user().map(|u| u.username).unwrap_or_default();
    let count = games.len();
    write_atomic(
        out,
        &json!({
            "version": 1,
            "synced_at": synced_at,
            "username": username,
            "market": market,
            "subscriptions": subscriptions,
            "game_pass": pass,
            "games": games,
        }),
    )?;
    Ok(json!({ "games": count, "subscriptions": subscriptions, "game_pass": pass }))
}
