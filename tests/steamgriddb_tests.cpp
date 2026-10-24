#include "../src/artwork/steamgriddb.hpp"
#include "../src/ui/artwork_browser.hpp"
#include "image_fixture.hpp"
#include "../src/platform/text_encoding.hpp"
#include <iostream>

namespace {
HttpResponse jsonReply(uint32_t status, const std::string& text) { return {status, {text.begin(), text.end()}}; }
const std::string page = R"({"success":true,"data":[{"id":77,"width":512,"height":512,"url":"https://cdn.example/full.png","thumb":"https://cdn.example/thumb.png","author":{"name":"Artist"}}]})";
}

int main() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const auto root = std::filesystem::temp_directory_path() / (L"launchme-grid-tests-" + std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { std::filesystem::path root; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(root, ec); CoUninitialize(); } } cleanup{root};
    const auto cache = root / "covers";
    Game game{620, L"Portal 2"};
    std::vector<std::wstring> urls;
    const ArtworkHttp fallback = [&](const std::wstring& url, const std::wstring& headers, size_t) {
        urls.push_back(url);
        if (headers.find(L"Bearer synthetic") == std::wstring::npos) return jsonReply(401, "{}");
        if (url.find(L"/steam/620") != std::wstring::npos) return jsonReply(404, "{}");
        if (url.find(L"/autocomplete/") != std::wstring::npos)
            return jsonReply(200, R"({"data":[{"id":90,"name":"Portal 2 Demo"},{"id":99,"name":"Portal 2"}]})");
        if (url.find(L"/game/99") != std::wstring::npos) return jsonReply(200, page);
        return jsonReply(500, "{}");
    };
    const auto result = browseSteamGridDB(game, ArtworkKind::cover, 0, {}, L"synthetic", fallback);
    if (urls.size() != 3 || !result.error.empty() || result.assets.size() != 1 || result.source.steam ||
        result.source.id != 99 || result.assets[0].author != L"Artist") return 1;
    urls.clear();
    const auto next = browseSteamGridDB(game, ArtworkKind::cover, 1, result.source, L"synthetic", fallback);
    if (urls.size() != 1 || urls[0].find(L"page=1") == std::wstring::npos || !next.error.empty()) return 2;
    Game gog;
    gog.kind = GameKind::store; gog.source = GameSource::gog; gog.source_id = "620"; gog.name = L"Portal 2";
    urls.clear();
    const auto external = browseSteamGridDB(gog, ArtworkKind::hero, 0, {}, L"synthetic", fallback);
    if (!external.error.empty() || urls.size() != 2 || urls.back().find(L"/heroes/game/") == std::wstring::npos) return 3;
    for (const auto& url : urls) if (url.find(L"/steam/") != std::wstring::npos) return 4;
    // Posters and wide tiles are grids of their own dimensions.
    urls.clear();
    const auto posters = browseSteamGridDB(game, ArtworkKind::poster, 0, {99, false}, L"synthetic", fallback);
    if (urls.size() != 1 || urls[0].find(L"/grids/game/99") == std::wstring::npos ||
        urls[0].find(L"dimensions=600x900,342x482,660x930") == std::wstring::npos || posters.assets.size() != 1) return 23;
    urls.clear();
    browseSteamGridDB(game, ArtworkKind::landscape, 0, {99, false}, L"synthetic", fallback);
    if (urls.size() != 1 || urls[0].find(L"/grids/game/99") == std::wstring::npos ||
        urls[0].find(L"dimensions=920x430,460x215") == std::wstring::npos) return 24;
    int calls = 0;
    const auto refused = browseSteamGridDB(game, ArtworkKind::logo, 0, {}, L"synthetic", [&](const auto&, const auto&, size_t) {
        ++calls; return jsonReply(401, "{}");
    });
    if (refused.error.empty() || calls != 1) return 5;
    const auto no_key = browseSteamGridDB(game, ArtworkKind::cover, 0, {}, {}, fallback);
    if (no_key.error.empty()) return 6;
    if (parseGridPage("{}").error.empty()) return 7;
    if (!parseGridPage(R"({"data":[{"id":1,"url":"file:///bad","width":512,"height":512}]})").assets.empty()) return 8;
    const auto image = pngFixture();
    if (!decodableArtwork(image) || decodableArtwork({1, 2, 3})) return 9;
    bool leaked = false;
    auto downloaded = downloadGridImage(result.assets.front(), true, [&](const auto&, const auto& headers, size_t) {
        leaked |= headers.find(L"Authorization") != std::wstring::npos;
        return HttpResponse{200, image};
    });
    if (downloaded != image || leaked) return 10;
    if (!cacheArtworkVariant(game, ArtworkKind::cover, 77, image, cache) ||
        !selectArtworkVariant(game, ArtworkKind::cover, 77, cache) || selectedArtworkId(game, ArtworkKind::cover, cache) != 77) return 11;
    if (artworkForGame(game, ArtworkKind::cover, cache, {}) != image || selectedArtworkId(gog, ArtworkKind::cover, cache)) return 12;
    if (selectArtworkVariant(game, ArtworkKind::cover, 78, cache) || selectedArtworkId(game, ArtworkKind::cover, cache) != 77) return 13;
    const auto second = pngFixture(210);
    if (!cacheArtworkVariant(game, ArtworkKind::hero, 77, second, cache) || !selectArtworkVariant(game, ArtworkKind::hero, 77, cache)) return 14;
    if (selectedArtwork(game, ArtworkKind::cover, cache) != image || selectedArtwork(game, ArtworkKind::hero, cache) != second) return 15;
    auto catalog = game; catalog.kind = GameKind::catalog;
    if (artworkForGame(catalog, ArtworkKind::cover, cache, {}) != image || gameCoverPath(game, cache) != artworkVariantPath(game, ArtworkKind::cover, 77, cache)) return 16;
    for (const auto kind : artwork_ui::kinds) {
        artwork_ui::Layout layout{1600, 900, kind};
        const auto scroll = layout.ensureVisible(23, 24, 0);
        const auto rect = layout.cell(23, scroll);
        if (rect.top < layout.top || rect.bottom > layout.bottom() || layout.hit(rect.left + 3, rect.top + 3, 24, scroll) != 23) return 17;
    }
    const auto candidates = searchGridGames(L"Alan Wake II", L"synthetic", [](const auto& url, const auto&, size_t) {
        if (url.find(L"/autocomplete/Alan%20Wake%20II") == std::wstring::npos) return jsonReply(400, "{}");
        return jsonReply(200, R"({"data":[{"id":12,"name":"Alan Wake 2"},{"id":13,"name":"Alan Wake"}]})");
    });
    if (candidates.games.size() != 2 || candidates.games[0].name != L"Alan Wake 2") return 18;
    const auto by_id = searchGridGames(L"620", L"synthetic", [](const auto& url, const auto&, size_t) {
        if (!url.ends_with(L"/games/steam/620")) return jsonReply(400, "{}");
        return jsonReply(200, R"({"data":{"id":99,"name":"Portal 2"}})");
    });
    if (by_id.games.size() != 1 || by_id.games[0].source.id != 99) return 19;
    const auto identity = gameKey(gog);
    const auto borrowed = browseSteamGridDB(gog, ArtworkKind::cover, 0, by_id.games[0].source, L"synthetic", fallback);
    if (borrowed.assets.size() != 1 || gameKey(gog) != identity || gog.app_id != 0) return 20;
    if (!cacheArtworkVariant(gog, ArtworkKind::cover, 77, image, cache) || !selectArtworkVariant(gog, ArtworkKind::cover, 77, cache) ||
        selectedArtwork(gog, ArtworkKind::cover, cache) != image) return 21;
    int invalid_requests = 0;
    const auto invalid = searchGridGames(L"99999999999999999", L"synthetic", [&](const auto&, const auto&, size_t) { ++invalid_requests; return HttpResponse{}; });
    if (invalid.error.empty() || invalid_requests) return 22;
    // A SteamGridDB link or "sgdb:<id>" names that exact game; release dates become years.
    for (const auto* query : {L"https://www.steamgriddb.com/game/5247473", L"steamgriddb.com/game/5247473/grids", L"sgdb:5247473", L"SGDB: 5247473"}) {
        const auto linked = searchGridGames(query, L"synthetic", [](const auto& url, const auto&, size_t) {
            if (!url.ends_with(L"/games/id/5247473")) return jsonReply(400, "{}");
            return jsonReply(200, R"({"success":true,"data":{"id":5247473,"name":"Revenant","release_date":941328000}})");
        });
        if (linked.games.size() != 1 || linked.games[0].source.id != 5247473 || linked.games[0].source.steam ||
            linked.games[0].year != 1999 || !linked.error.empty()) return 23;
    }
    int bad_link_requests = 0;
    if (searchGridGames(L"sgdb:abc", L"synthetic", [&](const auto&, const auto&, size_t) { ++bad_link_requests; return HttpResponse{}; }).error.empty() ||
        bad_link_requests) return 24;
    const auto dated = searchGridGames(L"Revenant", L"synthetic", [](const auto& url, const auto&, size_t) {
        if (url.find(L"/autocomplete/") == std::wstring::npos) return jsonReply(400, "{}");
        return jsonReply(200, R"({"data":[{"id":5389392,"name":"Revenant","release_date":1704067200},{"id":5247473,"name":"Revenant","release_date":941328000}]})");
    });
    if (dated.games.size() != 2 || dated.games[0].year != 2024 || dated.games[1].year != 1999) return 25;
    const auto check = [](bool ok, const char* label) { if (!ok) std::cerr << "SGDB match: " << label << '\n'; return ok; };
    for (const auto& pair : {std::pair{L"Alan Wake II", L"Alan Wake 2"}, {L"Control Ultimate Edition", L"Control"},
        {L"Batman™: Arkham Asylum GOTY Edition", L"Batman: Arkham Asylum"}, {L"Assassin’s Creed® II", L"Assassin's Creed 2"},
        {L"Pokémon", L"POKEMON"}, {L"Final Fantasy VII", L"Final Fantasy 7"}, {L"Borderlands3", L"Borderlands 3"},
        {L"S.T.A.L.K.E.R.: Call of Pripyat", L"STALKER Call of Pripyat"}, {L"The Witcher 3: Wild Hunt (Epic Games Store)", L"Witcher 3 Wild Hunt"}})
        if (!check(gridTitleScore(pair.first, pair.second) >= 960, "normalization / packaging")) {
            std::cerr << text_encoding::utf8(pair.first) << " -> " << text_encoding::utf8(pair.second) << " score=" << gridTitleScore(pair.first, pair.second) << '\n'; return 25;
        }
    if (!check(gridTitleScore(L"Death Stradning", L"Death Stranding") >= 880, "transposed letters")) return 26;
    for (const auto& pair : {std::pair{L"Alan Wake II", L"Alan Wake"}, {L"Mafia II", L"Mafia III"}, {L"F1 2023", L"F1 2024"},
        {L"Portal 2", L"Portal 2 Demo"}, {L"Control", L"Control Soundtrack"}, {L"Alan Wake", L"Alan Wake Remastered"},
        {L"Skyrim", L"Skyrim VR"}, {L"Mafia", L"Mafia Definitive Edition"}, {L"Death Stranding", L"Death Stranding Director's Cut"},
        {L"Portal", L"Postal"}, {L"Tomb Raider", L"Rise of the Tomb Raider"}})
        if (!check(gridTitleScore(pair.first, pair.second) == 0, "sequel / remake / different title rejected")) return 27;
    if (!check(matchGridGame(L"Control", {{{1, false}, L"Control"}, {{2, false}, L"Control"}}).id == 0, "ambiguous exact IDs")) return 28;
    if (!check(matchGridGame(L"Ghost Reconn", {{{1, false}, L"Ghost Recon"}, {{2, false}, L"Ghost Recoin"}}).id == 0, "ambiguous fuzzy results")) return 29;
    if (!check(matchGridGame(L"Control Ultimate Edition", {{{1, false}, L"Control"}, {{2, false}, L"Control Ultimate Edition"}}).id == 2, "exact edition preferred")) return 30;
    urls.clear();
    auto epic = gog; epic.source = GameSource::epic; epic.source_id = "fixture-epic"; epic.name = L"Batman™: Arkham Asylum GOTY Edition";
    const auto fuzzy = browseSteamGridDB(epic, ArtworkKind::cover, 0, {}, L"synthetic", [&](const auto& url, const auto&, size_t) {
        urls.push_back(url);
        if (url.find(L"/autocomplete/batman%20arkham%20asylum") != url.npos)
            return jsonReply(200, R"({"data":[{"id":19,"name":"Batman: Arkham City"},{"id":20,"name":"Batman: Arkham Asylum"}]})");
        if (url.find(L"/game/20?") != url.npos) return jsonReply(200, page);
        return jsonReply(200, R"({"data":[]})");
    });
    if (!check(fuzzy.source.id == 20 && fuzzy.assets.size() == 1 && urls.size() == 3, "automatic browser fallback by cleaned full title")) return 31;
    for (const auto& url : urls) if (url.find(L"/steam/") != url.npos) return 32;
    calls = 0;
    const auto numeric_title = resolveGridGame(L"140", L"synthetic", [&](const auto& url, const auto&, size_t) {
        ++calls;
        return url.ends_with(L"/autocomplete/140") ? jsonReply(200, R"({"data":[{"id":1400,"name":"140"}]})") : jsonReply(400, "{}");
    });
    if (!check(numeric_title.source.id == 1400 && calls == 1, "numeric title stays a title")) return 33;
    for (const uint32_t status : {0u, 401u, 403u, 429u, 503u}) {
        calls = 0;
        const auto failed = resolveGridGame(L"Death Stradning Deluxe Edition", L"synthetic", [&](const auto&, const auto&, size_t) { ++calls; return jsonReply(status, "{}"); });
        if (!check(!failed.source.id && !failed.error.empty() && calls == 1, "HTTP error stops fallbacks")) return 34;
    }
    calls = 0;
    const auto missing = resolveGridGame(L"Imaginary Missing Game Deluxe Edition", L"synthetic", [&](const auto&, const auto&, size_t) { ++calls; return jsonReply(200, R"({"data":[]})"); });
    if (!check(!missing.source.id && calls <= 4, "bounded query expansion")) return 35;
    calls = 0;
    const auto typo = resolveGridGame(L"Death Stradning", L"synthetic", [&](const auto& url, const auto&, size_t) {
        ++calls;
        return url.ends_with(L"/autocomplete/death") ? jsonReply(200, R"({"data":[{"id":25,"name":"Death Stranding Director's Cut"},{"id":24,"name":"Death Stranding"}]})") : jsonReply(200, R"({"data":[]})");
    });
    if (!check(typo.source.id == 24 && calls <= 4, "retrieval by short query still scores the entire title")) return 36;
    std::cout << "SGDB routing, paging, image validation, persistent variants and browser geometry passed\n";
}
