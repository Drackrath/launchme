#pragma once
// A Steam friend's conversation inside the drawer (Widżety → Znajomi Steam → a friend), through
// Aurelia's chat commands (src/app/friend_chat.cpp). The drawer keeps its width; Back returns to the
// friends list with its cursor and scroll. Geometry is drawer-relative (left = the drawer's left).
#include "../store/aurelia.hpp"
#include "friends_layout.hpp"

#include <string>
#include <vector>

namespace friend_chat {
struct State {
    bool open = false;
    uint64_t id = 0;
    std::wstring name, presence; // presence: the friend's game or availability, as the list showed it.
    std::vector<aurelia::ChatMessage> messages;
    std::wstring input, status;
    bool busy = false, sending = false, live = false, failed = false;
    bool back_focus = false; // The pad/keys on Back instead of the field.
    float scroll = 0, content = 0; // Measured while drawing.
    bool follow = true;            // Pinned to the newest message.
    int hover = 0;                 // 0 none, 1 Back, 2 the field, 3 Send.
};
constexpr size_t input_limit = 4000;

struct Layout {
    float left = 0, height = 900;
    // hints: the input hints' corner below the field (Enter / Esc) stays clear.
    static constexpr float width = friends_layout::drawer_width, margin = 24, field_height = 52, hints = 64;
    friends_layout::Rect back() const { return friends_layout::listBack(left); }
    friends_layout::Rect field() const { return {left + margin, height - hints - margin - field_height, left + width - margin - field_height - 10, height - hints - margin}; }
    friends_layout::Rect send() const { const auto f = field(); return {f.right + 10, f.top, f.right + 10 + field_height, f.bottom}; }
    friends_layout::Rect log() const { return {left + margin, 112, left + width - margin, field().top - 14}; }
    // 1 Back, 2 the field, 3 Send, 0 nothing.
    int hit(float x, float y) const {
        if (back().contains(x, y)) return 1;
        if (field().contains(x, y)) return 2;
        if (send().contains(x, y)) return 3;
        return 0;
    }
};
}
