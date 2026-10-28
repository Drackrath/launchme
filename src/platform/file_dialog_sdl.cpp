// File and folder pickers on Linux (file_dialog_windows.cpp on Windows): SDL3's dialogs, which use
// the xdg-desktop-portal FileChooser (GNOME, KDE, Flatpak) or zenity/kdialog without a portal.
// SDL's dialogs answer through a callback; these functions wait for it, pumping SDL's events so
// the portal's D-Bus reply arrives, and so are modal like IFileOpenDialog::Show.
#include "file_dialog.hpp"
#include "../i18n/i18n.hpp"
#include "text_encoding.hpp"

#include <SDL3/SDL.h>

#include <atomic>
#include <mutex>

namespace {

std::string utf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

struct Filter {
    std::string label;
    std::string pattern;
};

struct Pending {
    std::mutex mutex;
    std::filesystem::path result;
    std::atomic<bool> done{false};
};

void SDLCALL finished(void* data, const char* const* files, int) {
    auto* pending = static_cast<Pending*>(data);
    {
        std::lock_guard lock(pending->mutex);
        // nullptr: an error (no portal and no zenity); an empty list: cancelled.
        if (files && files[0]) pending->result = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(files[0])));
    }
    pending->done = true;
}

std::filesystem::path show(platform::NativeWindow owner, bool folder, const std::wstring& title,
                           const std::vector<Filter>& filters, const std::filesystem::path& initial) {
    if (!SDL_WasInit(SDL_INIT_VIDEO)) return {}; // No window system: nothing to show it on.
    std::vector<SDL_DialogFileFilter> types;
    for (const auto& filter : filters) types.push_back({filter.label.c_str(), filter.pattern.c_str()});
    const std::string caption = text_encoding::utf8Lenient(title);
    const std::string location = utf8(file_dialog::startLocation(initial, folder));
    const SDL_PropertiesID properties = SDL_CreateProperties();
    if (!properties) return {};
    if (owner) SDL_SetPointerProperty(properties, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, owner);
    if (!caption.empty()) SDL_SetStringProperty(properties, SDL_PROP_FILE_DIALOG_TITLE_STRING, caption.c_str());
    if (!location.empty()) SDL_SetStringProperty(properties, SDL_PROP_FILE_DIALOG_LOCATION_STRING, location.c_str());
    if (!folder && !types.empty()) {
        SDL_SetPointerProperty(properties, SDL_PROP_FILE_DIALOG_FILTERS_POINTER, types.data());
        SDL_SetNumberProperty(properties, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER, static_cast<Sint64>(types.size()));
    }
    SDL_SetBooleanProperty(properties, SDL_PROP_FILE_DIALOG_MANY_BOOLEAN, false);
    // The filters, title and location stay alive until the answer: SDL may read them until then.
    Pending pending;
    SDL_ShowFileDialogWithProperties(folder ? SDL_FILEDIALOG_OPENFOLDER : SDL_FILEDIALOG_OPENFILE, finished, &pending, properties);
    while (!pending.done) {
        SDL_PumpEvents();
        SDL_Delay(10);
    }
    SDL_DestroyProperties(properties);
    std::lock_guard lock(pending.mutex);
    return pending.result;
}

Filter everything() { return {text_encoding::utf8Lenient(tr(L"Wszystkie pliki")), "*"}; }

std::filesystem::path pickFolder(platform::NativeWindow owner, const std::filesystem::path& initial, const wchar_t* title) {
    return show(owner, true, title, {}, initial);
}

} // namespace

namespace file_dialog {

std::string sdlPattern(const std::vector<std::wstring>& extensions) {
    std::string pattern;
    for (const auto& extension : extensions) {
        std::wstring_view name = extension;
        while (name.starts_with(L'*')) name.remove_prefix(1);
        while (name.starts_with(L'.')) name.remove_prefix(1);
        if (name.empty()) continue;
        if (!pattern.empty()) pattern += ';';
        pattern += text_encoding::utf8Lenient(name);
    }
    return pattern.empty() ? std::string("*") : pattern;
}

std::filesystem::path startLocation(const std::filesystem::path& initial, bool folder) {
    if (initial.empty()) return {};
    std::error_code error;
    if (std::filesystem::is_directory(initial, error)) return initial;
    if (std::filesystem::is_regular_file(initial, error)) return folder ? initial.parent_path() : initial;
    const auto parent = initial.parent_path();
    if (!parent.empty() && std::filesystem::is_directory(parent, error)) return parent;
    return {};
}

} // namespace file_dialog

std::filesystem::path pickPluginPackage(platform::NativeWindow owner) {
    return show(owner, false, tr(L"Zainstaluj plugin z ZIP"), {{"ZIP", "zip"}}, {});
}

std::filesystem::path pickGamesFolder(platform::NativeWindow owner) {
    return pickFolder(owner, {}, tr(L"Wybierz katalog z grami DRM-free"));
}

std::filesystem::path pickArtworkImage(platform::NativeWindow owner) {
    return show(owner, false, tr(L"Wybierz grafikę"), {{text_encoding::utf8Lenient(tr(L"Obrazy PNG i JPEG")), "png;jpg;jpeg"}}, {});
}

std::filesystem::path pickGamePath(platform::NativeWindow owner, bool folder, const std::filesystem::path& initial) {
    if (folder) return pickFolder(owner, initial, tr(L"Wybierz folder roboczy"));
    // As on Windows, then every file: Linux programs and scripts mostly have no extension.
    const std::vector<Filter> filters{{text_encoding::utf8Lenient(tr(L"Programy i skróty")), "exe;lnk;bat;cmd;url;sh;desktop;AppImage"},
                                      everything()};
    // Windows opens a file's folder here, not the file.
    std::error_code error;
    return show(owner, false, tr(L"Wybierz plik gry"), filters, std::filesystem::is_directory(initial, error) ? initial : std::filesystem::path{});
}

std::filesystem::path pickVideoFolder(platform::NativeWindow owner, const std::filesystem::path& initial) {
    return pickFolder(owner, initial, tr(L"Wybierz folder filmów"));
}

std::filesystem::path pickSoundFolder(platform::NativeWindow owner, const std::filesystem::path& initial) {
    return pickFolder(owner, initial, tr(L"Wybierz folder dźwięków"));
}

std::filesystem::path pickPath(platform::NativeWindow owner, bool folder, const std::wstring& title, const std::vector<std::wstring>& extensions,
                               const std::wstring& filter_label, bool all_files, const std::filesystem::path& initial) {
    // Its types: the requested extensions, then every file when offered or needed.
    std::vector<Filter> filters;
    if (!folder) {
        const std::string pattern = file_dialog::sdlPattern(extensions);
        if (pattern != "*") filters.push_back({filter_label.empty() ? pattern : text_encoding::utf8Lenient(filter_label), pattern});
        if (pattern == "*" || all_files) filters.push_back(everything());
    }
    return show(owner, folder, title, filters, initial);
}
