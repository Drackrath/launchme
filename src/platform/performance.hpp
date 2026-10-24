#pragma once
#include <array>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

struct RenderTiming {
    double draw_ms = 0, present_ms = 0, upload_ms = 0;
    unsigned uploads = 0;
};
struct FrameSample {
    double time_ms = 0, interval_ms = 0, tick_ms = 0, input_ms = 0, system_ms = 0;
    RenderTiming render;
};
struct PerformanceSummary {
    double fps = 0, frame_p95 = 0, frame_p99 = 0;
    double tick_ms = 0, tick_max = 0, input_ms = 0, system_ms = 0, draw_ms = 0, present_ms = 0, upload_ms = 0;
    unsigned uploads = 0;
};
class FrameProfiler {
public:
    ~FrameProfiler();
    void configure();
    void toggle() { overlay_ = !overlay_; }
    bool overlay() const { return overlay_; }
    bool enabled() const { return overlay_ || !trace_file_.empty(); }
    void tick(double total_ms, double input_ms, double system_ms);
    void frame(RenderTiming timing);
    PerformanceSummary summary() const;
    std::wstring label(int refresh_hz, int target_hz);
    // Startup milestones, ms since the process was created (first call per mark wins; -1 = not reached).
    // Written to the trace summary only; `first_frame` is recorded by frame() itself.
    enum class Startup { initialized, first_frame, first_shelf, count };
    void startup(Startup mark);
    double startupMs(Startup mark) const { return startup_[static_cast<size_t>(mark)]; }
private:
    using Clock = std::chrono::steady_clock;
    Clock::time_point start_ = Clock::now(), last_frame_{}, label_time_{};
    std::array<double, static_cast<size_t>(Startup::count)> startup_{-1, -1, -1};
    std::array<FrameSample, 240> frames_{};
    size_t count_ = 0, next_ = 0;
    FrameSample pending_;
    bool overlay_ = false;
    std::filesystem::path trace_file_;
    std::vector<FrameSample> trace_;
    std::wstring label_;
};
