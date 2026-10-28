#pragma once
#include "../model/game.hpp"
#include <optional>
#include <string_view>
#include <vector>

namespace battlenet {
// UID selects an installation variant, code joins Agent metadata, family is
// the launch command/legacy GameKey. These identifiers are not interchangeable.
struct ProductInstall {
    std::string uid, code, region, language, version;
    std::filesystem::path directory;
    std::optional<bool> installed, playable, update_complete;
    bool ready() const { return installed == true && playable == true && update_complete == true; }
    bool stateKnown() const { return installed.has_value() && playable.has_value() && update_complete.has_value(); }
};
struct AggregateEntry {
    std::string code;
    std::wstring name;
    uint64_t last_played = 0;
};
struct RegistryHint { std::string uid; std::filesystem::path directory; };
struct Scan { bool available = false; std::vector<Game> games; };

std::optional<std::vector<ProductInstall>> parseProductDb(std::string_view bytes);
std::optional<std::vector<AggregateEntry>> parseAggregate(std::string_view bytes);
std::string uninstallUid(std::wstring_view command);
// No catalogue/entitlement inference. Only known retail UIDs are launchable;
// PTR/beta/region variants are never silently mapped to the retail game.
// wine_prefix: the Agent runs in Wine (Linux); its install paths are mapped into the prefix.
Scan scan(const std::filesystem::path& database, const std::filesystem::path& aggregate,
          const std::wstring& client, const std::vector<RegistryHint>& hints = {},
          const std::vector<Game>& previous = {}, const std::filesystem::path& wine_prefix = {});
// A Windows path of a program in a Wine prefix ("C:\Program Files (x86)\Overwatch", "C:/...")
// → <prefix>/drive_c/Program Files (x86)/Overwatch. Empty for another drive or a relative path.
std::filesystem::path winePath(const std::filesystem::path& prefix, std::string_view windows_path);
// The launch code (Game::source_id) of an Agent product code: "pro" → "Pro"; empty when unknown.
std::string familyOfCode(std::string_view code);
// account.battle.net/api/games-and-subs as copied from the browser. Game accounts carry a title
// ID; only titles with a known launch code (the GameKey family) are returned, not installed.
// The page lists game accounts, not every licence: the result is a partial library.
// nullopt: not that page (e.g. the sign-in page or a truncated copy).
std::optional<std::vector<Game>> parseGamesAndSubs(std::string_view json);
}
