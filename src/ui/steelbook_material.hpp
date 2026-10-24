#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

// Worker-built colour-selective foil/height map. RGB is the reflection tint, alpha is
// metal coverage and shallow relief; D2D lights it at render time. Unselected ink is matte.
// Optional A8 gloss: the darkest ink is a flat polished lacquer that mirrors a soft oval of
// the light, while brushed foil scatters it (so foil coverage is excluded from the gloss).
// Optional relief: high-contrast edges are pressed into the metal and catch the light.
namespace steelbook_material {
constexpr uint32_t max_edge = 768;
inline float smooth(float a, float b, float value) {
    const float t = std::clamp((value - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}
inline float noise(uint32_t seed) {
    seed ^= seed >> 16; seed *= 0x7feb352du; seed ^= seed >> 15; seed *= 0x846ca68bu; seed ^= seed >> 16;
    return (seed & 65535u) / 32767.5f - 1;
}
inline float hue(float r, float g, float b) {
    const float top = std::max({r, g, b}), chroma = top - std::min({r, g, b});
    if (chroma < 0.0001f) return 0;
    float value = top == r ? (g - b) / chroma : top == g ? 2 + (b - r) / chroma : 4 + (r - g) / chroma;
    return value < 0 ? value + 6 : value;
}
// Full gloss on near-black ink; dark grey fades out, mid tones stay matte.
inline float polish(float luma) { return 1 - smooth(0.05f, 0.22f, luma); }
// Signed relief direction of the source art (call before bake, which overwrites it): the
// luma gradient after a ~1 px binomial blur, gated by contrast so only strong edges
// (about 0.3+ luma across the edge) are embossed. Opaque premultiplied BGRA:
// R/G = 128 + 127 * direction (x/y, towards lighter ink), B = 128. Lighter ink is raised.
// Returns false (and leaves the output neutral) when nothing reaches the threshold.
inline bool emboss(std::span<const uint8_t> pixels, uint32_t width, uint32_t height, std::span<uint8_t> relief) {
    const size_t count = static_cast<size_t>(width) * height;
    if (!width || !height || width > max_edge || height > max_edge || pixels.size() != count * 4 || relief.size() != count * 4) return false;
    std::vector<float> luma(count), pass(count);
    for (size_t i = 0; i < count; ++i) { // Premultiplied: transparent art is luma over black.
        const auto* p = pixels.data() + i * 4;
        luma[i] = (0.2126f * p[2] + 0.7152f * p[1] + 0.0722f * p[0]) / 255;
    }
    const int w = int(width), h = int(height);
    const auto at = [&](const std::vector<float>& v, int x, int y) { return v[size_t(std::clamp(y, 0, h - 1)) * w + std::clamp(x, 0, w - 1)]; };
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
        pass[size_t(y) * w + x] = (at(luma, x - 2, y) + 4 * at(luma, x - 1, y) + 6 * at(luma, x, y) + 4 * at(luma, x + 1, y) + at(luma, x + 2, y)) / 16;
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
        luma[size_t(y) * w + x] = (at(pass, x, y - 2) + 4 * at(pass, x, y - 1) + 6 * at(pass, x, y) + 4 * at(pass, x, y + 1) + at(pass, x, y + 2)) / 16;
    bool raised = false;
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const auto v = [&](int dx, int dy) { return at(luma, x + dx, y + dy); };
        const float gx = (v(1, -1) + 2 * v(1, 0) + v(1, 1) - v(-1, -1) - 2 * v(-1, 0) - v(-1, 1)) / 8;
        const float gy = (v(-1, 1) + 2 * v(0, 1) + v(1, 1) - v(-1, -1) - 2 * v(0, -1) - v(1, -1)) / 8;
        // A blurred step of contrast C peaks at ~0.31 C: 0.06..0.14 is roughly C = 0.2..0.45.
        const float g = std::sqrt(gx * gx + gy * gy), gate = smooth(0.06f, 0.14f, g);
        const float scale = gate > 0 ? gate / g : 0;
        auto* p = relief.data() + (size_t(y) * w + x) * 4;
        p[0] = 128; p[3] = 255;
        p[2] = static_cast<uint8_t>(std::lround(128 + 127 * std::clamp(gx * scale, -1.0f, 1.0f)));
        p[1] = static_cast<uint8_t>(std::lround(128 + 127 * std::clamp(gy * scale, -1.0f, 1.0f)));
        raised |= p[2] != 128 || p[1] != 128;
    }
    return raised;
}
// Optional A8 matte: printed ink that is neither foil nor polished lacquer scatters light
// diffusely. It carries a fine ink grain, so the renderer's broad haze looks like matte print.
inline void bake(std::span<uint8_t> pixels, uint32_t width, uint32_t height, std::span<uint8_t> gloss = {}, std::span<uint8_t> matte = {}) {
    const size_t count = static_cast<size_t>(width) * height;
    if (!width || !height || width > max_edge || height > max_edge || pixels.size() != count * 4) return;
    const bool glossy = gloss.size() == count, matt = matte.size() == count;
    struct Bin { float weight = 0, r = 0, g = 0, b = 0; };
    std::array<Bin, 64> palette{};
    for (size_t i = 0; i < count; ++i) {
        const auto* p = pixels.data() + i * 4;
        if (p[3] < 32) continue;
        const float a = p[3] / 255.0f, r = p[2] / float(p[3]), g = p[1] / float(p[3]), b = p[0] / float(p[3]);
        const int bin = std::min(3, int(r * 4)) * 16 + std::min(3, int(g * 4)) * 4 + std::min(3, int(b * 4));
        auto& tone = palette[bin]; tone.weight += a; tone.r += r * a; tone.g += g * a; tone.b += b * a;
    }
    const auto& dominant = *std::max_element(palette.begin(), palette.end(), [](const auto& a, const auto& b) { return a.weight < b.weight; });
    const float divisor = std::max(1.0f, dominant.weight);
    const float br = dominant.r / divisor, bg = dominant.g / divisor, bb = dominant.b / divisor;
    std::array<float, 12> hues{};
    for (size_t i = 0; i < count; ++i) {
        const auto* p = pixels.data() + i * 4;
        if (!p[3]) continue;
        const float r = p[2] / float(p[3]), g = p[1] / float(p[3]), b = p[0] / float(p[3]);
        const float top = std::max({r, g, b}), chroma = top - std::min({r, g, b});
        const float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        const int bin = std::min(11, int(hue(r, g, b) * 2));
        // A saturated background is ink on metal too. Do not discard it just because
        // it is the most common colour; pale skin/cloth should not win over that ink.
        hues[bin] += chroma * chroma * smooth(0.28f, 0.62f, chroma / std::max(0.001f, top)) *
            smooth(0.02f, 0.16f, luma) * (p[3] / 255.0f);
    }
    int chosen = 0;
    float score = 0;
    for (int i = 0; i < 12; ++i) {
        const float candidate = hues[i] + 0.35f * (hues[(i + 11) % 12] + hues[(i + 1) % 12]);
        if (candidate > score) { score = candidate; chosen = i; }
    }
    const bool coloured = score > count * 0.001f;
    const float chosen_hue = (chosen + 0.5f) * 0.5f;
    for (int y = 0; y < int(height); ++y) for (int x = 0; x < int(width); ++x) {
        const size_t i = static_cast<size_t>(y) * width + x;
        auto* p = pixels.data() + i * 4;
        if (!p[3]) { p[0] = p[1] = p[2] = 0; if (glossy) gloss[i] = 0; if (matt) matte[i] = 0; continue; }
        const float ink = p[3] / 255.0f, r = p[2] / float(p[3]), g = p[1] / float(p[3]), b = p[0] / float(p[3]);
        const float top = std::max({r, g, b}), chroma = top - std::min({r, g, b});
        const float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        const float contrast = std::sqrt((r - br) * (r - br) + (g - bg) * (g - bg) + (b - bb) * (b - bb));
        float distance = std::abs(hue(r, g, b) - chosen_hue); distance = std::min(distance, 6 - distance);
        const float pigment = coloured ? (1 - smooth(0.28f, 0.72f, distance)) * smooth(0.28f, 0.62f, chroma / std::max(0.001f, top)) *
            smooth(0.08f, 0.22f, chroma) : smooth(0.40f, 0.78f, luma) * smooth(0.13f, 0.48f, contrast);
        const float coverage = pigment * smooth(0.02f, 0.14f, luma) * ink;
        // Micro-ridges run across the light's horizontal travel: vertical ones flip their lit
        // side as the light passes and appear to slide over the print.
        const float grain = 0.96f + 0.025f * noise(uint32_t(y) * 79u) + 0.015f * noise(uint32_t(x) * 374761393u + uint32_t(y) * 668265263u);
        const auto alpha = static_cast<uint8_t>(std::lround(std::clamp(coverage * grain, 0.0f, 1.0f) * 255));
        if (glossy) gloss[i] = static_cast<uint8_t>(std::lround(std::clamp(polish(luma) * (1 - coverage) * ink, 0.0f, 1.0f) * 255));
        if (matt) {
            const float speck = 0.6f * (0.5f + 0.5f * noise(uint32_t(x) * 2246822519u + uint32_t(y) * 3266489917u)) +
                0.4f * (0.5f + 0.5f * noise(uint32_t(x / 2) * 668265263u + uint32_t(y / 2) * 374761393u));
            matte[i] = static_cast<uint8_t>(std::lround(std::clamp((1 - polish(luma)) * (1 - coverage) * ink * (0.45f + 0.55f * speck), 0.0f, 1.0f) * 255));
        }
        // Coloured metal reflects its own hue. Monochrome artwork gets silver, not coloured noise.
        const float peak = std::max(0.001f, top);
        p[0] = static_cast<uint8_t>(std::lround(alpha * (coloured ? 0.30f + 0.70f * b / peak : 1.0f)));
        p[1] = static_cast<uint8_t>(std::lround(alpha * (coloured ? 0.30f + 0.70f * g / peak : 1.0f)));
        p[2] = static_cast<uint8_t>(std::lround(alpha * (coloured ? 0.30f + 0.70f * r / peak : 1.0f)));
        p[3] = alpha;
    }
}
}
