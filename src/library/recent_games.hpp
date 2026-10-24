#pragma once
#include "../model/game.hpp"
#include <algorithm>
#include <map>
#include <vector>

namespace recent_games {
inline std::string historyKey(const Game& game, uint64_t steam_account, const std::string& gog_account, const std::string& epic_account = {}) {
    const auto key = gameKey(game);
    if (!isGame(game) || key.empty()) return {};
    if (game.source == GameSource::steam) return "steam-user:" + std::to_string(steam_account) + '/' + key;
    if (game.source == GameSource::gog && !gog_account.empty()) return "gog-user:" + gog_account + '/' + key;
    if (game.source == GameSource::epic && !epic_account.empty()) return "epic-user:" + epic_account + '/' + key;
    return "local/" + key;
}
inline void apply(std::vector<Game>& games, const std::map<std::string, uint64_t>& history, uint64_t steam_account, const std::string& gog_account, const std::string& epic_account = {}) {
    for (auto& game : games) {
        const auto found = history.find(historyKey(game, steam_account, gog_account, epic_account));
        if (found != history.end()) game.last_played = std::max(game.last_played, found->second);
    }
}
}
