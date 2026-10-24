#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

namespace companion {
// Ordered like the three-position slider: the existing style is its default maximum.
enum class ReplyLength { brief, concise, full };
struct ReplyStyle {
    const char* key;
    int percent, words, duo_words;
    int messages_numerator, messages_denominator;
    // Only the visible answer budget scales. Appraisal, thought and reasoning overhead stays separate.
    constexpr int tokens(int base) const { return (base * percent + 99) / 100; }
    constexpr int messageLimit(int base) const {
        return static_cast<int>(std::clamp<int64_t>((static_cast<int64_t>(base) * messages_numerator + messages_denominator - 1) / messages_denominator,
                                                   1, std::numeric_limits<int>::max()));
    }
};
inline constexpr std::array<ReplyStyle, 3> reply_styles{{
    {"brief", 34, 17, 24, 3, 1}, {"concise", 67, 34, 47, 3, 2}, {"full", 100, 50, 70, 1, 1}
}};
inline constexpr int replyLengthIndex(ReplyLength length) {
    const int index = static_cast<int>(length);
    return index >= 0 && index < 3 ? index : 2;
}
inline constexpr const ReplyStyle& replyStyle(ReplyLength length) { return reply_styles[replyLengthIndex(length)]; }
inline std::optional<ReplyLength> replyLengthFromKey(std::string_view key) {
    for (size_t i = 0; i < reply_styles.size(); ++i) if (key == reply_styles[i].key) return static_cast<ReplyLength>(i);
    return {};
}
}
