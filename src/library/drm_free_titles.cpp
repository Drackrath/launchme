// DRM-free games' names (src/library/drm_free.hpp): folder names cleaned into titles, engine
// products told apart from real names, and RAWG matching for the catalog's games (identify).
#include "drm_free.hpp"
#include "../platform/unix_time.hpp"
#include "drm_free_internal.hpp"
#include "game_ratings.hpp"
#include "../artwork/cover_cache.hpp"
#include "../artwork/identified_artwork.hpp"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cwctype>
#include <regex>
#include <thread>
#include <unordered_set>

namespace drm_free {
using namespace detail;
namespace {

std::wstring joined(const std::vector<std::wstring>& list) {
    std::wstring result;
    for (const auto& word : list) result += word;
    return result;
}
} // namespace

// --- Titles --------------------------------------------------------------------

Title cleanTitle(std::wstring_view folder_name) {
    Title result;
    // "Name.Of.The.Game-GROUP": no spaces at all means dots and dashes are separators.
    const bool scene = trim(folder_name).find(L' ') == std::wstring::npos;
    // Bracketed groups are release noise; a year inside one is a matching hint.
    std::wstring value;
    int depth = 0;
    std::wstring group;
    for (const wchar_t ch : folder_name) {
        if (ch == L'(' || ch == L'[' || ch == L'{') { if (depth++ == 0) group.clear(); continue; }
        if ((ch == L')' || ch == L']' || ch == L'}') && depth > 0) {
            if (--depth == 0) {
                static const std::wregex year(L"(?:^|[^0-9])((?:19|20)[0-9]{2})(?:[^0-9]|$)");
                std::wsmatch match;
                if (!result.year && std::regex_search(group, match, year)) result.year = std::stoi(match[1].str());
                value += L' ';
            }
            continue;
        }
        if (depth) group += ch; else value += ch;
    }
    // Versions and build numbers go before dots turn into word separators.
    const auto erase = [&](const wchar_t* pattern) {
        const std::wregex expression(pattern, std::regex::icase);
        value = std::regex_replace(value, expression, L" ");
    };
    erase(L"(^|[\\s_.\\-])v\\d+(?:[._]\\d+)*[a-z]?(?=$|[\\s_.\\-])");
    erase(L"(^|[\\s_.\\-])\\d+(?:\\.\\d+){2,}[a-z]?(?=$|[\\s_.\\-])");
    erase(L"(^|[\\s_.\\-])(?:build|b|r)[\\s_.]?\\d{3,}(?=$|[\\s_.\\-])");
    erase(L"(^|[\\s_.\\-])\\d{6,}(?=$|[\\s_.\\-])");
    std::replace(value.begin(), value.end(), L'_', L' ');
    // Scene style "Name.Of.The.Game": dots are spaces, except inside an acronym (S.T.A.L.K.E.R.).
    if (scene) {
        std::vector<std::wstring> parts;
        size_t start = 0;
        for (size_t dot = value.find(L'.'); ; dot = value.find(L'.', start)) {
            parts.push_back(value.substr(start, dot == std::wstring::npos ? std::wstring::npos : dot - start));
            if (dot == std::wstring::npos) break;
            start = dot + 1;
        }
        if (parts.size() >= 2) {
            std::wstring rebuilt;
            for (size_t i = 0; i < parts.size(); ++i) {
                if (parts[i].empty()) continue;
                const bool letter = parts[i].size() == 1 && std::iswalpha(parts[i][0]);
                const bool neighbour = (i > 0 && parts[i - 1].size() == 1 && std::iswalpha(parts[i - 1][0])) ||
                                       (i + 1 < parts.size() && parts[i + 1].size() == 1 && std::iswalpha(parts[i + 1][0]));
                if (letter && neighbour) {
                    if (!rebuilt.empty() && rebuilt.back() != L'.') rebuilt += L' ';
                    rebuilt += parts[i] + L'.';
                } else {
                    if (!rebuilt.empty()) rebuilt += L' ';
                    rebuilt += parts[i];
                }
            }
            value = rebuilt;
        }
    }
    // Release group / store suffix: "Game-GOG", "Game-CODEX".
    static const std::unordered_set<std::wstring> groups{L"gog", L"codex", L"plaza", L"skidrow", L"reloaded", L"flt", L"dodi", L"fitgirl",
        L"elamigos", L"tenoke", L"rune", L"empress", L"razor1911", L"prophet", L"cpy", L"hoodlum", L"portable", L"repack", L"drmfree",
        L"drm-free", L"itch", L"itchio", L"steamrip", L"goldberg", L"dinobytes", L"i_know", L"kaos"};
    if (const auto dash = value.rfind(L'-'); dash != std::wstring::npos && dash > 0) {
        const auto suffix = trim(std::wstring_view(value).substr(dash + 1));
        // An unknown all-capitals tag only in scene-style names; "Final-Fantasy-VIII" keeps its numeral.
        const bool capitals = scene && suffix.size() >= 3 && suffix.size() <= 12 && suffix.find(L' ') == std::wstring::npos &&
            std::any_of(suffix.begin(), suffix.end(), [](wchar_t ch) { return std::iswalpha(ch); }) &&
            std::none_of(suffix.begin(), suffix.end(), [](wchar_t ch) { return std::iswlower(ch); }) &&
            suffix.find_first_not_of(L"IVXLC") != std::wstring::npos;
        if (groups.contains(lower(suffix)) || capitals) value.resize(dash);
    }
    // Words: trailing store/platform noise and a trailing year.
    std::vector<std::wstring> tokens;
    for (size_t at = 0; at < value.size();) {
        const auto start = value.find_first_not_of(L" \t", at);
        if (start == std::wstring::npos) break;
        const auto end = value.find_first_of(L" \t", start);
        tokens.push_back(value.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start));
        at = end == std::wstring::npos ? value.size() : end;
    }
    static const std::unordered_set<std::wstring> noise{L"gog", L"drm", L"drmfree", L"drm-free", L"portable", L"repack", L"x64", L"x86",
        L"x86_64", L"amd64", L"win64", L"win32", L"windows", L"win", L"pc", L"goty", L"multi", L"eng", L"english", L"en", L"pl", L"polish",
        L"polski", L"+dlc", L"dlc", L"dlcs", L"incl", L"update", L"hotfix", L"-", L"itch", L"installer", L"rip", L"proper", L"64bit", L"32bit"};
    while (tokens.size() > 1) {
        const auto word = lower(tokens.back());
        const bool multi = word.starts_with(L"multi") && word.size() <= 7 && word.find_first_not_of(L"0123456789", 5) == std::wstring::npos;
        if (noise.contains(word) || multi) { tokens.pop_back(); continue; }
        if (word.size() == 4 && (word.starts_with(L"19") || word.starts_with(L"20")) && word.find_first_not_of(L"0123456789") == std::wstring::npos) {
            if (!result.year) result.year = std::stoi(word);
            tokens.pop_back();
            continue;
        }
        break;
    }
    value.clear();
    for (const auto& token : tokens) { if (!value.empty()) value += L' '; value += token; }
    // "HollowKnight" → "Hollow Knight", "DOOMEternal" → "DOOM Eternal", "Portal2" → "Portal 2".
    // Mostly-capital names ("BALLxPIT", "XCOM2") keep their case boundaries.
    if (value.find(L' ') == std::wstring::npos && value.size() > 4) {
        const auto lowercase = std::count_if(value.begin(), value.end(), [](wchar_t ch) { return std::iswlower(ch) != 0; });
        const bool camel = lowercase >= 3;
        std::wstring split;
        for (size_t i = 0; i < value.size(); ++i) {
            const wchar_t ch = value[i];
            if (i > 0) {
                const wchar_t previous = value[i - 1];
                const bool next_lower = i + 1 < value.size() && std::iswlower(value[i + 1]);
                if ((camel && std::iswlower(previous) && std::iswupper(ch)) ||
                    (camel && std::iswupper(previous) && std::iswupper(ch) && next_lower && i >= 2) ||
                    (std::iswalpha(previous) && std::iswdigit(ch) && value.size() - i <= 3 && i >= 3))
                    split += L' ';
            }
            split += ch;
        }
        value = split;
    }
    result.text = trim(value, L" \t-_,:;+");
    if (result.text.empty()) result.text = trim(folder_name);
    if (result.text.size() > max_name) result.text.resize(max_name);
    return result;
}

