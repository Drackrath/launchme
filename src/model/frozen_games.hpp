#pragma once
#include "game_session.hpp"
#include "key_codec.hpp"

#include <charconv>
#include <string>
#include <string_view>
#include <vector>

// Games left suspended by quick resume. The list survives a launcher restart
// or crash, so a suspended game is never orphaned without a way to resume it.
// One line per game: <hex game key> <pid>:<creation> [<pid>:<creation> ...]
struct FrozenGame {
    std::string key;
    std::vector<GameProcess> processes;
    bool operator==(const FrozenGame&) const = default;
};

inline std::string formatFrozenGames(const std::vector<FrozenGame>& games) {
    std::string text;
    for (const auto& game : games) {
        if (game.key.empty() || game.processes.empty()) continue;
        text += key_codec::encode(game.key);
        for (const auto& process : game.processes)
            text += ' ' + std::to_string(process.pid) + ':' + std::to_string(process.created);
        text += '\n';
    }
    return text;
}

inline std::vector<FrozenGame> parseFrozenGames(std::string_view text, bool* complete = nullptr) {
    if (complete) *complete = true;
    std::vector<FrozenGame> games;
    const auto number = [](std::string_view value, auto& result) {
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
        return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
    };
    while (!text.empty() && games.size() < 64) {
        const auto end = text.find('\n');
        auto line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        const auto space = line.find(' ');
        if (space == std::string_view::npos) { if (complete) *complete = false; continue; }
        const auto key = key_codec::decode(line.substr(0, space));
        if (!key || key->empty()) { if (complete) *complete = false; continue; }
        FrozenGame game{*key, {}};
        bool valid = true;
        for (auto rest = line.substr(space + 1); valid && !rest.empty();) {
            const auto next = rest.find(' ');
            const auto item = rest.substr(0, next);
            rest = next == std::string_view::npos ? std::string_view{} : rest.substr(next + 1);
            const auto colon = item.find(':');
            GameProcess process;
            valid = colon != std::string_view::npos && number(item.substr(0, colon), process.pid) &&
                    number(item.substr(colon + 1), process.created) && process.pid && process.created;
            if (valid) game.processes.push_back(process);
        }
        if (valid && !game.processes.empty() && game.processes.size() <= 64) games.push_back(std::move(game));
        else if (complete) *complete = false;
    }
    if (!text.empty() && complete) *complete = false;
    return games;
}
