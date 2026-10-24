#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <utility>

enum class GameActivity { unknown, stopped, running };
enum class SessionResult { waiting, ended, start_failed };
// Creation time guards every stored PID against reuse by an unrelated process.
struct GameProcess {
    uint32_t pid = 0;
    uint64_t created = 0; // FILETIME ticks.
    bool operator==(const GameProcess&) const = default;
};
class GameSession {
public:
    explicit GameSession(uint64_t started = 0) : started_(started) {}
    SessionResult observe(GameActivity activity, uint64_t now) {
        // Only consecutive, confirmed running observations count. Unknown telemetry,
        // startup, suspend and long gaps (sleep/hibernation) never become playtime.
        if (activity == GameActivity::running && !frozen()) {
            if (playtime_sample_ && now >= *playtime_sample_ && now - *playtime_sample_ <= 10000) {
                playtime_delta_ += now - *playtime_sample_;
                played_ms_ += now - *playtime_sample_;
            }
            playtime_sample_ = now;
        } else playtime_sample_.reset();
        if (activity == GameActivity::running) { seen_running_ = true; stopped_since_.reset(); return SessionResult::waiting; }
        if (activity == GameActivity::unknown) { stopped_since_.reset(); return SessionResult::waiting; }
        if (!seen_running_) return now >= started_ && now - started_ >= 120000 ? SessionResult::start_failed : SessionResult::waiting;
        if (!stopped_since_) stopped_since_ = now;
        return now >= *stopped_since_ && now - *stopped_since_ >= 1500 ? SessionResult::ended : SessionResult::waiting;
    }
    // Confirmed by telemetry at least once, not merely dispatched.
    bool running() const { return seen_running_; }
    // Experimental quick resume: the processes the launcher suspended, in RAM.
    bool frozen() const { return !frozen_.empty(); }
    const std::vector<GameProcess>& frozenProcesses() const { return frozen_; }
    void freeze(std::vector<GameProcess> processes) { frozen_ = std::move(processes); playtime_sample_.reset(); }
    void thaw() { frozen_.clear(); playtime_sample_.reset(); }
    uint64_t takePlaytime() { return std::exchange(playtime_delta_, 0); }
    uint64_t playedMs() const { return played_ms_; }
    uint64_t startedAt() const { return started_; }
private:
    uint64_t started_ = 0;
    bool seen_running_ = false;
    std::optional<uint64_t> stopped_since_;
    std::vector<GameProcess> frozen_;
    std::optional<uint64_t> playtime_sample_;
    uint64_t playtime_delta_ = 0;
    uint64_t played_ms_ = 0;
};

enum class SessionState { none, running, frozen };
inline SessionState sessionState(const std::map<std::string, GameSession>& sessions, const std::string& key) {
    if (sessions.empty() || key.empty()) return SessionState::none;
    const auto found = sessions.find(key);
    if (found == sessions.end()) return SessionState::none;
    return found->second.frozen() ? SessionState::frozen : found->second.running() ? SessionState::running : SessionState::none;
}
