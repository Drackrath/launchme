#pragma once
// The game index's worker (src/app/game_index.cpp starts it on BackgroundWork): loads the saved
// index, follows library snapshots, reads every cached description of each game once per start
// (and again when a cache is touched), and slowly fills the gaps in the background — the Steam
// store page, GOG's product page, RAWG for the rest — one request every few seconds, paused
// while a game runs. Readers on any thread use read(). The network and the clock are injected.
#include "game_index.hpp"
#include "game_index_sources.hpp"
#include "../net/http.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <unordered_map>

namespace game_index {
// One library game as the launcher shows it.
struct LibraryGame {
    std::string key, cache_key;  // gameKey(), gameCacheKey()
    std::wstring title, store, platform;
    uint32_t app_id = 0;          // Steam games.
    std::string gog_id;           // GOG games.
    bool installed = false, emulated = false;
    uint64_t first_seen = 0, last_played = 0;
    std::optional<uint32_t> playtime_minutes; // Non-Steam play time the launcher measured.
    // Edytuj metadane: the player's own facts, ahead of every source (empty: none of their own).
    std::wstring own_description;
    std::vector<std::wstring> own_developers, own_publishers, own_genres;
    Date own_release;
    std::optional<uint32_t> own_hltb_main; // Minutes.
};
Patch libraryPatch(const LibraryGame& game);

struct ServiceConfig {
    std::filesystem::path config;      // <config>: metadata/, ratings/, plugins/data/…, knowledge/
    std::filesystem::path steam_root;  // appinfo.vdf, play time and achievements.
    std::wstring rawg_key;             // Empty: no RAWG lookups.
    bool english = false;              // Store pages in English (else Polish).
    using Get = std::function<HttpResponse(const std::wstring& url, const std::wstring& headers, size_t limit)>;
    Get get, get_private;              // Public pages; key-bearing URLs (no redirects).
    std::function<uint64_t()> now;     // Unix seconds.
    std::chrono::milliseconds spacing{2500}; // Between background requests.
    size_t requests_per_start = 240;
    bool background = true;            // Fill gaps from the network.
    // A Steam game's local play time and achievements: {minutes, unlocked, total}.
    std::function<std::tuple<std::optional<uint32_t>, uint32_t, uint32_t>(uint64_t account, uint32_t app_id)> steam_stats;
    std::function<void(uint64_t revision)> changed; // After a batch changed the index (worker thread).
};

class Service {
public:
    explicit Service(ServiceConfig config);
    void run();  // The worker's body; returns after stop().
    void stop();
    // From the UI thread (cheap: queued for the worker).
    void library(std::vector<LibraryGame> games, uint64_t steam_account);
    void touch(std::vector<std::string> keys);
    void merge(std::string key, Source source, Patch patch);
    void pause(bool paused);
    // Any thread: the index under a shared lock.
    template <class F> auto read(F&& reader) const {
        std::shared_lock lock(index_mutex_);
        return reader(static_cast<const Index&>(index_));
    }
    bool loaded() const { return loaded_.load(); }
    size_t requests() const { return requests_.load(); }
    std::filesystem::path file() const; // <config>/knowledge/games-v1.json
    bool idle() const;                  // Nothing queued and nothing left to fetch (tests).

private:
    struct Inbox {
        std::optional<std::pair<std::vector<LibraryGame>, uint64_t>> library;
        std::vector<std::string> touched;
        std::vector<std::tuple<std::string, Source, Patch>> patches;
    };
    bool process(Inbox inbox);        // True when the index changed.
    void readOffline(const std::vector<std::string>& keys);
    void loadTags();
    bool fillStep();                  // One background lookup; false when nothing is left.
    void save(bool force);
    bool mergeLocked(const std::string& key, Source source, Patch patch);
    std::optional<LibraryGame> game(const std::string& key) const;
    CacheFolders folders() const;
    std::filesystem::path knowledge() const { return config_.config / L"knowledge"; }
    bool missed(const std::string& cache_key, const char* source, uint64_t days) const;
    void markMissed(const std::string& cache_key, const char* source) const;
    HttpResponse fetch(const std::wstring& url, size_t limit, bool secret);

    ServiceConfig config_;
    mutable std::shared_mutex index_mutex_;
    Index index_;
    mutable std::mutex inbox_mutex_;
    std::condition_variable wake_;
    Inbox inbox_;
    std::atomic_bool alive_{true}, paused_{false}, loaded_{false}, working_{false};
    std::atomic<size_t> requests_{0};
    // Worker-only state.
    std::unordered_map<std::string, LibraryGame> games_;
    uint64_t account_ = 0;
    std::set<std::string> read_;      // Keys whose caches were read this start.
    std::set<std::string> attempted_; // Background lookups tried this start.
    TagNames tags_;
    std::unordered_map<uint32_t, steam_appinfo::App> appinfo_;
    std::set<uint32_t> appinfo_requested_; // The app IDs the last reading looked for.
    std::filesystem::file_time_type appinfo_time_{};
    uint64_t saved_revision_ = 0, notified_revision_ = 0;
    std::chrono::steady_clock::time_point last_save_{}, next_request_{}, backoff_until_{};
    bool tags_checked_ = false, filled_ = false;
};
}
