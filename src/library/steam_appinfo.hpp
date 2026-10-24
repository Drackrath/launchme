#pragma once
// The Steam client's offline app metadata cache (Steam/appcache/appinfo.vdf): release dates,
// review scores, Metacritic, genres, store tags, developers, publishers and franchises of every
// app the client has seen, without the network. v27/v28 store keys inline; v29 (current)
// stores them as indices into a string table at the end of the file.
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace steam_appinfo {
struct App {
    uint32_t app_id = 0;
    std::wstring name, type;               // type: "game", "demo", "dlc", "tool", …
    uint64_t steam_release = 0;            // Unix seconds of the Steam release; 0 unknown.
    uint64_t original_release = 0;         // The game's first release elsewhere (older ports); 0 unknown.
    std::optional<int> review_percent;     // Share of positive Steam reviews, 0–100.
    std::optional<int> review_score;       // Steam's label 1 (Overwhelmingly Negative) … 9 (Overwhelmingly Positive).
    std::optional<int> metacritic;         // 0–100.
    std::vector<uint32_t> genres, tags, categories; // Store genre IDs, store tag IDs (most voted first), feature categories.
    uint32_t primary_genre = 0;
    std::vector<std::wstring> developers, publishers, franchises;
    std::wstring controller_support;       // "full", "partial" or empty.
    int deck = 0;                          // Steam Deck: 0 unknown, 1 unsupported, 2 playable, 3 verified.
};
// `wanted(app_id)` selects the records to decode (the file lists every app the client knows).
// nullopt: not an appinfo cache or a torn/unknown layout.
std::optional<std::vector<App>> parse(std::span<const uint8_t> data, const std::function<bool(uint32_t)>& wanted);
std::optional<std::vector<App>> read(const std::filesystem::path& steam_root, const std::function<bool(uint32_t)>& wanted);

// The store's genre IDs (Action = 1, RPG = 3, …) by their English names; nullptr when unknown.
const wchar_t* genreName(uint32_t id);
}
