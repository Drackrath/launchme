// Product → Windows.Desktop content id → base package files, as xodus-cli's package.rs (GPL-3.0,
// the Xodus authors) but without prompts: bundles resolve to their single PC sub-product.

use xodus::XBOX_LIVE_PACKAGES_PC;
use xodus::api::displaycatalog::find_products_by_id;
use xodus::models::packagespc::{PackageDetails, PackageResponse};
use xodus::tokens::TokenManager;

use crate::output::Failure;

pub async fn content_id(client: &reqwest::Client, product: &str, market: &str, depth: u8) -> Result<String, Failure> {
    if depth > 3 {
        return Err(Failure::new("not_found", "bundle nesting too deep"));
    }
    let catalog = find_products_by_id(
        client,
        product.to_string(),
        market.to_string(),
        vec!["en".to_string(), "neutral".to_string()],
    )
    .await
    .map_err(Failure::network)?;
    let mut subproducts: Vec<String> = Vec::new();
    for availability in &catalog.product.display_sku_availabilities {
        for package in &availability.sku.properties.packages {
            if package
                .platform_dependencies
                .iter()
                .any(|d| d.platform_name == "Windows.Desktop")
            {
                return package
                    .content_id
                    .clone()
                    .ok_or_else(|| Failure::new("unsupported_package", "the PC package has no content id"));
            }
        }
        for availability in &availability.availabilities {
            for satisfies in availability
                .licensing_data
                .iter()
                .flat_map(|l| l.satisfying_entitlement_keys.iter())
            {
                for key in &satisfies.entitlement_keys {
                    let parts: Vec<&str> = key.split(':').collect();
                    if parts.len() == 3 && parts[0] == "big" && parts[1] != product {
                        subproducts.push(parts[1].to_string());
                    }
                }
            }
        }
    }
    subproducts.sort();
    subproducts.dedup();
    for sub in subproducts {
        if let Ok(id) = Box::pin(content_id(client, &sub, market, depth + 1)).await {
            return Ok(id);
        }
    }
    Err(Failure::new("unsupported_package", "no Windows.Desktop package"))
}

pub async fn base_package(client: &reqwest::Client, tokens: &TokenManager, content_id: &str) -> Result<PackageDetails, Failure> {
    let signed = crate::tokens::signed_in(client, tokens).await?;
    let xsts = xodus::api::xbox::run(client, signed.device, signed.user, "http://update.xboxlive.com").await;
    let response = client
        .get(format!("{XBOX_LIVE_PACKAGES_PC}/GetBasePackage/{content_id}"))
        .header("x-xbl-contract-version", "3")
        .header("Authorization", xodus::api::xbox::get_xsts_auth_header(xsts))
        .send()
        .await
        .and_then(|r| r.error_for_status())
        .map_err(Failure::network)?;
    match response.json::<PackageResponse>().await.map_err(Failure::network)? {
        PackageResponse::Found(package) => Ok(package),
        PackageResponse::NotFound { .. } => Err(Failure::new("not_entitled", "package not found: is the game owned?")),
    }
}
