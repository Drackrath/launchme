#pragma once
#include "../platform/text_encoding.hpp"
#include <string>
#include <string_view>

namespace url_encoding {
// RFC 3986 component encoding for WinHTTP URLs. Input bytes are UTF-8;
// only ASCII unreserved characters survive, and spaces become %20 (not '+').
inline std::wstring component(std::string_view bytes) {
    constexpr wchar_t hex[] = L"0123456789ABCDEF";
    std::wstring result;
    result.reserve(bytes.size());
    for (const unsigned char ch : bytes) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.' || ch == '~') result += ch;
        else { result += L'%'; result += hex[ch >> 4]; result += hex[ch & 15]; }
    }
    return result;
}

// Keep the strict UTF-16 conversion policy: malformed text yields an empty value.
inline std::wstring component(std::wstring_view text) { return component(text_encoding::utf8(text)); }
}
