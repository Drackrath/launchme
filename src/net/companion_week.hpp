#pragma once
// Her weekly small talk (with Settings → Pamięć on): about once a week, as a conversation starts,
// she knows roughly what the player did since the last time — the games they played most, new
// games in the library, a suggestion of hers they took — and may mention it in passing, like a
// friend who noticed, never as a report (no numbers: "a few hours", "quite a lot").
// <config>/companion_week.txt keeps the snapshot it compares with: "day=<yyyymmdd>" and
// "<gameKey> <minutes>" per owned game. Pure logic here; the app reads the index and the file on
// its companion worker (src/app/companion.cpp).
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace companion::week {
inline constexpr int every_days = 7;
inline constexpr uint32_t worth_minutes = 30;        // A game played less than this since is not news.
inline constexpr uint32_t second_game_minutes = 60;  // A second game only when it took this much.

struct Snapshot {
    int day = 0; // yyyymmdd; 0: none yet.
    std::map<std::string, uint32_t, std::less<>> minutes; // gameKey → play time then.
};
struct Game { std::string key; std::wstring title; uint32_t minutes = 0; };
struct Pick { std::string key; std::wstring title; };

inline Snapshot parse(std::string_view text) {
    Snapshot out;
    while (!text.empty()) {
        const auto end = text.find('\n');
        auto line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.starts_with("day=")) {
            int day = 0;
            line.remove_prefix(4);
            if (std::from_chars(line.data(), line.data() + line.size(), day).ec == std::errc{}) out.day = day;
            continue;
        }
        const auto space = line.rfind(' ');
        uint32_t minutes = 0;
        if (space == std::string_view::npos || space == 0) continue;
        const auto value = line.substr(space + 1);
        if (std::from_chars(value.data(), value.data() + value.size(), minutes).ec != std::errc{}) continue;
        out.minutes[std::string(line.substr(0, space))] = minutes;
    }
    return out;
}

inline std::string serialize(const Snapshot& snapshot) {
    std::string out = "day=" + std::to_string(snapshot.day) + '\n';
    for (const auto& [key, minutes] : snapshot.minutes) out += key + ' ' + std::to_string(minutes) + '\n';
    return out;
}

inline Snapshot snapshot(const std::vector<Game>& games, int today) {
    Snapshot out;
    out.day = today;
    for (const auto& game : games) if (!game.key.empty() && game.key.find('\n') == std::string::npos) out.minutes[game.key] = game.minutes;
    return out;
}

// Whole days between two yyyymmdd dates; a large number when either is not one.
inline int daysBetween(int from, int to) {
    const auto date = [](int value) {
        return std::chrono::year_month_day{std::chrono::year{value / 10000}, std::chrono::month{static_cast<unsigned>(value / 100 % 100)},
                                           std::chrono::day{static_cast<unsigned>(value % 100)}};
    };
    const auto a = date(from), b = date(to);
    if (!a.ok() || !b.ok()) return 1'000'000;
    return static_cast<int>((std::chrono::sys_days{b} - std::chrono::sys_days{a}).count());
}
// A new snapshot is due: none yet, or the last one is a week old.
inline bool due(const Snapshot& snapshot, int today) {
    if (!snapshot.day) return true;
    const int days = daysBetween(snapshot.day, today);
    return days >= every_days || days < 0;
}

// Play time in words: never a number.
inline std::wstring amount(uint32_t minutes, bool english) {
    if (english) return minutes < 90 ? L"a little" : minutes < 240 ? L"a few hours" : minutes < 720 ? L"quite a lot" : L"a whole lot";
    return minutes < 90 ? L"chwilę" : minutes < 240 ? L"parę godzin" : minutes < 720 ? L"sporo" : L"bardzo dużo";
}

// The week since `before` as a line for her prompt; empty when nothing is worth a word (or there
// was no snapshot to compare with). `picks`: games she suggested lately.
inline std::wstring text(const Snapshot& before, const std::vector<Game>& games, const std::vector<Pick>& picks, bool english) {
    if (!before.day) return {};
    struct Played { const Game* game; uint32_t minutes; };
    std::vector<Played> played;
    std::vector<const Game*> added;
    for (const auto& game : games) {
        const auto found = before.minutes.find(game.key);
        if (found == before.minutes.end()) { if (!game.title.empty()) added.push_back(&game); continue; }
        if (game.minutes > found->second && !game.title.empty()) played.push_back({&game, game.minutes - found->second});
    }
    std::sort(played.begin(), played.end(), [](const Played& a, const Played& b) { return a.minutes > b.minutes; });
    std::vector<std::wstring> parts;
    if (!played.empty() && played[0].minutes >= worth_minutes) {
        std::wstring part = english ? L"played " + amount(played[0].minutes, true) + L" of [[" + played[0].game->title + L"]]"
                                    : L"‹grał|grała› " + amount(played[0].minutes, false) + L" w [[" + played[0].game->title + L"]]";
        if (played.size() > 1 && played[1].minutes >= second_game_minutes)
            part += english ? L" and " + amount(played[1].minutes, true) + L" of [[" + played[1].game->title + L"]]"
                            : L" i " + amount(played[1].minutes, false) + L" w [[" + played[1].game->title + L"]]";
        parts.push_back(std::move(part));
    }
    if (!added.empty()) {
        const auto& first = added.front()->title;
        if (english) parts.push_back(added.size() == 1 ? L"got a new game, [[" + first + L"]]" : (added.size() < 5 ? L"got a few new games (" : L"got a pile of new games (") +
                                                                                                    std::wstring(L"[[") + first + L"]] among them)");
        else parts.push_back(added.size() == 1 ? L"‹dostał|dostała› nową grę, [[" + first + L"]]" : (added.size() < 5 ? L"‹dorzucił|dorzuciła› kilka nowych gier (" : L"‹dorzucił|dorzuciła› całą stertę nowych gier (") +
                                                                                             std::wstring(L"m.in. [[") + first + L"]])");
    }
    for (const auto& pick : picks) {
        const auto game = std::find_if(played.begin(), played.end(), [&](const Played& item) { return item.game->key == pick.key; });
        if (game == played.end() || game->minutes < worth_minutes) continue;
        parts.push_back(english ? L"tried [[" + game->game->title + L"]], which you suggested" : L"‹zagrał|zagrała› w [[" + game->game->title + L"]], którą ‹mu|jej› poleciłaś");
        break;
    }
    if (parts.empty()) return {};
    std::wstring list;
    for (size_t i = 0; i < parts.size(); ++i) list += (i == 0 ? L"" : i + 1 == parts.size() ? (english ? L" and " : L" i ") : L", ") + parts[i];
    return english ? L"\n(Small talk for this week: since about a week ago ‹he|she|they› " + list +
                         L". If it fits, mention it once, in passing, like a friend who happened to notice — no numbers, no lists, nothing like a report.)"
                   : L"\n(Na small talk o tym tygodniu: od mniej więcej tygodnia ‹on|ona› " + list +
                         L". Jeśli pasuje, wspomnij o tym raz, mimochodem, jak koleżanka, która przypadkiem to zauważyła — bez liczb, bez wyliczania, nic w stylu raportu.)";
}
}
