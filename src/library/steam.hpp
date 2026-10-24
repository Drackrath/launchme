#pragma once

#include "../model/game.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <set>

struct SteamLibrary {
    std::vector<Game> visible;
    std::vector<Game> private_games;
    bool complete = true;
    std::vector<std::filesystem::path> unavailable_libraries;
    std::set<uint32_t> unavailable_apps;
    std::set<uint32_t> excluded;
};

// Order of scanned games: played ones first (most recent first), then by update time and name.
inline bool steamLibraryOrder(const Game& left, const Game& right) {
    if (!!left.last_played != !!right.last_played) return !!left.last_played;
    if (left.last_played != right.last_played) return left.last_played > right.last_played;
    if (left.last_updated != right.last_updated) return left.last_updated > right.last_updated;
    return left.name < right.name;
}

// Steam tools rather than games, never listed: redistributables, runtimes, Proton, launcher
// clients and helpers (Ubisoft Connect, FramePacer).
bool steamTool(const Game& game);
std::filesystem::path findSteamRoot();
// The Steam folder and every library folder from libraryfolders.vdf, each once.
std::vector<std::filesystem::path> steamLibraryFolders(const std::filesystem::path& steam_root);
SteamLibrary scanSteamLibrary(const std::filesystem::path& steam_root);
SteamLibrary retainUnavailableSteamGames(SteamLibrary current, const SteamLibrary& previous);
// limit=0 returns the full library; callers choose which shelf to present.
std::vector<Game> scanInstalledGames(const std::filesystem::path& steam_root, size_t limit = 0);
std::wstring steamActionUrl(const Game& game);
// steam.exe in the Steam folder, or empty when it is not there.
std::filesystem::path steamExecutable(const std::filesystem::path& steam_root);
inline std::filesystem::path installedSteamExecutable() { return steamExecutable(findSteamRoot()); }
// Steam → Ciche uruchamianie: a steam://rungameid play action handed to steam.exe with -silent
// (as Steam's own autostart), so a Steam started for the game stays in the tray, and with
// keep_focus. Any other action, or an empty steam_exe, is returned unchanged.
GameAction silentSteamAction(GameAction action, const std::filesystem::path& steam_exe);
// A Steam game's own launch options ("Opcje uruchamiania…"): its steam://rungameid/<app_id> play
// action becomes steam.exe [-silent] -applaunch <app_id> <options> (Steam passes them on after the
// game's own launch options, without the confirmation steam://run asks for), with keep_focus so a
// refusal falls back to the plain link. Any other action, empty options or no steam.exe: unchanged.
GameAction steamOptionsAction(GameAction action, const std::filesystem::path& steam_exe, uint32_t app_id,
                              const std::wstring& options, bool silent);
