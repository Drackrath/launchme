#pragma once

#include <span>
#include <string>
#include <string_view>

// UI text, gettext style: the Polish source text is the key. tr() returns the English
// translation from strings_en.cpp while English is active, otherwise its argument.
// Identities and data (platform keys, store names, INI values, paths, URLs) are never
// passed through tr().
namespace i18n {
enum class Language { pl, en };

// Windows display language: Polish -> pl, anything else -> en.
Language systemLanguage();
// Process-wide; Polish until the launcher applies the user's setting.
void setLanguage(Language language);
Language language();
// BCP-47 name for GetDateFormatEx/GetTimeFormatEx: "pl-PL" or "en-US".
const wchar_t* localeName();

// Translation of a Polish key into a given language (the key itself when missing).
const wchar_t* translate(const wchar_t* polish, Language language);
// True when text is the key or any of its translations (e.g. a stored placeholder
// written before the language changed).
bool matches(std::wstring_view text, const wchar_t* polish);

struct Entry { const wchar_t* polish; const wchar_t* english; };
// The English table (src/i18n/strings_en.cpp), for tests.
std::span<const Entry> englishEntries();
} // namespace i18n

const wchar_t* tr(const wchar_t* polish);
std::wstring tr(std::wstring_view polish);

// Marks a Polish literal as a translation key without translating it (constexpr tables);
// the use site calls tr(). The i18n test checks every tr(L"...") and trKey(L"...") key.
constexpr const wchar_t* trKey(const wchar_t* polish) { return polish; }
