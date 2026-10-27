#pragma once
#include <algorithm>
#include <cmath>

// Starting a game: its background, a touch dimmed, slowly zooms in while its logo (its title
// without one) rests in the middle; the game opens at once underneath. The picture stays until
// the game is there: once the game's window takes the foreground it rests, still, behind the game
// (no repaints), and the launcher fades it out when it is seen again or the game ends. Left in
// front, it lasts at least `seconds`, longer while the game still starts (up to `longest`).
// Back (Esc, B, the mouse's back button) hides it after `dismiss_after`.
namespace launch_animation {
inline constexpr float seconds = 10.0f;      // The shortest run in front of the user.
inline constexpr float longest = 120.0f;     // A game that never shows (the session's start-up limit).
inline constexpr float zoom_seconds = 14.0f; // The background's zoom, then a still picture.
inline constexpr float fade_in = 0.45f, fade_out = 0.7f;
inline constexpr float dismiss_after = 0.6f; // The press that started the game is not a Back.
inline constexpr float dim = 0.40f;          // Black over the background, before the bottom shade.
inline constexpr float zoom_extra = 0.08f;   // The background ends 8% larger.
inline float ease(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}
// The whole picture: fades in over the screen; `ending` (seconds since its fade-out began, < 0
// while it stays) fades it out (the screen returns beneath).
inline float opacity(float age, float ending = -1.0f) {
    return ease(age / fade_in) * (ending < 0 ? 1.0f : ease((fade_out - ending) / fade_out));
}
// The background's scale: slow, decelerating, then still.
inline float zoom(float age) {
    const float t = std::clamp(age / zoom_seconds, 0.0f, 1.0f);
    return 1.0f + zoom_extra * (1 - (1 - t) * (1 - t));
}
// Still: nothing on the picture moves any more (the zoom ended; the text pulse is not worth a frame).
inline bool still(float age) { return age >= zoom_seconds; }
// The logo (or title) a beat after the background, settling from slightly smaller.
inline float logo(float age) { return ease((age - 0.2f) / 0.6f); }
inline float logoScale(float age) { return 0.94f + 0.06f * logo(age); }
}
