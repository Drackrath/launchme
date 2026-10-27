#include "i18n.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <atomic>
#include <unordered_map>

namespace i18n {
namespace {
std::atomic<Language> current{Language::pl};

using Table = std::unordered_map<std::wstring_view, const wchar_t*>;
Table build(std::span<const Entry> entries) {
    Table result;
    result.reserve(entries.size());
    for (const auto& entry : entries) result.emplace(entry.polish, entry.english);
    return result;
}
const Table& table(Language language) {
    static const Table english = build(englishEntries()), portuguese = build(portugueseEntries()), spanish = build(spanishEntries());
    return language == Language::pt_br ? portuguese : language == Language::es ? spanish : english;
}
// The language's text, then English's, then nothing.
const wchar_t* lookup(std::wstring_view polish, Language language) {
    if (language != Language::en) {
        const auto& own = table(language);
        if (const auto found = own.find(polish); found != own.end()) return found->second;
    }
    const auto& english = table(Language::en);
    const auto found = english.find(polish);
    return found == english.end() ? nullptr : found->second;
}
} // namespace

Language systemLanguage() {
    switch (PRIMARYLANGID(GetUserDefaultUILanguage())) {
    case LANG_POLISH: return Language::pl;
    case LANG_PORTUGUESE: return Language::pt_br;
    case LANG_SPANISH: return Language::es;
    default: return Language::en;
    }
}

void setLanguage(Language language) { current.store(language, std::memory_order_relaxed); }
Language language() { return current.load(std::memory_order_relaxed); }
const wchar_t* localeName() {
    switch (language()) {
    case Language::en: return L"en-US";
    case Language::pt_br: return L"pt-BR";
    case Language::es: return L"es-ES";
    default: return L"pl-PL";
    }
}
const wchar_t* steamLanguage() {
    switch (language()) {
    case Language::en: return L"english";
    case Language::pt_br: return L"brazilian";
    case Language::es: return L"spanish";
    default: return L"polish";
    }
}

const wchar_t* translate(const wchar_t* polish, Language language) {
    if (!polish || language == Language::pl) return polish;
    const auto* found = lookup(polish, language);
    return found ? found : polish;
}

bool matches(std::wstring_view text, const wchar_t* polish) {
    if (text == polish) return true;
    for (const auto language : {Language::en, Language::pt_br, Language::es})
        if (text == translate(polish, language)) return true;
    return false;
}
} // namespace i18n

const wchar_t* tr(const wchar_t* polish) { return i18n::translate(polish, i18n::language()); }

std::wstring tr(std::wstring_view polish) {
    const auto language = i18n::language();
    if (language == i18n::Language::pl || polish.empty()) return std::wstring(polish);
    const auto* found = i18n::lookup(polish, language);
    return found ? std::wstring(found) : std::wstring(polish);
}
