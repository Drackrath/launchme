#pragma once

#include <span>
#include <string>
#include <string_view>

// UI text, gettext style: the Polish source text is the key. tr() returns the translation of the
// active language (strings_en.cpp, strings_pt_br.cpp, strings_es.cpp), otherwise its argument. A key
// missing from the Portuguese or Spanish table falls back to English.
// Identities and data (platform keys, store names, INI values, paths, URLs) are never
// passed through tr().
namespace i18n {
enum class Language { pl, en, pt_br, es };

// Windows display language: Polish -> pl, Portuguese -> pt_br, Spanish -> es, anything else -> en.
Language systemLanguage();
// Process-wide; Polish until the launcher applies the user's setting.
void setLanguage(Language language);
Language language();
// Content written in the code in Polish and English only (the companion's lines, Tarot, plugin
// manifests, Steam's English data): true for every language but Polish.
inline bool english() { return language() != Language::pl; }
// BCP-47 name for GetDateFormatEx/GetTimeFormatEx: "pl-PL", "en-US", "pt-BR" or "es-ES".
const wchar_t* localeName();
// Steam's own language names (store API `l=`): "polish", "english", "brazilian", "spanish".
const wchar_t* steamLanguage();

// Translation of a Polish key into a given language (the key itself when missing).
const wchar_t* translate(const wchar_t* polish, Language language);
// True when text is the key or any of its translations (e.g. a stored placeholder
// written before the language changed).
bool matches(std::wstring_view text, const wchar_t* polish);

struct Entry { const wchar_t* polish; const wchar_t* english; }; // `english`: the table's translation.
// The tables (src/i18n/strings_*.cpp), for tests.
std::span<const Entry> englishEntries();
std::span<const Entry> portugueseEntries();
std::span<const Entry> spanishEntries();
} // namespace i18n

const wchar_t* tr(const wchar_t* polish);
std::wstring tr(std::wstring_view polish);

// Marks a Polish literal as a translation key without translating it (constexpr tables);
// the use site calls tr(). The i18n test checks every tr(L"...") and trKey(L"...") key.
constexpr const wchar_t* trKey(const wchar_t* polish) { return polish; }
