#pragma once
#include "../model/game.hpp"
#include <map>
#include <optional>
#include <string_view>
#include <vector>

// Ubisoft Connect keeps a Ubisoft-signed list of the signed-in account's products in its
// cache (cache/ownership/<account GUID>) and the recently played ones in cache/settings/.
// Reading them needs neither a running client nor credentials. The files carry product
// IDs, not titles; titles come from a public catalogue (ubisoft_catalog.hpp).
namespace ubisoft {
struct Product {
    std::string id;   // Decimal product ID for uplay://launch|install, also the GameKey.
    std::string uuid; // Lowercase product UUID of the public catalogue; may be empty.
};
struct Account {
    std::string id;                              // The cache file name: Ubisoft account GUID.
    std::vector<Product> products;               // Base games only; add-ons are skipped.
    std::map<std::string, uint64_t> last_played; // Product ID -> Unix seconds.
};

// <u32 signature size><signature><u32 payload size><protobuf>. Record field 4 may hold a
// CD key: it is never read. nullopt = unreadable/unknown layout, not an empty library.
std::optional<std::vector<Product>> parseOwnership(std::string_view bytes);
std::map<std::string, uint64_t> parseRecentlyPlayed(std::string_view bytes);
// Ubisoft Connect's own artwork of each product, from cache/configuration/configurations (one record per
// product: field 1 its ID, field 3 its YAML configuration). The YAML names files on Ubisoft's public CDN:
// thumb_image (a small, tall box), background_image (about 1200×700) and logo_image (a transparent PNG).
// Full https URLs; empty when the client has none.
inline constexpr std::string_view asset_cdn = "https://ubistatic3-a.akamaihd.net/orbit/uplay_launcher_3_0/assets/";
inline constexpr size_t max_configuration_bytes = 32 * 1024 * 1024;
struct Art { std::string thumb, background, logo; };
std::map<std::string, Art> parseConfigurations(std::string_view bytes); // Product ID -> its images.
std::map<std::string, Art> readConfigurations(const std::vector<std::filesystem::path>& caches);
// The most recently written account among the client's cache roots. A newer but unreadable
// file is reported as unavailable instead of falling back to another (older) account.
std::optional<Account> readAccount(const std::vector<std::filesystem::path>& caches);

// Title cache: "<key>\t<name>" UTF-8 lines; key = lowercase UUID or decimal product ID.
std::map<std::string, std::wstring> parseNames(std::string_view text);
std::string serializeNames(const std::map<std::string, std::wstring>& names);
std::map<std::string, std::wstring> loadNames(const std::filesystem::path& file);
// Public catalogues: {"games":{"<uuid>":{"name":"…"}}} and "<id>, <name>" lines.
std::map<std::string, std::wstring> parseUuidCatalog(std::string_view json);
std::map<std::string, std::wstring> parseIdList(std::string_view text);
std::wstring title(const std::map<std::string, std::wstring>& names, const Product& product);
// Products without a title yet.
std::vector<Product> unnamed(const std::map<std::string, std::wstring>& names, const std::vector<Product>& products);
bool validUuid(std::string_view text);
}
