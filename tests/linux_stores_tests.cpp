// Linux store discovery: Heroic (Epic, GOG), Lutris (pga.db, Battle.net in its Wine prefix) and
// Prism Launcher's Linux folders, from fixture files in a temp folder.
#include "../src/library/battlenet.hpp"
#include "../src/library/heroic.hpp"
#include "../src/library/lutris.hpp"
#include "../src/library/pc_platforms.hpp"
#include "../src/platform/text_encoding.hpp"
#include <Windows.h>
#include <sqlite3.h>
#include <sys/stat.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
namespace fs = std::filesystem;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void write(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary | std::ios::trunc) << bytes;
}
std::string number(uint64_t value) {
    std::string bytes;
    do { bytes += static_cast<char>((value & 127) | (value > 127 ? 128 : 0)); value >>= 7; } while (value);
    return bytes;
}
std::string field(unsigned id, const std::string& value) { return number((id << 3) | 2) + number(value.size()) + value; }
std::string flag(unsigned id, uint64_t value) { return number(id << 3) + number(value); }
// One product.db record as the Agent writes it (see battlenet_tests.cpp), with a Windows install path.
std::string record(const std::string& uid, const std::string& code, const std::string& path) {
    return field(1, field(1, uid) + field(2, code) + field(3, field(1, path)) + field(4, field(1, flag(1, 1) + flag(2, 1) + flag(3, 1))));
}
const Game* find(const std::vector<Game>& games, const std::string& key) {
    const auto found = std::find_if(games.begin(), games.end(), [&](const Game& game) { return gameKey(game) == key; });
    return found == games.end() ? nullptr : &*found;
}
void exec(sqlite3* db, const char* sql) {
    char* error = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &error) != SQLITE_OK) { sqlite3_free(error); throw std::runtime_error(sql); }
}
}

