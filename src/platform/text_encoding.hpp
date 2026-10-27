#pragma once
#ifdef _WIN32
#include <Windows.h>
#endif
#include <cstdint>
#include <string>
#include <string_view>

// The single UTF-8 <-> wide conversion point. Pick the variant by the trust
// level of the input instead of writing another local helper. Wide strings are
// UTF-16 on Windows and UTF-32 elsewhere (wchar_t's width).
namespace text_encoding {
namespace detail {
#ifdef _WIN32
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
inline std::wstring strictWide(std::string_view value) { return wide(value, CP_UTF8, MB_ERR_INVALID_CHARS); }
inline std::wstring lenientWide(std::string_view value) { return wide(value, CP_UTF8, 0); }
inline std::string strictUtf8(std::wstring_view value) { return utf8(value, WC_ERR_INVALID_CHARS); }
inline std::string lenientUtf8(std::wstring_view value) { return utf8(value, 0); }
inline std::wstring ansiWide(std::string_view value) { return wide(value, CP_ACP, 0); }
#else
static_assert(sizeof(wchar_t) == 4, "non-Windows builds expect UTF-32 wchar_t");
constexpr char32_t replacement = 0xFFFD;

// Decodes one scalar value at value[at]; `length` is how many bytes it took (at least 1).
// Overlong forms, surrogates and values past U+10FFFF are malformed.
inline bool decode(std::string_view value, size_t at, char32_t& code, size_t& length) {
    const auto byte = [&](size_t i) { return static_cast<uint8_t>(value[i]); };
    const uint8_t lead = byte(at);
    length = 1;
    if (lead < 0x80) { code = lead; return true; }
    size_t extra; char32_t minimum;
    if ((lead & 0xE0) == 0xC0) { extra = 1; minimum = 0x80; code = lead & 0x1F; }
    else if ((lead & 0xF0) == 0xE0) { extra = 2; minimum = 0x800; code = lead & 0x0F; }
    else if ((lead & 0xF8) == 0xF0) { extra = 3; minimum = 0x10000; code = lead & 0x07; }
    else return false;
    for (size_t i = 1; i <= extra; ++i) {
        if (at + i >= value.size() || (byte(at + i) & 0xC0) != 0x80) return false;
        code = (code << 6) | (byte(at + i) & 0x3F);
        length = i + 1;
    }
    return code >= minimum && code <= 0x10FFFF && (code < 0xD800 || code > 0xDFFF);
}
inline std::wstring wide(std::string_view value, bool strict) {
    std::wstring result;
    result.reserve(value.size());
    for (size_t at = 0; at < value.size();) {
        char32_t code; size_t length;
        if (!decode(value, at, code, length)) {
            if (strict) return {};
            code = replacement;
        }
        result.push_back(static_cast<wchar_t>(code));
        at += length;
    }
    return result;
}
inline std::string utf8(std::wstring_view value, bool strict) {
    std::string result;
    result.reserve(value.size());
    for (const wchar_t character : value) {
        auto code = static_cast<char32_t>(character);
        if (code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) {
            if (strict) return {};
            code = replacement;
        }
        if (code < 0x80) result.push_back(static_cast<char>(code));
        else if (code < 0x800) {
            result.push_back(static_cast<char>(0xC0 | (code >> 6)));
            result.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else if (code < 0x10000) {
            result.push_back(static_cast<char>(0xE0 | (code >> 12)));
            result.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            result.push_back(static_cast<char>(0xF0 | (code >> 18)));
            result.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }
    return result;
}
inline std::wstring strictWide(std::string_view value) { return wide(value, true); }
inline std::wstring lenientWide(std::string_view value) { return wide(value, false); }
inline std::string strictUtf8(std::wstring_view value) { return utf8(value, true); }
inline std::string lenientUtf8(std::wstring_view value) { return utf8(value, false); }
// No system ANSI code page: the files come from Windows installs, most likely Windows-1252.
inline std::wstring ansiWide(std::string_view value) {
    static constexpr char16_t high[32] = {
        0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD,
        0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178};
    std::wstring result;
    result.reserve(value.size());
    for (const char character : value) {
        const auto byte = static_cast<uint8_t>(character);
        result.push_back(static_cast<wchar_t>(byte >= 0x80 && byte < 0xA0 ? high[byte - 0x80] : byte));
    }
    return result;
}
#endif
}

// Strict: malformed input yields an empty string, never a half-decoded value.
inline std::string utf8(std::wstring_view value) { return value.empty() ? std::string{} : detail::strictUtf8(value); }
inline std::wstring wide(std::string_view value) { return value.empty() ? std::wstring{} : detail::strictWide(value); }

// Lenient: malformed sequences become U+FFFD (display text from web APIs,
// file names that may contain lone surrogates).
inline std::string utf8Lenient(std::wstring_view value) { return value.empty() ? std::string{} : detail::lenientUtf8(value); }
inline std::wstring wideLenient(std::string_view value) { return value.empty() ? std::wstring{} : detail::lenientWide(value); }

// Legacy local files (older Steam VDF/shortcuts): UTF-8 when valid, otherwise
// the ANSI code page the file was most likely written in.
inline std::wstring wideLegacy(std::string_view value) {
    auto result = wide(value);
    return result.empty() && !value.empty() ? detail::ansiWide(value) : result;
}
}
