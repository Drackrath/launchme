#pragma once
#include "background_work.hpp"
#include "system_audio.hpp"
#include <optional>
#include <utility>

// Latest-only volume snapshots. All Core Audio interfaces (the PulseAudio connection on Linux)
// are created, used and released by one MTA worker; take()/refresh() never perform a system audio call.
class VolumeMonitor {
public:
    using Reader = std::function<std::optional<VolumeLevel>(bool refresh)>;
    VolumeMonitor() = default;
    VolumeMonitor(const VolumeMonitor&) = delete;
    VolumeMonitor& operator=(const VolumeMonitor&) = delete;
    ~VolumeMonitor() { stop(); }
    bool start(BackgroundWork& work, Reader reader = {});
    std::optional<VolumeLevel> take() {
        std::lock_guard lock(state_->mutex);
        return std::exchange(state_->pending, std::nullopt);
    }
    void refresh() {
        { std::lock_guard lock(state_->mutex); ++state_->generation; state_->refresh = true; state_->pending.reset(); }
        state_->wake.notify_one();
    }
    void stop() {
        { std::lock_guard lock(state_->mutex); state_->stopped = true; state_->pending.reset(); }
        state_->wake.notify_all();
    }
    // The volume indicator is only seen while the launcher is in front: in the background (a game
    // runs, the window is minimized) the reader sleeps instead of reading every 120 ms.
    void watched(bool value) {
        {
            std::lock_guard lock(state_->mutex);
            if (state_->watched == value) return;
            state_->watched = value;
            if (value) state_->refresh = true; // The level may have changed meanwhile.
        }
        state_->wake.notify_one();
    }
private:
    struct State {
        std::mutex mutex;
        std::condition_variable wake;
        std::optional<VolumeLevel> pending;
        uint64_t generation = 0;
        bool stopped = false, refresh = true, watched = true;
    };
    std::shared_ptr<State> state_ = std::make_shared<State>();
    bool started_ = false;
};
