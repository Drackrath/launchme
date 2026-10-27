// System → Wydajność → Tryb niskiego zużycia RAM (AppSettings::low_memory): the renderer's artwork
// policy, the artwork cache's budget and the browser window follow the setting; behind a game the
// launcher gives its graphics and artwork back and builds them again when it is in front once more.
#include "launcher.hpp"
#include "../platform/memory_usage.hpp"

#include <sstream>

void Launcher::applyLowMemory() {
    renderer_.setLowMemory(settings_.low_memory, settings_.low_memory_bc1);
    art_.setBudget(settings_.low_memory ? low_memory_art_budget : art_budget);
    trimArt();
    if (browser_) browser_->setLowMemory(settings_.low_memory);
    if (!settings_.low_memory && memory_released_) restoreMemory();
}

// Minimized, or a game session runs and another window covering the launcher's monitor is in
// front (a game, or its client in full screen). The Guide drawer over a game keeps the launcher's
// graphics; a window beside the launcher (a windowed game, the desktop window side by side) too.
bool Launcher::launcherHidden() const {
    if (!hwnd_ || closing_) return false;
    if (IsIconic(hwnd_)) return true;
    if (!anyActiveSession() || gameOverlayOpen()) return false;
    const HWND front = foreground_window_ ? foreground_window_() : GetForegroundWindow();
    if (!front || front == hwnd_ || (browser_ && browser_->owns(front))) return false;
    const HMONITOR monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
    if (MonitorFromWindow(front, MONITOR_DEFAULTTONEAREST) != monitor) return false;
    MONITORINFO info{sizeof(info)};
    RECT covered{};
    if (!GetMonitorInfoW(monitor, &info) || !GetWindowRect(front, &covered)) return false;
    return covered.left <= info.rcMonitor.left && covered.top <= info.rcMonitor.top &&
           covered.right >= info.rcMonitor.right && covered.bottom >= info.rcMonitor.bottom;
}

void Launcher::pollMemory() {
    const bool hidden = settings_.low_memory && launcherHidden();
    if (!hidden) {
        memory_hidden_since_ = 0;
        if (memory_released_) restoreMemory();
    } else if (!memory_released_) {
        const auto now = GetTickCount64();
        if (!memory_hidden_since_) memory_hidden_since_ = now;
        else if (now - memory_hidden_since_ >= memory_release_delay_ms) releaseMemory();
    }
    // Slots whose bytes were let go lost their bitmaps too (a new device): bound again from art_ or disk.
    if (!memory_released_ && renderer_.takeArtLost() && !cards_.empty()) { requestVisibleArt(); InvalidateRect(hwnd_, nullptr, FALSE); }
}

void Launcher::releaseMemory() {
    if (memory_released_) return;
    const auto before = currentMemoryUsage();
    memory_released_ = true;
    stopVideo();
    renderer_.releaseGraphics();
    // The artwork cache, every entry: read again from the disk when the launcher is back.
    art_.setBudget(0);
    forgetArtRequests(art_.trim([](const std::string&) { return false; }));
    art_.setBudget(settings_.low_memory ? low_memory_art_budget : art_budget);
    trimProcessMemory();
    const auto after = currentMemoryUsage();
    std::ostringstream line;
    line << "Behind a game: memory given back (private " << before.private_bytes / 1048576 << " -> " << after.private_bytes / 1048576
         << " MB, working set " << before.working_set / 1048576 << " -> " << after.working_set / 1048576 << " MB, GPU "
         << (before.gpu_local + before.gpu_shared) / 1048576 << " -> " << (after.gpu_local + after.gpu_shared) / 1048576 << " MB)";
    diagnostics::log("info", line.str());
}

void Launcher::restoreMemory() {
    if (!memory_released_) return;
    memory_released_ = false;
    memory_hidden_since_ = 0;
    renderer_.takeArtLost();
    // The next paint makes the target again; the artwork comes from the disk lane as at startup.
    if (!cards_.empty()) requestVisibleArt();
    detail_ = 0;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::releaseFarHeroes() {
    // Flix's first row shows its games' wide art (capsules, backgrounds standing in); a starting
    // game's picture covers the screen.
    const size_t capsules = flixActive() && !flix_rows_.empty() ? flix_rows_[0].count : 0;
    for (size_t i = capsules; i < cards_.size(); ++i) {
        if (i + 1 >= selected_ && i <= selected_ + 1) continue;
        if (launchAnimationActive() && gameKey(cards_[i].game) == launch_artwork_key_) continue;
        renderer_.releaseHero(i);
    }
}
