#pragma once
// Built-in on-screen keyboard for the pad: a small panel at the bottom of the launcher. The
// launcher routes its keys into whichever text field is active (src/app/keyboard.cpp); this
// file holds the layout, the focus movement and the geometry shared by drawing and clicks.
#include "settings_view.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace osk {
enum class Kind { character, shift, layer, space, backspace, done };
struct Key {
    Kind kind = Kind::character;
    wchar_t character = 0;
    int units = 1; // Width in key units; every row is `columns` units wide.
};
inline constexpr int columns = 11, rows = 4;

struct State {
    bool open = false;
    bool shift = false;   // One letter in upper case (one-shot), as on console keyboards.
    bool symbols = false; // Digits, Polish letters and punctuation.
    int row = 1, key = 0;
    float column = 0;     // Unit column the focus aims at when moving between rows of other widths.
};

inline std::vector<Key> row(const State& state, int index) {
    const auto characters = [](std::wstring_view text) {
        std::vector<Key> keys;
        for (const auto ch : text) keys.push_back({Kind::character, ch, 1});
        return keys;
    };
    std::vector<Key> keys;
    switch (index) {
    case 0:
        keys = characters(state.symbols ? L"1234567890" : L"qwertyuiop");
        keys.push_back({Kind::backspace, 0, 1});
        break;
    case 1: keys = characters(state.symbols ? L"ąćęłńóśźż:/" : L"asdfghjkl'-"); break;
    case 2:
        keys.push_back({Kind::shift, 0, 1});
        for (const auto& key : characters(state.symbols ? L"@#&_+=()!\"" : L"zxcvbnm,.?")) keys.push_back(key);
        break;
    default: keys = {{Kind::layer, 0, 2}, {Kind::space, L' ', 6}, {Kind::done, 0, 3}}; break;
    }
    return keys;
}

// Left edge of a key and its centre, in units.
inline int keyStart(const std::vector<Key>& keys, int index) {
    int units = 0;
    for (int i = 0; i < index && i < static_cast<int>(keys.size()); ++i) units += keys[static_cast<size_t>(i)].units;
    return units;
}
inline float keyCenter(const std::vector<Key>& keys, int index) {
    return keyStart(keys, index) + keys[static_cast<size_t>(index)].units * 0.5f;
}
inline Key focused(const State& state) {
    const auto keys = row(state, state.row);
    return keys[static_cast<size_t>(std::clamp(state.key, 0, static_cast<int>(keys.size()) - 1))];
}
// The key a character would type: shift applies to letters (Polish ones too).
inline wchar_t typed(const State& state, wchar_t character) {
    if (!state.shift) return character;
    if (character >= L'a' && character <= L'z') return static_cast<wchar_t>(character - L'a' + L'A');
    // towupper in the "C" locale leaves letters outside ASCII as they are.
    constexpr std::wstring_view lower = L"ąćęłńóśźż", upper = L"ĄĆĘŁŃÓŚŹŻ";
    const auto found = lower.find(character);
    return found == std::wstring_view::npos ? character : upper[found];
}

inline void focus(State& state, int row_index, int key) {
    state.row = std::clamp(row_index, 0, rows - 1);
    const auto keys = row(state, state.row);
    state.key = std::clamp(key, 0, static_cast<int>(keys.size()) - 1);
    state.column = keyCenter(keys, state.key);
}
// One step; rows wrap around horizontally (as on consoles), not vertically. False at an edge.
inline bool move(State& state, int dx, int dy) {
    if (dx) {
        const auto keys = row(state, state.row);
        const int count = static_cast<int>(keys.size());
        state.key = (state.key + dx % count + count) % count;
        state.column = keyCenter(keys, state.key);
        return true;
    }
    if (!dy) return false;
    const int next = state.row + dy;
    if (next < 0 || next >= rows) return false;
    // The key under the remembered column, so Up/Down through the wide bottom row keep their line.
    const auto keys = row(state, next);
    int best = 0;
    for (int i = 0; i < static_cast<int>(keys.size()); ++i) {
        const float start = static_cast<float>(keyStart(keys, i));
        if (state.column >= start && state.column < start + keys[static_cast<size_t>(i)].units) { best = i; break; }
        if (state.column >= start) best = i;
    }
    const float column = state.column;
    state.row = next; state.key = best; state.column = column;
    return true;
}
inline void open(State& state) {
    state = {};
    state.open = true;
    focus(state, 1, 0); // On "a": the middle of the letters.
}

// Geometry in logical pixels: a small panel centred at the bottom, a preview line on top.
struct Layout {
    float width, height;
    static constexpr float key_width = 58, key_height = 46, gap = 6, padding = 16, preview = 42;
    float panelWidth() const { return columns * key_width + (columns - 1) * gap + padding * 2; }
    float panelHeight() const { return preview + rows * key_height + (rows - 1) * gap + padding * 2; }
    settings_ui::Rect panel(float progress = 1) const {
        const float left = (width - panelWidth()) * 0.5f;
        const float bottom = height - 28 + (1 - progress) * (panelHeight() + 40);
        return {left, bottom - panelHeight(), left + panelWidth(), bottom};
    }
    settings_ui::Rect previewRect(float progress = 1) const {
        const auto p = panel(progress);
        return {p.left + padding, p.top + padding * 0.5f, p.right - padding, p.top + padding * 0.5f + preview - 6};
    }
    settings_ui::Rect key(const State& state, int row_index, int index, float progress = 1) const {
        const auto keys = row(state, row_index);
        const auto p = panel(progress);
        const float unit = key_width + gap;
        const float left = p.left + padding + keyStart(keys, index) * unit;
        const float top = p.top + padding + preview + row_index * (key_height + gap);
        return {left, top, left + keys[static_cast<size_t>(index)].units * unit - gap, top + key_height};
    }
    // Row and key under a point, or {-1, -1}.
    std::array<int, 2> hit(const State& state, float x, float y) const {
        for (int r = 0; r < rows; ++r) {
            const auto keys = row(state, r);
            for (int i = 0; i < static_cast<int>(keys.size()); ++i)
                if (key(state, r, i).contains(x, y)) return {r, i};
        }
        return {-1, -1};
    }
};
} // namespace osk
