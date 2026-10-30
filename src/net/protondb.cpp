#include "protondb.hpp"
#include "http.hpp"
#include "json.hpp"
#include "../platform/atomic_file.hpp"

#include <charconv>
#include <fstream>
#include <iterator>

namespace protondb {
namespace {
std::string text(const std::vector<uint8_t>& bytes) { return std::string(bytes.begin(), bytes.end()); }

bool knownTier(std::string_view tier) {
    for (const auto* known : {"platinum", "gold", "silver", "bronze", "borked", "pending", "native"}) if (tier == known) return true;
    return false;
}

const wchar_t* tierName(std::string_view tier, bool english) {
    if (tier == "platinum") return english ? L"Platinum" : L"Platyna";
    if (tier == "gold") return english ? L"Gold" : L"Złoto";
    if (tier == "silver") return english ? L"Silver" : L"Srebro";
    if (tier == "bronze") return english ? L"Bronze" : L"Brąz";
    if (tier == "borked") return english ? L"Borked" : L"Nie działa";
    if (tier == "native") return english ? L"Native" : L"Natywna";
    if (tier == "pending") return english ? L"Pending" : L"W toku";
    return L"";
}
} // namespace

Summary parseSummary(std::string_view json) {
    Summary result;
    const auto root = json::parse(json);
    if (!root) return result;
    if (const auto* tier = root->get("tier"); tier && knownTier(tier->stringOr())) result.tier = tier->stringOr();
    if (const auto* trending = root->get("trendingTier"); trending && knownTier(trending->stringOr())) result.trending = trending->stringOr();
    if (const auto* total = root->get("total"); total && total->type == json::Value::Type::number && total->numeric >= 0 && total->numeric < 1e9)
        result.reports = static_cast<uint32_t>(total->numeric);
    return result;
}

int parseDeck(std::string_view json) {
    const auto root = json::parse(json);
    const auto* results = root ? root->get("results") : nullptr;
    const auto* category = results ? results->get("resolved_category") : nullptr;
    if (!category || category->type != json::Value::Type::number) return 0;
    // Checked as a double first: casting an out-of-range value to int is undefined behaviour.
    return category->numeric >= 0 && category->numeric <= 3 ? static_cast<int>(category->numeric) : 0;
}

uint32_t steamAppOf(std::string_view umu_id) {
    if (!umu_id.starts_with("umu-")) return 0;
    umu_id.remove_prefix(4);
    uint32_t app = 0;
    const auto [end, error] = std::from_chars(umu_id.data(), umu_id.data() + umu_id.size(), app);
    return error == std::errc{} && end == umu_id.data() + umu_id.size() ? app : 0;
}

std::optional<Summary> cached(const std::filesystem::path& folder, uint32_t app, uint64_t now) {
    std::ifstream in(folder / (std::to_string(app) + ".json"), std::ios::binary);
    if (!in) return std::nullopt;
    const std::string body{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    const auto root = json::parse(body);
    const auto* fetched = root ? root->get("fetched") : nullptr;
    if (!fetched || fetched->type != json::Value::Type::number || fetched->numeric + max_age_seconds < static_cast<double>(now)) return std::nullopt;
    Summary result;
    if (const auto* tier = root->get("tier"); tier && knownTier(tier->stringOr())) result.tier = tier->stringOr();
    if (const auto* trending = root->get("trending"); trending && knownTier(trending->stringOr())) result.trending = trending->stringOr();
    if (const auto* reports = root->get("reports"); reports && reports->type == json::Value::Type::number && reports->numeric >= 0 && reports->numeric < 1e9)
        result.reports = static_cast<uint32_t>(reports->numeric);
    if (const auto* deck = root->get("deck"); deck && deck->type == json::Value::Type::number && deck->numeric >= 0 && deck->numeric <= 3)
        result.deck = static_cast<int>(deck->numeric);
    return result;
}

bool store(const std::filesystem::path& folder, uint32_t app, const Summary& summary, uint64_t now) {
    auto value = json::object();
    value.properties.emplace("tier", json::string(summary.tier));
    value.properties.emplace("trending", json::string(summary.trending));
    value.properties.emplace("reports", json::number(summary.reports));
    value.properties.emplace("deck", json::number(summary.deck));
    value.properties.emplace("fetched", json::number(static_cast<double>(now)));
    return atomic_file::write(folder / (std::to_string(app) + ".json"), json::serialize(value));
}

std::optional<Summary> fetch(uint32_t app, const Get& get) {
    if (!app || !get) return std::nullopt;
    const auto id = std::to_wstring(app);
    bool answered = false;
    Summary result;
    if (const auto response = get(L"https://www.protondb.com/api/v1/reports/summaries/" + id + L".json"); response.status == 200) {
        result = parseSummary(text(response.body));
        answered = true;
    } else if (response.status == 404) answered = true; // No reports yet.
    if (const auto response = get(L"https://store.steampowered.com/saleaction/ajaxgetdeckappcompatibilityreport?nAppID=" + id); response.status == 200) {
        result.deck = parseDeck(text(response.body));
        answered = true;
    }
    return answered ? std::optional<Summary>(result) : std::nullopt;
}

std::wstring label(const Summary& summary, bool english) {
    std::wstring result;
    if (!summary.tier.empty() && summary.tier != "pending") {
        result = std::wstring(L"ProtonDB: ") + tierName(summary.tier, english);
        if (!summary.trending.empty() && summary.trending != summary.tier && summary.trending != "pending")
            result += std::wstring(english ? L" (now " : L" (ostatnio ") + tierName(summary.trending, english) + L")";
    }
    if (summary.deck >= 1 && summary.deck <= 3) {
        static constexpr const wchar_t* deck_en[] = {L"", L"Unsupported", L"Playable", L"Verified"};
        static constexpr const wchar_t* deck_pl[] = {L"", L"Nieobsługiwana", L"Grywalna", L"Zweryfikowana"};
        result += (result.empty() ? L"" : L" · ") + std::wstring(L"Steam Deck: ") + (english ? deck_en : deck_pl)[summary.deck];
    }
    return result;
}

std::wstring page(uint32_t app) { return L"https://www.protondb.com/app/" + std::to_wstring(app); }
}
