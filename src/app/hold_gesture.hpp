#pragma once
#include <cstdint>

// Short press vs hold for one button, shared by gamepad and keyboard.
// A press fires on release before the threshold. A hold fires once, while the
// button is still down, as soon as the threshold passes; its release is silent.
class HoldGesture {
public:
    enum class Event { none, press, hold };
    static constexpr uint64_t threshold_ms = 480; // Same feel as the Enter/Tab holds.

    Event update(bool down, uint64_t now_ms) {
        if (down && !down_) { down_ = true; held_ = false; since_ = now_ms; return Event::none; }
        if (down && !held_ && now_ms - since_ >= threshold_ms) { held_ = true; return Event::hold; }
        if (!down && down_) { down_ = false; return held_ ? Event::none : Event::press; }
        return Event::none;
    }
    // A button already down when input starts counting (reconnect, focus
    // regained, a modal just closed) must not act on its release.
    void swallow(bool down) { down_ = down; held_ = down; }
    bool down() const { return down_; }

private:
    bool down_ = false, held_ = false;
    uint64_t since_ = 0;
};
