// The window-bound helpers on Linux: frame pacing, transparent-window surfaces, the file pickers'
// requests and the in-game Guide's availability. An offscreen SDL window stands in for a display.
#include "platform/file_dialog.hpp"
#include "platform/frame_clock.hpp"
#include "platform/layered_surface.hpp"
#include "platform/overlay_support.hpp"
#include "platform/sdl_window.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

int schedule() {
    for (int hz : {60, 120, 144, 240}) {
        FrameSchedule clock; clock.rate(hz, 10);
        for (int frame = 0; frame < 2000; ++frame) {
            const double now = 10 + static_cast<double>(frame) / hz + 0.00001;
            if (!clock.ready(now)) return 1;
            clock.consume(now);
            if (clock.ready(now) || clock.delay(now) > 1.0 / hz) return 2;
        }
        clock.consume(100); // A pause must not start an unbounded catch-up loop.
        if (clock.ready(100) || clock.delay(100) > 1.0 / hz + 0.00001) return 3;
        clock.rate(30, 101); if (!clock.ready(101) || clock.hz() != 30) return 4;
    }
    return 0;
}

int frameClock(platform::SdlWindow* window) {
    FrameClock clock;
    clock.update(nullptr, true); // No window: the hidden rate.
    if (clock.targetHz() != 5) return 10;
    if (window) {
        clock.update(window->native(), true); // Hidden: 5 Hz, as a minimized window on Windows.
        if (clock.targetHz() != 5) return 11;
        SDL_ShowWindow(window->window());
        clock.update(window->native(), true);
        if (clock.targetHz() < 30 || clock.targetHz() > 240) return 12;
        clock.update(window->native(), false); // Behind another window.
        if (clock.targetHz() != 30) return 13;
        clock.limit(5);
        clock.update(window->native(), true);
        if (clock.targetHz() != 5) return 14;
        clock.limit(240);
    }
    // wait() sleeps until the next frame (here at most 1/30 s), not much longer.
    clock.update(window ? window->native() : nullptr, false);
    clock.consume();
    const auto start = std::chrono::steady_clock::now();
    while (!clock.ready()) clock.wait();
    const auto waited = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (waited > 0.5) return 15;
    return 0;
}

int surface(platform::SdlWindow* window) {
    LayeredSurface layered;
    if (layered.resize(0, 10) || layered.resize(16385, 1) || layered.resize(16384, 16384) || layered.pixels()) return 20;
    if (!layered.resize(64, 32) || layered.width() != 64 || layered.height() != 32 || layered.stride() != 256 || !layered.pixels()) return 21;
    if (layered.pixels()[0] != 0) return 22; // Starts transparent.
    const std::uint32_t* before = layered.pixels();
    if (!layered.resize(64, 32) || layered.pixels() != before) return 23; // Same size: kept.
    if (layered.present(nullptr)) return 24;
    if (window) {
        layered.pixels()[0] = 0x80402010u; // Premultiplied BGRA: B 0x10, G 0x20, R 0x40, A 0x80.
        if (!layered.present(window->native())) {
            std::cerr << "present: " << SDL_GetError() << "\n";
            return 25;
        }
        SDL_Surface* shown = SDL_GetWindowSurface(window->window());
        if (!shown) return 26;
        Uint8 r = 0, g = 0, b = 0, a = 0;
        if (!SDL_ReadSurfacePixel(shown, 0, 0, &r, &g, &b, &a)) return 27;
        // Copied, not blended over what was there (the alpha may be dropped by an opaque window surface).
        if (r != 0x40 || g != 0x20 || b != 0x10) return 28;
    }
    return 0;
}

int dialogs() {
    using file_dialog::sdlPattern;
    if (sdlPattern({L".png", L".jpg"}) != "png;jpg" || sdlPattern({}) != "*" || sdlPattern({L"*.zip"}) != "zip" ||
        sdlPattern({L"", L"."}) != "*" || sdlPattern({L".flac", L".ógg"}) != "flac;\xC3\xB3gg") return 30;
    const auto folder = std::filesystem::temp_directory_path() / ("launchme-dialog-" + std::to_string(::getpid()));
    std::filesystem::create_directories(folder);
    const auto file = folder / "cover.png";
    std::ofstream(file) << "x";
    using file_dialog::startLocation;
    const bool ok = startLocation(folder, true) == folder && startLocation(folder, false) == folder &&
                    startLocation(file, false) == file && startLocation(file, true) == folder &&
                    startLocation(folder / "missing.png", false) == folder && startLocation({}, false).empty() &&
                    startLocation(folder / "gone" / "missing.png", false).empty();
    std::filesystem::remove_all(folder);
    if (!ok) return 31;
    // Without SDL video there is nothing to show the dialog on: an immediate empty answer.
    if (!SDL_WasInit(SDL_INIT_VIDEO) && !pickArtworkImage(nullptr).empty()) return 32;
    return 0;
}

int overlay() {
    using overlay_support::Session;
    using overlay_support::session;
    if (session("wayland-0", ":0", "", "wayland") != Session::wayland) return 40; // Xwayland too: still Wayland.
    if (session("", ":0", "", "x11") != Session::x11 || session("", "", "", "x11") != Session::x11) return 41;
    if (session("wayland-0", ":1", "gamescope-0", "") != Session::gamescope) return 42;
    if (session("", "", "", "tty") != Session::none) return 43;
    if (overlay_support::available(Session::wayland) || overlay_support::available(Session::gamescope) ||
        overlay_support::available(Session::x11) || !overlay_support::available(Session::windows)) return 44;
    if (overlay_support::unavailableReason(Session::wayland).empty() || !overlay_support::unavailableReason(Session::windows).empty()) return 45;
    if (overlay_support::available()) return 46; // Not on Linux yet, whatever the session.
    return 0;
}

} // namespace

int main() {
    if (const int failure = schedule()) return failure;
    if (const int failure = dialogs()) return failure;
    if (const int failure = overlay()) return failure;
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    platform::SdlWindow window({"window platform test", 160, 120, SDL_WINDOW_HIDDEN, false});
    platform::SdlWindow* shown = window.valid() ? &window : nullptr;
    if (!shown) std::cerr << "no offscreen SDL video (" << window.error() << "): window checks skipped\n";
    if (const int failure = frameClock(shown)) return failure;
    if (const int failure = surface(shown)) return failure;
    std::cout << "frame clock, layered surface, file dialog requests and overlay support passed\n";
}
