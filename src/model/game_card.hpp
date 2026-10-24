#pragma once

#include "game.hpp"

// UI-independent state of a game in the carousel. Rendering resources live in Renderer.
struct GameCard {
    Game game;
    float x = 0;
    float y = 0;
    float size = 0;   // Height; also the width of a square card.
    float aspect = 1; // Width / height: XMB/Flix posters and landscape capsules are not square.
    float opacity = 0;
    float cover_opacity = 0;
    float emphasis = 0; // Eased focus weight; selection changes never snap the cover's zoom.
    float disc = 0; // Coverflow: delayed ejection, smoothly retracts when selection changes.
    float steelbook = 0; // Coverflow: 0 = plain print, 1 = full steelbook; replayed on every selection.
    float width() const { return size * aspect; }
    bool contains(float px, float py) const { return px >= x && px <= x + width() && py >= y && py <= y + size; }
};
