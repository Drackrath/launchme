// The game index's glue (the work is in src/library/game_index_service.cpp): the worker starts
// with the launcher and reads the saved index; library changes go to it once they settle; the
// places that fetch or learn something about a game (Szczegóły, list previews, ratings, RAWG,
// ES-DE/ScreenScraper, HowLongToBeat, ended sessions) tell it. Its own network lookups start
// 20 s after the launcher and pause while a game runs.
#include "launcher.hpp"
#include "../library/game_index_sources.hpp"
#include "../library/steam.hpp"
#include "../library/steam_stats.hpp"
#include "../platform/user_config.hpp"

namespace {
constexpr ULONGLONG fill_delay_ms = 20000;               // After the start, before network lookups.
constexpr auto settle = std::chrono::milliseconds(1500); // Library changes come in bursts.
// Game lengths: one HowLongToBeat lookup every 12 s at most (the plugin answers from its own
// 7-day cache without the network); a 10-minute pause after six lookups in a row found nothing
// (plugin_game_views.cpp).
constexpr ULONGLONG length_spacing_ms = 12000;
constexpr const char* hltb_plugin = "launchme.howlongtobeat";
}

void Launcher::startGameIndex() {
    if (game_index_) return;
    game_index::ServiceConfig config;
    config.config = userConfigDirectory();
    config.steam_root = findSteamRoot();
    config.rawg_key = userSecret(L"RAWG_API_KEY", L"rawg_api_key.txt");
    config.english = i18n::english();
    config.steam_stats = [root = config.steam_root](uint64_t account, uint32_t app_id) {
        const auto stats = readSteamStats(root, account, app_id);
        return std::tuple<std::optional<uint32_t>, uint32_t, uint32_t>{stats.playtime_minutes, stats.achievements_unlocked, stats.achievements_total};
    };
    // One message waits in the queue at a time; its handler reads the latest state anyway.
    config.changed = [hwnd = hwnd_, alive = alive_, posted = game_index_posted_](uint64_t revision) {
        if (!alive->load() || posted->exchange(true)) return;
        auto message = std::make_unique<GameIndexMessage>();
        message->revision = revision;
        if (PostMessageW(hwnd, game_index_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
        else posted->store(false);
    };
    if (game_index_configure_) game_index_configure_(config);
    game_index_ = std::make_shared<game_index::Service>(std::move(config));
    game_index_->pause(true); // Network lookups wait until the launcher has settled (tickGameIndex).
    game_index_started_ = GetTickCount64();
    work_.start([service = game_index_] {
        SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
        service->run();
        SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
    });
}

std::vector<game_index::LibraryGame> Launcher::gameIndexGames() const {
    std::vector<game_index::LibraryGame> games;
    games.reserve(public_library_.size());
    for (const auto& game : public_library_) {
        if (!isGame(game) || game.kind == GameKind::catalog || game.visibility != GameVisibility::public_game || locallyHidden(game) ||
            placeholderGameName(game.name) || game.name.empty()) continue;
        game_index::LibraryGame item;
        item.key = gameKey(game);
        if (item.key.empty()) continue;
        item.cache_key = gameCacheKey(game);
        item.title = game.name;
        item.store = sourceName(game.source);
        if (game.kind == GameKind::nonsteam && !game.platform.empty()) item.platform = game.platform;
        item.app_id = steamArtwork(game) ? game.app_id : 0;
        if (game.source == GameSource::gog) item.gog_id = game.source_id;
        item.installed = game.installed;
        item.emulated = game.kind == GameKind::nonsteam && !game.rom.empty();
        item.first_seen = library_view::addedTime(game, library_state_.first_seen); // The index's persisted name for its "added" sort.
        item.last_played = game.last_played;
        const auto history = recent_games::historyKey(game, account_, gog_state_.account, epic_state_.account);
        if (const auto found = library_state_.last_played.find(history); found != library_state_.last_played.end())
            item.last_played = std::max(item.last_played, found->second);
        if (!item.app_id) if (const auto minutes = gamePlaytime(game).value_or(0)) item.playtime_minutes = minutes;
        applyMetadataToIndex(item, game); // Edytuj metadane.
        games.push_back(std::move(item));
    }
    return games;
}

void Launcher::syncGameIndex() {
    game_index_dirty_ = true;
    game_index_changed_ = std::chrono::steady_clock::now();
}

void Launcher::tickGameIndex() {
    if (!game_index_) return;
    refreshTitleLookups();
    const auto now = std::chrono::steady_clock::now();
    if (game_index_dirty_ && !scanning_ && now - game_index_changed_ >= settle) {
        game_index_dirty_ = false;
        game_index_->library(gameIndexGames(), account_);
    }
    // Ended sessions: Steam writes the new play time a little later.
    if (!game_index_later_.empty() && GetTickCount64() >= game_index_later_at_) gameIndexTouch(std::exchange(game_index_later_, {}));
    // Background lookups once the start has settled; never while a game runs.
    const bool fill = GetTickCount64() - game_index_started_ >= fill_delay_ms && !anyActiveSession() && !scanning_;
    if (fill != game_index_filling_) {
        game_index_filling_ = fill;
        game_index_->pause(!fill);
    }
    fillGameLengths();
}

void Launcher::fillGameLengths() {
    const auto now = GetTickCount64();
    if (now < length_fill_at_ || !game_index_filling_ || !game_index_->loaded() || !runtime_started_ || plugins_busy_ || plugin_command_pending_) return;
    length_fill_at_ = now + length_spacing_ms;
    const auto* plugin = plugins_.find(hltb_plugin);
    if (!plugin || !plugin->enabled || !plugin->error.empty()) return;
    {
        std::lock_guard lock(plugin_jobs_->mutex);
        if (!plugin_jobs_->jobs.empty()) return; // What the player opened goes first.
    }
    // The next owned game the index has no length for: installed, then the most recently played.
    std::vector<const Game*> wanted;
    game_index_->read([&](const game_index::Index& index) {
        for (const auto& game : public_library_) {
            if (!isGame(game) || game.kind == GameKind::catalog || game.visibility != GameVisibility::public_game || locallyHidden(game) ||
                game.name.empty() || placeholderGameName(game.name)) continue;
            const auto key = gameKey(game);
            if (key.empty() || length_fill_tried_.contains(key)) continue;
            const auto* record = index.get(key);
            if (record && record->hltb_main) continue;
            wanted.push_back(&game);
        }
    });
    const auto best = std::min_element(wanted.begin(), wanted.end(), [](const Game* a, const Game* b) {
        if (a->installed != b->installed) return a->installed;
        return a->last_played > b->last_played;
    });
    if (best == wanted.end()) { length_fill_at_ = now + 10 * 60 * 1000; return; } // Nothing left: a look now and then for new games.
    const Game game = **best;
    length_fill_tried_.insert(gameKey(game));
    for (const auto& action : plugins_.contributions("details", &game)) {
        if (action.plugin != hltb_plugin) continue;
        plugins::Job job;
        job.kind = plugins::Job::Kind::details; job.plugin = action.plugin; job.key = action.item.id; job.game = game;
        job.background = true;
        job.value = pluginGameParams(game);
        job.value.properties["command"] = plugins::wire::string(action.item.command);
        job.value.properties["refresh"] = plugins::wire::boolean(false);
        submitPlugin(std::move(job));
        return;
    }
}

void Launcher::gameIndexTouch(std::vector<std::string> keys) {
    std::erase_if(keys, [](const std::string& key) { return key.empty(); });
    if (game_index_ && !keys.empty()) game_index_->touch(std::move(keys));
}

void Launcher::gameIndexMerge(const std::string& key, game_index::Source source, game_index::Patch patch) {
    if (game_index_ && !key.empty()) game_index_->merge(key, source, std::move(patch));
}

bool Launcher::completeDetails(GameDetails& details, const Game& game) const {
    if (!game_index_) return false;
    const auto key = gameKey(game);
    const bool en = i18n::english();
    return game_index_->read([&](const game_index::Index& index) {
        const auto* record = index.get(key);
        if (!record) return false;
        const auto join = [](const std::vector<std::wstring>& list, bool genres, bool english) {
            std::wstring out;
            for (size_t i = 0; i < list.size() && i < 4; ++i) out += (i ? L", " : L"") + (genres ? game_index::genreLabel(list[i], english) : list[i]);
            return out;
        };
        bool filled = false;
        const auto fill = [&](std::wstring& field, std::wstring value) { if (field.empty() && !value.empty()) { field = std::move(value); filled = true; } };
        fill(details.description, record->description);
        fill(details.release_date, game_index::dateLabel(record->release, en));
        fill(details.developer, join(record->developers, false, en));
        fill(details.publisher, join(record->publishers, false, en));
        fill(details.genres, join(record->genres, true, en));
        // Achievements of a game outside Steam (an achievements plugin's counts).
        if (!details.achievements_total && record->achievements_total) {
            details.achievements_total = record->achievements_total; details.achievements_unlocked = record->achievements_unlocked; filled = true;
        }
        // What it knows now is a description, not "none available".
        if (filled && !details.description.empty()) details.error.clear();
        return filled;
    });
}

void Launcher::onGameIndex(std::unique_ptr<GameIndexMessage> message) {
    game_index_posted_->store(false);
    game_index_revision_ = message->revision;
    // Szczegóły left with gaps fill in as the index learns more.
    if (screen_ == ScreenMode::details && !detail_games_.empty() && !details_loading_ && completeDetails(details_, detail_games_[0]))
        InvalidateRect(hwnd_, nullptr, FALSE);
    // Wydawcy and Developerzy follow what it learns of each game's studios; a game it now knows
    // complete takes its gold border.
    const auto studios_before = studio_groups_;
    if (!refreshStudios()) return;
    if (studio_groups_ != studios_before && !scanning_) refreshRecentGames();
    else InvalidateRect(hwnd_, nullptr, FALSE);
}
