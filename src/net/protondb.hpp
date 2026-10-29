#pragma once
// How a Windows game runs on Linux, by its Steam app ID: ProtonDB's community tier
// (protondb.com/api/v1/reports/summaries/<id>.json) and Valve's Steam Deck compatibility
// (store.steampowered.com/saleaction/ajaxgetdeckappcompatibilityreport). Both cached for three days in
// <cache>/protondb/<id>.json; the network goes through an injectable getter. Portable.
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

struct HttpResponse;

namespace protondb {
struct Summary {
    std::string tier;     // "platinum", "gold", "silver", "bronze", "borked", "pending"; empty: no reports.
    std::string trending; // The recent reports' tier.
    uint32_t reports = 0;
    int deck = 0;         // Steam Deck: 0 unknown, 1 unsupported, 2 playable, 3 verified.
    bool operator==(const Summary&) const = default;
};
inline constexpr uint64_t max_age_seconds = 3 * 24 * 3600;

Summary parseSummary(std::string_view json);
int parseDeck(std::string_view json); // results.resolved_category (0 when absent).

// A Steam app ID from umu-database's ID ("umu-397540" → 397540); 0 for any other.
uint32_t steamAppOf(std::string_view umu_id);

std::optional<Summary> cached(const std::filesystem::path& folder, uint32_t app, uint64_t now);
bool store(const std::filesystem::path& folder, uint32_t app, const Summary& summary, uint64_t now);

using Get = std::function<HttpResponse(const std::wstring& url)>;
// Both sources; nullopt when neither answered (offline: nothing is cached).
std::optional<Summary> fetch(uint32_t app, const Get& get);

// "ProtonDB: Gold · Steam Deck: Verified" (English or Polish); empty when nothing is known.
std::wstring label(const Summary& summary, bool english);
std::wstring page(uint32_t app); // https://www.protondb.com/app/<id>
}
