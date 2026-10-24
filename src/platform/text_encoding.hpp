#pragma once
#include <Windows.h>
#include <string>
#include <string_view>

// The single UTF-8 <-> UTF-16 conversion point. Pick the variant by the trust
// level of the input instead of writing another local helper.
namespace text_encoding {
namespace detail {
inline std::wstring wide(std::string_view value, UINT codepage, DWORD flags) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(codepage, flags, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (!count) return {};
    std::wstring result(count, 0);
    MultiByteToWideChar(codepage, flags, value.data(), static_cast<int>(value.size()), result.data(), count);
    return result;
}
inline std::string utf8(std::wstring_view value, DWORD flags) {
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, flags, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!count) return {};
    std::string result(count, 0);
    WideCharToMultiByte(CP_UTF8, flags, value.data(), static_cast<int>(value.size()), result.data(), count, nullptr, nullptr);
    return result;
}
}

// Strict: malformed input yields an empty string, never a half-decoded value.
inline std::string utf8(std::wstring_view value) { return detail::utf8(value, WC_ERR_INVALID_CHARS); }
inline std::wstring wide(std::string_view value) { return detail::wide(value, CP_UTF8, MB_ERR_INVALID_CHARS); }

// Lenient: malformed sequences become U+FFFD (display text from web APIs,
// file names that may contain lone surrogates).
inline std::string utf8Lenient(std::wstring_view value) { return detail::utf8(value, 0); }
inline std::wstring wideLenient(std::string_view value) { return detail::wide(value, CP_UTF8, 0); }

// Legacy local files (older Steam VDF/shortcuts): UTF-8 when valid, otherwise
// the ANSI code page the file was most likely written in.
inline std::wstring wideLegacy(std::string_view value) {
    auto result = wide(value);
    return result.empty() && !value.empty() ? detail::wide(value, CP_ACP, 0) : result;
}
}
