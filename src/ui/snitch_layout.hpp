#pragma once
// Snitch: one unwrapped row of equal square covers and a permanently visible compact dock.
#include "quick_menu.hpp"
#include "settings_view.hpp"
#include <algorithm>
#include <cstddef>

namespace snitch_layout {
inline constexpr size_t row_games = 12;
inline constexpr float left = 128, gap = 20, clip_left = 116;
inline float tileSize(float height) { return std::clamp(height * 0.36f, 240.0f, 360.0f); }
inline float rowTop(float height) { return height * 0.30f; }
inline float tileLeft(size_t index, size_t selected, float height) {
    return left + (static_cast<float>(index) - static_cast<float>(selected)) * (tileSize(height) + gap);
}
inline float dockY(float height) { return height * 0.85f; }
inline float dockScale(float width, int plugins) {
    return std::min(0.82f, (width - 128) / (quick_menu::count(plugins) * quick_menu::step));
}
inline float dockX(float width, int index, int plugins) {
    return width * 0.5f + (quick_menu::center(width, index, plugins) - width * 0.5f) * dockScale(width, plugins);
}
inline int hitDock(float width, float height, float x, float y, int plugins) {
    const float scale = dockScale(width, plugins), radius = 44 * scale;
    for (int i = 0; i < quick_menu::count(plugins); ++i) {
        const float dx = x - dockX(width, i, plugins), dy = y - dockY(height);
        if (dx * dx + dy * dy <= radius * radius) return i;
    }
    return -1;
}
inline settings_ui::Rect profile() { return {72, 48, 136, 112}; }
}
