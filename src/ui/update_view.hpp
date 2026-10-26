#pragma once
// Co nowego (src/app/updates.cpp, src/ui/update_renderer.cpp): after an update's restart, the new
// version's changes once, over whatever screen opened (Tryb TV or the desktop window's compact
// canvas). Text scrolls; OK, Enter, Esc, A or B close it.
#include "settings_view.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace update_ui {
struct WhatsNew {
    bool open = false;
    std::wstring version, text;
    float scroll = 0, content = 0; // content: the text's height as drawn last (Renderer).
};
struct WhatsNewLayout {
    float width, height;
    settings_ui::Rect panel() const {
        const float w = std::min(820.0f, width - 120), h = std::min(640.0f, height - 120);
        const float left = std::round((width - w) * 0.5f), top = std::round((height - h) * 0.5f);
        return {left, top, left + w, top + h};
    }
    settings_ui::Rect text() const { const auto p = panel(); return {p.left + 44, p.top + 136, p.right - 44, p.bottom - 104}; }
    settings_ui::Rect ok() const { const auto p = panel(); return {p.right - 44 - 180, p.bottom - 80, p.right - 44, p.bottom - 28}; }
    float maxScroll(float content) const { const auto t = text(); return std::max(0.0f, content - (t.bottom - t.top)); }
};
}
