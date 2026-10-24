#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <unordered_set>
#include "steam.hpp"

struct SteamCollection {
    std::wstring name;
    std::vector<uint32_t> app_ids; // Steam AppIDs and non-Steam shortcut IDs (steamShortcutId).
    std::string id;
};
// Non-Steam shortcut app IDs (shortcuts.vdf "appid") have the top bit set; Steam AppIDs never do.
inline bool steamShortcutId(uint32_t id) { return (id & 0x80000000u) != 0; }
// Written by Steam ROM Manager (one per parser category); the Emulacja row covers them.
inline bool srmCollection(const SteamCollection& collection) { return collection.id.starts_with("user-collections.srm-"); }
// Settings key of a collection (order, hidden): collection IDs are per Steam account.
inline std::string collectionKey(uint64_t account, const SteamCollection& collection) {
    if (collection.id.starts_with("local:")) return collection.id;
    return std::to_string(account) + ':' + collection.id;
}

std::vector<SteamCollection> readSteamCollections(const std::filesystem::path& steam_root);

struct SteamShelf {
    std::wstring name;
    std::vector<Game> games;
    std::string id; // SteamCollection::id.
};

std::vector<SteamShelf> publicSteamShelves(const std::vector<SteamCollection>& collections,
                                          const std::vector<Game>& visible,
                                          const std::unordered_set<uint32_t>& excluded = {},
                                          bool include_private = false); // Ctrl+H: hidden and private games too.
