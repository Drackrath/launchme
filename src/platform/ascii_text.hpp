#pragma once
// ASCII-only text operations for protocol and file-format strings (keys, headers, hosts, IDs):
// byte for byte, no locale. Words people read go through unicode_text.hpp instead.
#include <algorithm>
#include <string>
#include <string_view>

namespace ascii_text {
inline char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }
inline std::string lower(std::string_view text) {
    std::string result(text);
    for (auto& c : result) c = lower(c);
    return result;
}
inline bool equalIgnoreCase(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return lower(x) == lower(y); });
}
// Without the given characters at both ends (by default spaces, tabs and line ends).
inline std::string_view trim(std::string_view text, std::string_view characters = " \t\r\n") {
    const auto first = text.find_first_not_of(characters);
    if (first == std::string_view::npos) return {};
    return text.substr(first, text.find_last_not_of(characters) - first + 1);
}
inline std::wstring_view trim(std::wstring_view text, std::wstring_view characters = L" \t\r\n") {
    const auto first = text.find_first_not_of(characters);
    if (first == std::wstring_view::npos) return {};
    return text.substr(first, text.find_last_not_of(characters) - first + 1);
}
}
