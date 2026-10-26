// The plugin's requests: which source fits the game, the store-request rounds for GOG and Epic
// (the host makes them with the signed-in account) and RetroAchievements' lookups, cached in
// the plugin's data folder.
#include "achievements.hpp"
#include "../../src/model/key_codec.hpp"
#include "../../src/net/url_encoding.hpp"
#include "../../src/platform/atomic_file.hpp"
#include "../../src/platform/text_encoding.hpp"

#include <chrono>
#include <charconv>
#include <fstream>

namespace achievements {
using namespace plugins::wire;
namespace {
json::Value none() { auto out = object(); out.properties["achievements"] = json::Value{}; return out; }
json::Value message(const std::string& text) { auto out = object(); out.properties["message"] = string(text); return out; }
json::Value storeRequest(const std::string& id, const std::string& account, const std::wstring& url, const std::string& body = {}) {
    auto request = object();
    request.properties["id"] = string(id); request.properties["account"] = string(account);
    request.properties["method"] = string(body.empty() ? "GET" : "POST");
    request.properties["url"] = string(text_encoding::utf8Lenient(url));
    if (!body.empty()) request.properties["body"] = string(body);
    return request;
}
json::Value requests(std::vector<json::Value> list, json::Value state = {}) {
    auto out = object();
    auto& items = out.properties["storeRequests"]; items.type = json::Value::Type::array;
    items.elements = std::move(list);
    if (state.type != json::Value::Type::null_value) out.properties["state"] = std::move(state);
    return out;
}
// The answer to one of the last round's requests: {status, body}.
std::optional<std::pair<uint32_t, std::string>> response(const json::Value& params, std::string_view id) {
    for (const auto& item : field(params, "responses").elements)
        if (text(item, "id") == id) return std::pair{static_cast<uint32_t>(field(item, "status").numeric), text(item, "body")};
    return {};
}
bool flag(const json::Value& value, std::string_view key) { return field(value, key).type == json::Value::Type::boolean && field(value, key).flag; }
std::string lowered(std::string text) {
    for (auto& c : text) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return text;
}
std::string body(const HttpResponse& response) { return {response.body.begin(), response.body.end()}; }
}

Service::Service()
    : get([](const std::wstring& url) { HttpBudget budget(20000); return httpGetPrivate(url, L"Accept: application/json\r\n", 16 * 1024 * 1024); }),
      clock([] { return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()); }) {}

json::Value Service::dispatch(const json::Value& input) {
    const auto method = text(input, "method");
    const auto& params = field(input, "params");
    if (method == "activate") { data_ = text_encoding::wideLenient(text(params, "dataDirectory")); return object(); }
    if (method == "deactivate") { epic_schemas_.clear(); return object(); }
    if (method == "achievements") return answer(params, field(input, "settings"), text(input, "locale"));
    return object();
}

json::Value Service::answer(const json::Value& params, const json::Value& settings, const std::string& locale) {
    polish_ = locale == "pl";
    const auto game = gameInfo(field(params, "game"));
    const bool refresh = flag(params, "refresh");
    if (game.source == "gog" && !game.source_id.empty()) return gog(game, params);
    if (game.source == "epic") return epic(game, params, locale);
    if (!game.shortcut && game.rom.empty()) return none();
    // Emulated games: the PlayStation emulators' own trophies, else RetroAchievements.
    const auto platform = lowered(game.platform);
    const auto setting = [&](const char* id) { return std::filesystem::path(text_encoding::wideLenient(text(settings, id))); };
    if ((platform.find("ps3") != std::string::npos || platform.find("playstation 3") != std::string::npos) && !setting("rpcs3").empty()) {
        if (const auto folder = rpcs3TrophyFolder(setting("rpcs3"), ps3CommunicationId(game.rom, game.install_directory)))
            if (const auto list = readRpcs3(*folder)) return listJson(*list);
        return none();
    }
    if ((platform.find("ps4") != std::string::npos || platform.find("playstation 4") != std::string::npos) && !setting("shadps4").empty()) {
        if (const auto list = readShadps4(setting("shadps4"), game)) return listJson(*list);
        return none();
    }
    return retro(game, settings, refresh);
}

json::Value Service::gog(const GameInfo& game, const json::Value& params) {
    if (!flag(field(params, "accounts"), "gog")) return none(); // Without the GOG account: nothing to ask.
    const auto reply = response(params, "gog");
    if (!reply) return requests({storeRequest("gog", "gog", gogUrl(game.source_id))});
    if (reply->first == 404) return none();
    if (reply->first != 200) return message(reply->first == 401 || reply->first == 403 ? say("GOG: połącz konto ponownie.", "GOG: sign in again.") : say("GOG nie odpowiada.", "GOG did not answer."));
    const auto list = parseGog(reply->second);
    if (!list) return message(say("GOG: nieoczekiwana odpowiedź.", "GOG: unexpected answer."));
    return list->items.empty() ? none() : listJson(*list);
}

