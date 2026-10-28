#pragma once
#include "../store/gog_install.hpp"
#include "../library/steam_accounts.hpp"
#include "settings_view.hpp"

#include <cstdio>

// The install dialog of GOG, Epic and Steam games (Tryb TV: gog_install_renderer.cpp, desktop
// window: desktop_dialogs_renderer.cpp). Rows: 0 language/version (Steam: its library), 1 the
// game's folder (Steam: Steam's own install window), 2 the download folder (GOG only), 3 the
// action, 4 Powrót.
namespace gog_install_ui {
// Epic (Legendary) and Amazon Games (Nile): a helper downloads straight into the game's folder, so
// there is no installer and no download folder to choose.
inline bool helperInstall(GameSource source) { return source == GameSource::epic || source == GameSource::amazon; }
struct State {
    bool open = false;
    Game game;
    gog::InstallSnapshot snapshot;
    gog::InstallRequest request;
    int row = 0, variant = 0;
    // Steam (src/app/steam_install.cpp): the library folders with their free space, the chosen one,
    // and the restart under way.
    bool steam = false, steam_busy = false;
    std::vector<steam_accounts::Folder> libraries;
    int library = 0;
};
// "812 GB wolne z 1,8 TB" for a library's drive.
inline std::wstring spaceText(uint64_t free, uint64_t total) {
    const auto size = [](uint64_t bytes) {
        wchar_t text[32]{};
        const double gb = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
        if (gb >= 1000) swprintf_s(text, L"%.1f TB", gb / 1024.0);
        else swprintf_s(text, gb >= 100 ? L"%.0f GB" : L"%.1f GB", gb);
        return std::wstring(text);
    };
    if (!total) return tr(L"wolne miejsce nieznane");
    return size(free) + tr(L" wolne z ") + size(total);
}
inline std::wstring steamInstallStatus(const State& ui) {
    if (ui.steam_busy) return tr(L"Steam zamyka się i uruchamia ponownie w tle, potem zaczyna pobieranie…");
    return tr(L"Steam pobierze grę do wybranej biblioteki. Jeśli działa, uruchomi się ponownie w tle.");
}
inline std::wstring steamLibraryText(const State& ui) {
    if (ui.library < 0 || ui.library >= static_cast<int>(ui.libraries.size())) return tr(L"Brak bibliotek Steam");
    const auto& folder = ui.libraries[static_cast<size_t>(ui.library)];
    return folder.path.wstring() + L"  ·  " + spaceText(folder.free, folder.total);
}
struct Layout {
    float width, height;
    bool epic = false;
    settings_ui::Rect panel() const { const float half = epic ? 280.0f : 320.0f; return {width * 0.5f - 450, height * 0.5f - half, width * 0.5f + 450, height * 0.5f + half}; }
    // Header (title + up to two status lines) ends near top + 100; the progress bar sits between it and the rows.
    settings_ui::Rect progress() const { const auto p = panel(); return {p.left + 32, p.top + 114, p.right - 32, p.top + 118}; }
    settings_ui::Rect row(int index) const {
        if (epic && index >= 3) --index;
        const auto p = panel(); const float y = p.top + 134 + index * 80;
        return {p.left + 32, y, p.right - 32, y + 66};
    }
    int hit(float x, float y) const { for (int i = 0; i < 5; ++i) if ((!epic || i != 2) && row(i).contains(x, y)) return i; return -1; }
};
inline std::wstring action(const gog::InstallSnapshot& value);
inline std::wstring action(const State& ui) {
    if (ui.steam) return ui.steam_busy ? tr(L"Przygotowywanie…") : tr(L"Zainstaluj");
    return action(ui.snapshot);
}
inline std::wstring action(const gog::InstallSnapshot& value) {
    if (value.state == gog::InstallState::installing) return tr(L"Instalowanie…");
    if (value.busy()) return tr(L"Anuluj pobieranie");
    if (value.state == gog::InstallState::completed || value.error == gog::InstallError::cleanup) return tr(L"Gotowe");
    return value.state == gog::InstallState::error || value.state == gog::InstallState::cancelled ? tr(L"Ponów") : tr(L"Pobierz i zainstaluj");
}
}
