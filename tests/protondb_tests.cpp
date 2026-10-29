#include "../src/net/protondb.hpp"
#include "../src/net/http.hpp"

#include <filesystem>
#include <iostream>
#include <map>
#include <random>

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) { std::cerr << "FAILED: " << what << '\n'; ++failures; }
}
HttpResponse response(uint32_t status, std::string_view body) {
    HttpResponse result;
    result.status = status;
    result.body.assign(body.begin(), body.end());
    return result;
}
}

int main() {
    // ProtonDB's summary and Valve's Deck report.
    const auto summary = protondb::parseSummary(R"({"bestReportedTier":"platinum","confidence":"strong","score":0.77,"tier":"gold","total":342,"trendingTier":"platinum"})");
    check(summary.tier == "gold" && summary.trending == "platinum" && summary.reports == 342, "summary fields");
    check(protondb::parseSummary(R"({"tier":"<script>","total":-5})").tier.empty() && protondb::parseSummary("not json").reports == 0, "bad summaries");
    check(protondb::parseDeck(R"({"success":1,"results":{"appid":570,"resolved_category":3}})") == 3, "Deck verified");
    check(protondb::parseDeck(R"({"success":1,"results":{"resolved_category":9}})") == 0 && protondb::parseDeck("{}") == 0, "unknown Deck category");
    check(protondb::steamAppOf("umu-397540") == 397540 && !protondb::steamAppOf("umu-default") && !protondb::steamAppOf("397540"), "umu IDs");

    // Labels.
    protondb::Summary both{"gold", "platinum", 10, 2};
    check(protondb::label(both, true) == L"ProtonDB: Gold (now Platinum) · Steam Deck: Playable", "English label");
    check(protondb::label(both, false) == L"ProtonDB: Złoto (ostatnio Platyna) · Steam Deck: Grywalna", "Polish label");
    check(protondb::label({}, true).empty() && protondb::label({"pending", "", 0, 0}, true).empty(), "nothing known");
    check(protondb::label({"", "", 0, 3}, true) == L"Steam Deck: Verified", "Deck only");

    // Fetching: both sources through the getter; a 404 summary is no reports, not offline.
    std::map<std::wstring, HttpResponse> answers{
        {L"https://www.protondb.com/api/v1/reports/summaries/570.json", response(200, R"({"tier":"platinum","total":5,"trendingTier":"platinum"})")},
        {L"https://store.steampowered.com/saleaction/ajaxgetdeckappcompatibilityreport?nAppID=570", response(200, R"({"results":{"resolved_category":3}})")},
        {L"https://www.protondb.com/api/v1/reports/summaries/1.json", response(404, "")},
    };
    const protondb::Get get = [&](const std::wstring& url) { const auto found = answers.find(url); return found == answers.end() ? HttpResponse{} : found->second; };
    const auto fetched = protondb::fetch(570, get);
    check(fetched && fetched->tier == "platinum" && fetched->deck == 3, "fetched");
    const auto none = protondb::fetch(1, get);
    check(none && none->tier.empty() && none->deck == 0, "no reports yet");
    check(!protondb::fetch(2, get) && !protondb::fetch(0, get), "offline or no app");

    // The cache: kept for three days.
    std::random_device random;
    const auto folder = std::filesystem::temp_directory_path() / ("launchme-protondb-" + std::to_string(random()));
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup{folder};
    check(protondb::store(folder, 570, *fetched, 1000), "stored");
    const auto read = protondb::cached(folder, 570, 1000 + protondb::max_age_seconds);
    check(read && *read == *fetched, "cached");
    check(!protondb::cached(folder, 570, 1001 + protondb::max_age_seconds) && !protondb::cached(folder, 571, 1000), "expired or absent");
    check(protondb::page(570) == L"https://www.protondb.com/app/570", "page");

    if (failures) { std::cerr << failures << " failure(s)\n"; return 1; }
    std::cout << "ProtonDB and Steam Deck ratings passed\n";
    return 0;
}
