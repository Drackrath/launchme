#pragma once

#include "game_details_layout.hpp"
#include "settings_view.hpp"

#include <algorithm>
#include <string>
#include <vector>

// Szczegóły → Osiągnięcia (src/app/achievements.cpp, src/ui/achievements_renderer.cpp): the
// signed-in account's achievements of one Steam game over the details screen. Icons use the
// renderer's artwork thumbnail slots, indexed like `items`.
namespace achievements_ui {
struct Item {
    std::wstring name, description;
    std::wstring detail;   // "Zdobyte 12.04.2026" or "61,3% graczy".
    bool achieved = false;
};
struct State {
    bool open = false, loading = false, complete = false;
    uint32_t app_id = 0, unlocked = 0, total = 0;
    std::wstring title;    // Game name.
    std::vector<Item> items;
    std::vector<std::wstring> icons; // URL per item, for the icon worker.
    int selected = 0;
    float scroll = 0;
    // Items plus the "Pozostałe osiągnięcia: n" row when the list is the client's partial cache.
    size_t rows() const { return items.size() + (!complete && total > items.size() ? 1 : 0); }
};
struct Layout {
    float width, height;
    static constexpr float row_step = 92, row_height = 82, icon = 64;
    details_layout::Layout panel() const { return {width, height}; }
    float left() const { return panel().origin(); }
    float right() const { return panel().right(); }
    float top() const { return panel().top() + 118; }   // First row, under the header.
    float bottom() const { return height - 36; }
    settings_ui::Rect back() const { return details_layout::back(width, height); }
    settings_ui::Rect row(size_t index, float scroll) const {
        const float y = top() + index * row_step - scroll;
        return {left(), y, right(), y + row_height};
    }
    int hit(float x, float y, size_t count, float scroll) const {
        if (y < top() || y >= bottom()) return -1;
        const auto index = static_cast<size_t>((y - top() + scroll) / row_step);
        return index < count && row(index, scroll).contains(x, y) ? static_cast<int>(index) : -1;
    }
    float maxScroll(size_t count) const {
        return std::max(0.0f, count * row_step - (row_step - row_height) - (bottom() - top()));
    }
    float ensureVisible(int index, size_t count, float scroll) const {
        const auto r = row(static_cast<size_t>(std::max(0, index)), scroll);
        if (r.top < top()) scroll -= top() - r.top;
        else if (r.bottom > bottom()) scroll += r.bottom - bottom();
        return std::clamp(scroll, 0.0f, maxScroll(count));
    }
    // Rows near the viewport that need their icon.
    std::pair<size_t, size_t> visible(size_t count, float scroll) const {
        const auto first = static_cast<size_t>(std::max(0.0f, scroll / row_step));
        const auto last = static_cast<size_t>((scroll + bottom() - top()) / row_step) + 2;
        return {std::min(count, first > 2 ? first - 2 : 0), std::min(count, last + 2)};
    }
};
}
