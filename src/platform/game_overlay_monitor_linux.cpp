// Linux GameOverlayMonitor: the target is the running game found in /proc (no window: the
// foreground window is the display server's). PresentMon/ETW has no Linux counterpart, so the
// FPS reading is always disabled.
#include "game_overlay_monitor.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>

struct GameOverlayMonitor::State {
    mutable std::mutex mutex;
    std::condition_variable wake;
    bool started = false, stop = false, enabled = false;
    std::shared_ptr<const std::vector<Game>> candidates = std::make_shared<const std::vector<Game>>();
    Snapshot value;
};
GameOverlayMonitor::GameOverlayMonitor() : state_(std::make_shared<State>()) {}
GameOverlayMonitor::~GameOverlayMonitor() { stop(); }
void GameOverlayMonitor::stop() {
    { std::lock_guard lock(state_->mutex); state_->stop = true; }
    state_->wake.notify_all();
}
void GameOverlayMonitor::configure(bool enabled, bool, HWND) {
    { std::lock_guard lock(state_->mutex); state_->enabled = enabled; }
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
    const auto publish = [shared](std::optional<GameWindow> target) {
        std::lock_guard lock(shared->mutex);
        if (shared->value.target != target) { shared->value.target = std::move(target); ++shared->value.revision; }
    };
    work.start([shared, publish] {
        for (;;) {
            std::shared_ptr<const std::vector<Game>> games;
            {
                std::unique_lock lock(shared->mutex);
                shared->wake.wait(lock, [&] { return shared->stop || shared->enabled; });
                if (shared->stop) break;
                games = shared->candidates;
            }
            // The game started last: its newest process (a launcher stub's game outlives the stub).
            std::optional<GameWindow> target;
            for (const auto index : findRunningGames(*games)) {
                auto processes = findGameProcesses((*games)[index]);
                if (processes.empty()) continue;
                const auto newest = *std::max_element(processes.begin(), processes.end(),
                    [](const GameProcess& a, const GameProcess& b) { return a.created < b.created; });
                if (!target || newest.created > target->process.created) target = GameWindow{nullptr, newest, gameKey((*games)[index])};
            }
            publish(std::move(target));
            std::unique_lock lock(shared->mutex);
            shared->wake.wait_for(lock, std::chrono::seconds(1));
            if (shared->stop) break;
            if (!shared->enabled) { lock.unlock(); publish({}); }
        }
    }, [publish](std::exception_ptr) { publish({}); });
}
