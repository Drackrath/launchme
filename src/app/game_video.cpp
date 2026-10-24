// Game videos: a game focused for a while (AppSettings::video_delay) plays its video behind the
// shelf in place of its background, with the music ducked: emulated games their ES-DE video, else
// ScreenScraper's; Steam games (steam_trailers) their store trailer. Moving on, a menu, a launch,
// the screensaver or losing the foreground stops it at once.
#include "launcher.hpp"
#include "../artwork/screenscraper_art.hpp"
#include "../net/http.hpp"
#include "../store/steam_trailer.hpp"
#include "../platform/video_manifest.hpp"

#include <algorithm>

const Game* Launcher::videoCandidate() const {
    if ((!settings_.game_videos && !settings_.steam_trailers) || !runtime_started_ || pending_launch_ || launchAnimationActive() ||
        screensaverActive() || gameOverlayOpen()) return nullptr;
    const bool cinematic = screen_ == ScreenMode::library && libraryCinematic(); // The library's full-screen backdrop view.
    if (screen_ != ScreenMode::shelves && screen_ != ScreenMode::platform_games && screen_ != ScreenMode::details && !cinematic) return nullptr;
    if (menu_open_ || cards_.empty() || IsIconic(hwnd_) || GetForegroundWindow() != hwnd_) return nullptr;
    const auto& game = cards_[screen_ == ScreenMode::details ? 0 : std::min(selected_, cards_.size() - 1)].game;
    if (settings_.game_videos && game.kind == GameKind::nonsteam && game.platform != L"PC" && game.app_id) return &game;
    if (settings_.steam_trailers && steamArtwork(game) && game.kind == GameKind::steam && game.app_id) return &game;
    return nullptr;
}

void Launcher::stopVideo() {
    ++video_generation_; // A lookup still running answers for nothing.
    if (video_player_.active()) video_player_.stop();
    renderer_.clearVideo();
    audio_.duckMusic(false);
}

