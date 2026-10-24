#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

struct GameDetails {
    std::wstring description;
    std::wstring release_date;
    std::wstring developer;
    std::wstring publisher;
    std::wstring genres;
    std::wstring players; // "1-2" (ES-DE / ScreenScraper).
    std::wstring score;   // "16/20" (ScreenScraper's rating, also as ES-DE stored it).
    std::wstring error;
    // The signed-in account's Steam client data (library/steam_stats.hpp), read with the description.
    std::optional<uint32_t> playtime_minutes;
    uint32_t achievements_unlocked = 0, achievements_total = 0; // total 0 = no achievements known.
};

// Only an explicit four-digit year; an unavailable/announced date stays unknown.
inline std::wstring gameReleaseYear(std::wstring_view date) {
    const auto digit = [](wchar_t c) { return c >= L'0' && c <= L'9'; };
    for (size_t i = 0; i + 4 <= date.size(); ++i)
        if ((date[i] == L'1' || date[i] == L'2') && digit(date[i + 1]) && digit(date[i + 2]) && digit(date[i + 3]) &&
            (i == 0 || !digit(date[i - 1])) && (i + 4 == date.size() || !digit(date[i + 4]))) return std::wstring(date.substr(i, 4));
    return {};
}
