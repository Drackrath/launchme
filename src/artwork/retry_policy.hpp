#pragma once

#include <chrono>
#include <cstdint>

namespace artwork_retry {
constexpr unsigned max_retries = 3;

inline std::chrono::milliseconds delay(unsigned retry, uint32_t app_id) {
    constexpr int seconds[] = {5, 15, 45};
    return std::chrono::seconds(seconds[retry < max_retries ? retry : max_retries - 1]) +
           std::chrono::milliseconds(app_id % 1000);
}

inline std::chrono::minutes cooldown(bool transient) {
    return transient ? std::chrono::minutes(5) : std::chrono::minutes(30);
}
} // namespace artwork_retry
