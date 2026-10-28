// The desktop's file and folder picker on Linux (SDL3: the XDG desktop portal on Wayland and in
// Flatpak, Zenity/KDialog elsewhere), as file_dialog.cpp opens Windows' dialog.
#include "file_dialog.hpp"
#include "text_encoding.hpp"
#include "../i18n/i18n.hpp"
#include "../linux/window/window_system.hpp"

namespace {
using Filters = std::vector<lm_window::FileFilter>;
std::string label(const wchar_t* text) { return text_encoding::utf8(tr(text)); }
Filters allFiles() { return {{label(L"Wszystkie pliki"), "*"}}; }
}

std::filesystem::path pickPluginPackage(HWND owner) {
    return lm_window::pickFile(owner, false, {{"ZIP", "zip"}}, {});
}

std::filesystem::path pickGamesFolder(HWND owner) { return lm_window::pickFile(owner, true, {}, {}); }

std::filesystem::path pickArtworkImage(HWND owner) {
    return lm_window::pickFile(owner, false, {{label(L"Obrazy PNG i JPEG"), "png;jpg;jpeg"}}, {});
}

// Linux games are programs, scripts and .desktop entries; Windows programs run through Wine/Proton.
std::filesystem::path pickGamePath(HWND owner, bool folder, const std::filesystem::path& initial) {
    if (folder) return lm_window::pickFile(owner, true, {}, initial);
    auto filters = allFiles();
    filters.insert(filters.begin(), {label(L"Programy i skróty"), "sh;desktop;AppImage;exe;bat;msi;lnk;x86_64"});
    return lm_window::pickFile(owner, false, filters, initial);
}

std::filesystem::path pickVideoFolder(HWND owner, const std::filesystem::path& initial) { return lm_window::pickFile(owner, true, {}, initial); }
std::filesystem::path pickSoundFolder(HWND owner, const std::filesystem::path& initial) { return lm_window::pickFile(owner, true, {}, initial); }

std::filesystem::path pickPath(HWND owner, bool folder, const std::wstring&, const std::vector<std::wstring>& extensions,
                               const std::wstring& filter_label, bool all_files, const std::filesystem::path& initial) {
    if (folder) return lm_window::pickFile(owner, true, {}, initial);
    std::string pattern;
    for (const auto& extension : extensions) {
        const auto bare = text_encoding::utf8(extension.starts_with(L'.') ? extension.substr(1) : extension);
        if (!bare.empty()) pattern += (pattern.empty() ? "" : ";") + bare;
    }
    Filters filters;
    if (!pattern.empty()) filters.push_back({filter_label.empty() ? pattern : text_encoding::utf8(filter_label), pattern});
    if (pattern.empty() || all_files) filters.push_back(allFiles().front());
    return lm_window::pickFile(owner, false, filters, initial);
}
