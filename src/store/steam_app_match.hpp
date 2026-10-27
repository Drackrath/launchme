#pragma once
#include "../artwork/steamgriddb_search.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

// The Steam store app of a game from another store (Epic, GOG, EA, Ubisoft…), for its store trailer
// only: launching, installing and the game's identity never use it. A verified link wins (RAWG's
// Steam store link, the artwork source picked in "Zmień grafikę"); else the title is looked up in
// Steam's store search and taken only when one result matches it unambiguously, by the same rules as
// SteamGridDB's title matching (numbers, years and remaster/demo/VR qualifiers must agree).

// The AppID a storesearch answer's apps give for `title`; 0 for no or an ambiguous match.
uint32_t matchSteamApp(std::wstring_view title, std::string_view storesearch);

// Cached in <metadata>\<cache_key>.steamapp ("<AppID> <unix ms>"): a found app for good, no match
// for a week, a failed request not at all. Runs on a worker.
uint32_t steamAppForTitle(const std::wstring& title, const std::filesystem::path& metadata, const std::string& cache_key,
                          const ArtworkHttp& get = httpGet);
