#pragma once

#include "../model/game.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

// Heroic Games Launcher (Linux): the games it installed with its runners, read from its own files
// (never by running it or its tools). <config> is ~/.config/heroic or Flatpak's
// ~/.var/app/com.heroicgameslauncher.hgl/config/heroic.
// legendary (Epic): legendaryConfig/legendary/installed.json, metadata/<app>.json (catalog IDs);
// gogdl (GOG): gog_store/installed.json, titles from store_cache/gog_library.json (older Heroic:
// gog_store/library.json); Heroic's play time: store/timestamp.json.
// Every game starts through Heroic, as its own desktop shortcuts do:
// heroic://launch?appName=<id>&runner=<legendary|gog>.
// Amazon (nile) and sideloaded apps have no store in the game model: not listed.
namespace heroic {
struct Install {
    std::string app;                 // Epic AppName / GOG product ID: Game::source_id.
    std::wstring title;              // Empty when the file names none (GOG).
    std::filesystem::path directory;
    std::string platform;            // "Windows", "windows", "linux", "Mac"...
    bool dlc = false;
};
struct PlayTime { uint64_t last_played = 0; uint32_t minutes = 0; };
struct EpicIds { std::string namespace_id, catalog_item; };

// Never throw; malformed files give nothing, bad entries are skipped.
std::vector<Install> parseLegendaryInstalled(std::string_view json); // {"<app>": {app_name, title, install_path, is_dlc, platform}}
std::vector<Install> parseGogInstalled(std::string_view json);       // {"installed": [{appName, install_path, is_dlc, platform}]}
std::map<std::string, std::wstring> parseGogTitles(std::string_view json); // {"games": [{app_name, title}]}
std::map<std::string, PlayTime> parsePlayTimes(std::string_view json);     // {"<app>": {lastPlayed, totalPlayed (minutes)}}
EpicIds parseLegendaryMetadata(std::string_view json);                     // {"metadata": {namespace, id}}
// "2026-09-01T22:08:25.393Z" (UTC, as JavaScript's toISOString) → Unix seconds; 0 when malformed.
uint64_t isoTime(std::string_view text);

GameAction launchAction(std::string_view runner, const std::string& app);
// One game per installed, launchable title (GameSource::epic or ::gog); DLC is never a game.
std::vector<Game> scan(const std::filesystem::path& config);

// Linux: Heroic's config folders that exist (native, then Flatpak's) and its program (PATH,
// /opt/Heroic, Flatpak's wrapper); empty when none.
std::vector<std::filesystem::path> configFolders();
std::filesystem::path executable();
}
