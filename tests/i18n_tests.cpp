#include "../src/i18n/i18n.hpp"
#include "../src/platform/text_encoding.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace {
std::wstring wide(std::string_view bytes) { return text_encoding::wideLenient(bytes); }
std::string utf8(std::wstring_view text) { return text_encoding::utf8Lenient(text); }

bool identifier(wchar_t ch) { return (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9') || ch == L'_'; }

struct Key { std::wstring text; size_t line = 0; };

// Every tr(L"...") and trKey(L"...") literal in a source file, escapes decoded like the
// compiler does for the simple escapes the UI text uses. Anything else is reported.
std::vector<Key> translationKeys(const std::wstring& source, std::vector<std::wstring>& problems) {
    std::vector<Key> keys;
    for (const std::wstring marker : {L"tr(", L"trKey("}) {
        for (size_t at = source.find(marker); at != std::wstring::npos; at = source.find(marker, at + 1)) {
            if (at && identifier(source[at - 1])) continue;
            size_t cursor = at + marker.size();
            while (cursor < source.size() && (source[cursor] == L' ' || source[cursor] == L'\t')) ++cursor;
            if (source.compare(cursor, 2, L"L\"") != 0) continue; // A variable or another expression.
            const auto line = static_cast<size_t>(std::count(source.begin(), source.begin() + static_cast<std::ptrdiff_t>(at), L'\n')) + 1;
            std::wstring text;
            bool closed = false;
            for (cursor += 2; cursor < source.size() && source[cursor] != L'\n'; ++cursor) {
                const wchar_t ch = source[cursor];
                if (ch == L'"') { closed = true; break; }
                if (ch != L'\\') { text += ch; continue; }
                if (++cursor >= source.size()) break;
                switch (source[cursor]) {
                case L'\\': text += L'\\'; break;
                case L'"': text += L'"'; break;
                case L'\'': text += L'\''; break;
                case L'n': text += L'\n'; break;
                case L't': text += L'\t'; break;
                case L'r': text += L'\r'; break;
                default: problems.push_back(L"unsupported escape at line " + std::to_wstring(line)); break;
                }
            }
            if (!closed) problems.push_back(L"unterminated literal at line " + std::to_wstring(line));
            else keys.push_back({std::move(text), line});
        }
    }
    return keys;
}
} // namespace

int main() {
    // (c) One translation per key; no empty English text.
    std::map<std::wstring, std::wstring> table;
    int conflicts = 0;
    for (const auto& entry : i18n::englishEntries()) {
        if (!entry.polish || !entry.english || !*entry.polish || !*entry.english) {
            std::cerr << "Empty entry in strings_en.cpp\n";
            return 1;
        }
        const auto [found, inserted] = table.emplace(entry.polish, entry.english);
        if (!inserted && found->second != entry.english) {
            std::cerr << "Conflicting translations for \"" << utf8(entry.polish) << "\"\n";
            ++conflicts;
        }
    }
    if (conflicts) return 2;

    // (b) Lookup follows the active language; unknown keys fall back to the Polish text.
    i18n::setLanguage(i18n::Language::en);
    if (std::wstring(tr(L"Graj")) != L"Play" || std::wstring(tr(L"Ustawienia")) != L"Settings" ||
        tr(std::wstring(L"Anuluj")) != L"Cancel" || std::wstring(i18n::localeName()) != L"en-US") return 3;
    const wchar_t* unknown = L"Tekst spoza tabeli";
    if (tr(unknown) != unknown || tr(std::wstring_view(L"Tekst spoza tabeli")) != L"Tekst spoza tabeli" ||
        tr(std::wstring_view{}) != L"") return 4;
    i18n::setLanguage(i18n::Language::pl);
    if (std::wstring(tr(L"Graj")) != L"Graj" || tr(std::wstring(L"Anuluj")) != L"Anuluj" ||
        std::wstring(i18n::localeName()) != L"pl-PL" || i18n::language() != i18n::Language::pl) return 5;
    if (std::wstring(i18n::translate(L"Graj", i18n::Language::en)) != L"Play" ||
        !i18n::matches(L"Loading name…", L"Ładowanie nazwy…") || !i18n::matches(L"Ładowanie nazwy…", L"Ładowanie nazwy…") ||
        i18n::matches(L"Portal 2", L"Ładowanie nazwy…")) return 6;
    const auto system = i18n::systemLanguage();
    if (system != i18n::Language::pl && system != i18n::Language::en && system != i18n::Language::pt_br && system != i18n::Language::es) return 7;

    // (d) Portuguese (Brazil) and Spanish: every key also in the English table, one text per key, the
    // English's spacing, line breaks and placeholders kept; a key without one falls back to English.
    for (const auto& [name, entries, language] : {std::tuple{"pt-BR", i18n::portugueseEntries(), i18n::Language::pt_br},
                                                  std::tuple{"es", i18n::spanishEntries(), i18n::Language::es}}) {
        std::map<std::wstring, std::wstring> own;
        for (const auto& entry : entries) {
            if (!entry.polish || !entry.english || !*entry.polish || !*entry.english) { std::cerr << "Empty " << name << " entry\n"; return 20; }
            const auto english = table.find(entry.polish);
            if (english == table.end()) { std::cerr << name << ": no English entry for \"" << utf8(entry.polish) << "\"\n"; return 21; }
            const auto [found, inserted] = own.emplace(entry.polish, entry.english);
            if (!inserted && found->second != entry.english) { std::cerr << name << ": conflicting \"" << utf8(entry.polish) << "\"\n"; return 22; }
            const std::wstring text = entry.english, source = english->second;
            const auto space = [](wchar_t ch) { return ch == L' ' || ch == L'\n' || ch == L'\t'; };
            const auto count = [](const std::wstring& value, wchar_t ch) { return std::count(value.begin(), value.end(), ch); };
            if (space(text.front()) != space(source.front()) || space(text.back()) != space(source.back()) ||
                count(text, L'\n') != count(source, L'\n') || count(text, L'%') != count(source, L'%')) {
                std::cerr << name << ": \"" << utf8(entry.polish) << "\" changes the English text's spacing, line breaks or placeholders\n";
                return 23;
            }
        }
        i18n::setLanguage(language);
        if (own.contains(L"Graj") && std::wstring(tr(L"Graj")) != own.at(L"Graj")) return 24;
        for (const auto& [polish, english] : table)
            if (!own.contains(polish) && std::wstring(tr(polish.c_str())) != english) return 25; // Falls back to English.
        if (tr(unknown) != unknown || !i18n::matches(own.begin()->second, own.begin()->first.c_str())) return 26;
        std::cout << name << ": " << own.size() << " of " << table.size() << " entries translated\n";
    }
    i18n::setLanguage(i18n::Language::pt_br);
    if (std::wstring(i18n::localeName()) != L"pt-BR" || std::wstring(i18n::steamLanguage()) != L"brazilian" || !i18n::english()) return 27;
    i18n::setLanguage(i18n::Language::es);
    if (std::wstring(i18n::localeName()) != L"es-ES" || std::wstring(i18n::steamLanguage()) != L"spanish") return 28;
    i18n::setLanguage(i18n::Language::pl);
    if (i18n::english()) return 29;

    // (a) Every key used in src/ has an English entry.
    const auto root = std::filesystem::path(wide(LAUNCHME_SOURCE_DIR)) / L"src";
    if (!std::filesystem::is_directory(root)) {
        std::cerr << "Source directory not found: " << LAUNCHME_SOURCE_DIR << '\n';
        return 8;
    }
    size_t files = 0, keys = 0;
    std::vector<std::string> missing;
    for (const auto& item : std::filesystem::recursive_directory_iterator(root)) {
        const auto extension = item.path().extension();
        if (!item.is_regular_file() || (extension != L".cpp" && extension != L".hpp")) continue;
        const auto relative = std::filesystem::relative(item.path(), root);
        if (*relative.begin() == L"i18n") continue; // The table and the documented API itself.
        std::ifstream input(item.path(), std::ios::binary);
        const std::string bytes(std::istreambuf_iterator<char>{input}, {});
        std::vector<std::wstring> problems;
        const auto found = translationKeys(wide(bytes), problems);
        ++files;
        for (const auto& problem : problems) missing.push_back(utf8(relative.wstring()) + ": " + utf8(problem));
        for (const auto& key : found) {
            ++keys;
            if (!table.contains(key.text))
                missing.push_back(utf8(relative.wstring()) + ':' + std::to_string(key.line) + ": no English entry for \"" + utf8(key.text) + '"');
        }
    }
    if (files < 50 || keys < 200) {
        std::cerr << "Scanned only " << files << " files / " << keys << " keys; the scanner is broken\n";
        return 9;
    }
    if (!missing.empty()) {
        for (const auto& line : missing) std::cerr << line << '\n';
        return 10;
    }
    std::cout << "Translations: " << table.size() << " entries, " << keys << " keys in " << files << " source files\n";
    return 0;
}
