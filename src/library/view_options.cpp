#include "view_options.hpp"
#include "search.hpp"
#include <Windows.h>
#include <algorithm>

std::vector<Game> library_view::arrange(const std::vector<Game>& games, Options options, const std::map<std::string, uint64_t>& first_seen,
                                        bool include_private, const std::unordered_map<std::string, int>* released) {
    // Sorted as pointers with the "added" time (or release date) looked up once per game: the
    // comparator used to rebuild gameKey and search first_seen on every comparison, and to move Games.
    struct Entry { const Game* game; uint64_t added; };
    std::vector<Entry> entries;
    entries.reserve(games.size());
    for (const auto& game : games) {
        if ((!include_private && game.visibility != GameVisibility::public_game) || (options.installed_only && isGame(game) && !game.installed)) continue;
        uint64_t added = 0;
        if (options.order == Order::added) added = addedTime(game, first_seen);
        if (options.order == Order::release && released)
            if (const auto found = released->find(gameKey(game)); found != released->end() && found->second > 0) added = static_cast<uint64_t>(found->second);
        entries.push_back({&game, added});
    }
    std::stable_sort(entries.begin(), entries.end(), [&](const Entry& left, const Entry& right) {
        const Game& a = *left.game;
        const Game& b = *right.game;
        if (a.kind == GameKind::back || b.kind == GameKind::back) return a.kind == GameKind::back && b.kind != GameKind::back;
        if (options.order == Order::last_played && a.last_played != b.last_played) return a.last_played > b.last_played;
        if ((options.order == Order::added || options.order == Order::release) && left.added != right.added) return left.added > right.added;
        // As a person reads titles: Ł after L, "Game 2" before "Game 10" (library/search.hpp).
        if (const int names = compareTitles(a.name, b.name)) return names < 0;
        return gameKey(a) < gameKey(b);
    });
    std::vector<Game> result;
    result.reserve(entries.size());
    for (const auto& entry : entries) result.push_back(*entry.game);
    return result;
}
