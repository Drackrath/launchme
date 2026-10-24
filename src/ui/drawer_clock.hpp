#pragma once

#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <string>

// Clock in the drawer dashboard header, formatted by Windows for the UI language
// (i18n::localeName()): "14:05" / "wtorek, 29 września", "2:05 PM" / "Tuesday, September 29".
namespace drawer_clock {
inline std::wstring time(const SYSTEMTIME& now, const wchar_t* locale) {
    wchar_t buffer[64]{};
    return GetTimeFormatEx(locale, TIME_NOSECONDS, &now, nullptr, buffer, 64) > 0 ? buffer : L"";
}

// Weekday, day and month without the year. Polish puts the day first, which also makes
// Windows use the genitive month name ("września").
inline std::wstring date(const SYSTEMTIME& now, const wchar_t* locale) {
    const bool day_first = std::wcsncmp(locale, L"en", 2) != 0;
    wchar_t buffer[96]{};
    return GetDateFormatEx(locale, 0, &now, day_first ? L"dddd, d MMMM" : L"dddd, MMMM d", buffer, 96, nullptr) > 0 ? buffer : L"";
}

// Changes exactly when the displayed minute (or day) changes.
inline uint64_t minute(const SYSTEMTIME& now) {
    return ((((static_cast<uint64_t>(now.wYear) * 13 + now.wMonth) * 32 + now.wDay) * 24 + now.wHour) * 60) + now.wMinute;
}

inline unsigned millisecondsToNextMinute(const SYSTEMTIME& now) {
    return (60u - std::min<unsigned>(now.wSecond, 59u)) * 1000u - std::min<unsigned>(now.wMilliseconds, 999u);
}
} // namespace drawer_clock
