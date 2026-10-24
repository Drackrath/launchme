#pragma once
#include "epic_account.hpp"
#include "../net/json.hpp"

namespace epic {
std::wstring quoteArgument(std::wstring_view);
std::optional<unsigned> downloadPercent(std::string_view line); // Hundredths of an actual reported percent.
std::optional<GameAction> launchAction(const json::Value&, const std::filesystem::path& install);
std::vector<Game> installedGames(const std::filesystem::path& root);
// "Odinstaluj" for a game Launchme installed through Legendary (the Epic launcher does not know
// it): Legendary deletes it, then Launchme's installation marker goes. Worker only.
bool uninstallManaged(const std::filesystem::path& root, const Game& game, const Cancel& cancel);
// Fetches a pinned, SHA-256-verified helper. Does not log in or run a game.
std::filesystem::path prepareLegendary(const std::filesystem::path& root, const Cancel&);
struct ProcessResult { bool ok = false; std::string output; };
struct LegendaryOperations {
    std::function<std::filesystem::path(const std::filesystem::path&, const Cancel&)> prepare;
    std::function<ProcessResult(const std::filesystem::path&, const std::filesystem::path&, const std::vector<std::wstring>&,
        const Cancel&, const std::function<void(std::string_view)>&)> invoke;
};
std::unique_ptr<Runner> makeRunner(const std::filesystem::path&, LegendaryOperations);
}