void Launcher::tickVideo(bool& needs_paint) {
    const auto* game = videoCandidate();
    const auto key = game ? gameKey(*game) : std::string{};
    const auto now = std::chrono::steady_clock::now();
    if (key != video_key_) {
        stopVideo();
        video_key_ = key;
        video_since_ = now;
        video_requested_ = false;
    }
    if (key.empty()) return;
    if (!video_requested_ && now - video_since_ >= std::chrono::seconds(settings_.video_delay)) {
        video_requested_ = true;
        const auto generation = ++video_generation_;
        const auto folder = videoFolder();
        const uint64_t space = uint64_t(std::max(100, settings_.trailer_space_mb)) * 1024 * 1024;
        work_.start([alive = alive_, hwnd = hwnd_, generation, key, game = *game, folder, space] {
            auto message = std::make_unique<VideoMessage>();
            message->generation = generation;
            message->key = key;
            // No HttpBudget here: a first video download may take minutes (downloadVideo).
            message->file = game.kind == GameKind::nonsteam ? gameVideoFile(game, userConfigDirectory(), folder)
                                                            : steamTrailerFile(game.app_id, userConfigDirectory(), folder, space);
            message->silent = game.kind != GameKind::nonsteam;
            if (alive->load() && PostMessageW(hwnd, video_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
        });
    }
    if (!video_player_.active()) return;
    if (video_player_.failed()) { // Unplayable file: the picture stays, no retry for this focus.
        video_player_.stop();
        renderer_.clearVideo();
        audio_.duckMusic(false);
        return;
    }
    uint32_t width = 0, height = 0;
    if (video_player_.frame(video_frame_, width, height)) renderer_.setVideoFrame(video_frame_.data(), width, height);
    needs_paint = true; // New frames and the fade-in.
}

void Launcher::onVideo(std::unique_ptr<VideoMessage> message) {
    if (message->generation != video_generation_ || message->key != video_key_ || message->file.empty()) return;
    // The video's own sound (Biblioteka → Dźwięk filmów); the music fades out meanwhile.
    const double volume = settings_ui::video_volume_levels[std::clamp<size_t>(settings_.video_volume, 0, settings_ui::video_volume_levels.size() - 1)];
    video_silent_ = message->silent;
    if (message->silent) { video_player_.open(message->file, 0.0); return; } // Music keeps playing.
    if (video_player_.open(message->file, volume)) audio_.duckMusic(true);
}

std::filesystem::path Launcher::videoFolder() const {
    return settings_.video_folder.empty() ? userConfigDirectory() / L"media" : settings_.video_folder;
}

namespace {
// Launchme's own videos in a folder: its manifest; the default folder (Launchme's alone)
// also counts what was downloaded before the manifest existed.
std::vector<std::filesystem::path> ownVideos(const std::filesystem::path& folder, bool whole_folder) {
    auto entries = video_manifest::read(folder);
    std::error_code ec;
    if (whole_folder)
        for (const auto& item : std::filesystem::recursive_directory_iterator(folder, ec)) {
            std::error_code file;
            if (!item.is_regular_file(file) || item.path().filename() == video_manifest::file_name) continue;
            auto relative = item.path().lexically_relative(folder);
            if (std::none_of(entries.begin(), entries.end(), [&](const auto& entry) { return _wcsicmp(entry.c_str(), relative.c_str()) == 0; }))
                entries.push_back(std::move(relative));
        }
    // Each video''s ".none"/".part" companions go with it.
    const auto count = entries.size();
    for (size_t i = 0; i < count; ++i)
        for (const auto* suffix : {L".none", L".part"}) {
            auto companion = entries[i]; companion += suffix;
            std::error_code file;
            if (std::filesystem::exists(folder / companion, file) &&
                std::none_of(entries.begin(), entries.end(), [&](const auto& entry) { return entry == companion; })) entries.push_back(companion);
        }
    std::erase_if(entries, [&](const auto& entry) { std::error_code file; return !std::filesystem::is_regular_file(folder / entry, file); });
    return entries;
}
bool samePath(const std::filesystem::path& a, const std::filesystem::path& b) {
    return _wcsicmp(a.lexically_normal().native().c_str(), b.lexically_normal().native().c_str()) == 0;
}
}

void Launcher::changeVideoFolder(const std::filesystem::path& folder) {
    const auto from = videoFolder();
    const auto to = folder.empty() ? userConfigDirectory() / L"media" : folder;
    if (samePath(from, to)) return;
    uint64_t bytes = 0;
    for (const auto& entry : ownVideos(from, samePath(from, userConfigDirectory() / L"media"))) {
        std::error_code ec;
        bytes += std::filesystem::file_size(from / entry, ec);
    }
    if (!bytes) { // Nothing to move: the new folder applies at once.
        settings_.video_folder = folder;
        settingsChanged(); audio_.context();
        return;
    }
    video_move_from_ = from; video_move_to_ = to; video_move_bytes_ = bytes;
    prompt_kind_ = PromptKind::move_videos;
    exit_prompt_ = 0;
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::moveVideoFolder(const std::filesystem::path& from, const std::filesystem::path& to) {
    stopVideo(); // A playing file cannot move.
    video_key_.clear();
    const bool whole = samePath(from, userConfigDirectory() / L"media");
    work_.start([alive = alive_, hwnd = hwnd_, from, to, whole] {
        auto message = std::make_unique<VideoMoveMessage>();
        std::vector<std::filesystem::path> moved;
        {
            std::lock_guard lock(video_manifest::guard());
            for (const auto& entry : ownVideos(from, whole)) {
                const auto source = from / entry, target = to / entry;
                std::error_code ec;
                if (std::filesystem::exists(target, ec)) { ++message->kept; continue; } // Never overwrite what is there.
                std::filesystem::create_directories(target.parent_path(), ec);
                std::filesystem::rename(source, target, ec);
                if (ec) { // Another drive: copy, then remove.
                    ec.clear();
                    if (std::filesystem::copy_file(source, target, ec) && !ec) std::filesystem::remove(source, ec);
                    else { std::error_code cleanup; std::filesystem::remove(target, cleanup); }
                }
                if (ec) ++message->failed; else { ++message->moved; moved.push_back(entry); }
            }
            // Manifests follow: the new folder lists them, the old one forgets them.
            auto destination = video_manifest::read(to);
            for (const auto& entry : moved)
                if (entry.extension() != L".none" && entry.extension() != L".part") destination.push_back(entry);
            video_manifest::write(to, destination);
            auto rest = video_manifest::read(from);
            std::erase_if(rest, [&](const auto& entry) { return std::find(moved.begin(), moved.end(), entry) != moved.end(); });
            video_manifest::write(from, rest);
        }
        // Empty "videos" and system folders left behind go too.
        for (const auto& entry : moved)
            for (auto folder = (from / entry).parent_path(); folder.native().size() > from.native().size(); folder = folder.parent_path()) {
                std::error_code ec;
                if (!std::filesystem::is_empty(folder, ec) || ec || !std::filesystem::remove(folder, ec)) break;
            }
        if (alive->load() && PostMessageW(hwnd, video_move_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    });
}

void Launcher::onVideoMove(std::unique_ptr<VideoMoveMessage> message) {
    auto text = tr(L"Przeniesiono: ") + std::to_wstring(message->moved);
    if (message->kept) text += tr(L" · już były w nowym folderze: ") + std::to_wstring(message->kept);
    if (message->failed) text += tr(L" · nie udało się: ") + std::to_wstring(message->failed);
    showNotification(message->failed ? NotificationKind::error : NotificationKind::launch, tr(L"Filmy"), text);
}
