#include "launcher.hpp"
#include "../artwork/bulk_covers.hpp"

void Launcher::openBulkCovers(std::vector<Game> games, bool start) {
    if (bulk_control_ && !bulk_ui_.progress.terminal()) { bulk_ui_.open = true; return; }
    bulk_ui_ = {}; bulk_ui_.open = true;
    bulk_control_ = std::make_shared<bulk_covers::Control>();
    if (start) { bulk_ui_.started = true; bulk_control_->start = true; }
    bulk_games_ = games.empty() ? public_library_ : std::move(games);
    const auto generation = ++bulk_generation_;
    const auto control = bulk_control_; const auto alive = alive_; const auto hwnd = hwnd_; const auto jobs = jobs_;
    { std::lock_guard lock(jobs->mutex); jobs->bulk = true; }
    jobs->wake.notify_all();
    const auto ops = bulk_ops_ ? *bulk_ops_ : bulk_covers::operations(coverCacheDirectory(), findSteamRoot(), settings_.emulation_rom_art);
    work_.start([games = bulk_games_, generation, control, alive, hwnd, jobs, ops] {
        bulk_covers::run(games, *control, ops, [&](const bulk_covers::Progress& progress) {
            if (progress.terminal()) {
                { std::lock_guard lock(jobs->mutex); jobs->bulk = false; }
                jobs->wake.notify_all();
            }
            if (!alive->load()) { control->cancel = true; return; }
            auto message = std::make_unique<BulkCoversMessage>(); message->generation = generation; message->progress = progress;
            if (PostMessageW(hwnd, bulk_covers_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
            else control->cancel = true;
        });
    });
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::onBulkCovers(std::unique_ptr<BulkCoversMessage> message) {
    if (message->generation != bulk_generation_) return;
    bulk_ui_.progress = std::move(message->progress);
    if (bulk_ui_.progress.terminal()) {
        bulk_ui_.cancelling = false;
        bulk_ui_.focus = 0;
        for (const auto& game : bulk_games_) for (const auto kind : {CoverMessage::Kind::cover, CoverMessage::Kind::poster, CoverMessage::Kind::landscape})
            invalidateArtwork(game, kind);
        bulk_games_.clear(); requestVisibleArt();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::bulkCoversKey(WPARAM key, bool repeated) {
    if (!bulk_ui_.open) return;
    if (key == VK_ESCAPE || key == VK_BACK) { bulkCoversClose(); return; }
    if (key == VK_F11 && !repeated) { toggleFullscreen(); return; }
    if (key == VK_LEFT || key == VK_UP) bulk_ui_.focus = 0;
    if (key == VK_RIGHT || key == VK_DOWN || key == VK_TAB) bulk_ui_.focus = (bulk_ui_.focus + 1) % bulk_ui_.buttons();
    if ((key == VK_RETURN || key == VK_SPACE) && !repeated) {
        if (bulk_ui_.focus == 1 || bulk_ui_.progress.terminal()) { bulkCoversClose(); return; }
        if (bulk_ui_.progress.phase == bulk_covers::Phase::scanning || bulk_ui_.cancelling || !bulk_control_) return;
        if (!bulk_ui_.started) { bulk_ui_.started = true; bulk_control_->start = true; }
        else { bulk_ui_.paused = !bulk_ui_.paused; bulk_control_->pause = bulk_ui_.paused; }
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::bulkCoversClose() {
    if (!bulk_ui_.progress.terminal() && bulk_control_) {
        bulk_control_->cancel = true; bulk_ui_.cancelling = true;
    } else bulk_ui_.open = false;
    InvalidateRect(hwnd_, nullptr, FALSE);
}
