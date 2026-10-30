#include "unicode_text.hpp"

#ifdef _WIN32
#include <Windows.h>
#else
#include <unicode/coll.h>
#include <unicode/normalizer2.h>
#include <unicode/uchar.h>
#include <unicode/unistr.h>

#include <memory>
#endif

#include <algorithm>
#include <cstdint>
#include <cwchar>

namespace unicode_text {
#ifdef _WIN32
namespace {
std::wstring normalize(std::wstring_view text, NORM_FORM form) {
    if (text.empty()) return {};
    std::wstring result;
    if (const int size = NormalizeString(form, text.data(), static_cast<int>(text.size()), nullptr, 0); size > 0) {
        result.resize(static_cast<size_t>(size));
        const int written = NormalizeString(form, text.data(), static_cast<int>(text.size()), result.data(), size);
        result.resize(written > 0 ? static_cast<size_t>(written) : 0);
    }
    return result.empty() ? std::wstring(text) : result;
}
WORD type(wchar_t c, DWORD kind) {
    WORD value = 0;
    GetStringTypeW(kind, &c, 1, &value);
    return value;
}
}

std::wstring decompose(std::wstring_view text) { return normalize(text, NormalizationD); }
std::wstring decomposeCompatible(std::wstring_view text) {
    if (text.empty()) return {};
    const auto size = FoldStringW(MAP_COMPOSITE | MAP_FOLDCZONE, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) return std::wstring(text);
    std::wstring folded(static_cast<size_t>(size), L'\0');
    if (!FoldStringW(MAP_COMPOSITE | MAP_FOLDCZONE, text.data(), static_cast<int>(text.size()), folded.data(), size)) return std::wstring(text);
    return folded;
}
std::wstring lower(std::wstring_view text) {
    std::wstring result(text);
    if (!text.empty()) LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, text.data(), static_cast<int>(text.size()),
                                     result.data(), static_cast<int>(result.size()), nullptr, nullptr, 0);
    return result;
}
bool isLetter(wchar_t c) { return type(c, CT_CTYPE1) & C1_ALPHA; }
bool isDigit(wchar_t c) { return type(c, CT_CTYPE1) & C1_DIGIT; }
bool isMark(wchar_t c) { return type(c, CT_CTYPE3) & (C3_NONSPACING | C3_DIACRITIC); }
int compareIgnoreCase(std::wstring_view a, std::wstring_view b) {
    const int result = CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE);
    return result ? result - CSTR_EQUAL : 0;
}
int compareNatural(std::wstring_view a, std::wstring_view b) {
    const int result = CompareStringEx(LOCALE_NAME_USER_DEFAULT, LINGUISTIC_IGNORECASE | SORT_DIGITSASNUMBERS,
                                       a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), nullptr, nullptr, 0);
    if (result) return result - CSTR_EQUAL;
    return compareIgnoreCase(a, b);
}
#else
// wchar_t is UTF-32 here; ICU works in UTF-16.
namespace {
icu::UnicodeString unicode(std::wstring_view text) {
    return icu::UnicodeString::fromUTF32(reinterpret_cast<const UChar32*>(text.data()), static_cast<int32_t>(text.size()));
}
std::wstring wide(const icu::UnicodeString& text) {
    std::wstring result(static_cast<size_t>(text.countChar32()), L'\0');
    UErrorCode status = U_ZERO_ERROR;
    const int32_t length = text.toUTF32(reinterpret_cast<UChar32*>(result.data()), static_cast<int32_t>(result.size()), status);
    result.resize(U_SUCCESS(status) ? static_cast<size_t>(length) : 0);
    return result;
}
std::wstring normalize(std::wstring_view text, const icu::Normalizer2* (*instance)(UErrorCode&)) {
    if (text.empty()) return {};
    UErrorCode status = U_ZERO_ERROR;
    const auto* normalizer = instance(status);
    if (U_FAILURE(status)) return std::wstring(text);
    const auto result = normalizer->normalize(unicode(text), status);
    return U_SUCCESS(status) ? wide(result) : std::wstring(text);
}
// One collator per thread: ICU collators are not safe to share while comparing.
icu::Collator* collator() {
    thread_local std::unique_ptr<icu::Collator> value = [] {
        UErrorCode status = U_ZERO_ERROR;
        // C and POSIX environments (LANG=C.UTF-8 in containers and WSL) map to en_US_POSIX, whose
        // collation is code-point order ("Beta" before "alpha", "Łódź" after "Zelda"); the root
        // collation orders letters as people read them.
        auto locale = icu::Locale::getDefault();
        if (std::string_view(locale.getVariant()) == "POSIX" || !*locale.getLanguage()) locale = icu::Locale::getRoot();
        std::unique_ptr<icu::Collator> made(icu::Collator::createInstance(locale, status));
        if (U_FAILURE(status) || !made) return std::unique_ptr<icu::Collator>{};
        made->setStrength(icu::Collator::SECONDARY); // Accents count, case does not.
        made->setAttribute(UCOL_NUMERIC_COLLATION, UCOL_ON, status);
        return made;
    }();
    return value.get();
}
}

std::wstring decompose(std::wstring_view text) { return normalize(text, icu::Normalizer2::getNFDInstance); }
std::wstring decomposeCompatible(std::wstring_view text) { return normalize(text, icu::Normalizer2::getNFKDInstance); }
std::wstring lower(std::wstring_view text) {
    std::wstring result(text);
    for (auto& c : result) c = static_cast<wchar_t>(u_tolower(static_cast<UChar32>(c)));
    return result;
}
bool isLetter(wchar_t c) { return u_isalpha(static_cast<UChar32>(c)); }
bool isDigit(wchar_t c) { return u_isdigit(static_cast<UChar32>(c)); }
bool isMark(wchar_t c) {
    const auto category = u_charType(static_cast<UChar32>(c));
    return category == U_NON_SPACING_MARK || category == U_ENCLOSING_MARK || u_hasBinaryProperty(static_cast<UChar32>(c), UCHAR_DIACRITIC);
}
int compareIgnoreCase(std::wstring_view a, std::wstring_view b) {
    const size_t count = std::min(a.size(), b.size());
    for (size_t i = 0; i < count; ++i) {
        // CompareStringOrdinal's ignore-case: both sides uppercased, simple mappings.
        const auto left = u_toupper(static_cast<UChar32>(a[i])), right = u_toupper(static_cast<UChar32>(b[i]));
        if (left != right) return left < right ? -1 : 1;
    }
    return a.size() == b.size() ? 0 : a.size() < b.size() ? -1 : 1;
}
int compareNatural(std::wstring_view a, std::wstring_view b) {
    if (auto* order = collator()) {
        UErrorCode status = U_ZERO_ERROR;
        const auto result = order->compare(unicode(a), unicode(b), status);
        if (U_SUCCESS(status) && result != UCOL_EQUAL) return result == UCOL_LESS ? -1 : 1;
    }
    return compareIgnoreCase(a, b);
}
#endif
}
