// FrameClock on Linux (frame_clock_windows.cpp on Windows): the SDL window's display mode for the
// refresh rate, SDL's event queue to wake on input.
#include "frame_clock.hpp"

#include <SDL3/SDL.h>

#include <chrono>
#include <cmath>

namespace {
double seconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
}

FrameClock::FrameClock() { schedule_.rate(60, seconds()); }
FrameClock::~FrameClock() = default;

void FrameClock::update(platform::NativeWindow window) {
    update(window, window && (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0);
}

void FrameClock::update(platform::NativeWindow window, bool foreground) {
    const auto now = seconds();
    if (now >= next_probe_) {
        next_probe_ = now + 1;
        refresh_hz_ = 0;
        if (window) {
            // The mode of the display the window is on (0 Hz: unspecified, e.g. some virtual outputs).
            if (const SDL_DisplayID display = SDL_GetDisplayForWindow(window))
                if (const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(display); mode && mode->refresh_rate > 1)
                    refresh_hz_ = static_cast<int>(std::lround(mode->refresh_rate));
        }
    }
    const SDL_WindowFlags flags = window ? SDL_GetWindowFlags(window) : SDL_WINDOW_HIDDEN;
    const bool away = (flags & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN | SDL_WINDOW_OCCLUDED)) != 0;
    const int active = std::clamp(refresh_hz_ ? refresh_hz_ : 60, 30, 240);
    schedule_.rate(std::min(limit_, away ? 5 : foreground ? active : 30), now);
}

bool FrameClock::ready() const { return schedule_.ready(seconds()); }
void FrameClock::consume() { schedule_.consume(seconds()); }

void FrameClock::wait() {
    // Whole milliseconds while watching the event queue (true: input arrived, the loop takes it
    // now), then the rest precisely.
    const auto delay = schedule_.delay(seconds());
    const auto milliseconds = static_cast<Sint32>(std::floor(delay * 1000));
    if (milliseconds > 0 && SDL_WaitEventTimeout(nullptr, milliseconds)) return;
    if (const auto rest = schedule_.delay(seconds()); rest > 0)
        SDL_DelayPrecise(static_cast<Uint64>(rest * 1e9));
}
