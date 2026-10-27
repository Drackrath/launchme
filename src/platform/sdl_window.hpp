#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace platform {

enum class KeyCode {
    Unknown,
    Escape,
    Up,
    Down,
    Left,
    Right,
};

struct QuitEvent {};

struct ResizeEvent {
    int width = 0;
    int height = 0;
};

struct KeyEvent {
    KeyCode key = KeyCode::Unknown;
    std::uint32_t native_code = 0;
    bool pressed = false;
    bool repeat = false;
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
};

struct GamepadEvent {
    GamepadAction action = GamepadAction::Connected;
    std::int64_t id = 0;
    GamepadButton button = GamepadButton::Unknown;
};

struct TextEvent {
    std::string text;
};

using AppEvent = std::variant<QuitEvent, ResizeEvent, KeyEvent, GamepadEvent, TextEvent>;

// Converts one SDL event without changing SDL state. This is also useful to
// platform-independent consumers that want to test their event handling.
std::optional<AppEvent> translateSdlEvent(const SDL_Event& event);

struct SdlWindowConfig {
    const char* title = "launchme";
    int width = 1280;
    int height = 720;
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;
};

class SdlWindow {
public:
    explicit SdlWindow(SdlWindowConfig config = {});
    ~SdlWindow();

    SdlWindow(const SdlWindow&) = delete;
    SdlWindow& operator=(const SdlWindow&) = delete;
    SdlWindow(SdlWindow&&) = delete;
    SdlWindow& operator=(SdlWindow&&) = delete;

    bool valid() const { return window_ != nullptr && renderer_ != nullptr; }
    const std::string& error() const { return error_; }
    SDL_Window* window() const { return window_; }
    SDL_Renderer* renderer() const { return renderer_; }
    std::vector<AppEvent> pollEvents() const;

private:
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    std::string error_;
};

} // namespace platform
