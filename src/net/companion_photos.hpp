#pragma once
// Towarzyszka's photos in the chat (Settings → System → Towarzyszka AI → "Zdjęcia od niej"): once
// per chat, after the player's 2nd, 3rd or 5th message, and at most one a day, she sends a picture
// from her own subfolder of the folder the player chose. The state lives in
// <config>/companion/photos/state.json. Workers only; no window here.
#include "companion.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace companion::photos {
struct State {
    int last_shown = 0;               // yyyymmdd of the last photo she sent.
    std::vector<std::wstring> recent; // Pictures sent lately (≤ 30, relative to the folder): not again soon.
};
std::filesystem::path folderOf(const std::filesystem::path& config); // <config>/companion/photos
State load(const std::filesystem::path& folder);
bool save(const std::filesystem::path& folder, const State& state);

int turn(uint32_t seed);                      // After which of the player's messages: 2, 3 or 5.
bool due(const State& state, int today);      // Nothing sent today yet.
bool imageBytes(std::span<const uint8_t> bytes); // PNG, JPEG, GIF, BMP or WebP by its signature.
// A random picture of hers (png/jpg/jpeg/gif/bmp/webp, ≤ 16 MiB), avoiding the recent ones while
// others are left: only from <folder>/<her key>/, its own files always and its range folders
// "00-09", "10-19", … (any "<from>-<to>") once `level` reaches <from> (gamification off: pass
// affinity::max_level). Nothing when she has no folder there: every companion has her own, and
// pictures elsewhere in the folder are never sent.
std::optional<std::filesystem::path> pick(const std::filesystem::path& folder, Look look, int level, const std::vector<std::wstring>& recent, uint32_t seed);
// "10-19" → 10; -1 when the name is not a range.
int rangeStart(std::wstring_view name);
std::optional<std::vector<uint8_t>> read(const std::filesystem::path& file);
// What the picture shows, from <stem>.txt beside it (UTF-8, ≤ 4 KB, one line; tools/companion_gen writes them): she
// talks about the photo she sends instead of a stock line. Empty when there is none.
std::wstring description(const std::filesystem::path& file);
}
