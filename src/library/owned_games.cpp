#include "owned_games.hpp"
#include "../net/http.hpp"
#include "../net/json.hpp"
#include "../platform/user_config.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/text_encoding.hpp"

#ifdef _WIN32
#include <Windows.h>
#endif

#include <algorithm>
#include <charconv>
#include <fstream>
#include <optional>
#include <unordered_set>

namespace {
using text_encoding::wide;

std::optional<json::Value> response(const std::wstring& url, size_t limit) {
    const auto reply = httpGet(url, L"Accept: application/json\r\n", limit);
    if (reply.status != 200) return {};
    return json::parse(std::string_view(reinterpret_cast<const char*>(reply.body.data()), reply.body.size()));
}

void ownedNames(uint64_t steam_id, const std::unordered_set<uint32_t>& wanted,
                std::unordered_map<uint32_t, std::wstring>& names) {
    const auto key = userSecret(L"STEAM_WEB_API_KEY", L"steam_web_api_key.txt");
    if (!steam_id || key.empty()) return;
    const auto data = response(L"https://api.steampowered.com/IPlayerService/GetOwnedGames/v1/?key=" +
                               key + L"&steamid=" + std::to_wstring(steam_id) +
                               L"&include_appinfo=true&include_played_free_games=true", 12 * 1024 * 1024);
    const auto* body = data ? data->get("response") : nullptr;
    const auto* games = body ? body->get("games") : nullptr;
    if (!games || games->type != json::Value::Type::array) return;
    for (const auto& entry : games->elements) {
        const auto* id = entry.get("appid");
        const auto* name = entry.get("name");
        if (!id || !name || !wanted.contains(static_cast<uint32_t>(id->intOr()))) continue;
        auto title = wide(name->stringOr());
        if (!title.empty()) names.emplace(static_cast<uint32_t>(id->intOr()), std::move(title));
    }
}

void publicAppNames(const std::unordered_set<uint32_t>& wanted,
                    std::unordered_map<uint32_t, std::wstring>& names) {
    const auto reply = httpGet(L"https://api.steampowered.com/ISteamApps/GetAppList/v2/",
                               L"Accept: application/json\r\n", 48 * 1024 * 1024);
    if (reply.status != 200) return;
    auto resolved = parseSteamAppListNames(
        std::string_view(reinterpret_cast<const char*>(reply.body.data()), reply.body.size()), wanted);
    for (auto& [id, title] : resolved) names.emplace(id, std::move(title));
}

std::wstring gridName(uint32_t id) {
    const auto key = userSecret(L"STEAMGRIDDB_API_KEY", L"steamgriddb_api_key.txt");
    if (key.empty()) return {};
    const auto url = L"https://www.steamgriddb.com/api/v2/games/steam/" + std::to_wstring(id);
    const auto reply = httpGet(url, L"Authorization: Bearer " + key + L"\r\nAccept: application/json\r\n",
                               256 * 1024);
    if (reply.status != 200) return {};
    const auto parsed = json::parse(std::string_view(reinterpret_cast<const char*>(reply.body.data()),
                                                      reply.body.size()));
    const auto* data = parsed ? parsed->get("data") : nullptr;
    const auto* name = data ? data->get("name") : nullptr;
    return name ? wide(name->stringOr()) : std::wstring{};
}

void saveName(uint32_t id, const std::wstring& title, const std::filesystem::path& cache) {
    if (title.empty()) return;
    // A torn write would leave a truncated name that the reader accepts as valid.
    atomic_file::write(cache / (std::to_wstring(id) + L".txt"), text_encoding::utf8Lenient(title));
}
} // namespace

std::unordered_map<uint32_t, std::wstring> cachedCollectionGameNames(
    const std::vector<uint32_t>& app_ids, const std::filesystem::path& cache) {
    std::unordered_map<uint32_t, std::wstring> names;
    for (uint32_t id : app_ids) {
        std::ifstream input(cache / (std::to_wstring(id) + L".txt"), std::ios::binary | std::ios::ate);
        if (!input || input.tellg() <= 0 || input.tellg() > 500) continue;
        std::string bytes(static_cast<size_t>(input.tellg()), '\0');
        input.seekg(0);
        if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) continue;
        auto title = wide(bytes);
        if (!title.empty()) names.emplace(id, std::move(title));
    }
    return names;
}

