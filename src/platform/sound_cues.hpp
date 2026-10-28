#pragma once

#include <array>

#include "../i18n/i18n.hpp"

// Built-in interface sounds: nothing here names a file that does not ship with Launchme. The
// navigation cue is synthesized (Audio::play); notifications and the start are silent unless a
// file of the sound folder is chosen (the start sound).
struct SoundOption { const wchar_t* name; const wchar_t* file; };

inline constexpr std::array<SoundOption, 1> ui_sounds{{
    {trKey(L"Domyślny"), nullptr},
}};

inline constexpr std::array<SoundOption, 1> toast_sounds{{
    {trKey(L"Brak"), nullptr},
}};

inline constexpr std::array<SoundOption, 1> boot_sounds{{
    {trKey(L"Brak"), nullptr},
}};
