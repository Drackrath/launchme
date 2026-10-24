#include "../src/library/game_ratings.hpp"
#include "../src/library/rawg.hpp"
#include <Windows.h>
#include <iostream>

namespace {
constexpr auto steam_json = R"({"success":1,"query_summary":{"total_positive":90,"total_negative":10,"total_reviews":100}})";
constexpr auto rawg_item = R"({"id":73,"name":"Hades","slug":"hades","rating":4.5,"ratings_count":120,"metacritic":93,"platforms":[{"platform":{"id":4,"name":"PC"}}],"released":"2020-09-17","description_raw":"A fixture description.","developers":[{"name":"Supergiant Games"}],"publishers":[{"name":"Supergiant Games"}],"genres":[{"name":"Action"}]})";
HttpResponse response(std::string_view text, uint32_t status = 200) { return {status, {text.begin(), text.end()}}; }
}
int main() {
    const auto root = std::filesystem::temp_directory_path() / (L"launchme-ratings-test-" + std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { std::filesystem::path file; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(file, ec); } } cleanup{root};
    Game steam{620, L"Portal 2"};
    Game other; other.name = L"Hades"; other.kind = GameKind::store; other.source = GameSource::gog; other.source_id = "fixture";
    const auto rawg_json = std::string("{\"results\":[") + rawg_item + "]}";
    const auto s = ratings::parseSteam(steam_json), r = ratings::parseRawg(other, rawg_json);
    if (!s.rating || !r.rating || ratings::compactLabel(*s.rating) != L"90%" || ratings::compactLabel(*r.rating) != L"4.5/5" ||
        !ratings::compactLabel(ratings::Rating{}).empty()) return 30;
    auto critic = *r.rating; critic.count = 0;
    if (ratings::compactLabel(critic) != L"93/100") return 31;
    if (gameReleaseYear(L"12 Nov 2020") != L"2020" || gameReleaseYear(L"1998-11-19") != L"1998" ||
        !gameReleaseYear(L"TBA").empty() || !gameReleaseYear(L"120200").empty() || !gameReleaseYear(L"").empty()) return 32;
    if (!s.valid || !s.rating || s.rating->score != 90 || s.rating->count != 100 ||
        !r.valid || !r.rating || r.rating->score != 4.5 || r.rating->metacritic != 93 || ratings::url(*r.rating, 0) != L"https://rawg.io/games/hades") return 1;
    if (ratings::parseSteam(R"({"success":1,"query_summary":{"total_positive":90,"total_negative":20,"total_reviews":100}})").valid ||
        ratings::parseSteam("{}").valid || ratings::parseRawg(other, "{}").valid) return 2;
    auto wrong_title = other; wrong_title.name = L"Hades II";
    if (ratings::parseRawg(wrong_title, rawg_json).rating ||
        ratings::parseRawg(other, std::string("{\"results\":[") + rawg_item + ',' + rawg_item + "]}").rating) return 3;
    auto console = other; console.kind = GameKind::nonsteam; console.platform = L"PS3";
    if (ratings::parseRawg(console, rawg_json).rating) return 4;
    auto privacy = steam; privacy.app_id = 999; privacy.visibility = GameVisibility::private_game;
    uint64_t now = 2000000000;
    int calls = 0, waits = 0;
    ratings::Map values;
    std::atomic_bool alive{true};
    ratings::Backend backend;
    backend.now = [&] { return now; };
    backend.wait = [&](auto interval) { if (interval.count() != 750) return false; ++waits; return true; };
    backend.get = [&](const std::wstring& url, bool secret) {
        ++calls;
        if (secret && url.find(L"&search=Hades&") == url.npos) return response("{}", 400);
        if (!secret && url.find(L"num_per_page=0") == url.npos) return response("{}", 400);
        return response(secret ? rawg_json : steam_json);
    };
    const auto publish = [&](ratings::Map ready) { for (auto& [key, value] : ready) values[key] = std::move(value); };
    ratings::collect({steam, steam, other, privacy}, root / "good", L"fixture-key", alive, publish, backend);
    if (calls != 2 || waits != 1 || values.size() != 2) return 5;
    calls = 0; values.clear(); now += 30 * 86400;
    ratings::collect({steam, other}, root / "good", L"", alive, publish, backend);
    if (calls || values.size() != 2) return 6; // Offline/key removed: saved scores still work, no periodic refresh.
    backend.get = [&](const auto&, bool) { ++calls; return response(R"({"results":[]})"); };
    ratings::collect({other}, root / "missing", L"fixture", alive, publish, backend);
    ratings::collect({other}, root / "missing", L"fixture", alive, publish, backend);
    if (calls != 1) return 7;
    now += 7 * 86400 + 1;
    ratings::collect({other}, root / "missing", L"fixture", alive, publish, backend);
    if (calls != 2) return 8; // Negative cache expires.
    calls = 0;
    backend.get = [&](const auto&, bool) { ++calls; auto limited = response("{}", 429); limited.retry_after_seconds = 900; return limited; };
    ratings::collect({other}, root / "limited", L"fixture", alive, publish, backend);
    auto another = other; another.source_id = "another";
    ratings::collect({another}, root / "limited", L"fixture", alive, publish, backend);
    if (calls != 1) return 9; // Provider-wide Retry-After survives startup, not just a per-game delay.
    now += 901;
    backend.get = [&](const auto&, bool) { ++calls; return response(rawg_json); };
    ratings::collect({other}, root / "limited", L"fixture", alive, publish, backend);
    if (calls != 2) return 10; // An error was never cached as "no match".
    calls = 0;
    backend.get = [&](const auto&, bool) { ++calls; return response(steam_json); };
    auto b = steam, c = steam; b.app_id = 621; c.app_id = 622;
    ratings::collect({steam, b, c}, root / "budget", L"", alive, publish, backend, {2});
    if (calls != 2) return 11;
    ratings::collect({steam, b, c}, root / "budget", L"", alive, publish, backend, {2});
    if (calls != 3) return 12;
    alive = false;
    ratings::collect({other}, root / "cancelled", L"fixture", alive, publish, backend);
    if (calls != 3) return 13;
    const auto metadata = rawg::parseDetails(rawg_item);
    if (!metadata || metadata->details.developer != L"Supergiant Games" || metadata->details.publisher != L"Supergiant Games" ||
        metadata->details.description != L"A fixture description." || metadata->rating.metacritic != 93) return 14;
    rawg::Details details{metadata, rawg_item};
    details.data->steam_app_id = 1145360;
    if (!rawg::save(other, details, root / "metadata") || !rawg::load(other, root / "metadata") ||
        !ratings::saveIdentified(root / "manual", other, metadata->rating)) return 15;
    if (rawg::load(other, root / "metadata")->steam_app_id != 1145360 || other.app_id != 0) return 18;
    other.name = L"My renamed shortcut"; alive = true; calls = 0; now = 2000000000;
    ratings::collect({other}, root / "manual", L"fixture", alive, publish, backend);
    if (calls || !rawg::load(other, root / "metadata")) return 16;
    int requests = 0;
    const rawg::Get fake = [&](const auto&, const auto&, size_t) { ++requests; return response(rawg_item); };
    if (rawg::fetch(73, L"fixture", fake).data->id != 73 || rawg::fetch(74, L"fixture", fake).data ||
        rawg::fetch(73, L"", fake).error.empty() || requests != 2) return 17;
    for (const auto* link : {"https://store.steampowered.com/app/1145360/Hades/", "http://store.steampowered.com/agecheck/app/1145360/", "https://store.steampowered.com/app/1145360?l=polish"})
        if (rawg::steamAppIdFromUrl(link) != 1145360) return 19;
    for (const auto* link : {"https://store.steampowered.com.evil/app/1145360", "https://store.steampowered.com@evil/app/1145360", "https://store.steampowered.com/sub/1145360", "https://store.steampowered.com/bundle/1145360", "https://store.steampowered.com/app/0", "https://store.steampowered.com/app/99999999999999", "https://store.steampowered.com/app/1145360junk"})
        if (rawg::steamAppIdFromUrl(link)) return 20;
    const auto links = rawg::parseStoreLinks(73, R"({"results":[{"store_id":999,"game_id":"73","url":"https://store.steampowered.com/app/1145360/Hades/"},{"store_id":1,"game_id":74,"url":"https://store.steampowered.com/app/620/"}]})");
    if (!links.valid || links.steam_app_id != 1145360 || rawg::parseStoreLinks(73, "{}").valid) return 21;
    if (rawg::parseStoreLinks(73, R"({"results":[{"store_id":620,"url":"https://www.gog.com/game/hades"}]})").steam_app_id ||
        rawg::parseStoreLinks(73, R"({"results":[{"url":"https://store.steampowered.com/app/1/"},{"url":"https://store.steampowered.com/app/2/"}]})").steam_app_id) return 22;
    requests = 0;
    const auto stores = rawg::stores(73, L"fixture&key", [&](const auto& address, const auto&, size_t) {
        ++requests;
        if (address != L"https://api.rawg.io/api/games/73/stores?key=fixture%26key&page_size=40") return response("{}", 400);
        return response(R"({"results":[{"game_id":73,"url":"https://store.steampowered.com/app/1145360/"}]})");
    });
    if (!stores.valid || stores.steam_app_id != 1145360 || requests != 1) return 23;
    std::cout << "Ratings: matching, privacy, request budgets, persistent cache/backoff and RAWG identification passed\n";
}
