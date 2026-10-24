#pragma once
#include <Windows.h>
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
class FrameClock {
public:
    FrameClock();
    ~FrameClock();
    void update(HWND window);
    void update(HWND window, bool foreground);
    // An upper bound for the next updates: the screensaver's slow motion (30 Hz), a display
    // Windows switched off (5 Hz). 240 = no bound.
    void limit(int hz) { limit_ = std::clamp(hz, 5, 240); }
    bool ready() const;
    void consume();
    void wait();
    int targetHz() const { return schedule_.hz(); }
    int refreshHz() const { return refresh_hz_; }
private:
    HANDLE timer_ = nullptr;
    FrameSchedule schedule_;
    double next_probe_ = 0;
    int refresh_hz_ = 0;
    int limit_ = 240;
};
