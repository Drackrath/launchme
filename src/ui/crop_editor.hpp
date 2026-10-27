#pragma once
#include "settings_view.hpp"
#include "../artwork/text_crop.hpp"

#include <utility>

namespace crop_ui {
// The crop editor ("Popraw kadr"): a cover's square window, or with `background` the screen-shaped
// window a game's background shows (aspect = the screen's width / height).
struct State {
    Game game;
    std::filesystem::path image;
    CropFocus focus, automatic, drag_focus;
    int width = 0, height = 0, control = -1; // -2 Back, -1 the picture, 0 "Automatyczny", 1 "Zapisz kadr".
    static constexpr int back = -2;
    bool loading = true, automatic_selected = false, dragging = false;
    bool background = false;
    float aspect = 1; // The window's width / height: 1 for a cover.
    float drag_x = 0, drag_y = 0;
    std::wstring error;
    // The visible window in the picture's pixels: as large as it fits.
    std::pair<float, float> window() const {
        const float w = static_cast<float>(width), h = static_cast<float>(height), shape = aspect > 0 ? aspect : 1;
        return w / h > shape ? std::pair{h * shape, h} : std::pair{w, w / shape};
    }
    bool adjustable() const {
        if (width <= 0 || height <= 0) return false;
        const auto [w, h] = window();
        return width - w >= 1 || height - h >= 1;
    }
    void clamp() {
        if (!width || !height) return;
        const auto [w, h] = window();
        focus.x = std::clamp(focus.x, w / (2 * width), 1 - w / (2 * width));
        focus.y = std::clamp(focus.y, h / (2 * height), 1 - h / (2 * height));
    }
    void move(int horizontal, int vertical) {
        if (!adjustable()) return;
        const auto [w, h] = window();
        focus.x += horizontal * (width - w) / width * 0.02f;
        focus.y += vertical * (height - h) / height * 0.02f;
        clamp(); automatic_selected = false;
    }
};
// Tryb TV: the back button and heading at the top, the picture and the buttons below. In a desktop
// sheet (desktop): its own title and ✕ replace them, and the picture with its column of buttons is a
// smaller group centred in the sheet's body. A background's preview has the screen's shape.
struct Layout {
    float width, height;
    bool desktop = false;
    float aspect = 1; // The preview's width / height.
    static constexpr float desktop_gap = 56, desktop_column = 380, tv_gap = 64, tv_buttons = 320;
    float size() const { // The preview's height.
        const float square = desktop ? std::clamp(height - 200, 200.0f, 400.0f) : std::min(520.0f, height - 280);
        if (aspect <= 1) return square;
        const float room = desktop ? width - 48 - desktop_gap - desktop_column : std::min(1100.0f, width - 128) - tv_gap - tv_buttons;
        return std::max(120.0f, std::min(square, room / aspect));
    }
    float previewWidth() const { return size() * std::max(1.0f, aspect); }
    float left() const {
        if (desktop) return std::max(24.0f, (width - (previewWidth() + desktop_gap + desktop_column)) * 0.5f);
        return (width - std::min(1100.0f, width - 128)) * 0.5f;
    }
    float top() const { return desktop ? std::max(24.0f, (height - size()) * 0.5f) : 178.0f; }
    settings_ui::Rect back() const { return desktop ? settings_ui::Rect{} : settings_ui::Rect{left(), 58, left() + 58, 104}; }
    settings_ui::Rect preview() const { return {left(), top(), left() + previewWidth(), top() + size()}; }
    settings_ui::Rect title() const { // The game's name, above the buttons.
        const float x = button(0).left;
        return desktop ? settings_ui::Rect{x, top(), button(0).right, top() + 96} : settings_ui::Rect{x, 180, button(0).right, 285};
    }
    settings_ui::Rect button(int index) const {
        if (desktop) {
            const float x = preview().right + desktop_gap, y = top() + 116 + index * 66.0f;
            return {x, y, x + desktop_column, y + 50};
        }
        const float x = preview().right + tv_gap, y = 310 + index * 76.0f;
        return {x, y, width - left(), y + 56};
    }
};
}
