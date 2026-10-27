#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Publishers and developers with a logo (res/studios/, from HVR88's Monochrome Gaming Logos):
// the libraries "Wydawcy" and "Developerzy" group the games by them. A studio without a logo is
// not offered. Names as the stores give them ("Ubisoft Montreal", "CAPCOM Co., Ltd.", "Bandai
// Namco Entertainment") match by a folded alias or a leading word run ("ubisoft …").
namespace studios {
enum class Role : uint8_t { publisher, developer };
inline constexpr int role_count = 2;
// A studio's home tile: 	ile_base + role * 0x10000 + its catalog index (stable across starts).
inline constexpr uint32_t tile_base = 0xF1000000u;

struct Studio {
    std::string_view id;                     // "capcom": stable, in settings and section ids.
    std::wstring_view name;                  // "Capcom".
    std::wstring_view file;                  // "publisher_capcom.png" in assets\studios.
    std::vector<std::wstring_view> names;    // Folded names that are exactly it.
    std::vector<std::wstring_view> prefixes; // Folded leading words that are it ("ubisoft" → "ubisoft montreal").
};
const std::vector<Studio>& catalog();
const Studio* byId(std::string_view id);

// "CAPCOM Co., Ltd." → "capcom": foldGameTitle, then legal endings dropped (inc, ltd, co, llc…).
std::wstring fold(std::wstring_view name);
// The studio a store's name is, or null: an exact name first, then the longest leading match.
const Studio* match(std::wstring_view name);

// "publisher:capcom" / "developer:capcom": a library's or a section's id; the reverse.
std::string key(Role role, std::string_view id);
std::optional<std::pair<Role, const Studio*>> parse(std::string_view key);

// Every studio with games: the game keys of each (a game counts once per studio, however many of
// its names match it), most games first, then by name.
struct Entry {
    const Studio* studio = nullptr;
    std::vector<std::string> games;
    bool operator==(const Entry&) const = default;
};
struct GameNames {
    std::string key;
    std::vector<std::wstring> names; // Its publishers (or developers) as the index has them.
};
std::vector<Entry> group(const std::vector<GameNames>& games);
} // namespace studios
