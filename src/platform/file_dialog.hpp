#pragma once
#include "native_window.hpp"
#include <filesystem>
#include <string>
#include <vector>

// Native file/folder picker (IFileOpenDialog on Windows; on Linux SDL3's, which asks the
// xdg-desktop-portal FileChooser, or zenity without a portal) for a user-added game (modal, owned
// by the launcher window). Shortcuts (.lnk, .url) are returned as picked, not resolved to their target.
// Empty when cancelled or unavailable.
std::filesystem::path pickGamePath(platform::NativeWindow owner, bool folder, const std::filesystem::path& initial);
std::filesystem::path pickPluginPackage(platform::NativeWindow owner);
// A folder whose subfolders are games (Settings → Platformy PC → DRM-free).
std::filesystem::path pickGamesFolder(platform::NativeWindow owner);
// A PNG/JPEG image for "Zmień grafikę → Z dysku…".
std::filesystem::path pickArtworkImage(platform::NativeWindow owner);
// Settings → Dźwięk → "Folder dźwięków": music themes and start sounds, opened at `initial`.
std::filesystem::path pickSoundFolder(platform::NativeWindow owner, const std::filesystem::path& initial);
std::filesystem::path pickVideoFolder(platform::NativeWindow owner, const std::filesystem::path& initial);
// Any request of the launcher's picker as the Windows dialog (desktop mode): a folder, or a file of
// `extensions` (lower case with the dot; empty = every file) under `filter_label`, with "Wszystkie
// pliki" too when `all_files`. Opens at `initial` (a folder, or a file's folder) when it exists.
std::filesystem::path pickPath(platform::NativeWindow owner, bool folder, const std::wstring& title, const std::vector<std::wstring>& extensions,
                               const std::wstring& filter_label, bool all_files, const std::filesystem::path& initial);

#ifndef _WIN32
// What file_dialog_sdl.cpp reads from a request (tests/file_dialog_tests.cpp).
namespace file_dialog {
// The extensions (".png", ".jpg") as an SDL filter pattern ("png;jpg"); "*" for none.
std::string sdlPattern(const std::vector<std::wstring>& extensions);
// Where a dialog opens: `initial` when it is a folder; when it is a file, that file when files are
// picked (preselected), else its folder; its folder when only that exists; else nothing.
std::filesystem::path startLocation(const std::filesystem::path& initial, bool folder);
}
#endif
