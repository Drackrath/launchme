#pragma once
#include "steam.hpp"
#include <chrono>
#include <functional>
#include <optional>
#include <span>

// Appinfo is only a list of candidates, never evidence of account ownership.
std::optional<std::vector<uint32_t>> steamAppInfoIds(std::span<const uint8_t> bytes);
std::optional<std::vector<Game>> clientSteamOwnedGames(const std::filesystem::path& root, uint64_t account);
void mergeSteamOwnedGames(SteamLibrary& library, const std::vector<Game>& owned);

// Worker-owned, account-scoped snapshot. Refresh at most every 30 s; keep the last
// successful snapshot while Steam is closed/offline, including across launcher restarts.
class SteamOwnedGames {
public:
    using Fetch = std::function<std::optional<std::vector<Game>>(const std::filesystem::path&, uint64_t)>;
    SteamOwnedGames(std::filesystem::path root, std::filesystem::path cache, Fetch fetch = clientSteamOwnedGames)
        : root_(std::move(root)), cache_(std::move(cache)), fetch_(std::move(fetch)) {}
    const std::vector<Game>& read(uint64_t account, bool force = false);
    // The last snapshot (from disk after an account change), without asking Steam.
    const std::vector<Game>& cached(uint64_t account);
    // Whether the last read() that asked Steam got an answer (false while Steam is closed).
    bool answered() const { return answered_; }
private:
    bool answered_ = false;
    std::filesystem::path root_, cache_;
    Fetch fetch_;
    uint64_t account_ = 0;
    std::chrono::steady_clock::time_point next_{};
    std::vector<Game> games_;
};
