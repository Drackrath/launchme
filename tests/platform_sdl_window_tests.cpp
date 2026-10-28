#undef NDEBUG
#include "platform/sdl_window.hpp"

#include <cassert>
#include <cstdio>
#include <utility>
#include <variant>

int main() {
    SDL_Event quit{};
    quit.type = SDL_EVENT_QUIT;
    auto quit_event = platform::translateSdlEvent(quit);
    assert(quit_event && std::holds_alternative<platform::QuitEvent>(*quit_event));

    SDL_Event resize{};
    resize.type = SDL_EVENT_WINDOW_RESIZED;
    resize.window.data1 = 800;
    resize.window.data2 = 600;
    auto resize_event = platform::translateSdlEvent(resize);
    assert(resize_event);
    const auto& dimensions = std::get<platform::ResizeEvent>(*resize_event);
    assert(dimensions.width == 800 && dimensions.height == 600);

    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.key = SDLK_ESCAPE;
    key.key.down = true;
    auto key_event = platform::translateSdlEvent(key);
    assert(key_event);
    const auto& escape = std::get<platform::KeyEvent>(*key_event);
    assert(escape.key == keys::escape && escape.pressed && !escape.repeat);

    SDL_Event text{};
    text.type = SDL_EVENT_TEXT_INPUT;
    text.text.text = "hello";
    auto text_event = platform::translateSdlEvent(text);
    assert(text_event && std::get<platform::TextEvent>(*text_event).text == "hello");

    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = 12.5f;
    motion.motion.y = 24.0f;
    motion.motion.xrel = 2.0f;
    motion.motion.yrel = -1.0f;
    auto motion_event = platform::translateSdlEvent(motion);
    assert(motion_event);
    const auto& pointer = std::get<platform::MouseMoveEvent>(*motion_event);
    assert(pointer.x == 12.5f && pointer.y == 24.0f && pointer.dx == 2.0f && pointer.dy == -1.0f);

    SDL_Event button{};
    button.type = SDL_EVENT_MOUSE_BUTTON_UP;
    button.button.button = SDL_BUTTON_LEFT;
    button.button.x = 40.0f;
    button.button.y = 50.0f;
    auto button_event = platform::translateSdlEvent(button);
    assert(button_event);
    const auto& click = std::get<platform::MouseButtonEvent>(*button_event);
    assert(click.button == platform::MouseButton::left && click.x == 40.0f && click.y == 50.0f && !click.pressed);

    SDL_Event focus{};
    focus.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    auto focus_event = platform::translateSdlEvent(focus);
    assert(focus_event && !std::get<platform::WindowFocusEvent>(*focus_event).focused);

    // Keys arrive as Windows virtual-key values, with their modifiers.
    assert(platform::virtualKey(SDLK_A) == 'A' && platform::virtualKey(SDLK_Z) == 'Z');
    assert(platform::virtualKey(SDLK_7) == '7' && platform::virtualKey(SDLK_F11) == keys::f11);
    assert(platform::virtualKey(SDLK_KP_0) == keys::numpad0 && platform::virtualKey(SDLK_KP_9) == keys::numpad(9));
    assert(platform::virtualKey(SDLK_PAGEUP) == keys::page_up && platform::virtualKey(SDLK_RETURN) == keys::enter &&
           platform::virtualKey(SDLK_KP_ENTER) == keys::enter && platform::virtualKey(SDLK_BACKSPACE) == keys::back);
    assert(platform::virtualKey(SDLK_RCTRL) == keys::control && platform::virtualKey(SDLK_MINUS) == keys::oem_minus);
    assert(platform::virtualKey(SDLK_UNKNOWN, SDL_SCANCODE_NONUSBACKSLASH) == keys::oem_102);
    assert(platform::virtualKey(SDLK_UNKNOWN) == keys::none);
    SDL_Event held{};
    held.type = SDL_EVENT_KEY_DOWN;
    held.key.key = SDLK_V;
    held.key.down = true;
    held.key.repeat = true;
    held.key.mod = SDL_KMOD_LCTRL | SDL_KMOD_RSHIFT;
    const auto paste = std::get<platform::KeyEvent>(*platform::translateSdlEvent(held));
    assert(paste.key == 'V' && paste.repeat && paste.modifiers == (platform::Modifiers{true, true, false}));

    // The wheel in 1/120 notches, positive away from the user; flipped devices turned back.
    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.y = 0.25f;
    wheel.wheel.mouse_x = 3;
    wheel.wheel.mouse_y = 4;
    auto scroll = std::get<platform::MouseWheelEvent>(*platform::translateSdlEvent(wheel));
    assert(scroll.delta == 30 && scroll.x == 3 && scroll.y == 4);
    wheel.wheel.y = 1;
    wheel.wheel.direction = SDL_MOUSEWHEEL_FLIPPED;
    assert(std::get<platform::MouseWheelEvent>(*platform::translateSdlEvent(wheel)).delta == -120);
    wheel.wheel.y = 0;
    wheel.wheel.x = 1; // Horizontal only: nothing the launcher scrolls.
    assert(!platform::translateSdlEvent(wheel));

    SDL_Event twice{};
    twice.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    twice.button.button = SDL_BUTTON_X1;
    twice.button.clicks = 2;
    const auto back = std::get<platform::MouseButtonEvent>(*platform::translateSdlEvent(twice));
    assert(back.button == platform::MouseButton::back && back.pressed && back.clicks == 2);

    // Window state.
    SDL_Event close{};
    close.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    assert(std::holds_alternative<platform::CloseRequestEvent>(*platform::translateSdlEvent(close)));
    SDL_Event maximized{};
    maximized.type = SDL_EVENT_WINDOW_MAXIMIZED;
    assert(std::get<platform::PlacementEvent>(*platform::translateSdlEvent(maximized)).placement == platform::WindowPlacement::maximized);
    SDL_Event scale{};
    scale.type = SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED;
    assert(std::holds_alternative<platform::DisplayScaleEvent>(*platform::translateSdlEvent(scale)));
    SDL_Event display{};
    display.type = SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED;
    assert(std::holds_alternative<platform::DisplayChangedEvent>(*platform::translateSdlEvent(display)));
    SDL_Event leave{};
    leave.type = SDL_EVENT_WINDOW_MOUSE_LEAVE;
    assert(std::holds_alternative<platform::MouseLeaveEvent>(*platform::translateSdlEvent(leave)));

    // Input method composition, touch, drops.
    SDL_Event editing{};
    editing.type = SDL_EVENT_TEXT_EDITING;
    editing.edit.text = "\xE3\x81\x8B"; // か
    editing.edit.start = 1;
    const auto composition = std::get<platform::TextEditingEvent>(*platform::translateSdlEvent(editing));
    assert(composition.text == "\xE3\x81\x8B" && composition.start == 1 && composition.length == 0);
    SDL_Event finger{};
    finger.type = SDL_EVENT_FINGER_UP;
    finger.tfinger.fingerID = 7;
    finger.tfinger.x = 0.5f;
    finger.tfinger.y = 0.25f;
    const auto touch = std::get<platform::TouchEvent>(*platform::translateSdlEvent(finger));
    assert(touch.action == platform::TouchAction::up && touch.finger == 7 && touch.x == 0.5f && touch.y == 0.25f);
    SDL_Event drop{};
    drop.type = SDL_EVENT_DROP_FILE;
    drop.drop.data = "/tmp/G\xC3\xA4me.exe";
    assert(std::get<platform::DropFileEvent>(*platform::translateSdlEvent(drop)).path.wstring() == L"/tmp/Gäme.exe");
    drop.drop.data = nullptr;
    assert(!platform::translateSdlEvent(drop));

    SDL_Event pad{};
    pad.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    pad.gbutton.which = 3;
    pad.gbutton.button = SDL_GAMEPAD_BUTTON_GUIDE;
    const auto guide = std::get<platform::GamepadEvent>(*platform::translateSdlEvent(pad));
    assert(guide.action == platform::GamepadAction::ButtonDown && guide.id == 3 && guide.button == platform::GamepadButton::Guide);

    // Desktop mode's own top bar.
    assert(platform::sdlHitTest(platform::HitArea::caption) == SDL_HITTEST_DRAGGABLE);
    assert(platform::sdlHitTest(platform::HitArea::top_left) == SDL_HITTEST_RESIZE_TOPLEFT);
    assert(platform::sdlHitTest(platform::HitArea::maximize_button) == SDL_HITTEST_NORMAL);

    // A real (offscreen) window: size, scale, text input and cursor calls.
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    {
        platform::SdlWindow window({"test", 320, 200, SDL_WINDOW_HIDDEN, false});
        if (window.valid()) {
            assert(window.native() == window.window() && !window.renderer());
            assert(window.size() == (std::pair<int, int>{320, 200}) && window.displayScale() > 0);
            window.setHitTest([](float, float y) { return y < 30 ? platform::HitArea::caption : platform::HitArea::client; });
            window.setTextInputArea(10, 10, 100, 20);
            window.setCursor(platform::Cursor::hand);
            window.setHitTest(nullptr);
            (void)window.pollEvents();
        } else {
            std::fprintf(stderr, "no offscreen SDL video (%s): window calls not checked\n", window.error().c_str());
        }
    }
    std::puts("SDL window events passed");
}
