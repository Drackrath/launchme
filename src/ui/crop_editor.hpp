#pragma once
#include "settings_view.hpp"
#include "../artwork/text_crop.hpp"

namespace crop_ui {
struct State {
    Game game;
    std::filesystem::path image;
    CropFocus focus, automatic, drag_focus;
    int width = 0, height = 0, control = -1; // -2 Back, -1 the picture, 0 "Automatyczny", 1 "Zapisz kadr".
    static constexpr int back = -2;
    bool loading = true, automatic_selected = false, dragging = false;
    float drag_x = 0, drag_y = 0;
    std::wstring error;
    bool adjustable() const { return width > 0 && height > 0 && width != height; }
    void clamp() {
        if (!width || !height) return;
        const float side = static_cast<float>(std::min(width, height));
        focus.x = std::clamp(focus.x, side / (2 * width), 1 - side / (2 * width));
        focus.y = std::clamp(focus.y, side / (2 * height), 1 - side / (2 * height));
    }
    void move(int horizontal, int vertical) {
        if (!adjustable()) return;
        const float side = static_cast<float>(std::min(width, height));
        focus.x += horizontal * (width - side) / width * 0.02f;
        focus.y += vertical * (height - side) / height * 0.02f;
        clamp(); automatic_selected = false;
    }
};
// Tryb TV: the back button and heading at the top, the cover and the buttons below. In a desktop
// sheet (desktop): its own title and ✕ replace them, and the cover with its column of buttons is a
// smaller group centred in the sheet's body.
struct Layout {
    float width, height;
    bool desktop = false;
    static constexpr float desktop_gap = 56, desktop_column = 380;
    float size() const { return desktop ? std::clamp(height - 200, 200.0f, 400.0f) : std::min(520.0f, height - 280); }
    float left() const {
        if (desktop) return std::max(24.0f, (width - (size() + desktop_gap + desktop_column)) * 0.5f);
        return (width - std::min(1100.0f, width - 128)) * 0.5f;
    }
    float top() const { return desktop ? std::max(24.0f, (height - size()) * 0.5f) : 178.0f; }
    settings_ui::Rect back() const { return desktop ? settings_ui::Rect{} : settings_ui::Rect{left(), 58, left() + 58, 104}; }
    settings_ui::Rect preview() const { return {left(), top(), left() + size(), top() + size()}; }
    settings_ui::Rect title() const { // The game's name, above the buttons.
        const float x = button(0).left;
        return desktop ? settings_ui::Rect{x, top(), button(0).right, top() + 96} : settings_ui::Rect{x, 180, button(0).right, 285};
    }
    settings_ui::Rect button(int index) const {
        if (desktop) {
            const float x = preview().right + desktop_gap, y = top() + 116 + index * 66.0f;
            return {x, y, x + desktop_column, y + 50};
        }
        const float x = preview().right + 64, y = 310 + index * 76.0f;
        return {x, y, width - left(), y + 56};
    }
};
}