bool genericProduct(std::wstring_view product) {
    static const std::unordered_set<std::wstring> generic{L"unity", L"unityplayer", L"unitygame", L"unrealengine", L"ue4", L"ue5", L"ue4game",
        L"ue5game", L"bootstrappackagedgame", L"godot", L"godotengine", L"gamemaker", L"gamemakerstudio", L"gamemakerstudio2", L"game",
        L"launcher", L"gamelauncher", L"electron", L"nwjs", L"renpy", L"rpgmaker", L"rpgmakermv", L"rpgmakermz", L"rpgmakervxace", L"rpgmakerxp",
        L"microsoftwindowsoperatingsystem", L"monogame", L"xna", L"love", L"java", L"python", L"dosbox", L"scummvm", L"application", L"app",
        L"windows", L"win64", L"setup", L"defold", L"cocos2dx", L"sdl", L"sfml", L"adobeair", L"flashplayer", L"shippinggame",
        L"start", L"play", L"main", L"client", L"run", L"nw", L"release", L"shipping", L"launch"};
    const auto name = ratings::normalizedTitle(product);
    if (generic.contains(name)) return true;
    for (const auto* engine : {L"kirikiri", L"scriptingplatform", L"unityplayer", L"unrealengine", L"godotengine", L"gamemakerstudio", L"rpgmaker"})
        if (name.find(engine) != std::wstring::npos) return true;
    return false;
}

