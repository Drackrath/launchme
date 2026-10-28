#include "platform/sdl_window.hpp"

#include <cmath>
#include <utility>

namespace platform {
namespace {

GamepadButton gamepadButton(Uint8 button) {
    switch (static_cast<SDL_GamepadButton>(button)) {
    case SDL_GAMEPAD_BUTTON_SOUTH: return GamepadButton::South;
    case SDL_GAMEPAD_BUTTON_EAST: return GamepadButton::East;
    case SDL_GAMEPAD_BUTTON_WEST: return GamepadButton::West;
    case SDL_GAMEPAD_BUTTON_NORTH: return GamepadButton::North;
    case SDL_GAMEPAD_BUTTON_BACK: return GamepadButton::Back;
    case SDL_GAMEPAD_BUTTON_START: return GamepadButton::Start;
    case SDL_GAMEPAD_BUTTON_GUIDE: return GamepadButton::Guide;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return GamepadButton::LeftShoulder;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return GamepadButton::RightShoulder;
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return GamepadButton::LeftStick;
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return GamepadButton::RightStick;
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return GamepadButton::DpadUp;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return GamepadButton::DpadDown;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return GamepadButton::DpadLeft;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return GamepadButton::DpadRight;
    default: return GamepadButton::Unknown;
    }
}

MouseButton mouseButton(Uint8 button) {
    switch (button) {
    case SDL_BUTTON_LEFT: return MouseButton::left;
    case SDL_BUTTON_MIDDLE: return MouseButton::middle;
    case SDL_BUTTON_RIGHT: return MouseButton::right;
    case SDL_BUTTON_X1: return MouseButton::back;
    case SDL_BUTTON_X2: return MouseButton::forward;
    default: return MouseButton::none;
    }
}

} // namespace

unsigned virtualKey(SDL_Keycode key, SDL_Scancode scancode) {
    if (key >= SDLK_A && key <= SDLK_Z) return keys::letter(static_cast<char>('A' + (key - SDLK_A)));
    if (key >= SDLK_0 && key <= SDLK_9) return keys::digit(static_cast<int>(key - SDLK_0));
    if (key >= SDLK_F1 && key <= SDLK_F12) return keys::function(static_cast<int>(key - SDLK_F1) + 1);
    if (key >= SDLK_KP_1 && key <= SDLK_KP_9) return keys::numpad(static_cast<int>(key - SDLK_KP_1) + 1);
    switch (key) {
    case SDLK_BACKSPACE: return keys::back;
    case SDLK_TAB: return keys::tab;
    case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_RETURN2: return keys::enter;
    case SDLK_LSHIFT: case SDLK_RSHIFT: return keys::shift;
    case SDLK_LCTRL: case SDLK_RCTRL: return keys::control;
    case SDLK_LALT: case SDLK_RALT: return keys::alt;
    case SDLK_PAUSE: return keys::pause;
    case SDLK_CAPSLOCK: return keys::caps_lock;
    case SDLK_ESCAPE: return keys::escape;
    case SDLK_SPACE: return keys::space;
    case SDLK_PAGEUP: return keys::page_up;
    case SDLK_PAGEDOWN: return keys::page_down;
    case SDLK_END: return keys::end;
    case SDLK_HOME: return keys::home;
    case SDLK_LEFT: return keys::left;
    case SDLK_UP: return keys::up;
    case SDLK_RIGHT: return keys::right;
    case SDLK_DOWN: return keys::down;
    case SDLK_PRINTSCREEN: return keys::print_screen;
    case SDLK_INSERT: return keys::insert;
    case SDLK_DELETE: return keys::del;
    case SDLK_APPLICATION: case SDLK_MENU: return keys::apps;
    case SDLK_KP_0: return keys::numpad0;
    case SDLK_KP_MULTIPLY: return keys::multiply;
    case SDLK_KP_PLUS: return keys::add;
    case SDLK_KP_MINUS: return keys::subtract;
    case SDLK_KP_PERIOD: case SDLK_KP_DECIMAL: return keys::decimal;
    case SDLK_KP_DIVIDE: return keys::divide;
    case SDLK_SEMICOLON: return keys::oem_1;
    case SDLK_EQUALS: case SDLK_PLUS: return keys::oem_plus;
    case SDLK_COMMA: return keys::oem_comma;
    case SDLK_MINUS: return keys::oem_minus;
    case SDLK_PERIOD: return keys::oem_period;
    case SDLK_SLASH: return keys::oem_2;
    case SDLK_GRAVE: return keys::oem_3;
    case SDLK_LEFTBRACKET: return keys::oem_4;
    case SDLK_BACKSLASH: return keys::oem_5;
    case SDLK_RIGHTBRACKET: return keys::oem_6;
    case SDLK_APOSTROPHE: return keys::oem_7;
    case SDLK_AC_BACK: return keys::escape; // A keyboard's or remote's Back: what Esc does.
    default: break;
    }
    // The ISO key beside the left Shift has no fixed keycode (it is <, |, ... by layout).
    if (scancode == SDL_SCANCODE_NONUSBACKSLASH) return keys::oem_102;
    return keys::none;
}

Modifiers modifiers(SDL_Keymod mod) {
    return Modifiers{(mod & SDL_KMOD_SHIFT) != 0, (mod & SDL_KMOD_CTRL) != 0, (mod & SDL_KMOD_ALT) != 0};
}

std::optional<WindowEvent> translateSdlEvent(const SDL_Event& event) {
    switch (event.type) {
    case SDL_EVENT_QUIT:
        return QuitEvent{};
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        return CloseRequestEvent{};
    case SDL_EVENT_WINDOW_RESIZED:
        return ResizeEvent{event.window.data1, event.window.data2};
    case SDL_EVENT_WINDOW_MAXIMIZED:
        return PlacementEvent{WindowPlacement::maximized};
    case SDL_EVENT_WINDOW_MINIMIZED:
        return PlacementEvent{WindowPlacement::minimized};
    case SDL_EVENT_WINDOW_RESTORED:
        return PlacementEvent{WindowPlacement::restored};
    case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
        return PlacementEvent{WindowPlacement::fullscreen};
    case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
        return PlacementEvent{WindowPlacement::windowed};
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        return WindowFocusEvent{true};
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        return WindowFocusEvent{false};
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        return DisplayScaleEvent{};
    case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
    case SDL_EVENT_DISPLAY_ADDED:
    case SDL_EVENT_DISPLAY_REMOVED:
    case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED:
    case SDL_EVENT_DISPLAY_ORIENTATION:
        return DisplayChangedEvent{};
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        return MouseLeaveEvent{};
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        return KeyEvent{virtualKey(event.key.key, event.key.scancode), static_cast<std::uint32_t>(event.key.key),
                        event.key.down, event.key.repeat, modifiers(event.key.mod)};
    case SDL_EVENT_TEXT_INPUT:
        return TextEvent{event.text.text ? event.text.text : ""};
    case SDL_EVENT_TEXT_EDITING:
        return TextEditingEvent{event.edit.text ? event.edit.text : "", event.edit.start, event.edit.length};
    case SDL_EVENT_MOUSE_MOTION:
        return MouseMoveEvent{event.motion.x, event.motion.y, event.motion.xrel, event.motion.yrel};
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        return MouseButtonEvent{mouseButton(event.button.button), event.button.x, event.button.y,
                                event.type == SDL_EVENT_MOUSE_BUTTON_DOWN, event.button.clicks};
    case SDL_EVENT_MOUSE_WHEEL: {
        // SDL: 1.0 per notch, positive away from the user (unless the system flips it).
        float amount = event.wheel.y;
        if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) amount = -amount;
        const int delta = static_cast<int>(std::lround(amount * wheel_notch));
        if (!delta) return std::nullopt; // A horizontal scroll, or less than 1/120 of a notch.
        return MouseWheelEvent{event.wheel.mouse_x, event.wheel.mouse_y, delta, (SDL_GetModState() & SDL_KMOD_CTRL) != 0};
    }
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED: {
        const TouchAction action = event.type == SDL_EVENT_FINGER_DOWN ? TouchAction::down :
                                   event.type == SDL_EVENT_FINGER_MOTION ? TouchAction::move :
                                   event.type == SDL_EVENT_FINGER_UP ? TouchAction::up : TouchAction::cancel;
        return TouchEvent{action, static_cast<std::uint64_t>(event.tfinger.fingerID), event.tfinger.x, event.tfinger.y,
                          event.tfinger.pressure};
    }
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
    case SDL_EVENT_DROP_FILE:
        if (!event.drop.data) return std::nullopt;
        return DropFileEvent{std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(event.drop.data)))};
    default:
        return std::nullopt;
    }
}

