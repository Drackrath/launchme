#pragma once
#include <algorithm>
#include <cmath>

namespace launch_animation {
inline constexpr float seconds = 0.85f;
inline float intro(float age) {
    const float t = std::clamp(age / 0.16f, 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}
inline float opacity(float age) { return intro(age) * std::clamp((seconds - age) / 0.22f, 0.0f, 1.0f); }
inline float scale(float age) {
    const float t = std::clamp(age / seconds, 0.0f, 1.0f);
    return 0.72f + 0.42f * (1 - std::pow(1 - t, 3.0f));
}
}