int main() try {
    const auto root = fs::temp_directory_path() / (L"launchme-linux-stores-tests-" + std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { fs::path root; ~Cleanup() { std::error_code ec; fs::remove_all(root, ec); } } cleanup{root};
    fs::create_directories(root);

    // Heroic: legendary (Epic) and gogdl (GOG) installs, titles, play time.
    require(heroic::isoTime("2026-09-01T22:08:25.393Z") == 1788300505 && heroic::isoTime("2000-02-29T00:00:00Z") == 951782400, "ISO time");
    require(!heroic::isoTime("2026-13-01T00:00:00Z") && !heroic::isoTime("yesterday") && !heroic::isoTime("2026-09-01T22:08:25+02:00"), "malformed ISO time");
    const auto config = root / "heroic", games = root / "Games";
    const auto hospital = games / "twopointhospital", mia = games / "Returning To Mia", cat = games / "Cat Quest II";
    for (const auto& folder : {hospital, mia, cat}) fs::create_directories(folder);
    write(config / "legendaryConfig" / "legendary" / "installed.json", R"({
        "4ad17ed617f44a4dbf7e8a960484c619": {"app_name": "4ad17ed617f44a4dbf7e8a960484c619", "title": "Two Point Hospital\u2122",
            "install_path": ")" + hospital.string() + R"(", "executable": "TPH.exe", "is_dlc": false, "platform": "Windows"},
        "dlcfixture": {"app_name": "dlcfixture", "title": "A DLC", "install_path": ")" + hospital.string() + R"(", "is_dlc": true},
        "gonefixture": {"app_name": "gonefixture", "title": "Removed", "install_path": ")" + (games / "gone").string() + R"("},
        "mismatch": {"app_name": "other", "title": "Wrong key", "install_path": ")" + hospital.string() + R"("}})");
    write(config / "legendaryConfig" / "legendary" / "metadata" / "4ad17ed617f44a4dbf7e8a960484c619.json",
        R"({"app_name":"4ad17ed617f44a4dbf7e8a960484c619","metadata":{"namespace":"0f4a572a3c1541319db70c3737d3a123","id":"b8d3e844044f4770a8167990b3ac3427"}})");
    write(config / "gog_store" / "installed.json", R"({"installed":[
        {"platform":"linux","install_path":")" + mia.string() + R"(","is_dlc":false,"appName":"1604903045"},
        {"platform":"windows","install_path":")" + cat.string() + R"(","is_dlc":false,"appName":"1958338581"},
        {"platform":"windows","install_path":")" + cat.string() + R"(","is_dlc":false,"appName":"gog-redist"}]})");
    write(config / "store_cache" / "gog_library.json", R"({"games":[{"app_name":"1604903045","title":"Returning To Mia","runner":"gog"}]})");
    write(cat / "goggame-1958338581.info", R"({"gameId":"1958338581","name":"Cat Quest II"})");
    write(config / "store" / "timestamp.json", R"({"1604903045":{"firstPlayed":"2026-06-16T19:12:38.413Z","lastPlayed":"2026-09-01T22:08:25.393Z","totalPlayed":126}})");
    require(heroic::parseLegendaryInstalled("{").empty() && heroic::parseGogInstalled("[]").empty() && heroic::parseGogTitles("null").empty(), "malformed Heroic files");
    const auto heroic_games = heroic::scan(config);
    require(heroic_games.size() == 3, "one game per installed Heroic title");
    const auto* epic = find(heroic_games, "epic:4ad17ed617f44a4dbf7e8a960484c619");
    require(epic && epic->name == L"Two Point Hospital" && epic->installed && epic->install_directory == hospital, "Epic via legendary");
    require(epic->play_action.target == L"heroic://launch?appName=4ad17ed617f44a4dbf7e8a960484c619&runner=legendary" &&
        epic->play_action.arguments.empty() && epic->play_action.kind == ActionKind::play, "Heroic launch link");
    require(epic->epic_namespace == "0f4a572a3c1541319db70c3737d3a123" && epic->epic_catalog == "b8d3e844044f4770a8167990b3ac3427", "Epic catalog IDs");
    require(primaryGameAction(*epic).target == epic->play_action.target, "store adapter plays the Heroic link");
    const auto* gog = find(heroic_games, "gog:1604903045");
    require(gog && gog->name == L"Returning To Mia" && gog->play_action.target == L"heroic://launch?appName=1604903045&runner=gog" &&
        gog->last_played == 1788300505 && gog->store_playtime_minutes == 126, "GOG via Heroic, with play time");
    const auto* fallback = find(heroic_games, "gog:1958338581");
    require(fallback && fallback->name == L"Cat Quest II" && !fallback->last_played, "GOG title from goggame-<id>.info");
    require(heroic::scan(root / "missing").empty(), "no Heroic");

    // Lutris: store games of pga.db, Battle.net in the prefix of its installer.
    require(lutris::winePrefix("game:\n  exe: drive_c/x.exe\n  prefix: '/home/u/Games/battlenet'\nwine:\n  prefix: /wrong\n") == "/home/u/Games/battlenet", "prefix of the game section");
    require(lutris::winePrefix("system:\n  prefix: /wrong\n").empty() && lutris::winePrefix("game:\n  prefix: relative\n").empty(), "no prefix");
    require(battlenet::winePath("/p", "C:\\Program Files (x86)\\Diablo IV") == fs::path("/p/drive_c/Program Files (x86)/Diablo IV") &&
        battlenet::winePath("/p", "c:/Games//X/") == fs::path("/p/drive_c/Games/X") && battlenet::winePath("/p", "D:\\X").empty() &&
        battlenet::winePath("/p", "C:\\..\\..\\etc").empty() && battlenet::winePath({}, "C:\\X").empty(), "Wine paths");
    require(battlenet::familyOfCode("pro") == "Pro" && battlenet::familyOfCode("hsb") == "WTCG" && battlenet::familyOfCode("nope").empty(), "Agent codes");
    const auto lutris_data = root / "lutris", prefix = games / "battlenet", lutris_bin = root / "bin" / "lutris";
    write(lutris_bin, "#!/bin/sh\n");
    fs::create_directories(lutris_data / "games");
    fs::create_directories(prefix / "drive_c" / "Program Files (x86)" / "Diablo IV");
    fs::create_directories(games / "witcher");
    write(lutris_data / "games" / "battlenet-1.yml", "game:\n  exe: drive_c/Program Files (x86)/Battle.net/Battle.net Launcher.exe\n  prefix: " + prefix.string() + "\nwine:\n  version: ge\n");
    const auto agent = prefix / "drive_c" / "ProgramData" / "Battle.net" / "Agent";
    write(agent / "product.db", record("fenris", "fenris", "C:\\Program Files (x86)\\Diablo IV") + record("prometheus", "pro", "C:/Program Files (x86)/Overwatch"));
    const auto database = lutris_data / "pga.db";
    {
        sqlite3* db = nullptr;
        require(sqlite3_open(database.string().c_str(), &db) == SQLITE_OK, "fixture database");
        struct Close { sqlite3* db; ~Close() { sqlite3_close(db); } } close{db};
        exec(db, "CREATE TABLE games (id INTEGER PRIMARY KEY, name TEXT, sortname TEXT, slug TEXT, installer_slug TEXT, parent_slug TEXT, platform TEXT,"
                 " runner TEXT, executable TEXT, directory TEXT, updated DATETIME, lastplayed INTEGER, installed INTEGER, installed_at INTEGER, year INTEGER,"
                 " configpath TEXT, has_custom_banner INTEGER, has_custom_icon INTEGER, has_custom_coverart_big INTEGER, playtime REAL, service TEXT,"
                 " service_id TEXT, discord_id TEXT)");
        const auto witcher = "INSERT INTO games (id, name, slug, runner, directory, lastplayed, installed, playtime, service, service_id, configpath) VALUES"
            " (3, 'The Witcher 3', 'the-witcher-3', 'wine', '" + (games / "witcher").string() + "', 1700000000, 1, 2.5, 'gog', '1207664663', 'the-witcher-3-3')";
        exec(db, witcher.c_str());
        exec(db, "INSERT INTO games (id, name, slug, runner, installed, service, service_id) VALUES"
                 " (4, 'Fortnite', 'fortnite', 'wine', 1, 'egs', 'Fortnite'),"
                 " (5, 'Overwatch 2', 'overwatch-2', 'wine', 1, 'battlenet', 'pro'),"
                 " (6, 'Portal 2', 'portal-2', 'steam', 1, 'steam', '620'),"
                 " (7, 'Doom', 'doom', 'dosbox', 1, NULL, NULL),"
                 " (8, 'Not installed', 'gog-game', 'wine', 0, 'gog', '1'),"
                 " (9, 'Assassin''s Creed', 'ac', 'wine', 1, 'ubisoft', '5c9d2e1d-uuid'),"
                 " (10, 'Mass Effect', 'me', 'wine', 1, 'ea_app', 'Origin.OFR.50.0001')");
        const auto client = "INSERT INTO games (id, name, slug, installer_slug, runner, directory, installed, configpath) VALUES"
            " (1, 'Battle.net', 'battlenet', 'battlenet-standard', 'wine', '" + prefix.string() + "', 1, 'battlenet-1')";
        exec(db, client.c_str());
    }
    const auto rows = lutris::readDatabase(database);
    require(rows && rows->size() == 9, "pga.db rows");
    require(!lutris::readDatabase(root / "missing.db") && lutris::storeOf("egs") == GameSource::epic && !lutris::storeOf("steam"), "Lutris services");
    const auto lutris_scan = lutris::scan(database, lutris_bin);
    require(lutris_scan.battlenet, "Battle.net prefix read");
    const auto* witcher = find(lutris_scan.games, "gog:1207664663");
    require(witcher && witcher->name == L"The Witcher 3" && witcher->play_action.target == lutris_bin.wstring() &&
        witcher->play_action.arguments == L"lutris:rungameid/3" && witcher->install_directory == games / "witcher" &&
        witcher->last_played == 1700000000 && witcher->store_playtime_minutes == 150, "GOG game in Lutris");
    require(find(lutris_scan.games, "epic:Fortnite") && find(lutris_scan.games, "ea:Origin.OFR.50.0001"), "Epic and EA rows");
    const auto* overwatch = find(lutris_scan.games, "battlenet:pro");
    require(overwatch && overwatch->source_id == "Pro" && overwatch->play_action.arguments == L"lutris:rungameid/5", "Battle.net row: its own Lutris game first");
    const auto* diablo = find(lutris_scan.games, "battlenet:fen");
    require(diablo && diablo->installed && diablo->install_directory == prefix / "drive_c" / "Program Files (x86)" / "Diablo IV" &&
        diablo->play_action.arguments == L"lutris:rungameid/1" && diablo->play_action.kind == ActionKind::open_client, "Battle.net in the prefix opens its client");
    require(lutris_scan.games.size() == 5, "Steam, storeless, uninstalled and invalid IDs are not listed");

    // Prism Launcher on Linux: Flatpak's wrapper keeps its data in the sandbox.
    const auto home = root / "home";
    SetEnvironmentVariableW(L"HOME", home.wstring().c_str());
    SetEnvironmentVariableW(L"XDG_DATA_HOME", nullptr);
    const auto wrapper = home / ".local" / "share" / "flatpak" / "exports" / "bin" / "org.prismlauncher.PrismLauncher";
    const auto sandbox = home / ".var" / "app" / "org.prismlauncher.PrismLauncher" / "data" / "PrismLauncher";
    write(wrapper, "#!/bin/sh\n");
    chmod(wrapper.c_str(), 0755);
    write(sandbox / "instances" / "Vanilla" / "instance.cfg", "[General]\nInstanceType=OneSix\nname=Vanilla\n");
    require(multimc::flavorOf("/usr/bin/prismlauncher") == multimc::Flavor::prism && multimc::flavorOf(wrapper) == multimc::Flavor::prism &&
        multimc::flavorOf("/x/PrismLauncher-Linux-x86_64.AppImage") == multimc::Flavor::prism && multimc::flavorOf("/usr/bin/multimc") == multimc::Flavor::multimc &&
        !multimc::flavorOf("/usr/bin/java"), "Linux launcher names");
    require(multimc::dataDirectory(wrapper, multimc::Flavor::prism) == sandbox, "Flatpak data folder");
    require(multimc::dataDirectory("/usr/bin/prismlauncher", multimc::Flavor::prism) == home / ".local" / "share" / "PrismLauncher", "XDG data folder");
    const auto found = multimc::discover();
    const auto flatpak = std::find_if(found.begin(), found.end(), [&](const auto& installation) { return installation.executable == wrapper; });
    require(flatpak != found.end() && flatpak->data == sandbox, "Flatpak Prism Launcher found");
    const auto instances = multimc::scan({*flatpak});
    require(instances.size() == 1 && instances[0].play_action.target == wrapper.wstring() && instances[0].play_action.arguments == L"--launch \"Vanilla\"", "instance launch");

    // The whole PC scan: Linux launchers' games next to the rest, Heroic as Epic's and GOG's client.
    PcDiscovery discovery;
    discovery.heroic_configs = {config};
    discovery.lutris_databases = {database};
    discovery.lutris = lutris_bin;
    discovery.multimc = {*flatpak};
    const auto library = scanNativePcLibraries(discovery);
    require(library.visible.size() == heroic_games.size() + lutris_scan.games.size() + 1 && library.battlenet_available, "native scan on Linux");
    const auto connect = connectPcClient(GameSource::battlenet, {{GameSource::battlenet, lutris_bin}});
    require(connect.target == lutris_bin.wstring() && connect.kind == ActionKind::open_client, "Lutris opens for Battle.net");
    std::cout << "Heroic, Lutris, Battle.net in Wine and Prism Launcher on Linux passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
