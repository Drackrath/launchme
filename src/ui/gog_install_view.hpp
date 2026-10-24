#pragma once
#include "../store/gog_install.hpp"
#include "settings_view.hpp"

namespace gog_install_ui {
struct State {
    bool open = false;
    Game game;
    gog::InstallSnapshot snapshot;
    gog::InstallRequest request;
    int row = 0, variant = 0;
};
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
inline std::wstring action(const gog::InstallSnapshot& value) {
    if (value.state == gog::InstallState::installing) return tr(L"Instalowanie…");
    if (value.busy()) return tr(L"Anuluj pobieranie");
    if (value.state == gog::InstallState::completed || value.error == gog::InstallError::cleanup) return tr(L"Gotowe");
    return value.state == gog::InstallState::error || value.state == gog::InstallState::cancelled ? tr(L"Ponów") : tr(L"Pobierz i zainstaluj");
}
}
