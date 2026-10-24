// Screensaver: after the idle period (or "Uruchom teraz" in Settings) the cached artwork
// of the public library plays as a shuffled slideshow above the launcher screen, which
// stays exactly as it was. Only files already on disk are shown; a worker scans and
// reads them, the renderer decodes off the UI thread. Drawing: src/ui/screensaver_renderer.cpp.
#include "launcher.hpp"
#include "launcher_internal.hpp"
#include "../artwork/cover_cache.hpp"

#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <unordered_set>

using screensaver::Verdict;

namespace {
ArtworkKind fileKind(screensaver::Image image) {
    return image == screensaver::Image::hero ? ArtworkKind::hero :
           image == screensaver::Image::poster ? ArtworkKind::poster : ArtworkKind::cover;
}
uint32_t mix(uint32_t value) { // Finalizer of MurmurHash3: spreads a seed over every bit.
    value ^= value >> 16; value *= 0x85EBCA6Bu; value ^= value >> 13; value *= 0xC2B2AE35u; value ^= value >> 16;
    return value;
}
}

bool Launcher::textEntry() const { return editingSecret() || customEditorOpen() || editingArtworkSearch() || identifyOpen(); }

void Launcher::startScreensaver() {
    if (saver_.active) return;
    cancelEnterHold(); cancelTabHold();
    saver_ = ScreensaverState{};
    saver_.active = true;
    saver_.style = settings_.screensaver_style;
    std::optional<std::pair<float, float>> cursor;
    POINT screen{};
    const bool pointer = GetCursorPos(&screen) != FALSE;
    if (POINT client = screen; pointer && ScreenToClient(hwnd_, &client))
        cursor = std::pair{static_cast<float>(client.x), static_cast<float>(client.y)};
    saver_gate_.activate(cursor);
    renderer_.clearScreensaver();
    // A snapshot of the public library (never private or hidden games); the worker checks the disk.
    std::vector<Game> games;
    std::unordered_set<std::string> seen;
    for (const auto& game : public_library_)
        if (isGame(game) && game.visibility == GameVisibility::public_game && !gameKey(game).empty() && !game.name.empty() && !placeholderGameName(game.name) &&
            seen.insert(gameKey(game)).second) games.push_back(game);
    // Nothing downloads while it runs, except the poster prefetch of the boxes style.
    {
        std::lock_guard lock(jobs_->mutex);
        jobs_->quiet = true;
    }
    const auto queue = saver_jobs_;
    uint32_t generation = 0;
    {
        std::lock_guard lock(queue->mutex);
        generation = queue->generation.fetch_add(1) + 1;
        queue->requests.clear();
    }
    queue->wake.notify_all();
    work_.start([queue, generation, alive = alive_, hwnd = hwnd_, games = std::move(games), style = saver_.style] {
        const auto current = [&] { return alive->load() && queue->generation.load() == generation; };
        const auto cache = coverCacheDirectory();
        const auto needed = screensaver::needed(style);
        auto scan = std::make_unique<ScreensaverMessage>();
        scan->generation = generation;
        scan->scan = true;
        for (const auto& game : games) {
            if (!current()) return;
            screensaver::Artwork art;
            if (needed.hero) art.hero = cachedArtworkAvailable(game, ArtworkKind::hero, cache);
            if (needed.poster) art.poster = cachedArtworkAvailable(game, ArtworkKind::poster, cache);
            if (needed.cover && !art.poster) art.cover = cachedArtworkAvailable(game, ArtworkKind::cover, cache);
            if (screensaver::imageFor(style, art) != screensaver::Image::none) scan->games.emplace_back(game, art);
        }
        if (!current() || !PostMessageW(hwnd, screensaver_message, 0, reinterpret_cast<LPARAM>(scan.get()))) return;
        scan.release();
        while (true) {
            ScreensaverRequest request;
            {
                std::unique_lock lock(queue->mutex);
                queue->wake.wait(lock, [&] { return !current() || !queue->requests.empty(); });
                if (!current()) return;
                request = std::move(queue->requests.front());
                queue->requests.pop_front();
            }
            auto image = std::make_unique<ScreensaverMessage>();
            image->generation = generation;
            image->sequence = request.sequence;
            image->bytes = cachedArtwork(request.game, request.kind, cache);
            if (!current()) return;
            if (PostMessageW(hwnd, screensaver_message, 0, reinterpret_cast<LPARAM>(image.get()))) image.release();
        }
    });
    // Console-like: no pointer over the artwork. The next WM_SETCURSOR after it ends restores it.
    if (pointer && WindowFromPoint(screen) == hwnd_) SetCursor(nullptr);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::stopScreensaver(bool woken) {
    if (!saver_.active) return;
    if (!woken) saver_gate_.deactivate();
    {
        std::lock_guard lock(saver_jobs_->mutex);
        saver_jobs_->generation.fetch_add(1);
        saver_jobs_->requests.clear();
    }
    saver_jobs_->wake.notify_all();
    {
        std::lock_guard lock(jobs_->mutex);
        jobs_->quiet = false;
    }
    jobs_->wake.notify_all(); // Held artwork downloads resume.
    saver_ = ScreensaverState{};
    renderer_.clearScreensaver();
    POINT cursor{};
    if (GetCursorPos(&cursor) && WindowFromPoint(cursor) == hwnd_) SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool Launcher::screensaverVerdict(Verdict verdict) {
    if (verdict == Verdict::wake) { stopScreensaver(true); noteUserInput(); }
    return verdict != Verdict::pass;
}

bool Launcher::screensaverInput(UINT message, WPARAM wparam, LPARAM lparam) {
    using screensaver::Button;
    const auto button = [&](Button which) { return screensaverVerdict(saver_gate_.button(which, GetTickCount64(), GetDoubleClickTime())); };
    switch (message) {
    case WM_KEYDOWN: case WM_SYSKEYDOWN:
        return screensaverVerdict(saver_gate_.keyDown(static_cast<unsigned>(wparam), (lparam & (1LL << 30)) != 0));
    case WM_KEYUP: case WM_SYSKEYUP: return screensaverVerdict(saver_gate_.keyUp(static_cast<unsigned>(wparam)));
    case WM_CHAR: case WM_SYSCHAR: case WM_DEADCHAR: case WM_SYSDEADCHAR: return screensaverVerdict(saver_gate_.character());
    case WM_LBUTTONDOWN: return button(Button::left);
    case WM_LBUTTONDBLCLK: return button(Button::left_double);
    case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: return button(Button::right);
    case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: return button(Button::middle);
    case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK: return button(Button::extra);
    case WM_RBUTTONUP: return screensaverVerdict(saver_gate_.buttonUp(Button::right));
    case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL: return screensaverVerdict(saver_gate_.wheel());
    case WM_MOUSEMOVE: {
        const std::pair position{static_cast<float>(GET_X_LPARAM(lparam)), static_cast<float>(GET_Y_LPARAM(lparam))};
        const auto verdict = saver_gate_.mouseMove(position.first, position.second);
        // Windows also sends WM_MOUSEMOVE without movement; only a real move restarts the idle period.
        if (verdict == Verdict::pass && saver_mouse_ && *saver_mouse_ != position) last_input_ = std::chrono::steady_clock::now();
        saver_mouse_ = position;
        return screensaverVerdict(verdict);
    }
    default: return false;
    }
}

void Launcher::updateScreensaver(std::chrono::steady_clock::time_point now, float dt) {
    const bool watched = windowWatched();
    if (watched && !saver_watched_) last_input_ = now; // Back in front: the idle period starts over.
    saver_watched_ = watched;
    if (!saver_.active) {
        const screensaver::Conditions conditions{settings_.screensaver, watched, anyActiveSession() || pending_launch_ || launchAnimationActive(),
                                                 textEntry() || tour_.open || libraryDialogOpen(), now - last_input_, settings_.screensaver_minutes};
        if (screensaver::shouldStart(conditions)) startScreensaver();
        return;
    }
    // Only while someone can see it: another window in front (a game, Alt+Tab) ends it.
    if (!watched || anyActiveSession()) { stopScreensaver(false); return; }
    saver_.time += dt;
    if (!saver_.scanned) return;
    auto& s = saver_;
    using Slot = Renderer::SlotState;
    const auto state = [&](const ScreensaverItem& item) {
        return item.key.empty() ? Slot::empty : item.loaded ? renderer_.screensaverSlot(item.slot) : Slot::pending;
    };
    // A game whose image cannot be read or decoded leaves the rotation.
    const auto drop = [&](ScreensaverItem& item) { s.playlist.remove(item.key); item.clear(); };
    if (s.current.key.empty() && !s.playlist.empty()) screensaverAssign(s.current);
    if (!s.current.key.empty() && s.current.shown_at < 0) {
        const auto current = state(s.current);
        if (current == Slot::ready) { s.current.shown_at = s.time; InvalidateRect(hwnd_, nullptr, FALSE); }
        else if (current == Slot::failed) drop(s.current);
    }
    if (s.current.shown_at < 0) return;
    const float age = s.time - s.current.shown_at;
    // Preload the next game while this one is on screen.
    if (s.upcoming.key.empty() && s.playlist.size() > 1) screensaverAssign(s.upcoming);
    if (!s.upcoming.key.empty()) {
        const auto upcoming = state(s.upcoming);
        if (upcoming == Slot::failed) drop(s.upcoming);
        else if (upcoming == Slot::ready && age >= screensaver::item_seconds) {
            std::swap(s.previous, s.current); // The shown game fades out,
            std::swap(s.current, s.upcoming); // the preloaded one fades in,
            s.upcoming.clear();               // and the slot faded out long ago takes the next.
            s.current.shown_at = s.time;
        }
    } else if (s.playlist.size() == 1 && age >= screensaver::item_seconds) {
        // A single game: crossfade into itself with a new motion instead of freezing.
        s.previous = s.current;
        s.current.shown_at = s.time;
        s.current.seed = mix(s.current.seed + 1);
    }
}

void Launcher::screensaverAssign(ScreensaverItem& item) {
    for (size_t attempt = 0; attempt <= saver_.playlist.size() + 1 && !saver_.playlist.empty(); ++attempt) {
        const auto key = saver_.playlist.next();
        // After a dropped game a new pass may repeat the one on screen; skip it.
        if (&item == &saver_.upcoming && key == saver_.current.key && saver_.playlist.size() > 1) continue;
        const auto found = saver_.games.find(key);
        if (found == saver_.games.end()) { saver_.playlist.remove(key); continue; }
        const auto image = screensaver::imageFor(saver_.style, found->second.second);
        item.clear();
        item.key = key;
        item.title = found->second.first.name;
        item.poster = image == screensaver::Image::poster;
        item.hero = image == screensaver::Image::hero;
        item.sequence = ++saver_.next_sequence;
        item.seed = mix(static_cast<uint32_t>(std::hash<std::string>{}(key)) ^ (item.sequence * 0x9E3779B9u));
        {
            std::lock_guard lock(saver_jobs_->mutex);
            saver_jobs_->requests.push_back({item.sequence, found->second.first, fileKind(image)});
        }
        saver_jobs_->wake.notify_one();
        return;
    }
}

void Launcher::onScreensaver(std::unique_ptr<ScreensaverMessage> message) {
    if (!saver_.active || message->generation != saver_jobs_->generation.load()) return;
    if (message->scan) {
        std::vector<std::string> keys;
        for (auto& [game, art] : message->games) {
            auto key = gameKey(game);
            if (saver_.games.emplace(key, std::pair{std::move(game), art}).second) keys.push_back(std::move(key));
        }
        const auto seed = saver_seed_ ? saver_seed_ : mix(static_cast<uint32_t>(GetTickCount64()) | 1u);
        saver_.playlist.reset(std::move(keys), seed);
        saver_.scanned = true;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    for (auto* item : {&saver_.current, &saver_.upcoming}) {
        if (item->key.empty() || item->loaded || item->sequence != message->sequence) continue;
        if (message->bytes.empty()) { saver_.playlist.remove(item->key); item->clear(); }
        else {
            renderer_.setScreensaverImage(item->slot, std::move(message->bytes), item->hero);
            item->loaded = true;
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
}

screensaver::View Launcher::screensaverView() const {
    screensaver::View view;
    view.style = saver_.style;
    view.time = saver_.time;
    view.opacity = std::clamp(saver_.time / screensaver::intro_seconds, 0.0f, 1.0f);
    view.empty = saver_.scanned && saver_.playlist.empty() && saver_.current.key.empty();
    const auto& current = saver_.current;
    if (current.key.empty() || current.shown_at < 0) return view;
    const auto describe = [&](const ScreensaverItem& item, float alpha) {
        return screensaver::View::Item{item.slot, saver_.time - item.shown_at, alpha, item.seed, item.poster, item.title};
    };
    const float fade = screensaver::fade(saver_.time - current.shown_at);
    view.current = describe(current, fade);
    if (!saver_.previous.key.empty() && saver_.previous.shown_at >= 0 && fade < 1)
        view.previous = describe(saver_.previous, 1 - fade);
    return view;
}
