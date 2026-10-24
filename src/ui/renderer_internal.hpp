#pragma once
// Small drawing helpers shared by renderer.cpp and the files split out of it (renderer_text.cpp,
// background_renderer.cpp, card_renderer.cpp, …). Older sibling files keep their own copies.
#include "renderer.hpp"
#include "../library/emulation_platforms.hpp"

#include <d2d1helper.h>

#include <algorithm>
#include <string>

namespace render_detail {
inline D2D1_COLOR_F color(float r, float g, float b, float a = 1.0f) { return D2D1::ColorF(r, g, b, a); }
inline D2D1_COLOR_F platformColor(const std::wstring& platform, float alpha = 1.0f) {
    if (const auto* found = findEmulationPlatform(platform)) return color(found->red, found->green, found->blue, alpha);
    return color(0.18f, 0.25f, 0.33f, alpha);
}
inline float smoothFade(float value) {
    const float t = std::clamp(value, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
// The same picture, also from another load of the same file (a new shared buffer).
inline bool samePicture(const ImageBytes& a, const ImageBytes& b) {
    return a == b || (a && b && a->size() == b->size() && *a == *b);
}
}
