#pragma once
// Achievements from a plugin (contribution kind "achievements", method "achievements"; docs/plugins/API.md):
// the plugin answers with the list, with none for the game, or with store requests the host makes
// for it with a signed-in account (GOG, Epic) and sends back in the next round. Tokens never reach
// the plugin: the account worker adds them (src/store/account_request.hpp).
#include "json.hpp"
#include "../library/steam_achievements.hpp"
#include "../store/account_request.hpp"

#include <optional>
#include <string>
#include <vector>

namespace plugins {
struct StoreRequest {
    std::string id;
    std::string account; // "gog" or "epic".
    AccountRequest request;
};
struct AchievementsAnswer {
    enum class Kind { list, none, requests, message } kind = Kind::none;
    SteamAchievements list;          // kind == list: sorted, counts filled.
    std::wstring source;             // "GOG", "Epic", "RetroAchievements"…, shown under the title.
    std::vector<StoreRequest> requests;
    json::Value state;               // Echoed back with the responses.
    std::wstring message;
};
inline constexpr size_t max_store_requests = 4, max_store_rounds = 6, max_achievements = 5000;
// The hosts a plugin may reach through each account.
bool storeHostAllowed(std::string_view account, std::wstring_view url);
// Throws std::runtime_error on a malformed answer (the plugin is then quarantined like any other).
AchievementsAnswer parseAchievements(const json::Value& response);
// {"id","status","body"} entries for the next round.
json::Value storeResponses(const std::vector<std::pair<std::string, std::optional<HttpResponse>>>& answers);
}
