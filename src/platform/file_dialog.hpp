#pragma once
#include <Windows.h>
#include <filesystem>
#include <string>
#include <vector>

// Native Windows file/folder picker for a user-added game (modal, owned by the launcher
// window). Shortcuts (.lnk, .url) are returned as picked, not resolved to their target.
// Empty when cancelled or unavailable.
std::filesystem::path pickGamePath(HWND owner, bool folder, const std::filesystem::path& initial);
std::filesystem::path pickPluginPackage(HWND owner);
// A folder whose subfolders are games (Settings → Platformy PC → DRM-free).
std::filesystem::path pickGamesFolder(HWND owner);
// A PNG/JPEG image for "Zmień grafikę → Z dysku…".
std::filesystem::path pickArtworkImage(HWND owner);
// Settings → Dźwięk → "Folder dźwięków": music themes and start sounds, opened at `initial`.
std::filesystem::path pickSoundFolder(HWND owner, const std::filesystem::path& initial);
std::filesystem::path pickVideoFolder(HWND owner, const std::filesystem::path& initial);
// Any request of the launcher's picker as the Windows dialog (desktop mode): a folder, or a file of
// `extensions` (lower case with the dot; empty = every file) under `filter_label`, with "Wszystkie
// pliki" too when `all_files`. Opens at `initial` (a folder, or a file's folder) when it exists.
std::filesystem::path pickPath(HWND owner, bool folder, const std::wstring& title, const std::vector<std::wstring>& extensions,
                               const std::wstring& filter_label, bool all_files, const std::filesystem::path& initial);
