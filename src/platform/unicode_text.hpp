#pragma once
// Language-aware text operations the library, search and stores need: case folding,
// normalization, character classes and title ordering. Windows uses NLS (the user's locale),
// other systems ICU, so search folding and sort order agree across platforms.
#include <string>
#include <string_view>

namespace unicode_text {
// Canonical decomposition (NFD): é becomes e + U+0301. ™ stays ™.
std::wstring decompose(std::wstring_view text);
// Decomposition with compatibility characters folded (full-width letters, ligatures, ™ → TM).
std::wstring decomposeCompatible(std::wstring_view text);
// Locale-independent lowercase (LOCALE_NAME_INVARIANT / ICU root); same length on Windows.
std::wstring lower(std::wstring_view text);

// Character classes (C1_ALPHA, C1_DIGIT, C3_NONSPACING | C3_DIACRITIC).
bool isLetter(wchar_t c);
bool isDigit(wchar_t c);
inline bool isLetterOrDigit(wchar_t c) { return isLetter(c) || isDigit(c); }
bool isMark(wchar_t c);

// Ordinal comparison, ignoring case (CompareStringOrdinal(..., TRUE), _wcsicmp): <0, 0, >0.
int compareIgnoreCase(std::wstring_view a, std::wstring_view b);
inline bool equalIgnoreCase(std::wstring_view a, std::wstring_view b) { return compareIgnoreCase(a, b) == 0; }
inline bool startsWithIgnoreCase(std::wstring_view text, std::wstring_view prefix) {
    return text.size() >= prefix.size() && equalIgnoreCase(text.substr(0, prefix.size()), prefix);
}
inline bool endsWithIgnoreCase(std::wstring_view text, std::wstring_view suffix) {
    return text.size() >= suffix.size() && equalIgnoreCase(text.substr(text.size() - suffix.size()), suffix);
}
// The user's language order, ignoring case, numbers by value ("Part 2" before "Part 10"); ties
// broken by compareIgnoreCase. <0, 0, >0.
int compareNatural(std::wstring_view a, std::wstring_view b);
}
