#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// The Steam accounts remembered on this PC (config/loginusers.vdf) and two things Launchme does
// with the Steam client around them, both without passwords:
// - start Steam signed in to another remembered account, as Steam's own account switcher and
//   TcNo Account Switcher do: the account becomes AutoLoginUser (registry) and the file's
//   AutoLogin account, then Steam restarts. Its saved "Remember me" sign-in does the rest;
// - let a game download into a library picked in Launchme: an appmanifest stub that says the game
//   needs an update, which Steam finds when it starts and downloads into that library.
namespace steam_accounts {
constexpr uint64_t steam_id_offset = 76561197960265728ULL;

struct Account {
    uint64_t steam_id = 0;    // SteamID64.
    std::string account_name; // The sign-in name (AutoLoginUser).
    std::wstring persona;     // The display name.
    bool remember = false;    // "Remember me": Steam can sign in to it without a password.
    bool auto_login = false;  // The account Steam signs in to next.
};
std::vector<Account> parseLoginUsers(std::string_view vdf);
std::vector<Account> loginUsers(const std::filesystem::path& steam_root);
// The same file with `steam_id` as the account Steam signs in to next: AutoLogin (and the older
// MostRecent) 1 for it and 0 for the others, AllowAutoLogin 1 for it. Empty when it is not listed.
std::string withAutoLogin(std::string_view vdf, uint64_t steam_id);

// Steam's process from HKCU\Software\Valve\Steam\ActiveProcess while it runs; 0 otherwise.
uint32_t runningSteamProcess();
// Asks Steam to quit (steam.exe -shutdown) and waits until its process has ended.
bool shutdownSteam(const std::filesystem::path& steam_exe, std::chrono::milliseconds timeout);
// Steam (closed) signs in to `account` when it next starts: AutoLoginUser and RememberPassword in
// the registry, the AutoLogin account in loginusers.vdf.
bool prepareSignIn(const std::filesystem::path& steam_root, const Account& account);
// Starts Steam in its tray (steam.exe -silent) without waiting for it.
bool startSteam(const std::filesystem::path& steam_exe);

// Steam's library folders with the free and total space of their drive (0 when unknown).
struct Folder { std::filesystem::path path; uint64_t free = 0, total = 0; };
std::vector<Folder> libraryFolders(const std::filesystem::path& steam_root);
// A folder name for the game under steamapps\common (Windows-safe; "app<id>" when nothing remains).
std::wstring installDirectoryName(const std::wstring& name, uint32_t app_id);
// appmanifest_<id>.acf for a game Steam has not installed: update required (StateFlags 1026), so
// Steam downloads it into the library holding the file when it starts.
std::string installManifest(uint32_t app_id, const std::wstring& name, const std::filesystem::path& steam_exe, uint64_t owner);
// Writes the stub into <library>\steamapps; false when a manifest for the game is already there or
// the file cannot be written.
bool writeInstallManifest(const std::filesystem::path& library, uint32_t app_id, const std::wstring& name,
                          const std::filesystem::path& steam_exe, uint64_t owner);
} // namespace steam_accounts
