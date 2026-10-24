#include "volume_monitor.hpp"

bool VolumeMonitor::start(BackgroundWork& work, Reader reader) {
    if (started_) return true;
    started_ = work.start([state = state_, reader = std::move(reader)]() mutable {
        const auto hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(hr)) return;
        struct Apartment { ~Apartment() { CoUninitialize(); } } apartment;
        SystemAudio audio;
        if (!reader) reader = [&audio](bool refresh) -> std::optional<VolumeLevel> {
            if (refresh) audio.refreshDevice();
            VolumeLevel value;
            return audio.read(value) ? std::optional{value} : std::nullopt;
        };
        auto next_refresh = std::chrono::steady_clock::time_point{};
        while (true) {
            const auto now = std::chrono::steady_clock::now();
            bool refresh;
            uint64_t generation;
            {
                std::lock_guard lock(state->mutex);
                if (state->stopped) return;
                refresh = std::exchange(state->refresh, false) || now >= next_refresh;
                generation = state->generation;
            }
            if (refresh) next_refresh = now + std::chrono::seconds(5);
            const auto value = reader(refresh); // May block; never hold the snapshot mutex here.
            std::unique_lock lock(state->mutex);
            if (state->stopped) return;
            if (state->generation == generation && value) state->pending = value;
            state->wake.wait_for(lock, std::chrono::milliseconds(120), [&] { return state->stopped || state->refresh; });
            state->wake.wait(lock, [&] { return state->stopped || state->refresh || state->watched; });
        }
    });
    return started_;
}
