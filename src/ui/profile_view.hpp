#pragma once
// Profil (src/app/profile.cpp runs it, src/ui/profile_renderer.cpp draws it): the player's page
// over the launcher, opened from its widget (the drawer's dashboard, the desktop window's widgets).
// One scrolling page: who plays (the Steam name and picture), this week (time, games, launches,
// unlocks, a chart of the seven days and the most played games), the latest achievements, the
// achievements of the last twelve months, the games closest to 100 % and every platform with its
// games and hours. The state one frame shows and the geometry clicks share with drawing.
#include "settings_view.hpp"
#include "../model/game.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace profile_ui {
using settings_ui::Rect;
inline constexpr size_t no_image = static_cast<size_t>(-1);
inline constexpr size_t max_top = 4, max_recent = 12, max_completion = 10;

// Focusable parts, top to bottom (the week's tiles and the history chart are read, not chosen).
enum class Section { close, top, recent, completion, platforms };
inline constexpr int section_count = 5;
struct Focus {
    Section section = Section::close;
    int item = 0;
    bool operator==(const Focus&) const = default;
};

struct Stat { std::wstring label, value, note; bool good = false, bad = false; };
struct TopGame { std::string key; std::wstring name, time; float share = 0; size_t image = no_image; };
struct Unlock {
    std::string key;                 // The game.
    std::wstring name, game, when;   // The achievement, its game, "2 dni temu".
    size_t image = no_image;         // The achievement's icon, else the game's cover.
    bool icon = false;               // `image` is the achievement's own icon.
    bool rare = false;               // Fewer than 10 % of players have it.
    std::wstring rarity;             // "3,2 % graczy".
};
struct Completion { std::string key; std::wstring name, count; float share = 0; size_t image = no_image; };
struct Platform {
    GameSource source = GameSource::steam;
    std::string brand;                // brand_icons key.
    std::wstring name, account, games, hours;
    bool connected = false;           // A signed-in account (a name or "Połączone").
};

struct State {
    bool open = false;
    float alpha = 0;                  // The page fading in and out.
    float time = 0;                   // Seconds since it opened: the chart grows in.
    bool loading = false;             // Achievements are still being read.
    std::wstring name, subtitle, initials;
    bool avatar = false;              // Image 0 is the player's picture.
    std::array<Stat, 4> stats;        // Czas gry, Gry, Uruchomienia, Osiągnięcia (this week).
    std::array<float, 7> week{};      // Each day's share of the week's longest day.
    std::array<std::wstring, 7> week_days, week_times;
    int today = 6;                    // The last bar is today.
    std::wstring week_total;
    std::vector<TopGame> top;
    std::vector<Unlock> recent;
    std::array<float, 12> months{};   // Each month's share of the busiest one.
    std::array<std::wstring, 12> month_labels, month_counts;
    std::array<Stat, 3> history;      // Łącznie, W tym roku, Gry na 100 %.
    std::vector<Completion> completion;
    std::vector<Platform> platforms;
    Focus focus;
    std::optional<Focus> hover;
    int recent_first = 0;             // The first achievement card shown (the row slides).
    float recent_offset = 0;          // Its gliding position.
    float scroll = 0, scroll_target = 0;
    bool pointer = false;             // The pointer moved last: no focus outline until a key.
    size_t count(Section section) const {
        switch (section) {
        case Section::close: return 1;
        case Section::top: return top.size();
        case Section::recent: return recent.size();
        case Section::completion: return completion.size();
        case Section::platforms: return platforms.size();
        }
        return 0;
    }
};

inline float ease(float t) { t = std::clamp(t, 0.0f, 1.0f); return 1 - std::pow(1 - t, 3.0f); }

// The page on the TV canvas (1600 × 900, or the desktop window's compact canvas); y in page space
// (add -scroll to draw), except the close button, which stays put.
struct Layout {
    float width = 1600, height = 900;
    static constexpr float top_pad = 108, header_h = 116, label_h = 34, gap = 16, section_gap = 34;
    static constexpr float stat_h = 112, week_h = 268, unlock_w = 316, unlock_h = 116, history_h = 236;
    static constexpr float completion_h = 76, platform_h = 132, bottom_pad = 72;
    float content() const { return std::min(1240.0f, width - 2 * std::max(40.0f, width * 0.05f)); }
    float left() const { return std::round((width - content()) * 0.5f); }
    float right() const { return left() + content(); }
    Rect close() const { return {left(), 28, left() + 48, 76}; } // Back, top left (the page's title beside it).
    Rect avatar() const { return {left(), top_pad, left() + header_h - 12, top_pad + header_h - 12}; }

