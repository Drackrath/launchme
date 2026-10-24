#pragma once
// Towarzyszka's tools (Gemini function calling): instead of the whole library in her prompt, the
// model asks the local game index — search with filters and sorting, one game's details, library
// statistics, similar games — and may note something about the player (memory). Declarations go
// into generateContent's "tools"; run() answers one call with a compact JSON object (≤ 20 games).
// Pure: no network, no window; the caller holds the index's lock.
#include "json.hpp"
#include "../library/game_index.hpp"

#include <functional>
#include <string>
#include <vector>

namespace companion::tools {
inline constexpr const char* search = "szukaj_w_bibliotece";
inline constexpr const char* details = "szczegoly_gry";
inline constexpr const char* statistics = "statystyki";
inline constexpr const char* similar = "podobne_gry";
inline constexpr const char* remember = "zapamietaj";
inline constexpr const char* recall = "przypomnij";  // Her earlier conversations with the player.
inline constexpr const char* news = "wiadomosci";    // What Glance already holds: news of their games, premieres, headlines.
inline constexpr const char* plan = "plan_grania";   // What fits into the time the player has.
inline constexpr const char* web = "szukaj_w_sieci";  // The web (Tavily), only when a key is there (web_search.hpp).
inline constexpr const char* achievements = "osiagniecia_gry";      // Detailed local Steam achievements of a game.
inline constexpr const char* friends = "znajomi_online";            // Steam friends online / in a game right now.
inline constexpr const char* collections = "kolekcje_i_ulubione";   // View or organize Steam collections and favorites.
inline constexpr const char* history = "historia_grania";           // Games played in the last N days.
inline constexpr const char* roulette = "zakrec_ruletka";           // Spin the chat roulette wheel with filters.

// The functionDeclarations array (JSON). `notes`: the memory tool too.
std::string declarations(bool notes);
// The four library tools and the chosen others.
struct Features {
    bool notes = false, recall = false, news = false, plan = false, web = false;
    bool achievements = false, friends = false, collections = false, history = false, roulette = false;
};
std::string declarations(const Features& features);

struct AchievementItem {
    std::wstring name, description;
    bool achieved = false, hidden = false;
    uint64_t unlocked_at = 0; // Unix seconds; 0 when locked or unknown.
    float global_percent = -1; // Share of players (0–100); -1 unknown.
};
struct AchievementSummary {
    uint32_t unlocked = 0, total = 0;
    std::vector<AchievementItem> items;
};

struct FriendOnline {
    std::wstring name;
    std::wstring state; // "in_game", "online", "away"
    std::wstring game;  // Game title when in_game.
    uint32_t app_id = 0;
};

struct CollectionInfo {
    std::wstring name;
    bool steam = true;     // Synced Steam collection (default) vs launcher-only local collection.
    bool favorites = false;
    std::vector<std::wstring> games;
};
struct CollectionMutation {
    enum class Action { create_or_add, remove, favorite, unfavorite } action = Action::create_or_add;
    std::wstring collection;
    bool steam = true; // Defaults to a Steam collection.
    std::vector<std::string> keys;
    std::vector<std::wstring> titles;
};

struct Context {
    bool english = false;
    uint64_t now = 0;  // Unix seconds (recently played, "days ago").
    uint32_t seed = 0; // Random picks.
    // zapamietaj: stores a note about the player (and optionally how she felt about it); false when memory is off or refused.
    std::function<bool(const std::wstring& note)> remember;
    std::function<bool(const std::wstring& note, const std::string& emotion, const std::wstring& feeling)> remember_affective;
    // przypomnij and wiadomosci: answered by the app (her chat files, Glance's cache); empty: unavailable.
    std::function<json::Value(const std::wstring& topic)> recall;
    std::function<json::Value()> news;
    // szukaj_w_sieci: the web search's answer for the query (on this worker); empty: no key.
    std::function<json::Value(const std::wstring& query)> web;
    // osiagniecia_gry: local Steam achievement schema/progress for a game.
    std::function<AchievementSummary(uint32_t app_id, const std::string& key)> achievements;
    // znajomi_online: Steam friends currently online or in a game.
    std::function<std::vector<FriendOnline>()> friends;
    // kolekcje_i_ulubione: read existing shelves/favorites and queue a collection change (Steam by default).
    std::function<std::vector<CollectionInfo>()> collections;
    std::function<bool(const CollectionMutation&)> mutate_collection;
    // zakrec_ruletka: starts the visual roulette reel in the chat with the chosen titles and winning key.
    std::function<void(const std::vector<std::wstring>& reel, const std::string& key)> roulette;
};
// One call's answer (a JSON object). Unknown tools and bad arguments answer {"error": …}.
// przypomnij and wiadomosci never touch the index: callers may run them without its lock (needsIndex).
json::Value run(std::string_view name, const json::Value& args, const game_index::Index& index, const Context& context);
bool needsIndex(std::string_view name);

// przypomnij's answer: lines of earlier conversations and her notes after them that match.
struct Recalled { std::wstring when; bool player = false; std::wstring text; };
json::Value recallAnswer(const std::vector<Recalled>& lines, const std::vector<Recalled>& summaries);
// wiadomosci's answer from Glance's feeds: news of the player's own games, premieres of the coming
// days (`today` yyyymmdd) and the industry's headlines; each list short.
struct Headline { std::wstring title, source; int day = 0; uint64_t published = 0; }; // day: yyyymmdd of a premiere, else 0.
json::Value newsAnswer(const std::vector<Headline>& owned, const std::vector<Headline>& premieres, const std::vector<Headline>& headlines, int today, uint64_t now);
json::Value achievementsAnswer(std::wstring_view title, const AchievementSummary& summary, uint64_t now);
json::Value friendsAnswer(const std::vector<FriendOnline>& list, const game_index::Index& index);

// The game the player is in the middle of (played in the last 10 days, a good part of the way
// through its HowLongToBeat main story, not completed), newest first.
std::vector<const game_index::Record*> inProgress(const game_index::Index& index, uint64_t now, size_t limit = 5);
// That game as a line for her prompt (PromptExtras::playing); empty when there is none.
std::wstring playingText(const game_index::Index& index, uint64_t now, bool english);

// A game as a tool shows it: title, year, store, genres, top tags, studio, state, play time,
// HowLongToBeat, scores; empty fields left out. `full` adds the description, publishers, series
// and every tag.
json::Value describe(const game_index::Record& record, const Context& context, bool full = false);
}
