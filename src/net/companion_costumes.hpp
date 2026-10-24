#pragma once
// Her costumes: one folder of pictures each, res/companion/<key>/costume_<NN>[_<name>]/
// (res/companion/README.md), listed with her character (companion_characters.hpp). Slot 0 is
// costume_00, her basic outfit; NN is the level that unlocks a costume with gamification on (off:
// every costume is free). Chosen in the chat's "…" → "Zmień strój"; Settings keep the folder name
// per character (companion_costume_<key>).
#include "companion.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace companion::costumes {
// Her face on one picture: its centre and size in the shared canvas's pixels (560×560); size 0 = unknown.
struct Face { float x = 0, y = 0, size = 0; };
struct Costume {
    Look look{};
    int slot = 0;       // Its sprites: companion_ui::spriteId(look, frame, emotion, slot).
    int level = 0;      // Unlocks at this level (gamification on).
    std::string folder; // "costume_10_kimono" (stable across builds, kept in settings).
    // Its costume.txt (optional): its name in the menu and her prompt, and the outfit in words.
    std::wstring name_pl, name_en, you_pl, you_en;
    // Its faces.txt (optional, tools/companion_faces.py): where her face is on each feeling's picture,
    // so a round avatar can centre it.
    std::array<Face, emotion_count> faces{};
};
// Its name: costume.txt's (name_pl / name_en), else from its folder: "costume_10_school-uniform" →
// "School uniform"; none → empty.
inline std::wstring name(const Costume& costume, bool english = false) {
    if (english && !costume.name_en.empty()) return costume.name_en;
    if (!costume.name_pl.empty()) return costume.name_pl;
    if (!costume.name_en.empty()) return costume.name_en;
    const std::string_view folder = costume.folder;
    if (folder.size() <= 11) return {}; // "costume_NN" without a name.
    std::wstring out;
    for (const char ch : folder.substr(11)) out += ch == '_' || ch == '-' ? L' ' : static_cast<wchar_t>(static_cast<unsigned char>(ch));
    if (!out.empty() && out[0] >= L'a' && out[0] <= L'z') out[0] = static_cast<wchar_t>(out[0] - L'a' + L'A');
    return out;
}
inline const Costume* find(const std::vector<Costume>& costumes, Look look, std::string_view folder) {
    const auto found = std::find_if(costumes.begin(), costumes.end(), [&](const Costume& c) { return c.look == look && c.folder == folder; });
    return found == costumes.end() ? nullptr : &*found;
}
} // namespace companion::costumes
