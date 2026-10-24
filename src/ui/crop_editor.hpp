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
struct Layout {
    float width, height;
    float left() const { return (width - std::min(1100.0f, width - 128)) * 0.5f; }
    float size() const { return std::min(520.0f, height - 280); }
    settings_ui::Rect back() const { return {left(), 58, left() + 58, 104}; }
    settings_ui::Rect preview() const { return {left(), 178, left() + size(), 178 + size()}; }
    settings_ui::Rect button(int index) const {
        const float x = preview().right + 64, y = 310 + index * 76.0f;
        return {x, y, width - left(), y + 56};
    }
};
}
