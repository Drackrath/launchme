#pragma once
// launchme.achievements: achievements of games outside Steam for Szczegóły → Osiągnięcia
// (contribution kind "achievements", docs/plugins/API.md). Sources and the formats they speak
// follow SuccessStory by Lacro59 (MIT, see NOTICE.md); this is a separate C++ implementation.
//   GOG      gameplay.gog.com with the signed-in GOG account (a store request the host makes).
//   Epic     the store's GraphQL with the signed-in Epic account (store requests).
//   RetroAchievements  emulated games: the ROM's hash (or title) and the user's Web API key.
//   RPCS3 / shadPS4    PS3 / PS4 trophies from the emulators' own files.
#include "../../src/net/http.hpp"
#include "../../src/plugins/json.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <tuple>
#include <optional>
#include <string>
#include <vector>

namespace achievements {
struct Item {
    std::string id, name, description, icon; // icon: https URL or a local picture.
    bool achieved = false, hidden = false;
    uint64_t unlocked_at = 0;                 // Unix seconds; 0 unknown or locked.
    double percent = -1;                      // Share of players; -1 unknown.
};
struct List {
    std::string source;                       // "GOG", "Epic", "RetroAchievements", "RPCS3", "shadPS4".
    std::vector<Item> items;
};
// What the host says about the game (params.game).
struct GameInfo {
    std::string key, name, source, source_id, platform, epic_namespace;
    std::filesystem::path rom, install_directory, executable;
    bool shortcut = false;
};
GameInfo gameInfo(const json::Value& game);
json::Value listJson(const List& list);

// "2023-05-01T12:34:56+00:00", "…Z", "…+0000", "2023-05-01 12:34:56" (UTC), with or without
// fractions; 0 when it is not a date.
uint64_t parseIsoTime(std::string_view text);

// GOG: GET https://gameplay.gog.com/clients/<product>/users/{account}/achievements.
std::wstring gogUrl(const std::string& product);
std::optional<List> parseGog(std::string_view body);

// Epic: the store's GraphQL. The schema of a sandbox (the game's namespace), then the player's
// unlocks of its product; "{account}" is filled in by the host.
inline constexpr wchar_t epic_graphql[] = L"https://launcher.store.epicgames.com/graphql";
std::string epicSchemaQuery(const std::string& sandbox, const std::string& locale);
std::string epicPlayerQuery(const std::string& product);
struct EpicSchema {
    std::string product; List list; // Locked texts until the player's answer.
    std::vector<std::tuple<std::string, std::string, std::string>> unlocked; // Per item: name, description, icon once earned.
};
std::optional<EpicSchema> parseEpicSchema(std::string_view body);
// Marks the player's unlocks on the schema's list; false when the answer is not one.
bool applyEpicPlayer(EpicSchema& schema, std::string_view body);

// RetroAchievements.
using Get = std::function<HttpResponse(const std::wstring& url)>;
int retroConsole(const std::string& platform, const std::filesystem::path& rom); // 0: not a console RA knows here.
// RA's hash of a ROM for its console (rcheevos' cartridge rules; a zip's first file); empty when
// the console is hashed another way (discs) or the file cannot be read.
std::string retroHash(int console, const std::filesystem::path& rom);
std::optional<uint32_t> parseRetroGameId(std::string_view body);       // dorequest.php?r=gameid
std::optional<uint32_t> retroGameByTitle(std::string_view game_list, const std::string& title); // API_GetGameList
std::optional<List> parseRetroProgress(std::string_view body);          // API_GetGameInfoAndUserProgress
std::string md5Hex(const std::vector<uint8_t>& bytes);

// RPCS3: <rpcs3>\dev_hdd0\home\<user>\trophy\<NPWR…>\ (TROPCONF.SFM, TROPUSR.DAT, TROPnnn.PNG).
std::string ps3CommunicationId(const std::filesystem::path& rom, const std::filesystem::path& install);
std::optional<std::filesystem::path> rpcs3TrophyFolder(const std::filesystem::path& rpcs3, const std::string& npcommid);
std::optional<List> readRpcs3(const std::filesystem::path& folder);
// TROPUSR.DAT: trophy id → unlock time (unix; 0 unlocked at an unknown time). nullopt: not one.
std::optional<std::vector<std::pair<uint32_t, std::optional<uint64_t>>>> parseTropusr(const std::vector<uint8_t>& bytes);
// shadPS4: <shadps4>\user\game_data\<CUSA…>\trophyfiles\trophy00\Xml\TROP.XML.
std::optional<List> readShadps4(const std::filesystem::path& shadps4, const GameInfo& game);
std::optional<List> parseTropXml(std::string_view xml, const std::filesystem::path& icons);

// The plugin: activate, deactivate and achievements rounds (store requests for GOG and Epic).
class Service {
public:
    Get get;               // RetroAchievements; injectable, tests never reach the service.
    std::function<uint64_t()> clock;
    Service();
    json::Value dispatch(const json::Value& input);
private:
    std::filesystem::path data_;
    std::map<std::string, EpicSchema> epic_schemas_; // Between rounds, by sandbox.
    bool polish_ = false; // The request's locale, for messages.
    std::string say(const char* pl, const char* en) const { return polish_ ? pl : en; }
    json::Value answer(const json::Value& params, const json::Value& settings, const std::string& locale);
    json::Value gog(const GameInfo&, const json::Value& params);
    json::Value epic(const GameInfo&, const json::Value& params, const std::string& locale);
    json::Value retro(const GameInfo&, const json::Value& settings, bool refresh);
    std::optional<std::string> cached(const std::string& name, uint64_t max_age) const;
    void cache(const std::string& name, const std::string& body) const;
};
}
