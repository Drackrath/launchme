#include "../src/artwork/ink_bounds.hpp"

#include <iostream>

int main() {
    constexpr uint32_t side = 32;
    std::vector<uint8_t> rgba(side * side * 4);
    const auto paint = [&](uint32_t x, uint32_t y, uint8_t r, uint8_t alpha) {
        auto* px = &rgba[(y * side + x) * 4];
        px[0] = px[1] = px[2] = r; px[3] = alpha;
    };
    for (uint32_t y = 10; y < 20; ++y)
        for (uint32_t x = 8; x < 22; ++x) paint(x, y, 255, 255);
    paint(0, 0, 255, 255); // A stray bright pixel must not add a gutter.
    auto bounds = findInkBounds(rgba, side, side, side * 4);
    if (!bounds.valid || bounds.left != 8 || bounds.top != 10 || bounds.right != 22 || bounds.bottom != 20) {
        std::cerr << "Transparent logo bounds included gutters or noise\n"; return 1;
    }
    for (uint32_t y = 0; y < side; ++y)
        for (uint32_t x = 0; x < side; ++x) paint(x, y, 0, 255);
    for (uint32_t y = 10; y < 20; ++y)
        for (uint32_t x = 8; x < 22; ++x) paint(x, y, 230, 255);
    paint(31, 31, 240, 255);
    bounds = findInkBounds(rgba, side, side, side * 4);
    if (!bounds.valid || bounds.left != 8 || bounds.top != 10 || bounds.right != 22 || bounds.bottom != 20) {
        std::cerr << "Opaque matte logo bounds included the canvas\n"; return 1;
    }
}
