#pragma once
#include "../model/game.hpp"
#include <optional>
#include <string_view>
#include <vector>

// Libraries imported from a page of the store's own website that the user copies into
// Launchme (EA, Battle.net). No password, cookie or token is stored: only the resulting list
// of owned games, protected with DPAPI in <config>/accounts/<source>.library. Refreshing
// means copying the page again.
namespace web_import {
struct Snapshot {
    std::string account;     // Store account ID; the Battle.net page has none ("battlenet").
    uint64_t synced_at = 0;  // Unix seconds of the import.
    std::vector<Game> games; // Store games, not installed: source, source_id and name.
};
std::string serialize(GameSource source, const Snapshot& snapshot);
std::optional<Snapshot> deserialize(GameSource source, std::string_view text);
std::filesystem::path file(const std::filesystem::path& root, GameSource source);
// nullopt when there is no import or it cannot be read.
std::optional<Snapshot> load(const std::filesystem::path& root, GameSource source);
bool save(const std::filesystem::path& root, GameSource source, const Snapshot& snapshot);
bool clear(const std::filesystem::path& root, GameSource source);
}
