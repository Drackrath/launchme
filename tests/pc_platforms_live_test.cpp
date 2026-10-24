#include "../src/library/pc_platforms.hpp"
#include "../src/library/ubisoft_catalog.hpp"
#include "../src/platform/user_config.hpp"
#include <algorithm>
#include <iostream>

int main() {
    const auto root = userConfigDirectory();
    auto libraries = scanPcLibraries(root / L"libraries");
    // Same order as the launcher's worker: titles for owned Ubisoft products, then a rescan.
    const auto unnamed = libraries.ubisoft_unnamed.size();
    if (unnamed && ubisoft::refreshNames(root / L"ubisoft", libraries.ubisoft_unnamed)) libraries = scanPcLibraries(root / L"libraries");
    for (const auto source : pc_sources) {
        const auto client = std::find_if(libraries.clients.begin(), libraries.clients.end(), [&](const auto& entry) {
            return entry.source == source && !entry.executable.empty();
        });
        const auto count = [&](bool installed) {
            return std::count_if(libraries.visible.begin(), libraries.visible.end(), [&](const auto& game) { return game.source == source && game.installed == installed; });
        };
        std::cout << sourceSlug(source) << " client=" << (client != libraries.clients.end()) << " installed=" << count(true) <<
            " not_installed=" << count(false) << " private=" <<
            std::count_if(libraries.private_games.begin(), libraries.private_games.end(), [&](const auto& game) { return game.source == source; }) << '\n';
    }
    std::cout << "ubisoft account_cache=" << libraries.ubisoft_account << " unnamed_before=" << unnamed <<
        " unnamed_after=" << libraries.ubisoft_unnamed.size() << '\n';
    std::cout << "Catalog errors: " << libraries.errors.size() << "; no clients or games started\n";
    return libraries.errors.empty() ? 0 : 1;
}
