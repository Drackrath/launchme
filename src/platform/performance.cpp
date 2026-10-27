#include "performance.hpp"
#include "../i18n/i18n.hpp"
#ifdef _WIN32
#include <Windows.h>
#endif
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>

void FrameProfiler::configure() {
    wchar_t value[4096]{};
    overlay_ = GetEnvironmentVariableW(L"LAUNCHME_PERF", value, 4096) == 1 && value[0] == L'1';
    const auto size = GetEnvironmentVariableW(L"LAUNCHME_PERF_TRACE", value, 4096);
    if (size && size < 4096) {
        const std::filesystem::path file(value);
        std::error_code error;
        if (file.is_absolute() && std::filesystem::is_directory(file.parent_path(), error)) {
            trace_file_ = file; trace_.reserve(36000);
        }
    }
}
FrameProfiler::~FrameProfiler() {
    if (trace_file_.empty()) return;
    // Export on close, not in the measured render loop. No game names, IDs,
    // paths, tokens or input values are written to the trace.
    std::ofstream file(trace_file_);
    file << "time_ms,frame_ms,tick_ms,input_ms,system_ms,draw_ms,end_draw_ms,bitmap_ms,bitmap_uploads\n" << std::fixed << std::setprecision(4);
    for (const auto& sample : trace_) file << sample.time_ms << ',' << sample.interval_ms << ',' << sample.tick_ms << ','
        << sample.input_ms << ',' << sample.system_ms << ',' << sample.render.draw_ms << ',' << sample.render.present_ms << ','
        << sample.render.upload_ms << ',' << sample.render.uploads << '\n';
    const auto stats = summary();
    std::ofstream report(trace_file_.wstring() + L".summary.txt");
    report << "Recent " << count_ << " frames\nFPS=" << stats.fps << "\nFrame p95=" << stats.frame_p95 << " ms\nFrame p99=" << stats.frame_p99
        << " ms\nUpdate=" << stats.tick_ms << " ms\nUpdate max=" << stats.tick_max << " ms\nInput=" << stats.input_ms
        << " ms\nSystem=" << stats.system_ms << " ms\nDraw=" << stats.draw_ms << " ms\nEndDraw/present=" << stats.present_ms
        << " ms\nBitmap=" << stats.upload_ms << " ms\nBitmap uploads=" << stats.uploads << '\n';
    report << "Startup from process creation: initialized=" << startupMs(Startup::initialized) << " ms; first frame="
        << startupMs(Startup::first_frame) << " ms; first shelf=" << startupMs(Startup::first_shelf) << " ms\n";
}
void FrameProfiler::startup(Startup mark) {
    auto& slot = startup_[static_cast<size_t>(mark)];
    if (slot >= 0 || !enabled()) return;
    FILETIME creation{}, exited{}, kernel{}, user{}, now{};
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exited, &kernel, &user)) return;
    GetSystemTimePreciseAsFileTime(&now);
    const auto ticks = [](FILETIME time) { return (static_cast<unsigned long long>(time.dwHighDateTime) << 32) | time.dwLowDateTime; };
    slot = static_cast<double>(ticks(now) - ticks(creation)) / 10000.0;
}
void FrameProfiler::tick(double total_ms, double input_ms, double system_ms) {
    pending_.tick_ms = total_ms; pending_.input_ms = input_ms; pending_.system_ms = system_ms;
}
void FrameProfiler::frame(RenderTiming timing) {
    if (!enabled()) return;
    if (startup_[static_cast<size_t>(Startup::first_frame)] < 0) startup(Startup::first_frame);
    const auto now = Clock::now();
    pending_.time_ms = std::chrono::duration<double, std::milli>(now - start_).count();
    pending_.interval_ms = last_frame_ == Clock::time_point{} ? 0 : std::chrono::duration<double, std::milli>(now - last_frame_).count();
    pending_.render = timing; last_frame_ = now;
    frames_[next_] = pending_; next_ = (next_ + 1) % frames_.size(); count_ = std::min(count_ + 1, frames_.size());
    if (!trace_file_.empty() && trace_.size() < 36000) trace_.push_back(pending_);
}
PerformanceSummary FrameProfiler::summary() const {
    PerformanceSummary result;
    if (!count_) return result;
    std::vector<double> intervals;
    double elapsed = 0;
    for (size_t i = 0; i < count_; ++i) {
        const auto& frame = frames_[i];
        if (frame.interval_ms > 0) { intervals.push_back(frame.interval_ms); elapsed += frame.interval_ms; }
        result.tick_ms += frame.tick_ms; result.tick_max = std::max(result.tick_max, frame.tick_ms);
        result.input_ms += frame.input_ms; result.system_ms += frame.system_ms;
        result.draw_ms += frame.render.draw_ms; result.present_ms += frame.render.present_ms;
        result.upload_ms += frame.render.upload_ms; result.uploads += frame.render.uploads;
    }
    result.tick_ms /= count_; result.input_ms /= count_; result.system_ms /= count_;
    result.draw_ms /= count_; result.present_ms /= count_; result.upload_ms /= count_;
    if (!intervals.empty()) {
        std::sort(intervals.begin(), intervals.end());
        result.fps = intervals.size() * 1000.0 / elapsed;
        result.frame_p95 = intervals[static_cast<size_t>((intervals.size() - 1) * 0.95)];
        result.frame_p99 = intervals[static_cast<size_t>((intervals.size() - 1) * 0.99)];
    }
    return result;
}
std::wstring FrameProfiler::label(int refresh_hz, int target_hz) {
    const auto now = Clock::now();
    if (!label_.empty() && now - label_time_ < std::chrono::milliseconds(400)) return label_;
    label_time_ = now;
    const auto stats = summary();
    std::wostringstream text;
    text << std::fixed << std::setprecision(1) << stats.fps << L" FPS    p95 " << stats.frame_p95 << L" ms    p99 " << stats.frame_p99 << L" ms\n"
         << L"Update " << stats.tick_ms << L" ms (max " << stats.tick_max << L")    input " << stats.input_ms << L"    system " << stats.system_ms << L"\n"
         << L"Draw " << stats.draw_ms << L" ms    EndDraw / present " << stats.present_ms << L" ms\n"
         << L"Bitmap " << stats.upload_ms << L" ms / " << stats.uploads << tr(L"    cel ") << target_hz << tr(L" Hz    ekran ");
    if (refresh_hz) text << refresh_hz << L" Hz"; else text << L"?";
    label_ = text.str(); return label_;
}
