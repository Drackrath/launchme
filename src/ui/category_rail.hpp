#pragma once

#include "carousel.hpp"

// Default home with visible categories. Shared by drawing, carousel placement and hit testing.
namespace category_rail {
struct Layout {
    float width, height;
    size_t count = 0, selected = 0;
    static constexpr float left = 32, row_height = 68;
    static constexpr float hidden_card_offset = 32, compact_lift = 72;
    float right() const { return std::clamp(width * 0.19f, 240.0f, 310.0f); }
    float contentClip() const { return right() + 24; }
    size_t visible() const {
        return std::min(count, static_cast<size_t>(std::max(1.0f, std::floor((carousel::bottom(height) - 112) / row_height))));
    }
    size_t first() const {
        const auto rows = visible();
        return std::min(selected > rows / 2 ? selected - rows / 2 : 0, count - rows);
    }
    float top() const { return carousel::bottom(height) - visible() * row_height; }
    float rowTop(size_t index) const { return top() + (static_cast<float>(index) - first()) * row_height; }
    int hit(float x, float y) const {
        if (x < left || x >= right() || y < top() || y >= top() + visible() * row_height) return -1;
        return static_cast<int>(first()) + static_cast<int>((y - top()) / row_height);
    }
};
} // namespace category_rail
