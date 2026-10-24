#include "../src/platform/volume_monitor.hpp"
#include <future>
#include <iostream>

int main() {
    using namespace std::chrono_literals;
    BackgroundWork work;
    VolumeMonitor monitor;
    std::promise<void> entered, release;
    auto entered_future = entered.get_future();
    const auto released = release.get_future().share();
    const auto owner = std::this_thread::get_id();
    std::atomic_int calls = 0;
    std::atomic_bool wrong_thread = false, wrong_refresh = false;
    if (!monitor.start(work, [&](bool refresh) -> std::optional<VolumeLevel> {
        if (std::this_thread::get_id() == owner) wrong_thread = true;
        const int call = ++calls;
        if (call <= 2 && !refresh) wrong_refresh = true;
        if (call == 1) { entered.set_value(); released.wait(); return VolumeLevel{0.1f, false}; }
        if (call == 3) return {}; // A transient read failure must not invent zero volume.
        return VolumeLevel{0.6f, true};
    })) return 1;
    struct Stop {
        VolumeMonitor& monitor; BackgroundWork& work; std::promise<void>& release;
        ~Stop() { try { release.set_value(); } catch (...) {} monitor.stop(); work.shutdown(2s); }
    } stop{monitor, work, release};
    if (entered_future.wait_for(2s) != std::future_status::ready) return 2;
    const auto started = std::chrono::steady_clock::now();
    if (monitor.take()) return 3;
    monitor.refresh(); // Endpoint changed while the old endpoint read is blocked.
    if (std::chrono::steady_clock::now() - started > 100ms) return 4;
    release.set_value();
    std::optional<VolumeLevel> value;
    const auto until = std::chrono::steady_clock::now() + 2s;
    while (!(value = monitor.take())) {
        if (std::chrono::steady_clock::now() >= until) return 5;
        std::this_thread::sleep_for(1ms);
    }
    if (value->level != 0.6f || !value->muted || wrong_thread || wrong_refresh) return 6;
    monitor.stop();
    if (!work.shutdown(2s) || monitor.take()) return 7;
    std::cout << "Off-thread volume, nonblocking snapshot, endpoint generation and shutdown passed\n";
}
