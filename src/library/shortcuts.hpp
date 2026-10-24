#pragma once

#include "collections.hpp"
#include "steam.hpp"

#include <filesystem>
#include <string_view>
#include <vector>

struct ShortcutGame {
    Game game;
    bool hidden = false;
};

std::wstring shortcutPlatform(const std::wstring& executable,
                              const std::wstring& options,
                              const std::wstring& title,
                              const std::wstring& tags = {});
// The ROM/ISO a Steam ROM Manager / EmuDeck shortcut starts: the last absolute file argument
// that is no emulator, launcher script or config file; empty for ordinary PC shortcuts.
std::filesystem::path shortcutRom(const std::wstring& executable, const std::wstring& options);
// The emulated platform a Steam ROM Manager collection name stands for ("Sony PlayStation",
// "Nintendo Switch - Eden"), or empty ("Emulation").
std::wstring collectionPlatform(std::wstring_view collection_name);
// A shortcut in an SRM collection naming a platform goes to that platform, ahead of the
// executable/title inference; the first specific collection in Steam's order decides.
// A generic Dolphin collection cannot replace a known Wii/GameCube console.
void applyCollectionPlatforms(std::vector<ShortcutGame>& shortcuts, const std::vector<SteamCollection>& collections);
std::vector<ShortcutGame> scanSteamShortcuts(const std::filesystem::path& steam_root,
                                             size_t* parse_error = nullptr);
