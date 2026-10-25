#pragma once
#include <algorithm>
#include <cmath>

// Starting a game: its background, a touch dimmed, slowly zooms in for `seconds` while its logo
// (its title without one) rests in the middle; the game opens at once underneath. Back (Esc, B,
// the mouse's back button) hides it after `dismiss_after`.
namespace launch_animation {
inline constexpr float seconds = 10.0f;
inline constexpr float fade_in = 0.45f, fade_out = 0.7f;
inline constexpr float dismiss_after = 0.6f; // The press that started the game is not a Back.
inline constexpr float dim = 0.40f;          // Black over the background, before the bottom shade.
inline constexpr float zoom_extra = 0.08f;   // The background ends 8% larger.
inline float ease(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}
// The whole picture: fades in over the screen, out at the end (the screen returns beneath).
inline float opacity(float age) { return ease(age / fade_in) * ease((seconds - age) / fade_out); }
// The background's scale: slow, decelerating, continuous for the whole run.
inline float zoom(float age) {
    const float t = std::clamp(age / seconds, 0.0f, 1.0f);
    return 1.0f + zoom_extra * (1 - (1 - t) * (1 - t));
}
// The logo (or title) a beat after the background, settling from slightly smaller.
inline float logo(float age) { return ease((age - 0.2f) / 0.6f); }
inline float logoScale(float age) { return 0.94f + 0.06f * logo(age); }
}
