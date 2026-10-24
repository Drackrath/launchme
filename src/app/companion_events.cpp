// Towarzyszka's reactions to the launcher (companion::Event, src/net/companion_events.hpp): a game
// launched here, a finished session (play time, Steam achievements, a completed game, an hours
// milestone), games added, installed or uninstalled, today's premieres from Glance's calendar and a
// late night. game_sessions.cpp and library_view.cpp call the hooks; pumpCompanionEvents() lets
// her say one remark at a time in her home bubble, only while the player is looking.
#include "launcher.hpp"
#include "../library/steam.hpp"
#include "../library/steam_stats.hpp"
#include "../platform/atomic_file.hpp"

#include <fstream>
#include <iterator>
#include <thread>

namespace {
bool english() { return i18n::language() == i18n::Language::en; }
int dateKey(const SYSTEMTIME& time) { return time.wYear * 10000 + time.wMonth * 100 + time.wDay; }
companion::Progress progressOf(const SteamStats& stats) { return {stats.playtime_minutes, stats.achievements_unlocked, stats.achievements_total}; }
// Pauses between her remarks, and how recent the player's input must be for a goodnight.
constexpr uint64_t remark_gap_ms = 6000;
constexpr auto goodnight_attention = std::chrono::seconds(30);
// Steam writes play time and unlocks when a game exits: read them a moment later.
constexpr auto steam_settle = std::chrono::seconds(4);
// HowLongToBeat's main story still ahead in a game, from the game index (0: unknown, not started or done).
uint32_t storyLeft(const std::shared_ptr<game_index::Service>& index, const std::string& key, std::optional<uint32_t> played) {
    if (!index || !index->loaded()) return 0;
    return index->read([&](const game_index::Index& games) -> uint32_t {
        const auto* record = games.get(key);
        if (!record || !record->hltb_main) return 0;
        const uint32_t minutes = played.value_or(record->playtime_minutes.value_or(0));
        return minutes && minutes < *record->hltb_main ? *record->hltb_main - minutes : 0;
    });
}
}

void Launcher::companionNotice(companion::Event event) {
    if (!settings_.companion) return;
    companion_.events.push(std::move(event), GetTickCount64());
}

