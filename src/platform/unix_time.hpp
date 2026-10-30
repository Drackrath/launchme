#pragma once
#include <chrono>
#include <cstdint>

// Wall-clock time as the launcher's files store it.
namespace unix_time {
// Seconds since the Unix epoch (system clock).
inline uint64_t now() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}
}
