#pragma once
#include "steamgriddb.hpp"
#include "../library/screenscraper.hpp"

// Artwork identity is independent of a download's row, file, stage and owning plugin.
// An update/DLC can name its base game without adding anything to the library.
namespace download_art {
struct Target {
    std::wstring name, platform;
    std::string title_id;
    uint32_t steam_app_id = 0;
    bool pc = false;
    std::string key() const;
};
Target forGame(const Game& game);
bool pcPlatform(std::wstring_view platform);
std::wstring normalizePlatform(std::wstring platform);
std::wstring cleanName(std::wstring name);
std::vector<uint8_t> fetch(const Target& target, const std::filesystem::path& cache,
                          const std::filesystem::path& metadata, const std::wstring& sgdb_key,
                          const screenscraper::Config& ss, const std::wstring& language,
                          const ArtworkHttp& get = httpGetPrivate);
}
