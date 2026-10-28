#pragma once
#include "native_window.hpp"
#include <algorithm>

// Absolute deadlines avoid accumulating timer/paint rounding at 60/120/144 Hz.
class FrameSchedule {
public:
    void rate(int hz, double now) {
        hz = std::clamp(hz, 5, 240);
        if (!initialized_ || hz != hz_) { hz_ = hz; next_ = now; initialized_ = true; }
    }
    bool ready(double now) const { return now >= next_; }
    void consume(double now) {
        const double period = 1.0 / hz_;
        if (next_ < now - period) next_ = now; // Do not replay missed frames after a stall.
        next_ += period;
    }
    double delay(double now) const { return std::max(0.0, next_ - now); }
    int hz() const { return hz_; }
private:
    int hz_ = 60;
    double next_ = 0;
    bool initialized_ = false;
};
// The frame loop's pace: the window's monitor refresh rate while it is in front, 30 Hz behind
// another window, 5 Hz minimized or hidden. wait() sleeps until the next frame or until input
// arrives (Windows: a waitable timer and MsgWaitForMultipleObjectsEx; Linux: SDL_WaitEventTimeout,
// then SDL_DelayPrecise for the last part of a millisecond). On Linux, work that must wake the
// loop early posts an SDL event.
class FrameClock {
public:
    FrameClock();
    ~FrameClock();
    void update(platform::NativeWindow window);
    void update(platform::NativeWindow window, bool foreground);
    // An upper bound for the next updates: the screensaver's slow motion (30 Hz), a display
    // the system switched off (5 Hz). 240 = no bound.
    void limit(int hz) { limit_ = std::clamp(hz, 5, 240); }
    bool ready() const;
    void consume();
    void wait();
    int targetHz() const { return schedule_.hz(); }
    int refreshHz() const { return refresh_hz_; }
private:
#ifdef _WIN32
    HANDLE timer_ = nullptr;
#endif
    FrameSchedule schedule_;
    double next_probe_ = 0;
    int refresh_hz_ = 0;
    int limit_ = 240;
};
