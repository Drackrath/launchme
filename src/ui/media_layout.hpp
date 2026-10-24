#pragma once
#include "../library/view_options.hpp"
#include "settings_view.hpp"
#include <array>

namespace media_layout {
struct Point { float x, y; };
struct Outline { std::array<Point, 10> points{}; size_t count = 0; };
inline Outline outline(library_view::View view, settings_ui::Rect r) {
    Outline shape;
    if (view == library_view::View::floppy)
        shape = {{{{0.02f, 0}, {0.92f, 0}, {1, 0.08f}, {1, 0.98f}, {0.98f, 1}, {0.02f, 1}, {0, 0.98f}, {0, 0.02f}}}, 8};
    else
        shape = {{{{0.06f, 0}, {0.94f, 0}, {1, 0.045f}, {1, 0.16f}, {0.96f, 0.16f}, {0.96f, 0.96f}, {0.91f, 1}, {0.05f, 1}, {0, 0.955f}, {0, 0.045f}}}, 10};
    for (size_t i = 0; i < shape.count; ++i) {
        auto& p = shape.points[i]; p.x = r.left + p.x * (r.right - r.left); p.y = r.top + p.y * (r.bottom - r.top);
    }
    return shape;
}
inline bool hit(library_view::View view, settings_ui::Rect r, float x, float y) {
    if (!r.contains(x, y)) return false;
    const auto shape = outline(view, r);
    bool inside = false;
    for (size_t i = 0, j = shape.count - 1; i < shape.count; j = i++) {
        const auto a = shape.points[i], b = shape.points[j];
        if ((a.y > y) != (b.y > y) && x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x) inside = !inside;
    }
    return inside;
}
inline settings_ui::Rect label(library_view::View view, settings_ui::Rect r) {
    const float w = r.right - r.left, h = r.bottom - r.top;
    if (view == library_view::View::floppy) return {r.left + w * 0.19f, r.top + h * 0.36f, r.left + w * 0.81f, r.top + h * 0.36f + w * 0.62f};
    // Switch cartridge: a 2:3 poster label.
    const float left = 0.10f, right = 0.89f, top = r.top + h * 0.075f;
    return {r.left + w * left, top, r.left + w * right, top + w * (right - left) * 1.5f};
}
}
