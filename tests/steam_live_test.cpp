#include "../src/library/steam.hpp"
#include "../src/library/collections.hpp"
#include "../src/library/visibility.hpp"
#include "../src/library/owned_games.hpp"
#include "../src/library/steam_owned.hpp"
#include "../src/library/steam_stats.hpp"
#include "../src/library/steam_achievements.hpp"
#include "../src/friends/steam_friends.hpp"
#include "../src/platform/user_config.hpp"

#include <iostream>
#include <unordered_set>

// Manual integration smoke test; never launches a game or prints private IDs.
// `--stats`: only the local play time / achievement counts, no network.
int main(int argc, char** argv) {
    const auto root = findSteamRoot();
    if (!activeSteamId(root)) { std::cerr << "No active Steam account\n"; return 1; }
    if (argc > 1 && std::string_view(argv[1]) == "--owned") {
        const auto owned = clientSteamOwnedGames(root, activeSteamId(root));
        if (!owned) { std::cerr << "Steam client ownership unavailable\n"; return 1; }
        auto library = scanSteamLibrary(root);
        mergeSteamOwnedGames(library, *owned);
        bool aion = false;
        for (const auto& game : library.visible) if (game.app_id == 3393110) {
            aion = true;
            std::cout << "AION 2 found; installed=" << game.installed << '\n';
        }
        std::cout << "Client-owned games=" << owned->size() << "; public library=" << library.visible.size() << '\n';
        return aion ? 0 : 2;
    }
    const auto games = scanSteamLibrary(root);
    {
        size_t with_time = 0, with_achievements = 0, mismatched = 0;
        for (const auto& game : games.visible) {
            const auto stats = readSteamStats(root, activeSteamId(root), game.app_id);
            with_time += stats.playtime_minutes.has_value();
            with_achievements += stats.achievements_total > 0;
            // The client's schema/unlock files against its own game page cache (where it has one).
            const auto cached = readCachedAchievements(root, activeSteamId(root), game.app_id);
            if (stats.achievements_total && cached.total &&
                (cached.total != stats.achievements_total || cached.unlocked != stats.achievements_unlocked)) {
                ++mismatched;
                std::cout << "app " << game.app_id << ": files " << stats.achievements_unlocked << '/' << stats.achievements_total
                          << ", page cache " << cached.unlocked << '/' << cached.total << '\n';
            }
        }
        std::cout << "achievement counts differing from the page cache: " << mismatched << '\n';
        std::cout << "visible=" << games.visible.size() << " with_playtime=" << with_time
                  << " with_achievements=" << with_achievements << '\n';
        if (argc > 1 && std::string_view(argv[1]) == "--stats") return with_time || with_achievements ? 0 : 1;
    }
    const auto collections = readSteamCollections(root);
    const auto shelves = publicSteamShelves(collections, games.visible);
    const auto hidden = excludedSteamApps(root);
    std::unordered_set<uint32_t> installed;
    for (const auto& game : games.visible) installed.insert(game.app_id);
    size_t uninstalled = 0;
    std::unordered_set<uint32_t> seen;
    std::vector<uint32_t> sample_ids;
    for (const auto& collection : collections)
        for (auto id : collection.app_ids)
            if (!steamShortcutId(id) && !hidden.contains(id) && !installed.contains(id) && seen.insert(id).second) {
                ++uninstalled;
                if (sample_ids.size() < 12) sample_ids.push_back(id);
            }
    const auto friends = fetchSteamFriends(activeSteamId(root));
    std::cout << "public=" << games.visible.size() << " private=" << games.private_games.size()
              << " collections=" << shelves.size() << " uninstalled_collection_apps=" << uninstalled
              << " friends=" << friends.friends.size() << '\n';
    if (!friends.error.empty()) { std::wcerr << friends.error << '\n'; return 1; }
    if (!friends.friends.empty()) {
        const auto avatar = fetchFriendAvatar(friends.friends.front(), userConfigDirectory() / L"avatars");
        std::cout << "first_avatar_bytes=" << avatar.size() << '\n';
        if (avatar.empty()) return 1;
    }
    const auto names = collectionGameNames(activeSteamId(root), {400, 620},
                                            userConfigDirectory() / L"metadata");
    std::cout << "resolved_store_or_owned_names=" << names.size() << '\n';
    if (names.size() != 2) return 1;
    const auto collection_names = collectionGameNames(activeSteamId(root), sample_ids,
                                                      userConfigDirectory() / L"metadata");
    std::cout << "sampled_collection_names=" << collection_names.size()
              << "/" << sample_ids.size() << '\n';
    return games.visible.empty() && games.private_games.empty() ? 1 : 0;
}