// --- RAWG matching ---------------------------------------------------------------

int matchScore(const Title& query, const rawg::Candidate& candidate, size_t rank) {
    const auto wanted = ratings::normalizedTitle(query.text), offered = ratings::normalizedTitle(candidate.name);
    if (wanted.empty() || offered.empty()) return -1000;
    const auto query_words = words(query.text), candidate_words = words(candidate.name);
    int score = 0;
    if (wanted == offered) score = 1000;
    else if (!query_words.empty() && joined(query_words) == joined(candidate_words)) score = 950;
    else {
        size_t shared = 0;
        for (const auto& word : query_words)
            if (std::find(candidate_words.begin(), candidate_words.end(), word) != candidate_words.end()) ++shared;
        if (!shared) return -1000 + static_cast<int>(rank);
        const size_t total = query_words.size() + candidate_words.size() - shared;
        score = static_cast<int>(700 * shared / std::max<size_t>(1, total));
        const auto query_join = joined(query_words), candidate_join = joined(candidate_words);
        if (candidate_join.starts_with(query_join)) score = std::max(score, 820 - 4 * static_cast<int>(candidate_join.size() - query_join.size()));
        else if (query_join.starts_with(candidate_join)) score = std::max(score, 780 - 4 * static_cast<int>(query_join.size() - candidate_join.size()));
    }
    if (query.year && candidate.released.size() >= 4) {
        int year = 0;
        for (size_t i = 0; i < 4; ++i) year = year * 10 + (std::iswdigit(candidate.released[i]) ? candidate.released[i] - L'0' : 0);
        score += year == query.year ? 80 : std::abs(year - query.year) <= 1 ? 30 : -80;
    }
    if (!candidate.platforms.empty()) {
        bool pc = false;
        for (size_t at = 0; at <= candidate.platforms.size();) {
            const auto end = std::min(candidate.platforms.find(L',', at), candidate.platforms.size());
            if (trim(std::wstring_view(candidate.platforms).substr(at, end - at)) == L"PC") pc = true;
            at = end + 1;
        }
        score += pc ? 40 : -150;
    }
    const auto offered_lower = lower(candidate.name), wanted_lower = lower(query.text);
    for (const auto* edition : {L"soundtrack", L" ost", L"artbook", L"demo", L"season pass", L"dlc", L"bundle", L"expansion", L"beta", L"prologue", L"playtest"})
        if (offered_lower.find(edition) != std::wstring::npos && wanted_lower.find(edition) == std::wstring::npos) { score -= 300; break; }
    score += static_cast<int>(rank < 10 ? (10 - rank) * 3 : 0);
    return score;
}

std::optional<size_t> bestMatch(const Title& query, const std::vector<rawg::Candidate>& candidates) {
    std::optional<size_t> best;
    int best_score = 0;
    for (size_t i = 0; i < candidates.size(); ++i) {
        const int score = matchScore(query, candidates[i], i);
        if (score > best_score) { best = i; best_score = score; }
    }
    return best;
}

bool needsIdentity(const Entry& entry, uint64_t now, const IdentifyLimits& limits) {
    return !entry.ignored && !entry.manual_identity && !entry.rawg_id && !entry.executable.empty() && !entry.title.empty() &&
           (!entry.checked || now >= entry.checked + limits.retry_unmatched || entry.checked > now + 86400);
}

