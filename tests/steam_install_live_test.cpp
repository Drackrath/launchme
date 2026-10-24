#include "../src/library/steam.hpp"
#include "../src/platform/steam_downloads.hpp"
#include <chrono>
#include <iostream>
#include <thread>

int main() {
    const auto root = findSteamRoot();
    double total_ms = 0;
    SteamDownloads downloads(root);
    for (int sample = 0; sample < 4; ++sample) {
        const auto start = std::chrono::steady_clock::now();
        const auto scan = scanSteamLibrary(root);
        const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(); total_ms += ms;
        size_t installed = 0, active = 0, known = 0, live = 0;
        for (const auto& game : scan.visible) {
            installed += game.installed; active += game.installation.active(); known += game.installation.percent().has_value();
            if (auto bytes = downloads.read(game.app_id)) { ++live; if (game.installation.active() && bytes->total) std::cout << "live_percent=" << static_cast<int>(100.0L * bytes->done / bytes->total) << '\n'; }
        }
        // Only aggregate public counts. Never print account, title, path or ID.
        std::cout << "sample=" << sample << " complete=" << scan.complete << " unavailable_libraries=" << scan.unavailable_libraries.size() <<
            " unreadable_apps=" << scan.unavailable_apps.size() << " installed=" << installed << " installing=" << active << " with_percent=" << known <<
            " ipc=" << downloads.connected() << " app_manager=" << downloads.appManagerConnected() << " live_counters=" << live << " scan_ms=" << ms << '\n';
        if (sample != 3) std::this_thread::sleep_for(std::chrono::milliseconds(2400));
    }
    std::cout << "mean_scan_ms=" << total_ms / 4 << '\n';
}
