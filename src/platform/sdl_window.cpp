#include "platform/sdl_window.hpp"

#include <utility>

namespace platform {
namespace {

KeyCode keyCode(SDL_Keycode key) {
    switch (key) {
    case SDLK_ESCAPE: return KeyCode::Escape;
    case SDLK_UP: return KeyCode::Up;
    case SDLK_DOWN: return KeyCode::Down;
    case SDLK_LEFT: return KeyCode::Left;
    case SDLK_RIGHT: return KeyCode::Right;
    default: return KeyCode::Unknown;
    }
}

GamepadButton gamepadButton(Uint8 button) {
    switch (static_cast<SDL_GamepadButton>(button)) {
    case SDL_GAMEPAD_BUTTON_SOUTH: return GamepadButton::South;
    case SDL_GAMEPAD_BUTTON_EAST: return GamepadButton::East;
    case SDL_GAMEPAD_BUTTON_WEST: return GamepadButton::West;
    case SDL_GAMEPAD_BUTTON_NORTH: return GamepadButton::North;
    case SDL_GAMEPAD_BUTTON_BACK: return GamepadButton::Back;
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return GamepadButton::DpadUp;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return GamepadButton::DpadDown;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return GamepadButton::DpadLeft;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return GamepadButton::DpadRight;
    default: return GamepadButton::Unknown;
    }
}

} // namespace

std::optional<AppEvent> translateSdlEvent(const SDL_Event& event) {
    switch (event.type) {
    case SDL_EVENT_QUIT:
        return QuitEvent{};
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        return ResizeEvent{event.window.data1, event.window.data2};
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        return KeyEvent{keyCode(event.key.key), static_cast<std::uint32_t>(event.key.key),
                        event.key.down, event.key.repeat};
    case SDL_EVENT_GAMEPAD_ADDED:
        return GamepadEvent{GamepadAction::Connected, event.gdevice.which};
    case SDL_EVENT_GAMEPAD_REMOVED:
        return GamepadEvent{GamepadAction::Disconnected, event.gdevice.which};
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        return GamepadEvent{
            event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ? GamepadAction::ButtonDown
                                                        : GamepadAction::ButtonUp,
            event.gbutton.which, gamepadButton(event.gbutton.button)};
    case SDL_EVENT_TEXT_INPUT:
        return TextEvent{event.text.text ? event.text.text : ""};
    default:
        return std::nullopt;
    }
}

SdlWindow::SdlWindow(SdlWindowConfig config) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        error_ = SDL_GetError();
        return;
    }
    window_ = SDL_CreateWindow(config.title, config.width, config.height, config.flags);
    if (!window_) {
        error_ = SDL_GetError();
        SDL_Quit();
        return;
    }
    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_) {
        error_ = SDL_GetError();
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        SDL_Quit();
    }
}

SdlWindow::~SdlWindow() {
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    if (renderer_ || window_) SDL_Quit();
}

std::vector<AppEvent> SdlWindow::pollEvents() const {
    std::vector<AppEvent> events;
    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
        if (auto translated = translateSdlEvent(event)) events.push_back(std::move(*translated));
    }
    return events;
}

} // namespace platform
