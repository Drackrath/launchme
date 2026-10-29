#pragma once
// A game's shortcuts outside Launchme: on the desktop and in the Start menu (Windows: .lnk files in
// the Desktop and Programs folders) or the applications menu (Linux: XDG .desktop entries in the
// desktop folder and ~/.local/share/applications). Each starts Launchme with --jump=launch:<key>, so
// the game runs with its launch options, Proton/Wine settings and account; Launchme's own shortcuts
// are told apart from any other by that argument and are never overwritten by it.
//
// Linux: shortcuts (and Launchme's games in Steam) start a small script, <data>/bin/launchme, that
// execs the current Launchme; it is rewritten when Launchme moves (a new AppImage), so they keep
// working (refreshHelper).
#include <filesystem>
#include <string>

namespace game_shortcuts {
enum class Place { desktop, menu };

struct Shortcut {
    std::wstring title;
    std::string key;                 // The game's key (gameKey).
    std::filesystem::path program;   // What it starts: launchme.exe (Windows), the helper script (Linux).
    std::filesystem::path icon;      // .ico or .exe (Windows), PNG (Linux); empty: the program's own / a generic one.
};

// "--jump=launch:<hex of key>".
std::wstring arguments(const std::string& key);
// The folder of a place; empty when the system has none (no desktop folder).
std::filesystem::path folder(Place place);
// Launchme's shortcut of the game there; empty when there is none.
std::filesystem::path find(Place place, const std::string& key, const std::wstring& title);
// Creates or refreshes it; its file, empty on failure.
std::filesystem::path create(Place place, const Shortcut& shortcut);
bool remove(Place place, const std::string& key, const std::wstring& title);
// A file-name-safe identity of a game ("epic-catnip-1a2b3c4d"), for its icon.
std::string slug(const std::string& key);

#ifndef _WIN32
// The helper the shortcuts start: written (or rewritten when `program` or the data folder changed)
// when `create` is true, else only refreshed when it exists. Its path; empty when it cannot be written.
std::filesystem::path helper(const std::filesystem::path& data, const std::filesystem::path& program, bool create);
#endif
} // namespace game_shortcuts
