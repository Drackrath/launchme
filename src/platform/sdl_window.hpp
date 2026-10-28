#pragma once
// The launcher window on Linux: an SDL3 window and its events in the platform-neutral form of
// platform/window_events.hpp (the Win32 window in src/main.cpp reports the same events).

#include "key_codes.hpp"
#include "native_window.hpp"
#include "window_events.hpp"

#include <SDL3/SDL.h>

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace platform {

// SDL keycode (layout-aware, as virtual keys are) to its keys:: value; 0 for keys the launcher
// has no use for.
unsigned virtualKey(SDL_Keycode key, SDL_Scancode scancode = SDL_SCANCODE_UNKNOWN);
Modifiers modifiers(SDL_Keymod mod);

// Converts one SDL event without changing SDL state (the wheel's Ctrl comes from the current
// modifier state). Also useful to platform-independent consumers that want to test their event
// handling. DisplayScaleEvent carries 0 here; SdlWindow::pollEvents fills in the window's scale.
std::optional<WindowEvent> translateSdlEvent(const SDL_Event& event);

SDL_HitTestResult sdlHitTest(HitArea area);

enum class Cursor { arrow, hand, text, hidden };

struct SdlWindowConfig {
    const char* title = "launchme";
    int width = 1280;
    int height = 720;
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    // The SDL 2D renderer (the interim Linux launcher draws with it). A Canvas backend that
    // presents through its own GPU or raster path creates the window without one.
    bool renderer = true;
};

class SdlWindow {
public:
    explicit SdlWindow(SdlWindowConfig config = {});
    ~SdlWindow();

    SdlWindow(const SdlWindow&) = delete;
    SdlWindow& operator=(const SdlWindow&) = delete;
    SdlWindow(SdlWindow&&) = delete;
    SdlWindow& operator=(SdlWindow&&) = delete;

    bool valid() const { return window_ != nullptr && (renderer_ != nullptr || !wants_renderer_); }
    const std::string& error() const { return error_; }
    SDL_Window* window() const { return window_; }
    // The handle window-bound helpers take (frame_clock, file_dialog, layered_surface, Renderer).
    NativeWindow native() const { return window_; }
    SDL_Renderer* renderer() const { return renderer_; }
    std::vector<WindowEvent> pollEvents() const;

    // Client size in logical units, in pixels, and their ratio's display scale (1.0 at 96 DPI).
    std::pair<int, int> size() const;
    std::pair<int, int> pixelSize() const;
    float displayScale() const;

    // Text input (and the input method's candidate window, placed beside `area`): on while a
    // search field or a text box has focus. Off, letters arrive only as keys.
    void startTextInput();
    void stopTextInput();
    void setTextInputArea(int x, int y, int width, int height, int cursor = 0);
    bool textInputActive() const;

    void setCursor(Cursor cursor);
    // Desktop mode's own top bar: which part of the window a point belongs to (nullptr: all client).
    void setHitTest(std::function<HitArea(float x, float y)> hit_test);
    void setTitle(const std::string& title);
    void maximize();
    void restore();
    void minimize();
    void setFullscreen(bool fullscreen);
    void raise();

private:
    static SDL_HitTestResult hitTestThunk(SDL_Window* window, const SDL_Point* point, void* data);
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    bool wants_renderer_ = true;
    std::string error_;
    std::function<HitArea(float, float)> hit_test_;
    Cursor cursor_ = Cursor::arrow;
    SDL_Cursor* cursors_[3]{};
};

} // namespace platform
