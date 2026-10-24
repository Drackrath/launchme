#pragma once
#include "spring.hpp"

// Guide owns its animation clock. It cannot depend on the minimized dashboard's
// viewport or on the main window having keyboard focus.
struct GuideMotion {
    float opacity = 0, bar = 0, drawer = 0;
    Spring circle;
    void open(float x) { opacity = bar = drawer = 0; circle.snap(x); }
    bool step(float seconds, bool closing, bool drawer_open, float x) {
        const float dt = std::clamp(seconds, 0.0f, 0.05f);
        bool changed = false;
        const auto move = [&](float& value, float target, float duration) {
            const float previous = value;
            value = value < target ? std::min(target, value + dt / duration) : std::max(target, value - dt / duration);
            changed |= value != previous;
        };
        move(opacity, closing ? 0.0f : 1.0f, closing ? 0.14f : 0.18f);
        move(bar, closing || drawer_open ? 0.0f : 1.0f, closing ? 0.14f : 0.18f);
        move(drawer, !closing && drawer_open ? 1.0f : 0.0f, closing ? 0.14f : 0.22f);
        if (!circle.settled(x)) { circle.step(x, dt, spring::glide.omega, spring::glide.zeta); changed = true; }
        else if (circle.value != x) { circle.snap(x); changed = true; }
        return changed;
    }
    bool hidden() const { return opacity == 0 && bar == 0 && drawer == 0; }
};