void Launcher::companionSessionStarted(const Game& game, bool launched_here) {
    if (!settings_.companion || !isGame(game) || game.visibility != GameVisibility::public_game || placeholderGameName(game.name)) return;
    const auto key = gameKey(game);
    companionHush(); // A game starts: she stops talking.
    if (launched_here) {
        companion::Event launch;
        launch.kind = companion::EventKind::launch;
        launch.titles = {game.name};
        companionNotice(std::move(launch));
    }
    // The progress before playing, to tell what this session added.
    if (!steamArtwork(game) || !game.app_id || !account_) { companion_.baselines[key] = {localPlaytime(game), 0, 0}; return; }
    const auto post = [hwnd = hwnd_, alive = alive_](std::unique_ptr<CompanionEventMessage> message) {
        if (alive->load() && PostMessageW(hwnd, companion_event_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    };
    work_.start([post, key, account = account_, app_id = game.app_id] {
        auto message = std::make_unique<CompanionEventMessage>();
        message->kind = CompanionEventMessage::Kind::baseline;
        message->key = key;
        message->progress = progressOf(readSteamStats(findSteamRoot(), account, app_id));
        post(std::move(message));
    });
}

void Launcher::companionSessionEnded(const Game& game, uint64_t played_ms) {
    if (!settings_.companion || !isGame(game) || game.visibility != GameVisibility::public_game || placeholderGameName(game.name)) return;
    const auto key = gameKey(game);
    if (!steamArtwork(game) || !game.app_id || !account_) {
        const auto found = companion_.baselines.find(key);
        const auto before = found == companion_.baselines.end() ? companion::Progress{} : found->second;
        if (found != companion_.baselines.end()) companion_.baselines.erase(found);
        companionAffinityPlayed(key, played_ms, before);
        companionMoodPlayed(key, played_ms);
        if (auto event = companion::sessionEvent(game.name, played_ms, before, {localPlaytime(game), 0, 0})) {
            const auto measured = localPlaytime(game);
            event->story_left = storyLeft(game_index_, key, measured ? std::optional<uint32_t>(measured) : std::nullopt);
            companion_.last_session = *event;
            companion_.last_session_at = GetTickCount64();
            companionNotice(std::move(*event));
        }
        return;
    }
    const auto post = [hwnd = hwnd_, alive = alive_](std::unique_ptr<CompanionEventMessage> message) {
        if (alive->load() && PostMessageW(hwnd, companion_event_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    };
    work_.start([post, alive = alive_, key, title = game.name, played_ms, account = account_, app_id = game.app_id] {
        for (auto waited = std::chrono::milliseconds(0); waited < steam_settle && alive->load(); waited += std::chrono::milliseconds(100))
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto message = std::make_unique<CompanionEventMessage>();
        message->kind = CompanionEventMessage::Kind::session;
        message->key = key; message->title = title; message->played_ms = played_ms;
        message->progress = progressOf(readSteamStats(findSteamRoot(), account, app_id));
        post(std::move(message));
    });
}

std::vector<companion::RosterEntry> Launcher::companionRoster() const {
    std::vector<companion::RosterEntry> roster;
    const auto add = [&](const Game& game) {
        if (!isGame(game) || game.visibility != GameVisibility::public_game || locallyHidden(game)) return;
        auto key = gameKey(game);
        if (key.empty()) return;
        roster.push_back({std::move(key), placeholderGameName(game.name) ? std::wstring{} : game.name, game.installed});
    };
    for (const auto& game : installed_library_) add(game);
    for (const auto& game : store_library_)
        if (!excludedGame(game) && settings_.enabled_pc_sources.contains(sourceSlug(game.source))) add(game);
    if (settings_.enabled_pc_sources.contains(sourceSlug(GameSource::drmfree))) for (const auto& game : drm_library_) add(game);
    for (const auto* list : {&custom_library_, &uninstalled_library_, &shortcut_library_}) for (const auto& game : *list) add(game);
    return roster;
}

void Launcher::companionLibraryChanged(std::vector<std::string> added_keys) {
    if (!settings_.companion) return;
    companion_.added_keys.insert(companion_.added_keys.end(), std::make_move_iterator(added_keys.begin()), std::make_move_iterator(added_keys.end()));
    if (scanning_) return; // A scan in progress shows partial lists; compare whole libraries only.
    for (auto& event : companion_.watch.update(companionRoster(), companion_.added_keys)) companionNotice(std::move(event));
    companion_.added_keys.clear();
}

std::vector<std::wstring> Launcher::glancePremieres(bool english, int today) {
    // companion_premieres.txt: the day she last announced them (once a day, across restarts).
    const auto announced = userConfigDirectory() / L"companion_premieres.txt";
    const auto day = std::to_string(today);
    std::string last;
    std::getline(std::ifstream(announced), last);
    if (last == day) return {};
    const auto feed = glance::loadUpcoming(english, userConfigDirectory() / L"glance", false);
    auto titles = companion::premieresToday(feed.items, today);
    if (!titles.empty()) atomic_file::write(announced, day + "\n");
    return titles;
}

void Launcher::requestCompanionPremieres() {
    if (!settings_.companion || !settings_.glance || companion_.premieres_requested) return; // Glance off: no calendar downloads.
    companion_.premieres_requested = true;
    const auto post = [hwnd = hwnd_, alive = alive_](std::unique_ptr<CompanionEventMessage> message) {
        if (alive->load() && PostMessageW(hwnd, companion_event_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    };
    work_.start([post, premieres = companion_premieres_, en = english(), today = dateKey(companion_.clock())] {
        auto message = std::make_unique<CompanionEventMessage>();
        message->kind = CompanionEventMessage::Kind::premieres;
        message->titles = premieres(en, today);
        if (!message->titles.empty()) post(std::move(message));
    });
}

void Launcher::onCompanionEvent(std::unique_ptr<CompanionEventMessage> message) {
    using Kind = CompanionEventMessage::Kind;
    switch (message->kind) {
    case Kind::baseline:
        companion_.baselines[message->key] = message->progress;
        break;
    case Kind::session: {
        const auto found = companion_.baselines.find(message->key);
        const auto before = found == companion_.baselines.end() ? companion::Progress{} : found->second;
        if (found != companion_.baselines.end()) companion_.baselines.erase(found);
        companionAffinityPlayed(message->key, message->played_ms, before);
        companionMoodPlayed(message->key, message->played_ms);
        if (auto event = companion::sessionEvent(message->title, message->played_ms, before, message->progress)) {
            event->story_left = storyLeft(game_index_, message->key, message->progress.minutes);
            companion_.last_session = *event;
            companion_.last_session_at = GetTickCount64();
            companionNotice(std::move(*event));
        }
        break;
    }
    case Kind::premieres: {
        companion::Event event;
        event.kind = companion::EventKind::premieres;
        event.titles = std::move(message->titles);
        companionNotice(std::move(event));
        break;
    }
    }
}

void Launcher::pumpCompanionEvents() {
    if (companionAsleep(companionLook())) return; // Her night: no remarks (stale ones expire meanwhile).
    const auto now = GetTickCount64();
    // Up late: one goodnight per night, while the player is actually here.
    const auto clock = companion_.clock();
    const bool attentive = std::chrono::steady_clock::now() - last_input_ < goodnight_attention;
    if (companion_.greeted && attentive && clock.wHour < 5 && companion_.goodnight_day != dateKey(clock)) {
        companion_.goodnight_day = dateKey(clock);
        companion::Event night;
        night.kind = companion::EventKind::late_night;
        night.hour = clock.wHour; night.minute = clock.wMinute;
        companionNotice(std::move(night));
    }
    // One remark at a time: after her greeting, with nothing on her mind, a moment after the last.
    const auto& bubble = companion_.bubble;
    if (bubble.waiting || bubble.alpha > 0) { companion_.quiet_until = now + remark_gap_ms; return; }
    if (!companion_.greeted || companion_.busy || now < companion_.quiet_until || !windowWatched()) return;
    const auto event = companion_.events.pop(now);
    if (!event) return;
    companion_.bubble = {}; companion_.bubble.waiting = true; companion_.bubble.alpha = 1;
    companion_.bubble_session = event->kind == companion::EventKind::session;
    companion_.dozing = false;
    const bool en = english();
    requestCompanion(CompanionMessage::Purpose::home, companion::eventKickoff(*event, en),
                     companion::eventLine(companionPersona(), *event, en, companion_.seed + static_cast<uint32_t>(now)));
}
