#include "platform/sdl_window.hpp"

#include <cassert>
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
    assert(escape.key == platform::KeyCode::Escape && escape.pressed);

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
    assert(click.button == SDL_BUTTON_LEFT && click.x == 40.0f && click.y == 50.0f && !click.pressed);

    SDL_Event focus{};
    focus.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    auto focus_event = platform::translateSdlEvent(focus);
    assert(focus_event && !std::get<platform::WindowFocusEvent>(*focus_event).focused);
}
