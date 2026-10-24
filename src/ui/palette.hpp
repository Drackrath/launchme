#pragma once

#include <d2d1.h>
#include <d2d1helper.h>

// Shared UI ink. Screens use these roles instead of local, near-identical literals, so
// secondary text or an error reads the same on every screen.
namespace palette {

inline D2D1_COLOR_F gray(float value, float alpha = 1) { return D2D1::ColorF(value, value, value, alpha); }

// Primary text and glyphs on dark panels.
inline D2D1_COLOR_F text(float alpha = 1) { return D2D1::ColorF(0.95f, 0.96f, 0.97f, alpha); }
// Secondary text: descriptions, metadata, captions (a cool gray).
inline D2D1_COLOR_F muted(float alpha = 1) { return D2D1::ColorF(0.58f, 0.62f, 0.65f, alpha); }
// "Włączone"/"Wyłączone" next to a switch; dimmer while the row is locked.
inline D2D1_COLOR_F stateLabel(bool locked = false, float alpha = 1) { return gray(locked ? 0.42f : 0.62f, alpha); }
// Errors, destructive actions and warnings (hot sensors).
inline D2D1_COLOR_F danger(float alpha = 1) { return D2D1::ColorF(0.96f, 0.56f, 0.48f, alpha); }
// Error toast badge: stronger than danger so it reads on frosted glass.
inline D2D1_COLOR_F dangerStrong(float alpha = 1) { return D2D1::ColorF(0.94f, 0.36f, 0.33f, alpha); }

}
