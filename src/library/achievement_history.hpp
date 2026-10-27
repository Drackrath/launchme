#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Every achievement the player unlocked that Launchme has seen, with its time: Steam's from the
// client's own files (read in the background for the profile), the others' when an achievements
// plugin answered for the game. Also each game's latest unlocked/total counts, and when a Steam
// game's stats file was last read (unchanged files are not read again). The profile's "Ostatnie
// osiągnięcia", its monthly history and "Najbardziej ukończone" read it.
// achievement-history.json in the config folder.
namespace achievement_history {
inline constexpr size_t max_unlocks = 20000;

struct Unlock {
    std::string game;            // Game key (gameKey).
    std::string id;              // The achievement's own ID within its game.
    std::wstring name, description;
    std::wstring icon;           // https URL (or a local picture); may be empty.
    uint64_t at = 0;             // Unix time of the unlock.
    float rarity = -1;           // Share of players with it, %; -1 unknown.
    bool operator==(const Unlock&) const = default;
};
struct Progress {
    uint32_t unlocked = 0, total = 0;
    bool operator==(const Progress&) const = default;
};
struct History {
    std::vector<Unlock> unlocks;                  // Newest first.
    std::map<std::string, Progress> progress;     // Game key → its counts.
    std::map<std::string, std::wstring> names;    // Game key → its name.
    std::map<std::string, uint64_t> scanned;      // "steam:<app>" → stats file stamp read last.
    bool operator==(const History&) const = default;
};

// Adds the game's unlocked achievements (ones without a time are kept out: no place on the
// timeline) and its counts; returns how many were new. Known ones keep their first time.
size_t merge(History& history, const std::string& game, const std::wstring& name, const std::vector<Unlock>& unlocked, Progress progress);

std::string format(const History& history);
History parse(std::string_view text);
History load(const std::filesystem::path& file);
bool save(const History& history, const std::filesystem::path& file);

// Unlocks per month for the twelve months ending in `year`/`month` (oldest first).
struct Month { int year = 0, month = 0; uint32_t count = 0; };
std::array<Month, 12> monthly(const History& history, int year, int month);
uint32_t since(const History& history, uint64_t unix_seconds); // Unlocks at or after that time.

// Games by completion: the largest share first, then the most unlocked; only games with any.
struct Completion { std::string game; uint32_t unlocked = 0, total = 0; float share() const { return total ? static_cast<float>(unlocked) / total : 0; } };
std::vector<Completion> completion(const std::map<std::string, Progress>& progress);
} // namespace achievement_history
