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
}
