#pragma once
#include <optional>
#include <string>
#include <string_view>

namespace key_codec {
inline std::string encode(std::string_view value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result; result.reserve(value.size() * 2);
    for (unsigned char ch : value) { result += digits[ch >> 4]; result += digits[ch & 15]; }
    return result;
}
inline std::optional<std::string> decode(std::string_view value) {
    if (value.size() % 2) return {};
    const auto digit = [](char ch) { return ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : -1; };
    std::string result; result.reserve(value.size() / 2);
    for (size_t i = 0; i < value.size(); i += 2) {
        const int high = digit(value[i]), low = digit(value[i + 1]);
        if (high < 0 || low < 0) return {};
        result += static_cast<char>(high * 16 + low);
    }
    return result;
}
}
