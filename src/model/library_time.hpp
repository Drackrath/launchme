#pragma once
#include <charconv>
#include "../platform/unix_time.hpp"
#include <chrono>
#include <cstdint>
#include <string_view>

// Acquisition dates only: never substitute a file mtime, catalog release or metadata update.
namespace library_time {
inline uint64_t now() {
    return unix_time::now();
}
inline uint64_t seconds(uint64_t value, uint64_t current = now()) { return value <= current ? value : 0; }
inline uint64_t seconds(std::string_view text, uint64_t current = now()) {
    if (text.empty()) return 0;
    uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size() ? seconds(value, current) : 0;
}
// Epic's library service returns UTC ISO 8601, with optional fractional seconds.
inline uint64_t utc(std::string_view text, uint64_t current = now()) {
    if (text.size() < 20 || text.size() > 30 || text[4] != '-' || text[7] != '-' || text[10] != 'T' ||
        text[13] != ':' || text[16] != ':' || text.back() != 'Z') return 0;
    if (text.size() != 20 && (text.size() < 22 || text[19] != '.' ||
        text.substr(20, text.size() - 21).find_first_not_of("0123456789") != std::string_view::npos)) return 0;
    const auto part = [&](size_t at, size_t count) {
        int value = 0;
        const auto slice = text.substr(at, count);
        if (slice.find_first_not_of("0123456789") != slice.npos) return -1;
        const auto [end, error] = std::from_chars(slice.data(), slice.data() + slice.size(), value);
        return error == std::errc{} && end == slice.data() + slice.size() ? value : -1;
    };
    const int y = part(0, 4), m = part(5, 2), d = part(8, 2), h = part(11, 2), min = part(14, 2), s = part(17, 2);
    if (y < 1970 || m < 1 || d < 1 || h < 0 || h > 23 || min < 0 || min > 59 || s < 0 || s > 59) return 0;
    const std::chrono::year_month_day day{std::chrono::year{y}, std::chrono::month{static_cast<unsigned>(m)}, std::chrono::day{static_cast<unsigned>(d)}};
    if (!day.ok()) return 0;
    const auto value = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::sys_days{day}.time_since_epoch()).count() + h * 3600 + min * 60 + s;
    return seconds(static_cast<uint64_t>(value), current);
}
}
