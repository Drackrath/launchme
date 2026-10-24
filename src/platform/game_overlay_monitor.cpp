#include "game_overlay_monitor.hpp"
#include <PresentMonTraceConsumer.hpp>
#include <TraceSession.hpp>

namespace {
double qpcSeconds() {
    LARGE_INTEGER now{}, frequency{};
    QueryPerformanceCounter(&now); QueryPerformanceFrequency(&frequency);
    return static_cast<double>(now.QuadPart) / frequency.QuadPart;
}
// Both the ETW session and its consumer outlive ProcessTrace. Only this worker
// starts/stops them; no caller waits for ETW on the window thread.
class Capture {
public:
    explicit Capture(uint32_t pid) : pid_(pid) {
        consumer_.mFilteredProcessIds = true;
        consumer_.AddTrackedProcessForFiltering(pid);
        const auto name = L"Launchme-FPS-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
        error_ = session_.Start(&consumer_, nullptr, nullptr, name.c_str());
        if (error_ != ERROR_SUCCESS) return;
        try {
            thread_ = std::thread([this, handle = session_.mTraceHandle] {
                try {
                    auto trace = handle;
                    const auto status = ProcessTrace(&trace, 1, nullptr, nullptr);
                    if (status != ERROR_SUCCESS && status != ERROR_CANCELLED) failed_ = true;
                } catch (...) { failed_ = true; }
            });
        } catch (...) { session_.Stop(); error_ = ERROR_NOT_ENOUGH_MEMORY; }
    }
    ~Capture() { session_.Stop(); if (thread_.joinable()) thread_.join(); }
    game_fps::Reading read(game_fps::History& history) {
        if (error_ != ERROR_SUCCESS) return {error_ == ERROR_ACCESS_DENIED || error_ == ERROR_PRIVILEGE_NOT_HELD ?
            game_fps::Status::denied : game_fps::Status::failed};
        if (failed_) return {game_fps::Status::failed};
        std::vector<std::shared_ptr<PresentEvent>> presents, lost;
        std::vector<ProcessEvent> processes;
        consumer_.DequeuePresentEvents(presents);
        consumer_.DequeueLostPresentEvents(lost);
        consumer_.DequeueProcessEvents(processes);
        ULONG events = 0, buffers = 0;
        if (session_.CheckLostReports(&events, &buffers) != ERROR_SUCCESS) return {game_fps::Status::failed};
        const bool discontinuity = events != lost_events_ || buffers != lost_buffers_ ||
            std::any_of(lost.begin(), lost.end(), [&](const auto& present) { return present->ProcessId == pid_; });
        lost_events_ = events; lost_buffers_ = buffers;
        if (discontinuity) { history.clear(); return {game_fps::Status::collecting}; }
        const double frequency = static_cast<double>(session_.mTimestampFrequency.QuadPart);
        if (frequency <= 0) return {game_fps::Status::failed};
        // Displayed presents only: dropped/test/failed presents do not inflate FPS.
        for (const auto& p : presents)
            if (p->ProcessId == pid_ && !p->IsLost && p->FinalState == PresentResult::Presented && p->ScreenTime)
                history.present(p->SwapChainAddress, p->ScreenTime / frequency);
        return history.read(qpcSeconds());
    }
private:
    PMTraceConsumer consumer_;
    uint32_t pid_ = 0;
    TraceSession session_;
    ULONG error_ = ERROR_SUCCESS, lost_events_ = 0, lost_buffers_ = 0;
    std::atomic_bool failed_{false};
    std::thread thread_;
};
}

struct GameOverlayMonitor::State {
    mutable std::mutex mutex;
    std::condition_variable wake;
    bool started = false, stop = false, enabled = false, fps = false;
    HWND retained = nullptr;
    std::shared_ptr<const std::vector<Game>> candidates = std::make_shared<const std::vector<Game>>();
    Snapshot value;
};
GameOverlayMonitor::GameOverlayMonitor() : state_(std::make_shared<State>()) {}
GameOverlayMonitor::~GameOverlayMonitor() { stop(); }
void GameOverlayMonitor::stop() {
    { std::lock_guard lock(state_->mutex); state_->stop = true; }
    state_->wake.notify_all();
}
void GameOverlayMonitor::configure(bool enabled, bool fps, HWND retained) {
    { std::lock_guard lock(state_->mutex); state_->enabled = enabled; state_->fps = fps; state_->retained = retained; }
    state_->wake.notify_one();
}
void GameOverlayMonitor::candidates(const std::vector<Game>& games) {
    { std::lock_guard lock(state_->mutex); state_->candidates = std::make_shared<const std::vector<Game>>(games); }
    state_->wake.notify_one();
}
GameOverlayMonitor::Snapshot GameOverlayMonitor::snapshot() const { std::lock_guard lock(state_->mutex); return state_->value; }
void GameOverlayMonitor::start(BackgroundWork& work) {
    { std::lock_guard lock(state_->mutex); if (state_->started) return; state_->started = true; }
    const auto shared = state_;
    const auto publish = [shared](std::optional<GameWindow> target, game_fps::Reading fps) {
        std::lock_guard lock(shared->mutex);
        if (shared->value.target != target || shared->value.fps != fps) {
            shared->value.target = std::move(target); shared->value.fps = fps; ++shared->value.revision;
        }
    };
    work.start([shared, publish] {
        std::unique_ptr<Capture> capture;
        game_fps::History history;
        std::optional<GameWindow> target;
        std::shared_ptr<const std::vector<Game>> candidates;
        HWND last_window = nullptr;
        uint64_t probe = 0;
        for (;;) {
            bool enabled = false, fps = false;
            HWND retained = nullptr;
            std::shared_ptr<const std::vector<Game>> games;
            {
                std::unique_lock lock(shared->mutex);
                if (!shared->enabled && !capture) shared->wake.wait(lock, [&] { return shared->stop || shared->enabled; });
                if (shared->stop) break;
                enabled = shared->enabled; fps = shared->fps; retained = shared->retained; games = shared->candidates;
            }
            const auto foreground = GetForegroundWindow();
            DWORD foreground_pid = 0;
            GetWindowThreadProcessId(foreground, &foreground_pid);
            const auto window = retained && foreground_pid == GetCurrentProcessId() ? retained : foreground;
            auto next = target;
            if (!enabled) next.reset();
            else if (window != last_window || games != candidates || GetTickCount64() >= probe) {
                next = findWindowGame(window, *games);
                last_window = window; candidates = games; probe = GetTickCount64() + 1000;
            }
            if (next != target || !fps) { capture.reset(); history.clear(); }
            target = next;
            if (enabled && fps && target && !capture) capture = std::make_unique<Capture>(target->process.pid);
            auto reading = game_fps::Reading{!fps ? game_fps::Status::disabled : game_fps::Status::waiting};
            if (capture) reading = capture->read(history); // Keep the drawer live while Guide is open.
            publish(target, reading);
            std::unique_lock lock(shared->mutex);
            shared->wake.wait_for(lock, std::chrono::milliseconds(250));
            if (shared->stop) break;
        }
    }, [publish](std::exception_ptr) { publish({}, {game_fps::Status::failed}); });
}
