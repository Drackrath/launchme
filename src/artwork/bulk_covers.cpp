#include "bulk_covers.hpp"
#include "esde_art.hpp"
#include "screenscraper_art.hpp"
#include "image_compact.hpp"
#include "../net/http.hpp"
#include <algorithm>
#include <chrono>
#include <set>
#include <thread>
#include <cmath>

namespace bulk_covers {
Operations operations(const std::filesystem::path& cache, const std::filesystem::path& steam, bool rom_first) {
    const auto exists = [cache](const Game& game, ArtworkKind kind) {
        std::error_code ec;
        // Even an invalid existing file is left alone: this tool fills holes, never repairs/replaces.
        return std::filesystem::exists(gameArtworkPath(game, kind, cache), ec) || cachedArtworkAvailable(game, kind, cache);
    };
    return {exists, [=, checked_account = false](const Game& game, ArtworkKind kind) mutable -> Attempt {
        if (exists(game, kind)) return {Result::saved};
        namespace ss = screenscraper;
        const bool emulated = game.kind == GameKind::nonsteam;
        const bool landscape = kind == ArtworkKind::landscape;
        const auto media_kind = landscape ? EmuArtKind::background : EmuArtKind::cover;
        const auto metadata = cache.parent_path() / L"metadata";
        const auto wait = [&]() -> Attempt {
            const auto sgdb = artworkRateLimitSeconds();
            const auto now = ss::unixMs(), until = emulated ? ss::pausedUntil(metadata) : 0;
            const auto seconds = until > now ? (until - now + 999) / 1000 : 0;
            if (seconds) return {Result::retry, seconds, tr(L"Przerwa ScreenScraper: limit lub niedostępność usługi")};
            if (sgdb) return {Result::retry, sgdb, tr(L"Limit SteamGridDB — oczekiwanie na wznowienie")};
            return {};
        };
        MissingImagesOnly missing_only;
        HttpBudget budget(30000);
        consumeArtworkRetryableError();
        std::vector<uint8_t> image;
        const auto local = [&] {
            image = esdeArtForShortcut(game.app_id, game.platform, game.name, media_kind, cache.parent_path() / L"emulation", game.rom);
            if (!image.empty() && kind != ArtworkKind::cover) writeCachedImage(gameArtworkPath(game, kind, cache), image);
        };
        const auto screenScraper = [&] {
            if (httpCancelled() || !ss::load().ready()) return;
            if (!checked_account && !ss::paused(metadata)) {
                checked_account = true;
                const auto info = ss::account(ss::load());
                if (info.status == ss::Status::quota || info.quota.exhausted()) ss::pause(metadata, 12ull * 60 * 60 * 1000);
                else if (info.status == ss::Status::rejected) ss::pause(metadata, 60ull * 60 * 1000);
            }
            // Identifies uncached ROMs too, unlike the normal cache-only fallback.
            screenscraperGallery(game, landscape ? std::vector<std::wstring_view>{L"fanart", L"ss", L"sstitle"} :
                                                   std::vector<std::wstring_view>{L"box-2D"}, cache.parent_path());
            if (!httpCancelled()) image = screenscraperArt(game, media_kind, cache.parent_path());
            if (!image.empty() && kind != ArtworkKind::cover) writeCachedImage(gameArtworkPath(game, kind, cache), image);
        };
        if (emulated) local();
        if (image.empty() && emulated && rom_first) screenScraper();
        if (image.empty() && !httpCancelled()) image = artworkForGame(game, kind, cache, steam);
        if (image.empty() && emulated && !rom_first) screenScraper();
        if (exists(game, kind)) return {Result::saved};
        if (auto blocked = wait(); blocked.wait_seconds) return blocked;
        if (consumeArtworkRetryableError()) return {Result::retry, 0, tr(L"Błąd połączenia lub zapisu — ponawianie")};
        if (emulated && !ss::load().ready()) return {Result::failed, 0, tr(L"Brak konfiguracji ScreenScraper")};
        return {image.empty() ? Result::missing : Result::failed};
    }};
}

void run(const std::vector<Game>& games, Control& control, const Operations& ops, const Publish& publish) {
    using clock = std::chrono::steady_clock;
    struct Task { Game game; ArtworkKind kind; };
    std::vector<Task> tasks;
    std::set<std::string> seen;
    Progress state;
    HttpCancellation cancellation(control.cancel);
    const auto emit = [&] { publish(state); };
    try {
        for (const auto& game : games) {
            if (control.cancel.load()) break;
            if (!isGame(game) || game.kind == GameKind::catalog || game.visibility != GameVisibility::public_game ||
                !seen.insert(gameKey(game)).second) continue;
            for (const auto kind : kinds) {
                ++state.total;
                if (ops.exists(game, kind)) ++state.existing;
                else tasks.push_back({game, kind});
            }
            if (state.total % 100 == 0) emit();
        }
        state.phase = Phase::ready; emit();
        const auto sleep = [] { std::this_thread::sleep_for(std::chrono::milliseconds(100)); };
        while (!control.cancel.load() && !control.start.load()) sleep();
        double average = 0; size_t samples = 0;
        for (const auto& task : tasks) {
            if (control.cancel.load()) break;
            unsigned retries = 0;
            state.current = task.game.name; state.reason.clear();
            for (;;) {
                bool paused = false;
                while (!control.cancel.load() && control.pause.load()) {
                    if (!paused) { state.phase = Phase::paused; state.eta_seconds.reset(); emit(); paused = true; }
                    sleep();
                }
                if (control.cancel.load()) break;
                state.phase = Phase::running; state.wait_seconds = 0;
                if (samples >= 3) state.eta_seconds = static_cast<uint64_t>(std::ceil(average * (state.total - state.completed())));
                else state.eta_seconds.reset();
                emit();
                // A foreground job may have finished since planning. Never fetch that slot again.
                if (ops.exists(task.game, task.kind)) { ++state.existing; break; }
                const auto started = clock::now();
                const auto result = ops.fetch(task.game, task.kind);
                if (control.cancel.load()) break;
                const auto duration = std::chrono::duration<double>(clock::now() - started).count();
                if (result.result == Result::retry && (result.wait_seconds || retries < 2)) {
                    const auto seconds = result.wait_seconds ? result.wait_seconds : (5ull << retries++);
                    const auto until = clock::now() + std::chrono::seconds(seconds);
                    state.reason = result.reason;
                    uint64_t previous = UINT64_MAX;
                    while (!control.cancel.load() && clock::now() < until) {
                        state.wait_seconds = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(until - clock::now()).count()) + 1;
                        state.phase = control.pause.load() ? Phase::paused : Phase::waiting;
                        if (state.wait_seconds != previous) {
                            previous = state.wait_seconds;
                            state.eta_seconds = samples >= 3 && !control.pause.load() ?
                                std::optional<uint64_t>(previous + static_cast<uint64_t>(std::ceil(average * (state.total - state.completed())))) : std::nullopt;
                            emit();
                        }
                        sleep();
                    }
                    continue;
                }
                average = samples++ ? average * 0.75 + duration * 0.25 : duration;
                state.reason = result.reason;
                if (result.result == Result::saved) ++state.downloaded;
                else if (result.result == Result::missing) ++state.missing;
                else ++state.failed;
                break;
            }
            emit();
        }
        state.phase = control.cancel.load() ? Phase::cancelled : Phase::finished;
    } catch (...) { state.phase = Phase::finished; state.reason = tr(L"Nie można kontynuować pobierania okładek"); ++state.failed; }
    state.wait_seconds = 0; state.eta_seconds.reset(); emit();
}
}
