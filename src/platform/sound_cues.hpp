#pragma once

#include <array>

#include "../i18n/i18n.hpp"

struct SoundOption { const wchar_t* name; const wchar_t* file; };

inline constexpr std::array<SoundOption, 1> ui_sounds{{
    {trKey(L"Domyślny"), L"2-05. (Bonus) Nintendo Network ID Settings - Loading (Chord).flac"},
}};

inline constexpr std::array<SoundOption, 1> toast_sounds{{
    {trKey(L"Domyślny"), L"2-05. (Bonus) Nintendo Network ID Settings - Loading (Chord).flac"},
}};

inline constexpr std::array<SoundOption, 2> boot_sounds{{
    {L"PSP", L"psp_boot_sound.mp3"},
    {trKey(L"Brak"), nullptr},
}};
