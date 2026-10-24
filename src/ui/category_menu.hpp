#pragma once

#include <algorithm>
#include <cstddef>

namespace category_menu {
constexpr float row_height = 68.0f;
inline float top(float height, size_t count) {
    return std::max(65.0f, (height - static_cast<float>(count) * row_height) * 0.5f);
}
inline float maxScroll(float height, size_t count) {
    return std::max(0.0f, top(height, count) + static_cast<float>(count) * row_height - (height - 25.0f));
}
inline int hit(float y, float height, size_t count, float scroll = 0) {
    if (y < 40 || y >= height - 15) return -1;
    const float start = top(height, count);
    const float offset = y + scroll - start;
    if (offset < 0 || offset >= count * row_height) return -1;
    return static_cast<int>(offset / row_height);
}
} // namespace category_menu