json::Value Service::epic(const GameInfo& game, const json::Value& params, const std::string& locale) {
    if (!flag(field(params, "accounts"), "epic") || game.epic_namespace.empty()) return none();
    const auto sandbox = game.epic_namespace;
    if (const auto player = response(params, "player")) {
        const auto schema = epic_schemas_.find(text(field(params, "state"), "sandbox"));
        if (schema == epic_schemas_.end()) return message(say("Epic: spróbuj ponownie.", "Epic: try again."));
        auto found = std::move(schema->second);
        epic_schemas_.erase(schema);
        if (player->first == 200) applyEpicPlayer(found, player->second); // Otherwise the list stays, all locked.
        return listJson(found.list);
    }
    if (const auto reply = response(params, "schema")) {
        if (reply->first != 200) return message(say("Epic nie odpowiada.", "Epic did not answer."));
        auto schema = parseEpicSchema(reply->second);
        if (!schema || schema->list.items.empty() || schema->product.empty()) return none();
        const auto product = schema->product;
        if (epic_schemas_.size() > 16) epic_schemas_.clear();
        epic_schemas_[sandbox] = std::move(*schema);
        auto state = object(); state.properties["sandbox"] = string(sandbox);
        return requests({storeRequest("player", "epic", epic_graphql, epicPlayerQuery(product))}, std::move(state));
    }
    return requests({storeRequest("schema", "epic", epic_graphql, epicSchemaQuery(sandbox, locale == "pl" ? "pl" : "en-US"))});
}

json::Value Service::retro(const GameInfo& game, const json::Value& settings, bool refresh) {
    const auto user = text(settings, "ra_user"), key = text(settings, "ra_key");
    const int console = retroConsole(game.platform, game.rom);
    if (user.empty() || key.empty() || !console) return none();
    const auto auth = L"z=" + url_encoding::component(user) + L"&y=" + url_encoding::component(key);
    const std::wstring api = L"https://retroachievements.org/API/";
    // The game's RA ID: remembered per game (a miss for a week), else by the ROM's hash, else by title.
    const auto id_file = "ra-" + key_codec::encode(game.key) + ".id";
    std::optional<uint32_t> id;
    if (const auto known = cached(id_file, refresh ? 0 : 7 * 86400)) {
        uint32_t value = 0;
        if (std::from_chars(known->data(), known->data() + known->size(), value).ec == std::errc{}) {
            if (!value) return none();
            id = value;
        }
    }
    if (!id) {
        if (const auto hash = retroHash(console, game.rom); !hash.empty()) {
            const auto reply = get(L"https://retroachievements.org/dorequest.php?r=gameid&m=" + text_encoding::wideLenient(hash));
            if (reply.status == 200) id = parseRetroGameId(body(reply));
            else if (reply.status == 0 || reply.status >= 500) return message(say("RetroAchievements nie odpowiada.", "RetroAchievements did not answer."));
        }
        if (!id) {
            const auto list_file = "ra-console-" + std::to_string(console) + ".json";
            auto list = cached(list_file, 7 * 86400);
            if (!list) {
                const auto reply = get(api + L"API_GetGameList.php?" + auth + L"&i=" + std::to_wstring(console) + L"&f=1");
                if (reply.status == 401 || reply.status == 403) return message(say("RetroAchievements: sprawdź nazwę użytkownika i klucz Web API.", "RetroAchievements: check the user name and Web API key."));
                if (reply.status != 200) return message(say("RetroAchievements nie odpowiada.", "RetroAchievements did not answer."));
                list = body(reply);
                cache(list_file, *list);
            }
            id = retroGameByTitle(*list, game.name);
        }
        cache(id_file, std::to_string(id.value_or(0)));
        if (!id) return none();
    }
    const auto reply = get(api + L"API_GetGameInfoAndUserProgress.php?" + auth + L"&u=" + url_encoding::component(user) + L"&g=" + std::to_wstring(*id));
    if (reply.status == 401 || reply.status == 403) return message(say("RetroAchievements: sprawdź nazwę użytkownika i klucz Web API.", "RetroAchievements: check the user name and Web API key."));
    if (reply.status != 200) return message(say("RetroAchievements nie odpowiada.", "RetroAchievements did not answer."));
    const auto list = parseRetroProgress(body(reply));
    if (!list) return message(say("RetroAchievements: nieoczekiwana odpowiedź.", "RetroAchievements: unexpected answer."));
    return list->items.empty() ? none() : listJson(*list);
}

std::optional<std::string> Service::cached(const std::string& name, uint64_t max_age) const {
    if (data_.empty()) return {};
    const auto file = data_ / text_encoding::wideLenient(name);
    std::error_code ec;
    const auto written = std::filesystem::last_write_time(file, ec);
    if (ec) return {};
    const auto age = std::chrono::duration_cast<std::chrono::seconds>(std::filesystem::file_time_type::clock::now() - written).count();
    if (age < 0 || static_cast<uint64_t>(age) > max_age) return {};
    std::ifstream in(file, std::ios::binary);
    std::string out((std::istreambuf_iterator<char>(in)), {});
    return out;
}

void Service::cache(const std::string& name, const std::string& value) const {
    if (!data_.empty()) atomic_file::write(data_ / text_encoding::wideLenient(name), value);
}
}
