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

const std::unordered_map<std::wstring_view, const wchar_t*>& english() {
    static const auto table = [] {
        std::unordered_map<std::wstring_view, const wchar_t*> result;
        const auto entries = englishEntries();
        result.reserve(entries.size());
        for (const auto& entry : entries) result.emplace(entry.polish, entry.english);
        return result;
    }();
    return table;
}
} // namespace

Language systemLanguage() {
    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_POLISH ? Language::pl : Language::en;
}

void setLanguage(Language language) { current.store(language, std::memory_order_relaxed); }
Language language() { return current.load(std::memory_order_relaxed); }
const wchar_t* localeName() { return language() == Language::en ? L"en-US" : L"pl-PL"; }

const wchar_t* translate(const wchar_t* polish, Language language) {
    if (!polish || language == Language::pl) return polish;
    const auto& table = english();
    const auto found = table.find(polish);
    return found == table.end() ? polish : found->second;
}

bool matches(std::wstring_view text, const wchar_t* polish) {
    return text == polish || text == translate(polish, Language::en);
}
} // namespace i18n

const wchar_t* tr(const wchar_t* polish) { return i18n::translate(polish, i18n::language()); }

std::wstring tr(std::wstring_view polish) {
    if (i18n::language() == i18n::Language::pl || polish.empty()) return std::wstring(polish);
    const auto& table = i18n::english();
    const auto found = table.find(polish);
    return found == table.end() ? std::wstring(polish) : std::wstring(found->second);
}
