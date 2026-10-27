#pragma once

#include "../model/game.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// User-added games ("Moje gry"): a versioned JSON list in the user config directory,
// {"version":1,"games":[{"id","name","path","arguments","directory"}]}. Parsing,
// validation and action planning are pure, so they are tested offline.
namespace custom_games {
inline constexpr size_t max_name = 200;
inline constexpr size_t max_arguments = 2000;
inline constexpr size_t max_path = 4096;
inline constexpr size_t max_games = 500;

struct Entry {
    std::string id;                  // 16 lowercase hex digits: Game::source_id, stable across renames.
    std::wstring name;
    std::filesystem::path path;      // Anything ShellExecute opens: .exe, .lnk, .bat/.cmd, .url...
    std::wstring arguments;
    std::filesystem::path directory; // Working directory; empty = the file's folder.
    bool operator==(const Entry&) const = default;
};

enum class Problem { none, name, path, arguments, directory };
Problem validate(const Entry& entry);
bool validId(std::string_view id);
// Trims spaces and one pair of surrounding quotes (Explorer's "Copy as path").
std::wstring cleanPath(std::wstring_view text);
std::wstring trim(std::wstring_view text);

struct List {
    std::vector<Entry> games;
    bool readable = true; // False: missing version 1 structure (the caller keeps a copy).
    size_t skipped = 0;   // Invalid or duplicate entries left out.
};
// Never throws; invalid entries are skipped, a damaged document yields an empty list.
List parse(std::string_view bytes);
std::string format(const std::vector<Entry>& games);
// Random 64-bit ID (system_info::randomBytes) that no entry uses yet; empty if the RNG fails.
std::string newId(const std::vector<Entry>& existing);

std::filesystem::path workingDirectory(const Entry& entry);
GameAction action(const Entry& entry);
// A library game: GameSource::custom, key "custom:<id>". Only a found file is playable.
Game makeGame(const Entry& entry, bool installed);

// A missing file is an empty, readable list. An unreadable one is copied to
// "<file>.bak" before the caller can overwrite it.
List load(const std::filesystem::path& file);
bool save(const std::vector<Entry>& games, const std::filesystem::path& file);
} // namespace custom_games