std::unordered_map<uint32_t, std::wstring> parseSteamAppListNames(
    std::string_view body, const std::unordered_set<uint32_t>& wanted) {
    std::unordered_map<uint32_t, std::wstring> names;
    size_t cursor = 0;
    while ((cursor = body.find("\"appid\"", cursor)) != std::string_view::npos) {
        const auto colon = body.find(':', cursor + 7);
        if (colon == std::string_view::npos) break;
        auto start = body.find_first_of("0123456789", colon + 1);
        if (start == std::string_view::npos || start - colon > 4) { cursor = colon + 1; continue; }
        uint32_t id = 0;
        const auto parsed = std::from_chars(body.data() + start, body.data() + body.size(), id);
        cursor = start + 1;
        if (parsed.ec != std::errc{} || !wanted.contains(id) || names.contains(id)) continue;
        const auto end = body.find('}', start);
        const auto field = body.find("\"name\"", start);
        if (end == std::string_view::npos || field == std::string_view::npos || field > end) continue;
        const auto key_end = body.find(':', field + 6);
        const auto quote = key_end == std::string_view::npos ? std::string_view::npos : body.find('"', key_end + 1);
        if (quote == std::string_view::npos || quote > end) continue;
        size_t close = quote + 1;
        bool escaped = false;
        for (; close < end; ++close) {
            if (body[close] == '"' && !escaped) break;
            if (body[close] == '\\' && !escaped) escaped = true;
            else escaped = false;
        }
        if (close >= end) continue;
        const auto decoded = json::parse(body.substr(quote, close - quote + 1));
        if (!decoded) continue;
        auto title = wide(decoded->stringOr());
        if (!title.empty()) names.emplace(id, std::move(title));
    }
    return names;
}

std::unordered_map<uint32_t, std::wstring> collectionGameNames(
    uint64_t steam_id, const std::vector<uint32_t>& app_ids,
    const std::filesystem::path& cache, const std::atomic_bool* alive,
    const std::function<void(const std::unordered_map<uint32_t, std::wstring>&)>& progress) {
    std::unordered_map<uint32_t, std::wstring> names;
    if (!steam_id) return names;
    const std::unordered_set<uint32_t> wanted(app_ids.begin(), app_ids.end());
    names = cachedCollectionGameNames(app_ids, cache);
    if (alive && !alive->load()) return names;
    std::unordered_set<uint32_t> stored;
    for (const auto& [id, title] : names) { (void)title; stored.insert(id); }
    if (names.size() < wanted.size()) ownedNames(steam_id, wanted, names);
    if (names.size() < wanted.size() && (!alive || alive->load())) publicAppNames(wanted, names);
    // Only new names: each save is a flushed atomic write, and cached ones never change here.
    for (const auto& [id, title] : names) if (!stored.contains(id)) saveName(id, title, cache);
    if (progress && !names.empty()) progress(names);
    std::vector<uint32_t> missing;
    for (uint32_t id : app_ids) if (!names.contains(id)) missing.push_back(id);
    for (size_t start = 0; start < std::min<size_t>(missing.size(), 24); ++start) {
        if (alive && !alive->load()) break;
        const uint32_t id = missing[start];
        const auto data = response(L"https://store.steampowered.com/api/appdetails?appids=" + std::to_wstring(id) +
                                   L"&filters=basic", 2 * 1024 * 1024);
        if (!data) continue;
        for (const auto& [key, entry] : data->properties) {
            (void)key;
            const auto* details = entry.get("data");
            const auto* app = details ? details->get("steam_appid") : nullptr;
            if (!app || app->intOr() != static_cast<int>(id)) continue;
            const auto* name = details ? details->get("name") : nullptr;
            if (!name) continue;
            auto title = wide(name->stringOr());
            if (title.empty()) continue;
            saveName(id, title, cache);
            names.emplace(id, std::move(title));
            break;
        }
        if (progress && start % 5 == 4 && (!alive || alive->load())) progress(names);
    }
    // SteamGridDB can know a delisted game's name even when AppList/Store do not.
    size_t attempts = 0;
    for (uint32_t id : missing) {
        if (names.contains(id) || (alive && !alive->load()) || ++attempts > 24) continue;
        auto title = gridName(id);
        if (title.empty()) continue;
        saveName(id, title, cache);
        names.emplace(id, std::move(title));
    }
    return names;
}