SDL_HitTestResult sdlHitTest(HitArea area) {
    switch (area) {
    case HitArea::client: return SDL_HITTEST_NORMAL;
    case HitArea::caption: return SDL_HITTEST_DRAGGABLE;
    case HitArea::maximize_button: return SDL_HITTEST_NORMAL; // The launcher draws and presses it itself.
    case HitArea::top: return SDL_HITTEST_RESIZE_TOP;
    case HitArea::top_left: return SDL_HITTEST_RESIZE_TOPLEFT;
    case HitArea::top_right: return SDL_HITTEST_RESIZE_TOPRIGHT;
    case HitArea::left: return SDL_HITTEST_RESIZE_LEFT;
    case HitArea::right: return SDL_HITTEST_RESIZE_RIGHT;
    case HitArea::bottom: return SDL_HITTEST_RESIZE_BOTTOM;
    case HitArea::bottom_left: return SDL_HITTEST_RESIZE_BOTTOMLEFT;
    case HitArea::bottom_right: return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
    }
    return SDL_HITTEST_NORMAL;
}

SdlWindow::SdlWindow(SdlWindowConfig config) : wants_renderer_(config.renderer) {
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
    if (!config.renderer) return;
    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_) {
        error_ = SDL_GetError();
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        SDL_Quit();
    }
}

