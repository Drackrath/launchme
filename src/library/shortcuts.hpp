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

// Adding games to Steam as shortcuts ("Dodaj do Steam", Linux): shortcuts.vdf of the active account,
// read and written whole (every other entry and field kept as it is). Steam reads the file when it
// starts, so a running Steam shows a change after its restart. Launchme's own shortcuts (launch
// options with --jump=play:) never show up in its library as non-Steam games.
namespace steam_shortcuts {
struct Shortcut {
    std::wstring name;            // AppName
    std::wstring exe;             // Exe, stored quoted
    std::wstring start_dir;       // StartDir, stored quoted
    std::wstring launch_options;  // LaunchOptions; identifies Launchme's shortcut of a game
    std::wstring icon;            // icon: an image file, or empty
};
// The appid Steam gives a shortcut and names its artwork by (config/grid/<appid>p.png...):
// CRC-32 of the quoted exe followed by the name, with the top bit set.
uint32_t appId(const std::wstring& exe, const std::wstring& name);
// <steam_root>/userdata/<account>/config of the active account; empty without one.
std::filesystem::path configFolder(const std::filesystem::path& steam_root);
// The appid of the shortcut with these launch options, or 0.
uint32_t find(const std::filesystem::path& shortcuts_file, std::wstring_view launch_options);
// Adds the shortcut (replacing one with the same launch options); its appid, or 0 when the file
// cannot be read as shortcuts or written. A missing file is created.
uint32_t add(const std::filesystem::path& shortcuts_file, const Shortcut& shortcut);
// Removes the shortcut with these launch options; false when the file cannot be written.
bool remove(const std::filesystem::path& shortcuts_file, std::wstring_view launch_options);
}
