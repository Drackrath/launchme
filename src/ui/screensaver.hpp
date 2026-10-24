#pragma once
// Screensaver logic without Win32 or Direct2D: when it may start, which games it shows,
// the shuffled order, what the renderer draws, and what happens to input while it runs.
// The launcher side is src/app/screensaver.cpp, drawing src/ui/screensaver_renderer.cpp.
#include "../platform/user_config.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace screensaver {
using Style = AppSettings::ScreensaverStyle;

inline constexpr std::array<int, 4> minute_choices{1, 3, 5, 10};
inline constexpr float item_seconds = 9.0f;   // Each game stays this long, its fade-in included.
inline constexpr float fade_seconds = 1.2f;   // Crossfade between two games.
inline constexpr float intro_seconds = 0.8f;  // The launcher screen dims into the screensaver.
inline constexpr float spin_seconds = 6.0f;   // One turn of a disc.
inline constexpr float wake_distance = 8.0f;  // Mouse travel (pixels) that ends it.
inline constexpr int stick_threshold = 12000; // Stick deflection that ends it.

inline int normalizedMinutes(int minutes) {
    return std::find(minute_choices.begin(), minute_choices.end(), minutes) != minute_choices.end() ? minutes : 1;
}

// --- Start condition ----------------------------------------------------------

struct Conditions {
    bool enabled = true;
    bool watched = false;    // Launcher window in the foreground and not minimized.
    bool session = false;    // A game is running or being launched.
    bool text_entry = false; // A text field or editor owns the keyboard.
    std::chrono::steady_clock::duration idle{};
    int minutes = 1;
};

inline bool shouldStart(const Conditions& conditions) {
    return conditions.enabled && conditions.watched && !conditions.session && !conditions.text_entry &&
           conditions.idle >= std::chrono::minutes(normalizedMinutes(conditions.minutes));
}

// --- Artwork ------------------------------------------------------------------

// Which images of a game are cached on disk.
struct Artwork { bool hero = false, poster = false, cover = false; };
enum class Image { none, hero, poster, cover };

// The image a style shows for a game: backgrounds need a hero, boxes a vertical poster
// (a square cover as fallback), discs a cover.
inline Image imageFor(Style style, Artwork art) {
    switch (style) {
    case Style::backgrounds: return art.hero ? Image::hero : Image::none;
    case Style::boxes: return art.poster ? Image::poster : art.cover ? Image::cover : Image::none;
    case Style::discs: return art.cover ? Image::cover : Image::none;
    }
    return Image::none;
}

// The files a style may read, so the disk scan probes nothing else.
inline Artwork needed(Style style) {
    return {style == Style::backgrounds, style == Style::boxes, style != Style::backgrounds};
}

struct Candidate { std::string key; Artwork art; };

// Keys of the games a style can show, in library order, each once.
inline std::vector<std::string> selection(const std::vector<Candidate>& candidates, Style style) {
    std::vector<std::string> keys;
    std::unordered_set<std::string> seen;
    for (const auto& candidate : candidates)
        if (!candidate.key.empty() && imageFor(style, candidate.art) != Image::none && seen.insert(candidate.key).second)
            keys.push_back(candidate.key);
    return keys;
}

// --- Order --------------------------------------------------------------------

// Shuffled passes over every game. A new pass never starts with the game just shown,
// so no game follows itself unless it is the only one.
class Playlist {
public:
    void reset(std::vector<std::string> keys, uint32_t seed) {
        keys_ = std::move(keys);
        deck_.clear();
        position_ = 0;
        last_.clear();
        state_ = seed ? seed : 0x9E3779B9u;
    }
    size_t size() const { return keys_.size(); }
    bool empty() const { return keys_.empty(); }
    // Empty when there is nothing to show.
    std::string next() {
        if (keys_.empty()) return {};
        if (position_ >= deck_.size()) shuffle();
        last_ = deck_[position_++];
        return last_;
    }
    // A game whose image could not be read or decoded leaves the rotation.
    void remove(const std::string& key) {
        std::erase(keys_, key);
        for (size_t i = deck_.size(); i-- > 0;) {
            if (deck_[i] != key) continue;
            deck_.erase(deck_.begin() + static_cast<std::ptrdiff_t>(i));
            if (i < position_) --position_;
        }
    }

private:
    uint32_t random() { // xorshift32: deterministic for a seed, plenty for a shuffle.
        state_ ^= state_ << 13; state_ ^= state_ >> 17; state_ ^= state_ << 5;
        return state_;
    }
    void shuffle() {
        deck_ = keys_;
        for (size_t i = deck_.size(); i > 1; --i) std::swap(deck_[i - 1], deck_[random() % i]);
        if (deck_.size() > 1 && deck_.front() == last_)
            std::swap(deck_.front(), deck_[1 + random() % (deck_.size() - 1)]);
        position_ = 0;
    }
    std::vector<std::string> keys_, deck_;
    size_t position_ = 0;
    std::string last_;
    uint32_t state_ = 0x9E3779B9u;
};

