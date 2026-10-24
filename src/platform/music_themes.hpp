#pragma once
#include <array>
#include "../i18n/i18n.hpp"

struct MusicTheme { const wchar_t* name; const wchar_t* file; };
inline constexpr std::array<MusicTheme, 12> music_themes{{
    {L"Home Menu", L"2-04. (Bonus) Home Menu - No Intro.flac"},
    {L"System Settings", L"1-42. System Settings - Main Theme.flac"},
    {L"Internet Settings", L"1-43. System Settings - Internet Settings.flac"},
    {L"Mii Maker", L"1-25. Mii Maker - Main Theme.flac"},
    {L"Nintendo eShop", L"1-30. Nintendo eShop - Main Theme.flac"},
    {L"Friend List", L"1-06. Friend List.flac"},
    {L"Activity Log", L"1-35. Activity Log - Main Theme.flac"},
    {L"Camera — Album", L"1-15. Nintendo 3DS Camera - Album.flac"},
    {L"Camera — Chilled", L"1-16. Nintendo 3DS Camera - Slideshow (Chilled).flac"},
    {L"Camera — Jazz", L"1-22. Nintendo 3DS Camera - Slideshow (Jazz).flac"},
    {trKey(L"Brak"), nullptr}, // Keep the existing Off index; append themes after it.
    {L"Launchme — Horizons", L"HorizonsTheme.mp3"}
}};
inline constexpr size_t music_pcm_limit = 64 * 1024 * 1024;
