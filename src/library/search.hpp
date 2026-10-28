#pragma once

#include "steam.hpp"
#include "../platform/unicode_text.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

// One folded form of a title for every search in the launcher (TV search, the library and
// desktop fields, Ctrl+K): lowercase in any language, without diacritics ("Żółw" → "zolw",
// "Pokémon" → "pokemon"), without ™ ® ©, apostrophes joined ("Baldur's" → "baldurs"), any
// other punctuation a single space ("Spider-Man: Miles" → "spider man miles"), and Roman
// numerals II–X as digits ("DARK SOULS™ III" → "dark souls 3").
inline std::wstring foldGameTitle(std::wstring_view text) {
    if (text.empty()) return {};
    // Decomposed: a letter and its accents become separate code points (é → e + ◌́). Canonical
    // only: the compatibility form would turn ™ into "TM".
    const auto lower = unicode_text::lower(unicode_text::decompose(text));
    std::wstring folded;
    folded.reserve(lower.size());
    bool space = true;
    const auto put = [&](wchar_t c) { folded += c; space = false; };
    for (const wchar_t c : lower) {
        if (c >= 0x0300 && c <= 0x036F) continue;              // Combining accents.
        if (c == L'\'' || c == 0x2019 || c == 0x2018 || c == L'`' || c == 0x00B4) continue; // Joined.
        if (c == 0x2122 || c == 0x00AE || c == 0x00A9) continue; // ™ ® ©
        switch (c) { // Letters that do not decompose.
        case 0x0142: put(L'l'); continue; // ł
        case 0x0111: put(L'd'); continue; // đ
        case 0x00F8: put(L'o'); continue; // ø
        case 0x0131: put(L'i'); continue; // ı
        case 0x00DF: put(L's'); put(L's'); continue; // ß
        case 0x00E6: put(L'a'); put(L'e'); continue; // æ
        case 0x0153: put(L'o'); put(L'e'); continue; // œ
        default: break;
        }
        const bool word = (c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || c > 0x7F;
        if (word && !unicode_text::isLetterOrDigit(c) && c > 0x7F) { if (!space) { folded += L' '; space = true; } continue; }
        if (word) put(c);
        else if (!space) { folded += L' '; space = true; }
    }
    while (!folded.empty() && folded.back() == L' ') folded.pop_back();
    // Roman numerals as whole words (I stays: it is also a word).
    static constexpr std::pair<std::wstring_view, std::wstring_view> numerals[]{
        {L"ii", L"2"}, {L"iii", L"3"}, {L"iv", L"4"}, {L"v", L"5"}, {L"vi", L"6"}, {L"vii", L"7"}, {L"viii", L"8"}, {L"ix", L"9"}, {L"x", L"10"}};
    std::wstring result;
    result.reserve(folded.size());
    size_t start = 0;
    while (start <= folded.size()) {
        const auto end = std::min(folded.find(L' ', start), folded.size());
        std::wstring_view part(folded.data() + start, end - start);
        for (const auto& [roman, digit] : numerals) if (part == roman) { part = digit; break; }
        if (!result.empty()) result += L' ';
        result += part;
        start = end + 1;
    }
    return result;
}

// How well a folded title answers a folded query; 0 = not at all. Every word of the query must
// appear in the title, in any order ("gate baldurs"); written together it also matches
// ("spiderman" finds "spider man"). 4: the title starts with the query, 3: a word starts with
// the first query word, 2: every word starts a title word, 1: inside words.
inline int titleMatch(std::wstring_view title, std::wstring_view query) {
    if (query.empty()) return 1;
    if (title.empty()) return 0;
    if (title.starts_with(query)) return 4;
    const auto wordStart = [&](size_t at) { return at == 0 || title[at - 1] == L' '; };
    int score = 3;
    bool first = true, all = true;
    size_t start = 0;
    while (start < query.size()) {
        const auto end = std::min(query.find(L' ', start), query.size());
        const auto word = query.substr(start, end - start);
        start = end + 1;
        if (word.empty()) continue;
        size_t at = title.find(word);
        if (at == std::wstring_view::npos) { score = 0; break; }
        bool starts = false;
        for (size_t i = at; i != std::wstring_view::npos; i = title.find(word, i + 1)) if (wordStart(i)) { starts = true; break; }
        if (first && !starts) score = 2;
        all = all && starts;
        first = false;
    }
    if (score) return score == 3 ? 3 : all ? 2 : 1;
    // Typed together: "spiderman", "finalfantasy".
    std::wstring compact_title, compact_query;
    for (const wchar_t c : title) if (c != L' ') compact_title += c;
    for (const wchar_t c : query) if (c != L' ') compact_query += c;
    if (compact_query.size() < 3) return 0;
    const auto at = compact_title.find(compact_query);
    return at == std::wstring::npos ? 0 : at == 0 ? 3 : 1;
}
inline int titleMatch(const std::wstring& title, const std::wstring& query) { return titleMatch(std::wstring_view(title), std::wstring_view(query)); }

// Title order for every list: the user's language (Ł after L, not after Z), case as a person
// reads it, word by word ("A Hat in Time" before "Abzu"), numbers by value ("Game 2" before
// "Game 10"), ™ ® © ignored.
inline int compareTitles(std::wstring_view a, std::wstring_view b) {
    const auto plain = [](std::wstring_view text, std::wstring& storage) {
        if (text.find_first_of(L"\u2122\u00AE\u00A9") == std::wstring_view::npos) return text;
        storage.assign(text);
        std::erase_if(storage, [](wchar_t c) { return c == 0x2122 || c == 0x00AE || c == 0x00A9; });
        return std::wstring_view(storage);
    };
    std::wstring left_storage, right_storage;
    a = plain(a, left_storage); b = plain(b, right_storage);
    return unicode_text::compareNatural(a, b);
}

// Titles matching the phrase (every word, any order), the best matches first, then by title.
// Each title is folded once. Hidden and private games only match with `include_private`
// (Ctrl+H for this session).
inline std::vector<Game> searchPublicGames(const std::vector<Game>& games, const std::wstring& phrase, bool include_private = false) {
    std::vector<Game> matches;
    const auto query = foldGameTitle(phrase);
    if (query.empty()) return matches;
    struct Match { int score; const Game* game; };
    std::vector<Match> found;
    for (const auto& game : games) {
        if (!isGame(game) || (!include_private && game.visibility != GameVisibility::public_game)) continue;
        if (const int score = titleMatch(foldGameTitle(game.name), query)) found.push_back({score, &game});
    }
    std::stable_sort(found.begin(), found.end(), [](const Match& a, const Match& b) {
        return a.score != b.score ? a.score > b.score : compareTitles(a.game->name, b.game->name) < 0;
    });
    matches.reserve(found.size());
    for (const auto& match : found) matches.push_back(*match.game);
    return matches;
}