    float weekTop() const { return top_pad + header_h + section_gap + label_h; }
    Rect stat(int index) const {
        const float w = (content() - 3 * gap) / 4, x = left() + index * (w + gap);
        return {x, weekTop(), x + w, weekTop() + stat_h};
    }
    float chartSplit() const { return std::round(content() * 0.58f); }
    Rect chart() const { const float y = weekTop() + stat_h + gap; return {left(), y, left() + chartSplit(), y + week_h}; }
    Rect topCard() const { const float y = weekTop() + stat_h + gap; return {left() + chartSplit() + gap, y, right(), y + week_h}; }
    Rect topRow(int index) const {
        const auto card = topCard();
        const float row = (week_h - 64) / static_cast<float>(max_top), y = card.top + 52 + index * row;
        return {card.left + 12, y, card.right - 12, y + row - 4};
    }

    float recentTop() const { return chart().bottom + section_gap + label_h; }
    int unlockColumns() const { return std::max(1, static_cast<int>((content() + gap) / (unlock_w + gap))); }
    float unlockWidth() const { const int n = unlockColumns(); return (content() - (n - 1) * gap) / n; }
    // `offset`: the row's gliding first card (fractional while it slides).
    Rect unlock(int index, float offset = 0) const {
        const float w = unlockWidth(), x = left() + (index - offset) * (w + gap);
        return {x, recentTop(), x + w, recentTop() + unlock_h};
    }
    float recentHeight(size_t count) const { return count ? unlock_h : 64; }

    float historyTop(size_t recent) const { return recentTop() + recentHeight(recent) + section_gap + label_h; }
    Rect historyCard(size_t recent) const { return {left(), historyTop(recent), right(), historyTop(recent) + history_h}; }

    float completionTop(size_t recent) const { return historyCard(recent).bottom + section_gap + label_h; }
    int completionColumns() const { return content() >= 900 ? 2 : 1; }
    Rect completion(size_t recent, int index) const {
        const int columns = completionColumns();
        const float w = (content() - (columns - 1) * gap) / columns;
        const float x = left() + (index % columns) * (w + gap), y = completionTop(recent) + (index / columns) * (completion_h + 10);
        return {x, y, x + w, y + completion_h};
    }
    float completionHeight(size_t count) const {
        if (!count) return 64;
        const int columns = completionColumns();
        const int rows = static_cast<int>((count + columns - 1) / columns);
        return rows * (completion_h + 10) - 10;
    }

    float platformsTop(size_t recent, size_t completion) const { return completionTop(recent) + completionHeight(completion) + section_gap + label_h; }
    int platformColumns() const { return std::max(1, static_cast<int>((content() + gap) / (280 + gap))); }
    Rect platform(size_t recent, size_t completion, int index) const {
        const int columns = platformColumns();
        const float w = (content() - (columns - 1) * gap) / columns;
        const float x = left() + (index % columns) * (w + gap), y = platformsTop(recent, completion) + (index / columns) * (platform_h + gap);
        return {x, y, x + w, y + platform_h};
    }
    float bottom(const State& state) const {
        const auto count = state.platforms.size();
        const int columns = platformColumns();
        const int rows = static_cast<int>((count + columns - 1) / columns);
        return platformsTop(state.recent.size(), state.completion.size()) + std::max(1, rows) * (platform_h + gap) - gap + bottom_pad;
    }
    float maxScroll(const State& state) const { return std::max(0.0f, bottom(state) - height); }

    // An item's rectangle in page space (the close button: in screen space).
    Rect item(const State& state, Focus focus) const {
        switch (focus.section) {
        case Section::close: return close();
        case Section::top: return topRow(focus.item);
        case Section::recent: return unlock(focus.item, state.recent_offset);
        case Section::completion: return completion(state.recent.size(), focus.item);
        case Section::platforms: return platform(state.recent.size(), state.completion.size(), focus.item);
        }
        return {};
    }
    // What lies under a point of the screen.
    std::optional<Focus> hit(const State& state, float x, float y) const {
        if (close().contains(x, y)) return Focus{Section::close, 0};
        if (y < 82) return std::nullopt; // The bar: the page scrolls under it.
        const float py = y + state.scroll;
        for (const auto section : {Section::top, Section::recent, Section::completion, Section::platforms})
            for (int i = 0; i < static_cast<int>(state.count(section)); ++i) {
                const auto r = item(state, {section, i});
                if (section == Section::recent && (r.left < left() - 1 || r.right > right() + 1)) continue; // Slid out of the row.
                if (r.contains(x, py)) return Focus{section, i};
            }
        return std::nullopt;
    }
    // The scroll that shows `focus` (with its section's heading above it).
    float reveal(const State& state, Focus focus, float current) const {
        if (focus.section == Section::close) return 0;
        const auto r = item(state, focus);
        constexpr float bar = 92; // Under the bar (Back and the title) nothing is seen.
        const float top = r.top - label_h - 10 - bar, bottom = r.bottom + 28;
        float next = current;
        if (top < next) next = top;
        if (bottom > next + height) next = bottom - height;
        if (focus.section == Section::top) next = std::min(next, weekTop() - label_h - 10 - bar); // The week stays whole.
        return std::clamp(next, 0.0f, maxScroll(state));
    }
};
} // namespace profile_ui
