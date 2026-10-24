#include "identified_artwork.hpp"
#include "../i18n/i18n.hpp"
#include <algorithm>
#include <cstdlib>
#include <cwctype>
#include <unordered_set>

GridSource matchIdentifiedArtwork(std::wstring_view title, const std::vector<GridGame>& candidates, int year) {
    if (year) {
        // Same title, different games: the one released in the confirmed year (±1 for regional dates).
        GridSource dated; std::unordered_set<uint32_t> seen; int found = 0;
        for (const auto& candidate : candidates) {
            if (!candidate.source.id || !candidate.year || std::abs(candidate.year - year) > 1 ||
                gridTitleScore(title, candidate.name) < 960 || !seen.insert(candidate.source.id).second) continue;
            dated = candidate.source; ++found;
        }
        if (found == 1) return dated;
    }
    const auto match = matchGridGame(title, candidates);
    if (match.id) {
        // A single exact title of a different era is someone else's game.
        const auto chosen = std::find_if(candidates.begin(), candidates.end(), [&](const auto& c) { return c.source.id == match.id; });
        if (!year || chosen == candidates.end() || !chosen->year || std::abs(chosen->year - year) <= 1) return match;
        return {};
    }
    // Don't override an ambiguous normal match. Only a long, exact subtitle is
    // accepted here, never a fuzzy suffix ("Heroes of the Lance" vs a different Heroes).
    for (const auto& candidate : candidates) if (gridTitleScore(title, candidate.name) >= 880) return {};
    int words = 0; bool in_word = false;
    for (const auto ch : title) {
        if (std::iswspace(ch)) in_word = false;
        else { if (!in_word) ++words; in_word = true; }
    }
    if (words < 3 || title.size() < 14) return {};
    GridSource selected; std::unordered_set<uint32_t> seen;
    for (const auto& candidate : candidates) {
        if (!candidate.source.id || !seen.insert(candidate.source.id).second) continue;
        const auto colon = candidate.name.find(L':');
        if (colon == candidate.name.npos) continue;
        auto prefix = candidate.name.substr(0, colon);
        for (auto& ch : prefix) ch = static_cast<wchar_t>(std::towlower(ch));
        bool edition = false;
        for (const auto* qualifier : {L"soundtrack", L"dlc", L"demo", L"remaster", L"remake", L"collection", L"bundle", L"trilogy"})
            edition |= prefix.find(qualifier) != prefix.npos;
        if (edition || gridTitleScore(title, std::wstring_view(candidate.name).substr(colon + 1)) != 1000) continue;
        if (selected.id) return {};
        selected = candidate.source;
    }
    return selected;
}

IdentifiedArtworkResult fillIdentifiedArtwork(const Game& game, const std::wstring& title, uint32_t steam_app_id,
    const std::filesystem::path& cache, const std::wstring& key, const std::function<bool()>& active, const ArtworkHttp& get, int year) {
    IdentifiedArtworkResult result;
    if (!isGame(game) || gameKey(game).empty() || !active()) return result;
    std::vector<ArtworkKind> missing;
    // The cover comes first; Flix's two shapes, background and logo are separate slots.
    for (const auto kind : {ArtworkKind::cover, ArtworkKind::hero, ArtworkKind::logo, ArtworkKind::poster, ArtworkKind::landscape}) {
        if (!active()) return result;
        if (!decodableArtwork(cachedArtwork(game, kind, cache))) missing.push_back(kind);
    }
    result.missing = missing.size();
    const GridSource steam{steam_app_id, true};
    if (steam.id && !selectArtworkSource(game, steam, cache)) result.error = tr(L"Nie można zapisać dopasowania grafik.");
    if (missing.empty()) return result;
    if (key.empty()) { result.error = tr(L"Brak klucza SteamGridDB."); return result; }
    HttpBudget budget(20000);
    bool failed = false, resolved = false;
    GridSource fallback;
    // Stop the entire chain on rate limiting / authentication / transport failures.
    // A missing or malformed individual asset can still try the next candidate.
    const ArtworkHttp request = [&](const auto& url, const auto& headers, size_t limit) {
        if (!active() || failed) return HttpResponse{};
        auto response = get(url, headers, limit);
        failed = !response.status || response.status == 401 || response.status == 403 || response.status == 429 || response.status >= 500;
        return response;
    };
    const auto byTitle = [&] {
        if (!resolved && !failed && active()) {
            resolved = true;
            if (!title.empty() && title.find_first_not_of(L"0123456789") == title.npos) {
                const auto match = resolveGridGame(title, key, request); // A numeric title is not an AppID.
                fallback = match.source;
                if (!match.error.empty() && result.error.empty()) result.error = match.error;
            } else {
                const auto search = searchGridGames(title, key, request);
                fallback = matchIdentifiedArtwork(title, search.games, year);
                if (!fallback.id && result.error.empty()) result.error = search.error.empty() ? tr(L"Nie znaleziono tej gry w SteamGridDB.") : search.error;
            }
            if (!steam.id && fallback.id && !selectArtworkSource(game, fallback, cache)) result.error = tr(L"Nie można zapisać dopasowania grafik.");
        }
        return fallback;
    };
    const auto fill = [&](ArtworkKind kind, GridSource source) {
        if (!source.id || failed || !active()) return false;
        const auto page = browseSteamGridDB(game, kind, 0, source, key, request);
        if (!page.error.empty()) { if (result.error.empty()) result.error = page.error; return false; }
        for (size_t i = 0; i < std::min<size_t>(3, page.assets.size()) && !failed && active(); ++i) {
            auto bytes = downloadGridImage(page.assets[i], false, request);
            if (bytes.empty()) continue;
            if (!active()) return false;
            if (!cacheArtworkVariant(game, kind, page.assets[i].id, bytes, cache) ||
                !selectArtworkVariant(game, kind, page.assets[i].id, cache)) {
                result.error = tr(L"Nie można zapisać grafiki w cache."); return false;
            }
            result.images.push_back({kind, std::move(bytes)});
            return true;
        }
        return false;
    };
    for (const auto kind : missing) {
        if (!active() || failed) break;
        if (steam.id && fill(kind, steam)) continue;
        if (!failed) fill(kind, byTitle());
    }
    if (result.images.size() == result.missing) result.error.clear();
    else if (result.error.empty()) result.error = tr(L"Nie udało się uzupełnić wszystkich grafik. Możesz wybrać je w „Zmień grafikę”.");
    return result;
}