SdlWindow::~SdlWindow() {
    for (SDL_Cursor* cursor : cursors_)
        if (cursor) SDL_DestroyCursor(cursor);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) {
        SDL_SetWindowHitTest(window_, nullptr, nullptr);
        SDL_DestroyWindow(window_);
        SDL_Quit();
    }
}

std::vector<WindowEvent> SdlWindow::pollEvents() const {
    std::vector<WindowEvent> events;
    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
        auto translated = translateSdlEvent(event);
        if (!translated) continue;
        if (auto* scale = std::get_if<DisplayScaleEvent>(&*translated)) scale->scale = displayScale();
        events.push_back(std::move(*translated));
    }
    return events;
}

std::pair<int, int> SdlWindow::size() const {
    int width = 0, height = 0;
    if (window_) SDL_GetWindowSize(window_, &width, &height);
    return {width, height};
}

std::pair<int, int> SdlWindow::pixelSize() const {
    int width = 0, height = 0;
    if (window_) SDL_GetWindowSizeInPixels(window_, &width, &height);
    return {width, height};
}

float SdlWindow::displayScale() const {
    const float scale = window_ ? SDL_GetWindowDisplayScale(window_) : 0.0f;
    return scale > 0 ? scale : 1.0f;
}

void SdlWindow::startTextInput() { if (window_) SDL_StartTextInput(window_); }
void SdlWindow::stopTextInput() { if (window_) SDL_StopTextInput(window_); }
bool SdlWindow::textInputActive() const { return window_ && SDL_TextInputActive(window_); }

void SdlWindow::setTextInputArea(int x, int y, int width, int height, int cursor) {
    if (!window_) return;
    const SDL_Rect area{x, y, width, height};
    SDL_SetTextInputArea(window_, &area, cursor);
}

void SdlWindow::setCursor(Cursor cursor) {
    if (cursor == cursor_) return;
    cursor_ = cursor;
    if (cursor == Cursor::hidden) { SDL_HideCursor(); return; }
    const int index = cursor == Cursor::hand ? 1 : cursor == Cursor::text ? 2 : 0;
    if (!cursors_[index])
        cursors_[index] = SDL_CreateSystemCursor(index == 1 ? SDL_SYSTEM_CURSOR_POINTER :
                                                 index == 2 ? SDL_SYSTEM_CURSOR_TEXT : SDL_SYSTEM_CURSOR_DEFAULT);
    if (cursors_[index]) SDL_SetCursor(cursors_[index]);
    SDL_ShowCursor();
}

SDL_HitTestResult SdlWindow::hitTestThunk(SDL_Window*, const SDL_Point* point, void* data) {
    const auto* self = static_cast<const SdlWindow*>(data);
    if (!self || !point || !self->hit_test_) return SDL_HITTEST_NORMAL;
    return sdlHitTest(self->hit_test_(static_cast<float>(point->x), static_cast<float>(point->y)));
}

void SdlWindow::setHitTest(std::function<HitArea(float x, float y)> hit_test) {
    hit_test_ = std::move(hit_test);
    if (window_) SDL_SetWindowHitTest(window_, hit_test_ ? &SdlWindow::hitTestThunk : nullptr, hit_test_ ? this : nullptr);
}

void SdlWindow::setTitle(const std::string& title) { if (window_) SDL_SetWindowTitle(window_, title.c_str()); }
void SdlWindow::maximize() { if (window_) SDL_MaximizeWindow(window_); }
void SdlWindow::restore() { if (window_) SDL_RestoreWindow(window_); }
void SdlWindow::minimize() { if (window_) SDL_MinimizeWindow(window_); }
void SdlWindow::setFullscreen(bool fullscreen) { if (window_) SDL_SetWindowFullscreen(window_, fullscreen); }
void SdlWindow::raise() { if (window_) SDL_RaiseWindow(window_); }

} // namespace platform
