#include "steam_friends.hpp"
#include "../i18n/i18n.hpp"
#include "../net/http.hpp"
#include "../net/json.hpp"
#include "../platform/user_config.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/text_encoding.hpp"

#ifdef _WIN32
#include <Windows.h>
#endif

#include <algorithm>
#include <fstream>
#include <unordered_map>

namespace {
// Persona names are user-typed display text; never drop one for a bad byte.
std::wstring wide(std::string_view text) { return text_encoding::wideLenient(text); }

const json::Value* member(const json::Value* object, std::string_view field) {
    return object ? object->get(field) : nullptr;
}

std::string string(const json::Value* object, std::string_view field) {
    const auto* value = member(object, field);
    return value ? value->stringOr() : std::string{};
}

uint64_t steamId(const std::string& id) {
    try { return std::stoull(id); } catch (...) { return 0; }
}

std::optional<json::Value> steamJson(const std::wstring& uri) {
    const auto response = httpGet(uri, L"Accept: application/json\r\n", 2 * 1024 * 1024);
    if (response.status != 200) return std::nullopt;
    return json::parse(std::string_view(reinterpret_cast<const char*>(response.body.data()),
                                        response.body.size()));
}

bool image(const std::vector<uint8_t>& bytes) {
    return bytes.size() > 128 && ((bytes[0] == 0xff && bytes[1] == 0xd8) ||
           (bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G'));
}
} // namespace

FriendsResult fetchSteamFriends(uint64_t steam_id) {
    if (!steam_id) return {{}, tr(L"Nie znaleziono aktywnego konta Steam")};
    const auto key = userSecret(L"STEAM_WEB_API_KEY", L"steam_web_api_key.txt");
    if (key.empty()) return {{}, tr(L"Skonfiguruj klucz Steam Web API w ustawieniach użytkownika")};
    const auto friend_url = L"https://api.steampowered.com/ISteamUser/GetFriendList/v1/?key=" +
                            key + L"&steamid=" + std::to_wstring(steam_id) + L"&relationship=friend";
    const auto reply = httpGet(friend_url, L"Accept: application/json\r\n", 1024 * 1024);
    if (reply.status == 401 || reply.status == 403) return {{}, tr(L"Lista znajomych jest prywatna")};
    if (reply.status != 200) return {{}, tr(L"Nie można pobrać znajomych ze Steam")};
    const auto data = json::parse(std::string_view(reinterpret_cast<const char*>(reply.body.data()),
                                                   reply.body.size()));
    const auto* friends = member(member(data ? &*data : nullptr, "friendslist"), "friends");
    if (!friends || friends->type != json::Value::Type::array) return {{}, tr(L"Brak dostępu do listy znajomych")};
    FriendsResult result;
    std::unordered_map<uint64_t, size_t> positions;
    for (const auto& item : friends->elements) {
        const uint64_t id = steamId(string(&item, "steamid"));
        if (!id || positions.contains(id)) continue;
        positions[id] = result.friends.size();
        result.friends.push_back(SteamFriend{id, tr(L"Znajomy Steam")});
    }
    for (size_t offset = 0; offset < result.friends.size(); offset += 100) {
        std::wstring ids;
        for (size_t i = offset; i < std::min(result.friends.size(), offset + 100); ++i) {
            if (!ids.empty()) ids += L',';
            ids += std::to_wstring(result.friends[i].id);
        }
        const auto profiles = steamJson(L"https://api.steampowered.com/ISteamUser/GetPlayerSummaries/v2/?key=" +
                                        key + L"&steamids=" + ids);
        const auto* players = member(member(profiles ? &*profiles : nullptr, "response"), "players");
        if (!players || players->type != json::Value::Type::array) continue;
        for (const auto& profile : players->elements) {
            const auto at = positions.find(steamId(string(&profile, "steamid")));
            if (at == positions.end()) continue;
            auto& person = result.friends[at->second];
            const auto name = string(&profile, "personaname");
            if (!name.empty()) person.name = wide(name);
            person.game = wide(string(&profile, "gameextrainfo"));
            person.avatar_url = wide(string(&profile, "avatarmedium"));
            if (const auto* state = profile.get("personastate")) person.status = state->intOr();
        }
    }
    std::stable_sort(result.friends.begin(), result.friends.end(), [](const SteamFriend& a, const SteamFriend& b) {
        const int left = !a.game.empty() ? 2 : a.status ? 1 : 0;
        const int right = !b.game.empty() ? 2 : b.status ? 1 : 0;
        if (left != right) return left > right;
        return a.name < b.name;
    });
    return result;
}

std::vector<uint8_t> fetchFriendAvatar(const SteamFriend& info, const std::filesystem::path& cache) {
    if (!info.id || !info.avatar_url.starts_with(L"https://")) return {};
    const auto path = cache / (std::to_wstring(info.id) + L".img");
    {
        std::ifstream input(path, std::ios::binary);
        if (input) {
            std::vector<uint8_t> bytes(std::istreambuf_iterator<char>{input}, {});
            if (image(bytes)) return bytes;
        }
    }
    auto bytes = httpGet(info.avatar_url, L"Accept: image/jpeg, image/png\r\n", 1024 * 1024).body;
    if (!image(bytes)) return {};
    atomic_file::write(path, bytes);
    return bytes;
}
