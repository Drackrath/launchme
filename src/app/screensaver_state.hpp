#pragma once
// The screensaver's state in Launcher (src/app/screensaver.cpp): the playlist and the three games
// on screen, each bound to a renderer image slot.
#include "../model/game.hpp"
#include "../ui/screensaver.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>

namespace launcher_state {
// One game of the slideshow bound to a renderer image slot (0-2; the three items
// keep distinct slots and swap them as the show advances).
struct ScreensaverItem {
    std::string key;
    std::wstring title;
    int slot = -1;
    uint32_t sequence = 0; // Image request; answers for another sequence are stale.
    uint32_t seed = 0;
    bool poster = false, hero = false;
    bool loaded = false;   // Bytes handed to the renderer.
    float shown_at = -1;   // Screensaver time it became current; -1 = waiting for its image.
    void clear() { const int keep = slot; *this = {}; slot = keep; }
};
struct ScreensaverState {
    bool active = false;
    screensaver::Style style = screensaver::Style::backgrounds;
    screensaver::Playlist playlist;
    std::unordered_map<std::string, std::pair<Game, screensaver::Artwork>> games;
    bool scanned = false;
    float time = 0;
    uint32_t next_sequence = 0;
    ScreensaverItem previous{.slot = 0}, current{.slot = 1}, upcoming{.slot = 2};
};
} // namespace launcher_state
