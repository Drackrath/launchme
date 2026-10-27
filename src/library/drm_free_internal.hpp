#pragma once
// Helpers shared by the DRM-free catalog's own files (drm_free.cpp: catalog, detection, scan;
// drm_free_titles.cpp: titles and RAWG matching). Not part of the drm_free.hpp interface.
#include "../platform/unicode_text.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace drm_free::detail {
inline constexpr size_t max_name = 200;

inline std::wstring lower(std::wstring_view text) {
    return unicode_text::lower(text);
}

inline std::wstring trim(std::wstring_view text, std::wstring_view characters = L" \t\u00a0") {
    const auto first = text.find_first_not_of(characters);
    if (first == std::wstring_view::npos) return {};
    return std::wstring(text.substr(first, text.find_last_not_of(characters) - first + 1));
}

inline std::vector<std::wstring> words(std::wstring_view title) {
    std::vector<std::wstring> result;
    std::wstring current;
    const auto flush = [&] {
        if (current.empty()) return;
        static const std::pair<const wchar_t*, const wchar_t*> roman[]{{L"ii", L"2"}, {L"iii", L"3"}, {L"iv", L"4"}, {L"vi", L"6"},
            {L"vii", L"7"}, {L"viii", L"8"}, {L"ix", L"9"}, {L"x", L"10"}};
        for (const auto& [numeral, digits] : roman) if (current == numeral && !result.empty()) { current = digits; break; }
        if (current != L"the" || !result.empty()) result.push_back(current);
        current.clear();
    };
    for (const wchar_t ch : lower(title)) {
        if (unicode_text::isLetterOrDigit(ch)) current += ch;
        else if (ch != L'\'' && ch != L'\u2019') flush(); // "Baldur's" stays one word.
    }
    flush();
    return result;
}
}
