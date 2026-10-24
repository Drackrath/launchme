#pragma once
// How she feels as a new conversation starts (with Settings → Pamięć on): she missed him after a
// few days away; jealous (as far as their stage and her stance allow) when he talked with another
// companion since; pleased when he played the game she suggested; worried after a night of gaming.
// A near stranger feels none of the first two. One line in her mind's workspace for the start of
// that conversation, said once, in her own way. Kept in
// <config>/companion_mood.txt: "talked=<key> <local seconds>" (when each companion's last
// conversation started) and "note=<key> <pleased|late> <local seconds> <title>". Pure logic here;
// the app reads and writes the file (src/app/companion.cpp, companion_events.cpp).
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace companion::mood {
inline constexpr uint64_t day = 86400;
inline constexpr uint64_t missed_after = 3 * day;   // Away this long: she missed the player.
inline constexpr uint64_t jealous_within = 3 * day; // Another companion's talk since hers, this recent.
inline constexpr uint64_t note_life = 7 * day;      // A note waits this long for her next conversation.
inline constexpr size_t note_limit = 20;

enum class Kind { pleased, late, duo }; // Played her suggestion; gamed late at night; dropped into another companion's chat.
struct Note {
    std::string look;
    Kind kind = Kind::pleased;
    uint64_t at = 0;    // Local seconds.
    std::wstring title; // pleased: the game; duo: the host companion's name.
};
struct State {
    std::map<std::string, uint64_t, std::less<>> talked; // Her key → when her last conversation started.
    std::vector<Note> notes;                             // Oldest first.

    void note(std::string look, Kind kind, uint64_t at, std::wstring title = {}) {
        if (look.empty()) return;
        std::erase_if(notes, [&](const Note& old) { return old.look == look && old.kind == kind && old.title == title; });
        notes.push_back({std::move(look), kind, at, std::move(title)});
        if (notes.size() > note_limit) notes.erase(notes.begin(), notes.end() - static_cast<std::ptrdiff_t>(note_limit));
    }
};

inline State parse(std::string_view text) {
    State state;
    const auto number = [](std::string_view value, uint64_t& out) { return std::from_chars(value.data(), value.data() + value.size(), out).ec == std::errc{}; };
    const auto word = [](std::string_view& rest) {
        const auto space = rest.find(' ');
        const auto out = rest.substr(0, space);
        rest = space == std::string_view::npos ? std::string_view{} : rest.substr(space + 1);
        return out;
    };
    while (!text.empty()) {
        const auto end = text.find('\n');
        auto line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        const auto equals = line.find('=');
        if (equals == std::string_view::npos) continue;
        const auto key = line.substr(0, equals);
        auto rest = line.substr(equals + 1);
        uint64_t at = 0;
        if (key == "talked") {
            const auto look = word(rest);
            if (!look.empty() && look.size() <= 32 && number(rest, at)) state.talked[std::string(look)] = at;
        } else if (key == "note" && state.notes.size() < note_limit) {
            const auto look = word(rest), kind = word(rest), when = word(rest);
            if (look.empty() || look.size() > 32 || (kind != "pleased" && kind != "late" && kind != "duo") || !number(when, at)) continue;
            const Kind parsed_kind = kind == "late" ? Kind::late : kind == "duo" ? Kind::duo : Kind::pleased;
            state.notes.push_back({std::string(look), parsed_kind, at, text_encoding::wideLenient(rest)});
        }
    }
    return state;
}

inline std::string serialize(const State& state) {
    std::string out;
    for (const auto& [look, at] : state.talked) out += "talked=" + look + ' ' + std::to_string(at) + '\n';
    for (const auto& note : state.notes) {
        std::wstring title = note.title;
        std::replace(title.begin(), title.end(), L'\n', L' ');
        std::replace(title.begin(), title.end(), L'\r', L' ');
        const char* kind = note.kind == Kind::late ? " late " : note.kind == Kind::duo ? " duo " : " pleased ";
        out += "note=" + note.look + kind + std::to_string(note.at) + ' ' + text_encoding::utf8Lenient(title) + '\n';
    }
    return out;
}

// How close they are as the conversation starts (the app fills it from her level and character):
// the stage (affinity::Stage as a number: 0 strangers … 5 long-distance relationship) and how much
// jealousy fits (affinity::Jealousy as a number: 0 none, 1 a joke, 2 light, 3 real). A near
// stranger neither misses him nor minds whom else he talks with.
struct Closeness {
    int stage = 3;
    int jealousy = 2;
};

// A new conversation with `look` starts `now` (local seconds): how she feels, as a line for her
// prompt (empty: just her usual self). Her notes are used up and `talked` remembers this start.
// `name` gives a companion's name by her key. Texts speak of him as ‹on|ona› (companion_person.hpp).
inline std::wstring begin(State& state, std::string_view look, uint64_t now, bool english, const std::function<std::wstring(std::string_view)>& name,
                          Closeness closeness = {}) {
    std::wstring out;
    const auto found = state.talked.find(look);
    const uint64_t last = found == state.talked.end() ? 0 : found->second;
    // Away and back: another companion's talk since hers, else how long it has been.
    std::string_view rival;
    uint64_t rival_at = 0;
    for (const auto& [other, at] : state.talked)
        if (other != look && at > last && at > rival_at && now >= at && now - at <= jealous_within) { rival = other; rival_at = at; }
    const auto days = last && now > last ? std::to_wstring((now - last) / day) : std::wstring(L"0");
    if (last && !rival.empty() && name && closeness.stage >= 1) {
        const auto other = name(rival);
        std::wstring feel;
        switch (closeness.jealousy) {
        case 0: feel = english ? L"You don't mind at all — at most you are curious how it went." : L"Wcale ci to nie przeszkadza — najwyżej ciekawi cię, jak było."; break;
        case 1: feel = english ? L"You may tease ‹him|her|them› about it once, with a theatrical, playful jealousy — no real hurt." : L"Możesz raz ‹go|ją› o to zaczepić z teatralną, żartobliwą zazdrością — bez prawdziwej urazy."; break;
        case 3: feel = english ? L"It stung a little — let real jealousy show once, your way, without a scene." : L"Trochę cię to ukłuło — okaż raz prawdziwą zazdrość, po swojemu, bez scen."; break;
        default: feel = english ? L"Let a little jealousy show once, lightly and in your own way." : L"Okaż raz odrobinę zazdrości, lekko i po swojemu."; break;
        }
        out += english ? L"\n(How you feel as this conversation starts: since your last talk ‹he|she|they› chatted with " + other +
                             L", another of the launcher's companions — you noticed. " + feel + L")"
                       : L"\n(Jak się czujesz na starcie tej rozmowy: od waszej ostatniej rozmowy ‹gadał|gadała› z " + other +
                             L" — inną towarzyszką launchera — i zauważyłaś to. " + feel + L")";
    } else if (last && now >= last + missed_after && closeness.stage >= 1) {
        // A light flirt notices the absence; from flirting on she missed him a little; from friends on, really.
        if (closeness.stage == 1)
            out += english ? L"\n(How you feel as this conversation starts: ‹he hasn't|she hasn't|they haven't› written for " + days +
                                 L" days — you noticed and you are glad ‹he is|she is|they are› back; mention it once at most, lightly, without longing.)"
                           : L"\n(Jak się czujesz na starcie tej rozmowy: nie ‹pisał|pisała› od " + days +
                                 L" dni — zauważyłaś to i cieszysz się, że ‹wrócił|wróciła›; najwyżej raz o tym lekko wspomnij, bez tęsknoty.)";
        else if (closeness.stage == 2)
            out += english ? L"\n(How you feel as this conversation starts: ‹he hasn't|she hasn't|they haven't› talked with you for " + days +
                                 L" days and you missed ‹him|her|them› a little. Show it once, lightly and playfully, without reproach.)"
                           : L"\n(Jak się czujesz na starcie tej rozmowy: nie ‹rozmawiał|rozmawiała› z tobą od " + days +
                                 L" dni i trochę ci ‹go|jej› brakowało. Okaż to raz, lekko i żartobliwie, bez wyrzutów.)";
        else
            out += english ? L"\n(How you feel as this conversation starts: ‹he hasn't|she hasn't|they haven't› talked with you for " + days +
                                 L" days and you missed ‹him|her|them›. Show it once, briefly and in your own way, without reproaching ‹him|her|them›.)"
                           : L"\n(Jak się czujesz na starcie tej rozmowy: nie ‹rozmawiał|rozmawiała› z tobą od " + days +
                                 L" dni i stęskniłaś się. Okaż to raz, krótko i po swojemu, bez wyrzutów.)";
    }
    // Her notes: the newest of each kind, fresh enough.
    const Note* pleased = nullptr;
    const Note* late = nullptr;
    const Note* duo = nullptr;
    for (const auto& note : state.notes) {
        if (note.look != look || now > note.at + note_life) continue;
        if (note.kind == Kind::pleased && !note.title.empty()) pleased = &note;
        if (note.kind == Kind::late) late = &note;
        if (note.kind == Kind::duo && !note.title.empty()) duo = &note;
    }
    if (pleased)
        out += english ? L"\n(‹He|She|They› recently played [[" + pleased->title + L"]], the game you suggested to ‹him|her|them›. You're pleased; you may mention it once, in your own way.)"
                       : L"\n(‹Zagrał|Zagrała› niedawno w [[" + pleased->title + L"]], grę, którą ‹mu|jej› poleciłaś. Cieszy cię to; możesz raz o tym wspomnieć, po swojemu.)";
    else if (late)
        out += english ? L"\n(‹He was|She was|They were› gaming late into the night recently. You're a little worried; you may bring it up once, gently and in your own way — no lecture.)"
                       : L"\n(Niedawno ‹grał|grała› do późna w nocy. Trochę się martwisz; możesz raz o tym wspomnieć, łagodnie i po swojemu — bez kazań.)";
    if (duo)
        out += english ? L"\n(You recently dropped into the chat while ‹he was|she was|they were› talking with " + duo->title +
                             L" — you remember that three-way conversation and may refer to it once, in character.)"
                       : L"\n(Niedawno wpadłaś do czatu, gdy ‹rozmawiał|rozmawiała› z " + duo->title +
                             L" — pamiętasz tamtą rozmowę we trójkę i możesz raz do niej nawiązać, w swoim charakterze.)";
    std::erase_if(state.notes, [&](const Note& note) { return note.look == look || now > note.at + note_life; });
    state.talked[std::string(look)] = now;
    return out;
}
}
