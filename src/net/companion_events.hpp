#pragma once
// Towarzyszka's reactions to the launcher and her game roulette, UI-free. What happened becomes an
// Event (a game launched or finished — play time, new achievements, a completed game, an hours
// milestone —, games added, installed or uninstalled, today's premieres, a late night); the
// EventQueue paces her remarks and LibraryWatch finds the library changes. src/app/
// companion_events.cpp feeds them; she says each remark on the home in her bubble, through the
// model (eventKickoff) or offline (eventLine, companion_lines.cpp).
#include "companion.hpp"
#include "glance.hpp"

#include <optional>
#include <unordered_map>

namespace companion {
// level_up: gamification on, her sympathy reached a new level away from the chat (count: the level;
// titles: costumes it unlocked).
enum class EventKind { launch, session, added, installed, uninstalled, premieres, late_night, level_up };
struct Event {
    EventKind kind = EventKind::launch;
    std::vector<std::wstring> titles; // The games concerned (at most a few); premieres: store titles.
    uint32_t count = 0;               // added/installed/uninstalled: how many games in total; level_up: the level.
    uint32_t minutes = 0;             // session: played this time.
    uint32_t achievements = 0;        // session: unlocked this time.
    uint32_t unlocked = 0, total = 0; // session: achievement progress after it (total 0: none known).
    uint32_t milestone = 0;           // session: total play time just passed this many hours.
    bool completed = false;           // session: the last achievement fell this time.
    uint32_t story_left = 0;          // session: minutes of HowLongToBeat's main story still ahead (0: unknown or done).
    int hour = -1, minute = 0;        // late_night: the local time.
};
// A hidden player turn telling her what happened (the model answers in one or two sentences).
std::wstring eventKickoff(const Event& event, bool english);
// Her offline remark (tagged like a model reply).
std::wstring eventLine(Persona persona, const Event& event, bool english, uint32_t seed);

// One game's play time and achievements at a moment (Steam's local files; else minutes only).
struct Progress { std::optional<uint32_t> minutes; uint32_t unlocked = 0, total = 0; };
// What a finished session is worth a remark for; nullopt for one under a minute.
std::optional<Event> sessionEvent(std::wstring title, uint64_t played_ms, const Progress& before, const Progress& after);
// The largest of 1, 5, 10, 25, 50, 100, 250, 500 and 1000 hours passed between two totals; 0: none.
uint32_t milestone(uint32_t minutes_before, uint32_t minutes_after);

// Today's releases in Glance's calendar (most popular first), at most `limit` titles.
std::vector<std::wstring> premieresToday(const std::vector<glance::Item>& calendar, int today_yyyymmdd, size_t limit = 3);

// Every owned game the launcher knows (its key and title; title empty while unnamed).
struct RosterEntry { std::string key; std::wstring title; bool installed = false; };
// Library changes between snapshots: installs and uninstalls of known games, and games new to the
// library (`added_keys`: first seen by the launcher, so also bought while it was closed; more than
// 40 at once is an import, not news). The first update only remembers which games are installed.
class LibraryWatch {
public:
    std::vector<Event> update(const std::vector<RosterEntry>& roster, const std::vector<std::string>& added_keys);
    bool ready() const { return ready_; }
    bool knows(const std::string& key) const { return installed_.contains(key); }
    void reset() { installed_.clear(); ready_ = false; }
private:
    std::unordered_map<std::string, bool> installed_;
    bool ready_ = false;
};

// Paces her remarks: one at a time, the most important first, stale ones dropped; repeats of a
// kind merge (two installs become one remark) and a newer launch, premiere list or late night
// replaces the queued one.
class EventQueue {
public:
    void push(Event event, uint64_t now_ms);
    std::optional<Event> pop(uint64_t now_ms);
    size_t size() const { return entries_.size(); }
    void clear() { entries_.clear(); }
private:
    struct Entry { Event event; uint64_t queued = 0, expires = 0; };
    std::vector<Entry> entries_;
};

// The roulette (chat → "Wylosuj grę"): indices into a pool of games, read as a reel that slows
// down and stops on its last entry (the pick). Neighbours differ when the pool has two games.
std::vector<size_t> spinReel(size_t pool_size, uint32_t seed, size_t length = 28);
std::wstring rouletteKickoff(const std::wstring& title, bool english);
std::wstring rouletteLine(Persona persona, const std::wstring& title, bool english, uint32_t seed);
} // namespace companion
