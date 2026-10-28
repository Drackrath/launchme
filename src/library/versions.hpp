#pragma once
#include "../model/game.hpp"
#include <map>
#include <vector>

namespace versions {
// Conservative matching: case, whitespace and trademark marks only. Editions,
// remasters, sequels and emulated platforms keep their identities. Besides the title,
// entries starting the very same .exe (same visibility) join one group whatever their names.
std::string identity(const Game& game);
struct Index {
    std::map<std::string, std::vector<Game>> groups;
    std::map<std::string, std::string> by_game;
    explicit Index(const std::vector<Game>& games = {});
    const std::vector<Game>* find(const Game& game) const;
    std::vector<Game> collapse(const std::vector<Game>& games, const std::map<std::string, std::string>& preferred,
                               const std::map<std::string, uint64_t>& first_seen = {}) const;
};
}