// Crossfade progress of the game that became current `age` seconds ago.
inline float fade(float age) { return std::clamp(age / fade_seconds, 0.0f, 1.0f); }

// --- Drawing ------------------------------------------------------------------

struct View {
    Style style = Style::backgrounds;
    float opacity = 1;  // < 1 while the launcher screen dims into the screensaver.
    float time = 0;     // Seconds since it started: glow drift.
    bool empty = false; // No game qualifies: a dim screen with a slow accent glow.
    struct Item {
        int slot = -1;      // Renderer image slot; -1 = nothing.
        float age = 0;      // Seconds since this game became current.
        float alpha = 0;
        uint32_t seed = 0;  // Motion variety (pan direction, rotation phase).
        bool poster = false; // Boxes: a 2:3 poster face; otherwise a square cover face.
        std::wstring title;
    };
    Item previous, current;
};

// --- Input --------------------------------------------------------------------

// Every input event while the screensaver runs, and the tail of the one that ended it,
// is either passed on, swallowed, or swallowed while ending the screensaver. The event
// that wakes it never acts on the screen beneath: neither its character, key release,
// auto-repeat, the right-button release, a double-click's second press, nor a pad
// button or stick that is still held.
enum class Verdict { pass, swallow, wake };
enum class Button { left, left_double, right, middle, extra };

class InputGate {
public:
    // `cursor`: client position when it started; without one the first move sets it.
    void activate(std::optional<std::pair<float, float>> cursor) {
        active_ = true;
        anchor_ = cursor;
        pad_baseline_ = true;
        key_ = 0; character_ = false; right_up_ = false; pad_ = false;
        double_click_until_ = 0;
    }
    // Ended without input (focus lost): nothing left to swallow.
    void deactivate() { active_ = false; key_ = 0; character_ = false; right_up_ = false; pad_ = false; double_click_until_ = 0; }
    bool active() const { return active_; }

    Verdict keyDown(unsigned key, bool repeated) {
        if (active_) {
            if (repeated) return Verdict::swallow; // Held since before it started.
            key_ = key; character_ = true;
            return wake();
        }
        if (key_ && key == key_ && repeated) return Verdict::swallow;
        if (key_ && key != key_) { key_ = 0; character_ = false; }
        return Verdict::pass;
    }
    Verdict character() const {
        return active_ || character_ ? Verdict::swallow : Verdict::pass;
    }
    Verdict keyUp(unsigned key) {
        if (active_) return Verdict::swallow; // A key pressed before it started (e.g. "Start now").
        if (key_ && key == key_) { key_ = 0; character_ = false; return Verdict::swallow; }
        return Verdict::pass;
    }
    Verdict button(Button button, uint64_t now_ms, uint32_t double_click_ms) {
        if (active_) {
            if (button == Button::right) right_up_ = true;
            if (button == Button::left) double_click_until_ = now_ms + double_click_ms;
            return wake();
        }
        if (button == Button::left_double && now_ms < double_click_until_) { double_click_until_ = 0; return Verdict::swallow; }
        return Verdict::pass;
    }
    Verdict buttonUp(Button button) {
        if (button == Button::right && (active_ || right_up_)) { right_up_ = false; return Verdict::swallow; }
        return Verdict::pass;
    }
    Verdict wheel() { return active_ ? wake() : Verdict::pass; }
    Verdict mouseMove(float x, float y) {
        if (!active_) return Verdict::pass;
        if (!anchor_) { anchor_ = std::pair{x, y}; return Verdict::swallow; }
        const float dx = x - anchor_->first, dy = y - anchor_->second;
        return dx * dx + dy * dy > wake_distance * wake_distance ? wake() : Verdict::swallow;
    }
    // One gamepad frame. Buttons and a stick held when it started are ignored until released.
    Verdict pad(uint16_t buttons, bool stick) {
        if (active_) {
            if (pad_baseline_) { held_ = buttons; stick_held_ = stick; pad_baseline_ = false; return Verdict::swallow; }
            held_ &= buttons;
            if (!stick) stick_held_ = false;
            if ((buttons & ~held_) || (stick && !stick_held_)) { pad_ = true; return wake(); }
            return Verdict::swallow;
        }
        if (pad_) {
            if (!buttons && !stick) pad_ = false; // Released: the next frame counts again.
            return Verdict::swallow;
        }
        return Verdict::pass;
    }
    std::optional<std::pair<float, float>> anchor() const { return anchor_; }

private:
    Verdict wake() { active_ = false; return Verdict::wake; }
    bool active_ = false;
    std::optional<std::pair<float, float>> anchor_;
    unsigned key_ = 0;          // The waking key: its repeats and release are swallowed.
    bool character_ = false;    // ...and the characters it types.
    bool right_up_ = false;     // A waking right-click: its release opens no menu.
    uint64_t double_click_until_ = 0;
    bool pad_ = false;          // A waking pad input: swallowed until everything is released.
    bool pad_baseline_ = false;
    uint16_t held_ = 0;
    bool stick_held_ = false;
};
} // namespace screensaver
