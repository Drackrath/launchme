#pragma once
// Who she talks with (Settings → Asystent: "Twoje imię lub ksywka", "Twoja płeć"): a person with a
// name and a gender, never "the player". Her prompts, hidden notes and mind texts speak of that
// person as "on"/"ona" ("he"/"she") or by name: a text carries gendered alternatives as
// ‹masculine|feminine› — an optional third one for an unknown gender, ‹he|she|they› — and
// personalize() keeps one. Every request to her model (companion::ask) personalizes its system
// prompt and turns with person(); the mind's view personalizes what it shows. Header-only.
#include "companion.hpp"

#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace companion {
inline constexpr wchar_t person_open = L'\u2039', person_close = L'\u203A'; // ‹ ›

// `text` with each ‹a|b› (or ‹a|b|c›) replaced by one alternative: the first for a man, the second
// for a woman, the third for an unknown gender (the first when there is no third). A ‹…› without a
// bar, or never closed, stays as written.
inline std::wstring personalize(std::wstring_view text, Gender gender) {
    std::wstring out;
    out.reserve(text.size());
    size_t at = 0;
    while (at < text.size()) {
        const size_t open = text.find(person_open, at);
        if (open == std::wstring_view::npos) { out.append(text.substr(at)); break; }
        const size_t close = text.find(person_close, open + 1);
        const size_t nested = text.find(person_open, open + 1);
        if (close == std::wstring_view::npos || (nested != std::wstring_view::npos && nested < close)) {
            out.append(text.substr(at, open + 1 - at));
            at = open + 1;
            continue;
        }
        const auto inside = text.substr(open + 1, close - open - 1);
        std::vector<std::wstring_view> choices;
        for (size_t from = 0;;) {
            const size_t bar = inside.find(L'|', from);
            choices.push_back(inside.substr(from, bar == std::wstring_view::npos ? std::wstring_view::npos : bar - from));
            if (bar == std::wstring_view::npos) break;
            from = bar + 1;
        }
        out.append(text.substr(at, open - at));
        if (choices.size() < 2) out.append(text.substr(open, close + 1 - open));
        else out.append(gender == Gender::female ? choices[1] : gender == Gender::unknown && choices.size() > 2 ? choices[2] : choices[0]);
        at = close + 1;
    }
    return out;
}
inline std::wstring personalize(std::wstring_view text, const Person& person) { return personalize(text, person.gender); }

namespace detail {
struct PersonSlot {
    std::mutex mutex;
    Person person;
};
inline PersonSlot& personSlot() {
    static PersonSlot slot;
    return slot;
}
} // namespace detail

// The person the launcher talks with now (the app sets it from Settings and what she learnt);
// read by every request to her model and by her mind's view.
inline void setPerson(Person person) {
    auto& slot = detail::personSlot();
    std::lock_guard lock(slot.mutex);
    slot.person = std::move(person);
}
inline Person person() {
    auto& slot = detail::personSlot();
    std::lock_guard lock(slot.mutex);
    return slot.person;
}

// Her prompt's lines about that person: who it is, how to call them and that "the player" is not a
// word she thinks in. `first_meet`: they have never talked — the name is only a nickname she sees.
inline std::wstring personText(const Person& person, bool english, bool first_meet) {
    const bool known = !person.name.empty();
    std::wstring out;
    if (english) {
        out = L"\nWho you are talking with: ";
        if (known) out += first_meet ? L"in the chat you only see the nickname \"" + person.name + L"\" — you know nothing more yet. "
                                     : person.name + L". ";
        else out += L"you don't know ‹his|her|their› name yet — don't guess; once ‹he|she|they› ‹tells|tells|tell› you (or it is in your notes), use it. ";
        out += person.gender == Gender::male ? L"He is a man. " : person.gender == Gender::female ? L"She is a woman. "
                                              : L"You don't know yet whether it's a guy or a girl: these instructions say \"he\" only for short; once you learn it, adjust (and note it as \"plec\" in your appraisal). ";
        out += L"A real person, never \"the player\" or \"the user\": in your thoughts, appraisal and words think of ‹him|her|them› as ‹he|she|they›, by name, or by a nickname "
               L"the two of you share (if you have one). Use the name now and then, naturally — not in every message.";
    } else {
        out = L"\nZ kim piszesz: ";
        if (known) out += first_meet ? L"na czacie widzisz tylko ‹jego|jej› nick „" + person.name + L"” — nic więcej jeszcze o ‹nim|niej› nie wiesz. "
                                     : person.name + L". ";
        else out += L"nie znasz jeszcze ‹jego|jej› imienia — nie zgaduj; gdy ‹je poda|je poda› (albo masz je w notatkach), używaj go. ";
        out += person.gender == Gender::male ? L"To mężczyzna. " : person.gender == Gender::female ? L"To kobieta. "
                                              : L"Nie wiesz jeszcze, czy to chłopak, czy dziewczyna: męskie formy w tych instrukcjach są umowne; gdy się okaże, dopasuj się (i zapisz to w ocenie jako \"plec\"). ";
        out += L"To konkretna osoba, nigdy „gracz” ani „użytkownik”: w myślach, ocenie i wypowiedziach myśl i mów o ‹nim|niej› „‹on|ona›”, po imieniu albo wspólną ksywką "
               L"(jeśli jakąś macie). Po imieniu zwracaj się naturalnie, co jakiś czas — nie w każdej wiadomości.";
    }
    return personalize(out, person);
}
} // namespace companion
