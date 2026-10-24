#pragma once
#include "cover_cache.hpp"
#include <atomic>
#include <functional>
#include <optional>
#include <array>

namespace bulk_covers {
inline constexpr std::array kinds{ArtworkKind::cover, ArtworkKind::poster, ArtworkKind::landscape};
enum class Phase { scanning, ready, running, paused, waiting, finished, cancelled };
struct Progress {
    Phase phase = Phase::scanning;
    size_t total = 0, existing = 0, downloaded = 0, missing = 0, failed = 0;
    uint64_t wait_seconds = 0;
    std::optional<uint64_t> eta_seconds;
    std::wstring current, reason;
    size_t completed() const { return existing + downloaded + missing + failed; }
    bool terminal() const { return phase == Phase::finished || phase == Phase::cancelled; }
};
struct Control { std::atomic_bool start{false}, pause{false}, cancel{false}; };
enum class Result { saved, missing, failed, retry };
struct Attempt { Result result = Result::missing; uint64_t wait_seconds = 0; std::wstring reason; };
struct Operations {
    std::function<bool(const Game&, ArtworkKind)> exists;
    std::function<Attempt(const Game&, ArtworkKind)> fetch;
};
using Publish = std::function<void(const Progress&)>;
Operations operations(const std::filesystem::path& cache, const std::filesystem::path& steam, bool rom_first);
void run(const std::vector<Game>& games, Control& control, const Operations& operations, const Publish& publish);
}
