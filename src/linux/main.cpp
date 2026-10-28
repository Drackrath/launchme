#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "library/steam.hpp"
#include "platform/text_encoding.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

constexpr int window_width = 1280;
constexpr int window_height = 720;

struct TextTexture {
    SDL_Texture* texture = nullptr;
    int width = 0;
    int height = 0;
};

TextTexture textTexture(SDL_Renderer* renderer, TTF_Font* font, const std::string& text, SDL_Color color) {
    SDL_Surface* surface = TTF_RenderText_Blended(font, text.c_str(), text.size(), color);
    if (!surface) return {};
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    TextTexture result{texture, surface->w, surface->h};
    SDL_DestroySurface(surface);
    return result;
}

bool reportFailure(const char* operation, const char* detail) {
    std::fprintf(stderr, "launchme: %s: %s\n", operation, detail && *detail ? detail : "unknown error");
    return false;
}

bool reportSdlFailure(const char* operation) {
    return reportFailure(operation, SDL_GetError());
}

void drawText(SDL_Renderer* renderer, TTF_Font* font, const std::string& text, float x, float y, SDL_Color color) {
    TextTexture rendered = textTexture(renderer, font, text, color);
    if (!rendered.texture) return;
    SDL_FRect destination{x, y, static_cast<float>(rendered.width), static_cast<float>(rendered.height)};
    SDL_RenderTexture(renderer, rendered.texture, nullptr, &destination);
    SDL_DestroyTexture(rendered.texture);
}

void drawPanel(SDL_Renderer* renderer, float x, float y, float width, float height, SDL_Color color) {
    SDL_FRect panel{x, y, width, height};
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer, &panel);
}

bool run() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) return reportSdlFailure("SDL initialization failed");
    if (!TTF_Init()) {
        SDL_Quit();
        return reportSdlFailure("SDL_ttf initialization failed");
    }

    SDL_Window* window = SDL_CreateWindow("launchme", window_width, window_height, SDL_WINDOW_RESIZABLE);
    if (!window) {
        TTF_Quit();
        SDL_Quit();
        return reportSdlFailure("window creation failed");
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return reportSdlFailure("renderer creation failed");
    }

    TTF_Font* font = nullptr;
    for (const char* path : {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                             "/usr/share/fonts/TTF/DejaVuSans.ttf"}) {
        font = TTF_OpenFont(path, 22.0f);
        if (font) break;
    }
    if (!font) {
        const char* error = SDL_GetError();
        std::fprintf(stderr, "launchme: no usable font found (tried DejaVuSans.ttf): %s\n",
                     error && *error ? error : "unknown error");
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return false;
    }

    std::vector<Game> games;
    const std::filesystem::path steam_root = findSteamRoot();
    if (!steam_root.empty()) games = scanInstalledGames(steam_root);
    std::sort(games.begin(), games.end(), [](const Game& left, const Game& right) {
        return steamLibraryOrder(left, right);
    });

    SDL_Gamepad* gamepad = nullptr;
    int gamepad_count = 0;
    SDL_JoystickID* gamepad_ids = SDL_GetGamepads(&gamepad_count);
    if (gamepad_ids && gamepad_count > 0) gamepad = SDL_OpenGamepad(gamepad_ids[0]);
    SDL_free(gamepad_ids);

    size_t selected = 0;
    bool running = true;
    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) {
                running = false;
            }
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && !games.empty()) {
                if (event.key.key == SDLK_DOWN || event.key.key == SDLK_RIGHT)
                    selected = (selected + 1) % games.size();
                if (event.key.key == SDLK_UP || event.key.key == SDLK_LEFT)
                    selected = (selected + games.size() - 1) % games.size();
            }
            if (event.type == SDL_EVENT_GAMEPAD_ADDED && !gamepad)
                gamepad = SDL_OpenGamepad(event.gdevice.which);
            if (event.type == SDL_EVENT_GAMEPAD_REMOVED && gamepad &&
                SDL_GetGamepadID(gamepad) == event.gdevice.which) {
                SDL_CloseGamepad(gamepad);
                gamepad = nullptr;
            }
            if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && !games.empty()) {
                if (event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_DOWN ||
                    event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT)
                    selected = (selected + 1) % games.size();
                if (event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_UP ||
                    event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_LEFT)
                    selected = (selected + games.size() - 1) % games.size();
                if (event.gbutton.button == SDL_GAMEPAD_BUTTON_BACK) running = false;
            }
        }

        SDL_SetRenderDrawColor(renderer, 14, 17, 24, SDL_ALPHA_OPAQUE);
        SDL_RenderClear(renderer);
        drawText(renderer, font, "launchme - Linux", 40, 30, SDL_Color{238, 238, 238, SDL_ALPHA_OPAQUE});
        drawText(renderer, font, games.empty() ? "No Steam games found" : "Steam library", 40, 75,
                 SDL_Color{150, 160, 180, SDL_ALPHA_OPAQUE});
        const size_t first = games.empty() ? 0 : (selected > 4 ? selected - 4 : 0);
        const size_t last = std::min(games.size(), first + 8);
        for (size_t index = first; index < last; ++index) {
            const float y = 130.0f + static_cast<float>(index - first) * 55.0f;
            const bool focused = index == selected;
            drawPanel(renderer, 28, y - 6, 720, 45,
                      focused ? SDL_Color{45, 65, 95, SDL_ALPHA_OPAQUE}
                              : SDL_Color{24, 28, 36, SDL_ALPHA_OPAQUE});
            drawText(renderer, font, text_encoding::utf8Lenient(games[index].name), 45, y,
                     focused ? SDL_Color{255, 255, 255, SDL_ALPHA_OPAQUE}
                             : SDL_Color{185, 190, 200, SDL_ALPHA_OPAQUE});
        }
        drawPanel(renderer, 790, 110, 450, 490, SDL_Color{24, 29, 39, SDL_ALPHA_OPAQUE});
        if (!games.empty()) {
            const Game& game = games[selected];
            drawText(renderer, font, text_encoding::utf8Lenient(game.name), 820, 140,
                     SDL_Color{255, 255, 255, SDL_ALPHA_OPAQUE});
            drawText(renderer, font, "Steam", 820, 195, SDL_Color{150, 160, 180, SDL_ALPHA_OPAQUE});
            drawText(renderer, font, game.installed ? "Installed" : "Not installed", 820, 235,
                     game.installed ? SDL_Color{115, 210, 145, SDL_ALPHA_OPAQUE}
                                    : SDL_Color{220, 170, 110, SDL_ALPHA_OPAQUE});
            if (game.app_id != 0)
                drawText(renderer, font, "App ID: " + std::to_string(game.app_id), 820, 275,
                         SDL_Color{185, 190, 200, SDL_ALPHA_OPAQUE});
            if (!game.install_directory.empty())
                drawText(renderer, font, "Location: " + game.install_directory.string(), 820, 315,
                         SDL_Color{185, 190, 200, SDL_ALPHA_OPAQUE});
        } else {
            drawText(renderer, font, "Install Steam games to browse them here.", 820, 140,
                     SDL_Color{185, 190, 200, SDL_ALPHA_OPAQUE});
        }
        drawText(renderer, font, "Arrow keys / D-pad: select   Esc / Back: quit", 40, 660,
                 SDL_Color{125, 135, 150, SDL_ALPHA_OPAQUE});
        SDL_RenderPresent(renderer);
    }

    if (gamepad) SDL_CloseGamepad(gamepad);
    TTF_CloseFont(font);
    TTF_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return true;
}

} // namespace

int main() {
    return run() ? 0 : 1;
}
