#pragma once

#include <algorithm>
#include <cmath>

// Damped spring toward a (possibly moving) target. Semi-implicit Euler in steps of at most
// 4 ms keeps it stable for the frame clock's 50 ms dt cap. zeta = 1 arrives without
// overshoot; below 1 it overshoots and swings back.
struct Spring {
    float value = 0, velocity = 0;
    void snap(float target) { value = target; velocity = 0; }
    void step(float target, float dt, float omega, float zeta) {
        const int steps = std::clamp(static_cast<int>(std::ceil(dt / 0.004f)), 1, 64);
        const float h = dt / static_cast<float>(steps);
        for (int i = 0; i < steps; ++i) {
            velocity += (omega * omega * (target - value) - 2.0f * zeta * omega * velocity) * h;
            value += velocity * h;
        }
    }
    bool settled(float target, float tolerance = 0.25f) const {
        return std::abs(target - value) <= tolerance && std::abs(velocity) <= tolerance * 8.0f;
    }
};

namespace spring {
struct Tuning { float omega, zeta; };
// Quick bar circle and Settings highlights: a glide without overshoot (~0.25 s).
inline constexpr Tuning glide{26.0f, 1.0f};
// Experimental "PS5" focus: about 9 % overshoot, settled in ~0.35 s.
inline constexpr Tuning bounce{20.0f, 0.6f};
}

// A focus frame: four edges on springs, so it can move between items of different sizes
// (a row, a small button, a tile) and keep its own motion.
struct SpringBox {
    Spring left, top, right, bottom;
    void snap(float l, float t, float r, float b) { left.snap(l); top.snap(t); right.snap(r); bottom.snap(b); }
    void step(float l, float t, float r, float b, float dt, spring::Tuning tuning) {
        left.step(l, dt, tuning.omega, tuning.zeta); top.step(t, dt, tuning.omega, tuning.zeta);
        right.step(r, dt, tuning.omega, tuning.zeta); bottom.step(b, dt, tuning.omega, tuning.zeta);
    }
    bool settled(float l, float t, float r, float b) const {
        return left.settled(l) && top.settled(t) && right.settled(r) && bottom.settled(b);
    }
};
