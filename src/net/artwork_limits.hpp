#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>

// Millisecond monotonic clock. Pure state machine; the HTTP transport owns its mutex.
struct ArtworkLimit {
    uint64_t next = 0, blocked = 0;
    uint64_t delay(uint64_t now) const { const auto until = std::max(next, blocked); return until > now ? until - now : 0; }
    void reserve(uint64_t now) { next = now + 1100; } // Conservative default, shared by all SGDB callers.
    void observe(uint64_t now, uint32_t status, uint64_t retry_ms,
                 std::optional<uint64_t> remaining, uint64_t reset_ms) {
        if (status == 429 || (remaining && !*remaining))
            blocked = std::max(blocked, now + std::max<uint64_t>(retry_ms, reset_ms ? reset_ms : 60000));
    }
};
