#pragma once
// What a launcher window reports, in one platform-neutral form: SDL3 events on Linux
// (platform/sdl_window.cpp translates them) and the WM_* messages that carry input on Windows
// (src/main.cpp). app/window_input.hpp turns these into the Launcher's calls, so both systems share
// one routing of keys, text, the mouse wheel and clicks.
//
// Coordinates are window client coordinates in the units the launcher lays out in (logical
// pixels on Linux, physical pixels on Windows, where the launcher is per-monitor DPI aware).
#include <cstdint>
#include <filesystem>
#include <string>
#include <variant>

namespace platform {

struct Modifiers {
    bool shift = false;
    bool control = false;
    bool alt = false;
    bool operator==(const Modifiers&) const = default;
};

// The system or the user asked the program to end (SDL_EVENT_QUIT, a signal, logout).
struct QuitEvent {};
// The window's close button, Alt+F4 (WM_CLOSE): the launcher may refuse and ask first.
struct CloseRequestEvent {};

struct ResizeEvent {
    int width = 0;
    int height = 0;
};

enum class WindowPlacement { restored, maximized, minimized, fullscreen, windowed };
struct PlacementEvent {
    WindowPlacement placement = WindowPlacement::restored;
};

struct WindowFocusEvent {
    bool focused = false;
};

// The window's content scale changed (a monitor with another DPI, a scale setting). 0: unknown,
// ask the window.
struct DisplayScaleEvent {
    float scale = 0;
};

// A display was added, removed or changed mode, or the window moved to another display.
struct DisplayChangedEvent {};

struct KeyEvent {
    unsigned key = 0; // keys:: (Windows virtual-key) value; 0 when the key has none.
    std::uint32_t native_code = 0; // SDL_Keycode on Linux, the VK value on Windows.
    bool pressed = false;
    bool repeat = false;
    Modifiers modifiers;
};

// Committed text (UTF-8): typed characters and the result of an input method.
struct TextEvent {
    std::string text;
};

// An input method's composition in progress (UTF-8), with its cursor or selection.
struct TextEditingEvent {
    std::string text;
    int start = 0;
    int length = 0;
};

struct MouseMoveEvent {
    float x = 0;
    float y = 0;
    float dx = 0;
    float dy = 0;
};

// The pointer left the window's client area.
struct MouseLeaveEvent {};

enum class MouseButton : std::uint8_t { none, left, middle, right, back, forward };

struct MouseButtonEvent {
    MouseButton button = MouseButton::none;
    float x = 0;
    float y = 0;
    bool pressed = false;
    int clicks = 1; // 2 on a double click (WM_LBUTTONDBLCLK); SDL counts on for further quick clicks.
};

// One notch is 120 (WHEEL_DELTA); positive turns away from the user. Precision touchpads send
// fractions of a notch.
constexpr int wheel_notch = 120;
struct MouseWheelEvent {
    float x = 0;
    float y = 0;
    int delta = 0;
    bool control = false;
};

enum class TouchAction { down, move, up, cancel };
struct TouchEvent {
    TouchAction action = TouchAction::down;
    std::uint64_t finger = 0;
    float x = 0; // Normalized to the window, 0..1.
    float y = 0;
    float pressure = 0;
};

enum class GamepadAction {
    Connected,
    Disconnected,
    ButtonDown,
    ButtonUp,
};

enum class GamepadButton {
    Unknown,
    South,
    East,
    West,
    North,
    Back,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    Start,
    Guide,
    LeftShoulder,
    RightShoulder,
    LeftStick,
    RightStick,
};

struct GamepadEvent {
    GamepadAction action = GamepadAction::Connected;
    std::int64_t id = 0;
    GamepadButton button = GamepadButton::Unknown;
};

// A file or folder dropped on the window.
struct DropFileEvent {
    std::filesystem::path path;
};

using WindowEvent = std::variant<QuitEvent, CloseRequestEvent, ResizeEvent, PlacementEvent, WindowFocusEvent,
                                 DisplayScaleEvent, DisplayChangedEvent, KeyEvent, TextEvent, TextEditingEvent,
                                 MouseMoveEvent, MouseLeaveEvent, MouseButtonEvent, MouseWheelEvent, TouchEvent,
                                 GamepadEvent, DropFileEvent>;
using AppEvent = WindowEvent;

// The window areas a borderless window reports for its own top bar (desktop mode), so the system
// moves and resizes it: WM_NCHITTEST's HT* values on Windows, SDL_HitTestResult on Linux.
enum class HitArea { client, caption, maximize_button, top, top_left, top_right, left, right, bottom, bottom_left, bottom_right };

} // namespace platform
