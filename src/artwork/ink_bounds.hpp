#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <vector>

struct InkBounds {
    uint32_t left = 0, top = 0, right = 0, bottom = 0; // Right/bottom exclusive.
    bool valid = false;
};

// PBGRA pixels from WIC: transparent gutters, opaque matte, and isolated noise
// are discarded. The returned source rectangle has no artificial padding.
inline InkBounds findInkBounds(std::span<const uint8_t> pixels, uint32_t width,
                               uint32_t height, uint32_t stride) {
    if (!width || !height || stride < width * 4 ||
        pixels.size() < static_cast<size_t>(stride) * height) return {};
    size_t opaque = 0;
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x)
            if (pixels[static_cast<size_t>(y) * stride + x * 4 + 3] >= 245) ++opaque;
    const bool matte = opaque > static_cast<size_t>(width) * height * 9 / 10;
    std::array<uint8_t, 4> corners[3];
    for (int channel = 0; channel < 3; ++channel) {
        corners[channel] = {pixels[channel], pixels[(width - 1) * 4 + channel],
                            pixels[static_cast<size_t>(height - 1) * stride + channel],
                            pixels[static_cast<size_t>(height - 1) * stride + (width - 1) * 4 + channel]};
        std::sort(corners[channel].begin(), corners[channel].end());
    }
    std::vector<uint32_t> columns(width), rows(height);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const auto* pixel = &pixels[static_cast<size_t>(y) * stride + x * 4];
            if (pixel[3] < 40) continue;
            if (matte && std::max({std::abs(int(pixel[0]) - corners[0][2]),
                                   std::abs(int(pixel[1]) - corners[1][2]),
                                   std::abs(int(pixel[2]) - corners[2][2])}) < 26) continue;
            ++columns[x]; ++rows[y];
        }
    }
    const auto x_min = std::max(2u, height / 320), y_min = std::max(2u, width / 320);
    uint32_t left = 0, top = 0, right = width, bottom = height;
    while (left < right && columns[left] < x_min) ++left;
    while (right > left && columns[right - 1] < x_min) --right;
    while (top < bottom && rows[top] < y_min) ++top;
    while (bottom > top && rows[bottom - 1] < y_min) --bottom;
    return left < right && top < bottom ? InkBounds{left, top, right, bottom, true} : InkBounds{};
}
