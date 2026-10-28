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
// Games start through Heroic's backends (legendary, gogdl) with Heroic's settings, else through
// Heroic, as its own desktop shortcuts do: heroic://launch?appName=<id>&runner=<legendary|gog>.
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

// Heroic's own link: the fallback when its backend cannot be started directly.
GameAction launchAction(std::string_view runner, const std::string& app);

// Heroic's backends, started directly the way Heroic starts them (storeManagers/*/games.ts):
//   legendary launch <app> [--wine <bin> --wine-prefix <prefix> | --no-wine --wrapper "'<proton>' waitforexitandrun"]
//   gogdl --auth-config-path <config>/gog_store/auth.json launch <folder> <id> [wine flags] --platform <linux|windows>
// with LEGENDARY_CONFIG_PATH / GOGDL_CONFIG_PATH and the Wine/Proton variables Heroic sets. A
// GameAction carries no environment: the command runs through /usr/bin/env NAME=value ... <backend>.
struct Backends { std::filesystem::path legendary, gogdl; };
// Per-game settings (GamesConfig/<app>.json, keyed by the app; Heroic writes them from its defaults
// before a game's first start), only what a launch needs. A Windows game without them falls back.
struct Settings {
    std::string wine_type;           // "wine", "proton" (others: toolkit, crossover: not supported).
    std::filesystem::path wine_bin, wine_prefix;
    std::string launcher_args;       // Appended as typed (game arguments).
    std::string language;
    std::vector<std::pair<std::string, std::string>> environment; // enviromentOptions (Heroic's spelling).
    std::vector<std::string> wrappers;                            // wrapperOptions: "<exe> <args>".
    bool esync = false, fsync = false;
    bool found = false;              // The game has settings of its own.
};
Settings parseSettings(std::string_view json, std::string_view app);
// The backends of a Heroic config folder: the user's alternatives (config.json altLegendaryBin,
// altGogdlBin), then Heroic's bundled ones under <resources>/app.asar.unpacked/build/bin/x64/linux,
// then PATH. resources: Heroic's resources folders, most likely first.
Backends findBackends(const std::filesystem::path& config, const std::vector<std::filesystem::path>& resources);
// Where Heroic's resources are on this system for a config folder (Flatpak's own for its config).
std::vector<std::filesystem::path> resourceFolders(const std::filesystem::path& config);
// The direct launch of an installed game; empty (use launchAction) when the backend or, for a
// Windows game, a usable Wine/Proton and prefix are missing. home: for Proton's Steam path.
GameAction backendAction(std::string_view runner, const Install& install, const std::filesystem::path& config,
                         const Backends& backends, const Settings& settings, const std::filesystem::path& home);

// One game per installed, launchable title (GameSource::epic or ::gog); DLC is never a game.
std::vector<Game> scan(const std::filesystem::path& config);
std::vector<Game> scan(const std::filesystem::path& config, const Backends& backends);

// Linux: Heroic's config folders that exist (native, then Flatpak's) and its program (PATH,
// /opt/Heroic, Flatpak's wrapper); empty when none.
std::vector<std::filesystem::path> configFolders();
std::filesystem::path executable();
}
