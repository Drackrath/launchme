#include "steamgriddb_search.hpp"
#include "../platform/ascii_text.hpp"
#include "../net/json.hpp"
#include "../net/url_encoding.hpp"
#include "../platform/text_encoding.hpp"
#include "../i18n/i18n.hpp"
#include "../platform/unicode_text.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <ctime>
#include <cwctype>
#include <numeric>
#include <set>

namespace {
using Words = std::vector<std::wstring>;
std::wstring join(const Words& words, std::wstring_view separator = L" ") {
    std::wstring out;
    for (const auto& word : words) { if (!out.empty()) out += separator; out += word; }
    return out;
}
Words words(std::wstring_view value) {
    if (value.empty() || value.size() > 256) return {};
    std::wstring clean(value);
    // Width/compatibility folding can turn ™ into the letters TM attached to a word.
    std::erase_if(clean, [](wchar_t c) { return c == L'™' || c == L'®' || c == L'©'; });
    if (clean.empty()) return {};
    const auto folded = unicode_text::lower(unicode_text::decomposeCompatible(clean));
    Words result; std::wstring current;
    const auto flush = [&] { if (!current.empty()) { result.push_back(std::move(current)); current.clear(); } };
    for (wchar_t ch : folded) {
        if (unicode_text::isMark(ch)) continue;
        if (ch == L'™' || ch == L'®' || ch == L'©' || ch == L'\'' || ch == L'’' || ch == L'.') continue;
        if (ch == L'&') { flush(); result.emplace_back(L"and"); continue; }
        if (!unicode_text::isLetterOrDigit(ch)) { flush(); continue; }
        // Also accept "Borderlands3" while keeping sequel/year numbers significant.
        const bool digit = ch >= L'0' && ch <= L'9';
        if (!current.empty() && digit != (current.back() >= L'0' && current.back() <= L'9')) flush();
        switch (ch) {
        case L'ł': current += L'l'; break;
        case L'ø': current += L'o'; break;
        case L'đ': current += L'd'; break;
        case L'æ': current += L"ae"; break;
        case L'œ': current += L"oe"; break;
        case L'ß': current += L"ss"; break;
        default: current += ch; break;
        }
    }
    flush();
    std::erase(result, L"tm");
    constexpr std::array<std::wstring_view, 20> roman{L"i", L"ii", L"iii", L"iv", L"v", L"vi", L"vii", L"viii", L"ix", L"x", L"xi", L"xii", L"xiii", L"xiv", L"xv", L"xvi", L"xvii", L"xviii", L"xix", L"xx"};
    for (size_t i = 1; i < result.size(); ++i) {
        const auto found = std::find(roman.begin(), roman.end(), result[i]);
        if (found != roman.end()) result[i] = std::to_wstring(found - roman.begin() + 1);
    }
    return result;
}
Words core(Words result) {
    // Only known trailing packaging/store labels; never strip subtitles, remakes,
    // DLC, "Remastered", "VR" or the numbers identifying a sequel/year.
    constexpr std::wstring_view suffixes[]{L"game of the year edition", L"game of the year", L"digital deluxe edition", L"digital premium edition",
        L"collectors edition", L"collector s edition", L"complete edition",
        L"ultimate edition", L"deluxe edition", L"premium edition", L"standard edition", L"gold edition", L"goty edition", L"goty",
        L"epic games store", L"epic games", L"gogcom", L"windows edition"};
    for (bool removed = true; removed && result.size() > 1;) {
        removed = false;
        const auto title = join(result);
        for (const auto suffix : suffixes) if (title.size() > suffix.size() + 1 && title.ends_with(suffix) && title[title.size() - suffix.size() - 1] == L' ') {
            result = words(title.substr(0, title.size() - suffix.size() - 1)); removed = true; break;
        }
    }
    if (result.size() > 1 && result.front() == L"the") result.erase(result.begin());
    return result;
}
Words numbers(const Words& value) {
    Words result;
    for (const auto& word : value) if (word.find_first_not_of(L"0123456789") == word.npos) result.push_back(word);
    return result;
}
Words qualifiers(const Words& value) {
    Words result;
    for (const auto& word : value)
        if (word == L"demo" || word == L"prologue" || word == L"remastered" || word == L"remaster" || word == L"remake" || word == L"redux" ||
            word == L"soundtrack" || word == L"dlc" || word == L"vr" || word == L"beta" || word == L"alpha" || word == L"server" ||
            word == L"collection" || word == L"bundle" || word == L"trilogy" || word == L"anthology" || word == L"definitive" ||
            word == L"enhanced" || word == L"special" || word == L"directors") result.push_back(word);
    std::sort(result.begin(), result.end()); return result;
}
size_t editDistance(std::wstring_view a, std::wstring_view b) {
    std::vector<size_t> previous(b.size() + 1), older(b.size() + 1), current(b.size() + 1);
    std::iota(previous.begin(), previous.end(), size_t{0});
    for (size_t i = 1; i <= a.size(); ++i) {
        current[0] = i;
        for (size_t j = 1; j <= b.size(); ++j) {
            current[j] = std::min({previous[j] + 1, current[j - 1] + 1, previous[j - 1] + (a[i - 1] != b[j - 1])});
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) current[j] = std::min(current[j], older[j - 2] + 1);
        }
        older.swap(previous); previous.swap(current);
    }
    return previous.back();
}
std::vector<std::wstring> queries(const std::wstring& title) {
    std::vector<std::wstring> result{title};
    const auto add = [&](std::wstring value) {
        if (value.size() >= 3 && std::find(result.begin(), result.end(), value) == result.end() && result.size() < 4) result.push_back(std::move(value));
    };
    auto tokens = core(words(title)); add(join(tokens));
    if (tokens.size() >= 2) { auto prefix = tokens; prefix.pop_back(); add(join(prefix)); }
    // Autocomplete itself may not tolerate a typo. A distinctive word can retrieve
    // candidates which we still compare against the FULL original title.
    if (tokens.size() >= 2) {
        const auto longest = std::max_element(tokens.begin(), tokens.end(), [](const auto& a, const auto& b) { return a.size() < b.size(); });
        if (longest->size() >= 5) add(*longest);
    }
    return result;
}
// steam: a Steam AppID; game: a SteamGridDB game ID (both answer one game object).
GridSearch fetch(const std::wstring& query, const std::wstring& key, const ArtworkHttp& get, uint32_t steam = 0, uint32_t game = 0) {
    const auto url = steam ? L"https://www.steamgriddb.com/api/v2/games/steam/" + std::to_wstring(steam) :
        game ? L"https://www.steamgriddb.com/api/v2/games/id/" + std::to_wstring(game) :
        L"https://www.steamgriddb.com/api/v2/search/autocomplete/" + url_encoding::component(std::wstring_view(query));
    const bool single = steam || game;
    const auto response = get(url, L"Accept: application/json\r\nAuthorization: Bearer " + key + L"\r\n", 512 * 1024);
    if (response.status == 404) return {};
    if (response.status == 401 || response.status == 403) return {{}, tr(L"SteamGridDB odrzucił klucz API.")};
    if (response.status == 429) return {{}, tr(L"Limit zapytań SteamGridDB. Spróbuj ponownie za chwilę.")};
    if (response.status != 200) return {{}, tr(L"Nie można pobrać grafik ze SteamGridDB (HTTP ") + std::to_wstring(response.status) + L")."};
    const auto root = json::parse({reinterpret_cast<const char*>(response.body.data()), response.body.size()});
    const auto data = root ? root->get("data") : nullptr;
    if (!data || data->type != (single ? json::Value::Type::object : json::Value::Type::array)) return {{}, tr(L"Nieprawidłowa odpowiedź SteamGridDB.")};
    GridSearch result; std::set<uint32_t> seen;
    const auto add = [&](const json::Value& v) {
        const auto id = v.get("id"), name = v.get("name");
        if (!id || id->type != json::Value::Type::number || !std::isfinite(id->numeric) || id->numeric < 1 || id->numeric > UINT32_MAX || std::floor(id->numeric) != id->numeric ||
            !name || name->type != json::Value::Type::string || name->text.empty() || name->text.size() > 1024 || !seen.insert(static_cast<uint32_t>(id->numeric)).second) return;
        const auto title = text_encoding::wide(name->text);
        if (title.empty()) return;
        // release_date: Unix seconds; it tells apart games sharing a title ("Revenant" 1999 / 2024).
        int year = 0;
        if (const auto released = v.get("release_date"); released && released->type == json::Value::Type::number &&
            std::isfinite(released->numeric) && released->numeric > 0 && released->numeric < 4102444800.0) {
            const time_t seconds = static_cast<time_t>(released->numeric); tm utc{};
            if (!gmtime_s(&utc, &seconds)) year = utc.tm_year + 1900;
        }
        result.games.push_back({{static_cast<uint32_t>(id->numeric), false}, title, year});
    };
    if (single) add(*data);
    else for (const auto& item : data->elements) { add(item); if (result.games.size() == 32) break; }
    return result;
}
// "https://www.steamgriddb.com/game/5247473" or "sgdb:5247473": that exact SteamGridDB game.
// 0 = not such a query; UINT32_MAX = such a query with an invalid ID.
uint32_t gridGameId(std::wstring_view query) {
    std::wstring text(query);
    for (auto& c : text) c = static_cast<wchar_t>(std::towlower(c));
    std::wstring digits;
    if (const auto at = text.find(L"steamgriddb.com/game/"); at != std::wstring::npos) {
        for (auto i = at + 21; i < text.size() && std::iswdigit(text[i]); ++i) digits += text[i];
    } else if (text.starts_with(L"sgdb:")) {
        digits = text.substr(5);
        while (!digits.empty() && digits.front() == L' ') digits.erase(0, 1);
        if (digits.find_first_not_of(L"0123456789") != std::wstring::npos) return UINT32_MAX;
    } else return 0;
    uint32_t id = 0;
    const auto bytes = text_encoding::utf8(digits);
    const auto [end, error] = std::from_chars(bytes.data(), bytes.data() + bytes.size(), id);
    return digits.empty() || error != std::errc{} || end != bytes.data() + bytes.size() || !id || id == UINT32_MAX ? UINT32_MAX : id;
}
GridSearch search(const std::wstring& title, const std::wstring& key, const ArtworkHttp& get) {
    GridSearch result; std::set<uint32_t> seen;
    for (const auto& query : queries(title)) {
        auto page = fetch(query, key, get);
        if (!page.error.empty()) return {{}, std::move(page.error)}; // No retries/fuzzy requests after 401, 429 or network errors.
        for (auto& game : page.games) if (seen.insert(game.source.id).second) result.games.push_back(std::move(game));
        const auto match = matchGridGame(title, result.games);
        if (match.id) {
            const auto found = std::find_if(result.games.begin(), result.games.end(), [&](const auto& g) { return g.source.id == match.id; });
            if (gridTitleScore(title, found->name) >= 960) break;
        }
    }
    std::stable_sort(result.games.begin(), result.games.end(), [&](const auto& a, const auto& b) { return gridTitleScore(title, a.name) > gridTitleScore(title, b.name); });
    if (result.games.size() > 32) result.games.resize(32);
    return result;
}
bool validKey(const std::wstring& key) { return !key.empty() && key.size() <= 1024 && key.find_first_of(L"\r\n\0", 0, 3) == key.npos; }
std::wstring trimmed(const std::wstring& text) { return std::wstring(ascii_text::trim(text)); }
}
int gridTitleScore(std::wstring_view title, std::wstring_view candidate) {
    const auto full_a = words(title), full_b = words(candidate);
    if (full_a.empty() || full_b.empty()) return 0;
    auto a = core(full_a), b = core(full_b);
    if (a.empty() || b.empty() || numbers(a) != numbers(b) || qualifiers(a) != qualifiers(b)) return 0;
    if (join(full_a, L"") == join(full_b, L"")) return 1000;
    if (join(a, L"") == join(b, L"")) return 960;
    if (a.size() != b.size()) return 0; // Never fuzzy-match a base game to a different subtitle.
    const auto left = join(a), right = join(b);
    std::sort(a.begin(), a.end()); std::sort(b.begin(), b.end());
    if (a == b) return 930;
    const auto length = std::max(left.size(), right.size());
    if (std::min(left.size(), right.size()) < 8) return 0;
    const auto distance = editDistance(left, right);
    if (distance > 2 || distance * 100 > length * 12) return 0;
    return 900 - static_cast<int>(distance * 100 / length);
}
GridSource matchGridGame(std::wstring_view title, const std::vector<GridGame>& candidates) {
    int first = 0, second = 0; GridSource selected; std::set<uint32_t> seen;
    for (const auto& candidate : candidates) {
        if (!candidate.source.id || !seen.insert(candidate.source.id).second) continue;
        const auto score = gridTitleScore(title, candidate.name);
        if (score > first) { second = first; first = score; selected = candidate.source; }
        else second = std::max(second, score);
    }
    return first >= 880 && first - second >= 35 ? selected : GridSource{};
}
GridSearch searchGridGames(const std::wstring& query, const std::wstring& key, const ArtworkHttp& get) {
    if (!validKey(key)) return {{}, tr(L"Brak poprawnego klucza SteamGridDB.")};
    const auto title = trimmed(query);
    if (title.empty()) return {{}, tr(L"Wpisz nazwę gry lub Steam AppID.")};
    if (title.size() > 160) return {{}, tr(L"Zbyt długa nazwa.")};
    GridSearch result;
    const auto bytes = text_encoding::utf8(title);
    if (const auto game = gridGameId(title)) {
        if (game == UINT32_MAX) return {{}, tr(L"Nieprawidłowy identyfikator SteamGridDB.")};
        result = fetch(title, key, get, 0, game);
    } else if (!bytes.empty() && bytes.find_first_not_of("0123456789") == bytes.npos) {
        uint32_t app = 0; const auto [end, error] = std::from_chars(bytes.data(), bytes.data() + bytes.size(), app);
        if (error != std::errc{} || end != bytes.data() + bytes.size() || !app) return {{}, tr(L"Nieprawidłowy Steam AppID.")};
        result = fetch(title, key, get, app);
    } else result = search(title, key, get);
    if (result.games.empty() && result.error.empty()) result.error = tr(L"Nie znaleziono gry w SteamGridDB.");
    return result;
}
GridMatch resolveGridGame(const std::wstring& name, const std::wstring& key, const ArtworkHttp& get) {
    if (!validKey(key)) return {{}, tr(L"Brak poprawnego klucza SteamGridDB.")};
    const auto title = trimmed(name);
    if (title.empty() || title.size() > 160) return {{}, tr(L"Nie znaleziono tej gry w SteamGridDB.")};
    // Here a numeric title (e.g. "140") is a title, never a Steam AppID.
    const auto result = search(title, key, get);
    if (!result.error.empty()) return {{}, result.error};
    const auto match = matchGridGame(title, result.games);
    return {match, match.id ? std::wstring{} : std::wstring(tr(L"Nie znaleziono tej gry w SteamGridDB."))};
}