void identify(Catalog& catalog, const std::wstring& key, const IdentifyPaths& paths, const Cancelled& cancelled,
              const std::function<void(const Catalog&)>& changed, const IdentifyBackend& backend, IdentifyLimits limits) {
    const auto now = backend.now ? backend.now() : unix_time::now();
    const auto stop = [&] { return cancelled && cancelled(); };
    if (key.empty() || catalog.rawg_pause > now || stop()) return;
    const auto wait = [&](std::chrono::milliseconds duration) {
        if (backend.wait) return backend.wait(duration) && !stop();
        const auto until = std::chrono::steady_clock::now() + duration;
        while (!stop() && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        return !stop();
    };
    bool requested = false;
    const auto pace = [&] {
        if (requested && !wait(limits.spacing)) return false;
        requested = true;
        return true;
    };
    const auto pause = [&] { catalog.rawg_pause = now + limits.pause; if (changed) changed(catalog); };
    const auto get = backend.get ? backend.get : rawg::Get(httpGetPrivate);
    size_t used = 0;
    for (size_t index = 0; index < catalog.games.size(); ++index) {
        if (used >= limits.games || stop()) return;
        if (!needsIdentity(catalog.games[index], now, limits)) continue;
        ++used;
        const Title query{catalog.games[index].title, catalog.games[index].year};
        if (!pace()) return;
        auto search = rawg::search(query.text, key, get);
        if (!search.error.empty()) { pause(); return; }
        std::optional<rawg::Candidate> pick;
        int pick_score = INT_MIN;
        if (const auto best = bestMatch(query, search.games)) { pick = search.games[*best]; pick_score = matchScore(query, *pick, *best); }
        const auto product = catalog.games[index].product;
        if (pick_score < 1000 && !product.empty() && ratings::normalizedTitle(product) != ratings::normalizedTitle(query.text)) {
            if (!pace()) return;
            const auto second = rawg::search(product, key, get);
            if (!second.error.empty()) { pause(); return; }
            const Title product_query{product, query.year};
            if (const auto best = bestMatch(product_query, second.games)) {
                const int score = matchScore(product_query, second.games[*best], *best);
                if (score > pick_score) { pick = second.games[*best]; pick_score = score; }
            }
        }
        catalog.games[index].checked = now;
        if (!pick) { if (changed) changed(catalog); continue; }
        if (!pace()) return;
        auto details = rawg::fetch(pick->id, key, get);
        if (!details.data) { pause(); return; }
        uint32_t steam = catalog.games[index].steam_app_id;
        if (!steam) {
            if (!pace()) return;
            const auto links = rawg::stores(pick->id, key, get);
            if (!links.valid) { pause(); return; }
            steam = links.steam_app_id;
        }
        if (stop()) return;
        auto& entry = catalog.games[index];
        details.data->steam_app_id = steam;
        const auto game = makeGame(entry);
        if (!rawg::save(game, details, paths.metadata)) continue; // Disk trouble: asked again after a week.
        ratings::saveIdentified(paths.ratings, game, details.data->rating);
        auto name = trim(details.data->name);
        if (name.size() > max_name) name.resize(max_name);
        if (name.empty()) name = entry.title;
        bool source_changed = false;
        if (!selectedArtworkSource(game, paths.covers).id) {
            if (steam) source_changed = selectArtworkSource(game, {steam, true}, paths.covers);
            else if (!backend.sgdb_key.empty() && !stop()) {
                // Several games share a title ("Revenant"): RAWG's year picks the right artwork.
                const auto& released = details.data->details.release_date;
                const int year = released.size() >= 4 ? _wtoi(released.substr(0, 4).c_str()) : 0;
                const auto found = searchGridGames(name, backend.sgdb_key, backend.sgdb_get ? backend.sgdb_get : rawg::Get(httpGet));
                if (const auto source = matchIdentifiedArtwork(name, found.games, year); source.id)
                    source_changed = selectArtworkSource(game, source, paths.covers);
            }
        }
        // Automatic artwork followed the folder title; the identified game gets its own.
        if (source_changed || ratings::normalizedTitle(name) != ratings::normalizedTitle(entry.name)) removeGameArtwork(game, paths.covers, false);
        entry.rawg_id = pick->id;
        entry.name = name;
        entry.steam_app_id = steam;
        if (changed) changed(catalog);
    }
}
} // namespace drm_free
