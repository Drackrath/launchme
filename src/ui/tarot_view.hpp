#pragma once
// Tarot's overlay (src/app/tarot.cpp runs it, src/ui/tarot_renderer.cpp draws it): the state one
// frame shows and the geometry clicks share with drawing. Phases: the cards of a question fly in
// from the deck and turn face up; a chosen card rises and the others fall away; the deck shuffles
// while the cards are read (and Steam is asked); then up to three cards turn over to show games.
#include "settings_view.hpp"
#include "../library/tarot.hpp"
#include "../model/game.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace tarot_ui {
using settings_ui::Rect;
enum class Phase { question, leaving, divining, reading };
// Seconds: a card's flight from the deck, the next one's delay, the chosen card's exit, the
// shortest shuffle, a result card's turn, the delay before the first turns and between them.
inline constexpr float deal_time = 0.62f, deal_stagger = 0.11f, leave_time = 0.72f, divining_min = 1.7f;
inline constexpr float flip_time = 0.7f, reveal_delay = 0.25f, reveal_stagger = 0.42f;
inline constexpr size_t max_cards = 4, max_results = 3;
// Keyboard/pad focus besides the cards: the top-left back button and the reading's two buttons.
inline constexpr int focus_back = -4, focus_again = -2, focus_close = -3, no_hit = -100;

struct Result {
    std::string key;
    std::wstring title, detail, url; // url: a store pick's page; empty for a library game.
    Game game;
};
struct State {
    bool open = false;
    float alpha = 0;     // The overlay fading in and out.
    float time = 0;      // Seconds since it opened: the stars and the sigil turn with it.
    tarot::Reading reading;
    Phase phase = Phase::question;
    float phase_age = 0; // Seconds in the phase: the deal, the exit, the shuffle, the reveal.
    int chosen = -1;     // Phase::leaving: the card taken.
    int focus = 0, hover = no_hit;
    std::array<float, max_cards> lift{}; // 0..1 per card of the spread: focused cards rise.
    bool waiting = false; // The Steam search is still running.
    bool failed = false;  // Steam could not be asked.
    tarot::Pool pool = tarot::Pool::library;
    std::vector<Result> results;
    const tarot::Spread* spread() const {
        if (phase == Phase::leaving && reading.answers.size() < reading.spreads.size()) return &reading.spreads[reading.answers.size()];
        return phase == Phase::question ? reading.current() : nullptr;
    }
    size_t step() const { return reading.answers.size(); }
    size_t steps() const { return std::max<size_t>(reading.total, reading.spreads.size()); }
};

inline float ease(float t) { t = std::clamp(t, 0.0f, 1.0f); return 1 - std::pow(1 - t, 3.0f); }
inline float smooth(float t) { t = std::clamp(t, 0.0f, 1.0f); return t * t * (3 - 2 * t); }

struct Layout {
    float width, height;
    // Four cards and their gaps (4.6 card widths) fit the width with a margin.
    float cardHeight() const { return std::max(160.0f, std::min(std::clamp(height * 0.45f, 220.0f, 470.0f), (width - 160) / 4.6f / 0.6f)); }
    float cardWidth() const { return cardHeight() * 0.6f; }
    float gap() const { return cardWidth() * 0.2f; }
    float cardsCenter() const { return height * 0.59f; }
    Rect card(int index, int count) const {
        const float w = cardWidth(), h = cardHeight(), g = gap();
        const float row = count * w + std::max(0, count - 1) * g;
        const float left = (width - row) * 0.5f + index * (w + g), top = cardsCenter() - h * 0.5f;
        return {left, top, left + w, top + h};
    }
    // The reading: up to three posters (2:3), a little taller than the question cards.
    float posterHeight() const { return std::max(160.0f, std::min(std::clamp(height * 0.47f, 220.0f, 500.0f), (width - 200) / 3.7f * 1.5f)); }
    float posterWidth() const { return posterHeight() * 2.0f / 3.0f; }
    float postersTop() const { return height * 0.24f; }
    Rect poster(int index, int count) const {
        const float w = posterWidth(), h = posterHeight(), g = std::max(48.0f, w * 0.32f);
        const float row = count * w + std::max(0, count - 1) * g;
        const float left = (width - row) * 0.5f + index * (w + g), top = postersTop();
        return {left, top, left + w, top + h};
    }
    Rect button(int focus) const { // focus_again or focus_close.
        const float w = 250, h = 52, y = std::min(height - 92, postersTop() + posterHeight() + 128);
        const float left = width * 0.5f + (focus == focus_again ? -w - 12 : 12);
        return {left, y, left + w, y + h};
    }
    Rect back() const { return {56, 38, 110, 92}; }
    int hit(const State& state, float x, float y) const {
        if (back().contains(x, y)) return focus_back;
        if (state.phase == Phase::question) {
            if (const auto* spread = state.spread()) {
                const int count = static_cast<int>(spread->cards.size());
                for (int i = 0; i < count; ++i) {
                    auto r = card(i, count);
                    r.top -= 26; // The lifted card.
                    if (r.contains(x, y)) return i;
                }
            }
        } else if (state.phase == Phase::reading) {
            const int count = static_cast<int>(state.results.size());
            for (int i = 0; i < count; ++i) if (poster(i, count).contains(x, y)) return i;
            if (button(focus_again).contains(x, y)) return focus_again;
            if (button(focus_close).contains(x, y)) return focus_close;
        }
        return no_hit;
    }
};
}
