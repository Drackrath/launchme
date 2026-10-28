#include "../src/library/steam.hpp"
#include "../src/library/steam_accounts.hpp"
#include "../src/library/steam_stats.hpp"
#include "../src/library/steam_achievements.hpp"
#include <cmath>
#include "../src/platform/steam_downloads.hpp"

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

std::string vdfPath(const fs::path& path) {
    std::string escaped;
    for (wchar_t ch : path.wstring()) {
        if (ch == L'\\') escaped += "\\\\";
        else escaped += static_cast<char>(ch);
    }
    return escaped;
}

void manifest(const fs::path& folder, int id, const std::string& title, int state,
              int played, int updated, int size = 100) {
    fs::create_directories(folder / "steamapps");
    std::ofstream file(folder / "steamapps" / ("appmanifest_" + std::to_string(id) + ".acf"));
    file << "\"AppState\"\n{\n"
         << "  \"appid\" \"" << id << "\"\n"
         << "  \"name\" \"" << title << "\"\n"
         << "  \"StateFlags\" \"" << state << "\"\n"
         << "  \"LastPlayed\" \"" << played << "\"\n"
         << "  \"LastUpdated\" \"" << updated << "\"\n"
         << "  \"SizeOnDisk\" \"" << size << "\"\n}\n";
}

int main(int argc, char** argv) {
    uint8_t ipc[80]{}; uint32_t flags = 97; uint64_t total = 31975377684, done = 624980529;
    memcpy(ipc + 4, &flags, 4); memcpy(ipc + 64, &total, 8); memcpy(ipc + 72, &done, 8);
    const auto decoded = steamUpdateCounters(ipc);
    if (!decoded || decoded->done != done || decoded->total != total || decoded->phase != InstallPhase::downloading ||
        steamUpdateCounters(std::span<const uint8_t>{})) return 17;
    {
        // AppManager slots are matched by the method's own name: a RIP-relative LEA of its string.
        // At RVA 0x1000: nop; lea rdx, [rip+0x100] (48 8D 15) -> 0x1108; lea r8, [rip-0x10] (4C 8D 05) -> 0xFFF.
        const uint8_t lea[] = {0x90, 0x48, 0x8D, 0x15, 0x00, 0x01, 0x00, 0x00, 0x4C, 0x8D, 0x05, 0xF0, 0xFF, 0xFF, 0xFF};
        if (!referencesRva(lea, 0x1000, 0x1108) || !referencesRva(lea, 0x1000, 0x1000 + 15 - 16) ||
            referencesRva(lea, 0x1000, 0x1109) || referencesRva(std::span<const uint8_t>(lea, 7), 0x1000, 0x1108)) return 19;
        // mod=01 (a displacement from a register, not RIP) is no reference.
        const uint8_t register_based[] = {0x48, 0x8D, 0x55, 0x10, 0x00, 0x00, 0x00};
        if (referencesRva(register_based, 0x1000, 0x1017)) return 19;
    }
    done = UINT64_MAX; memcpy(ipc + 72, &done, 8);
    if (steamUpdateCounters(ipc)) return 18;
    Installation sample{InstallPhase::downloading, 0, 100};
    applyLiveDownload(sample, {});
    if (sample.percent()) return 14;
    applyLiveDownload(sample, DownloadBytes{42, 100});
    if (sample.percent() != 42 || !sample.live) return 15;
    sample.phase = InstallPhase::staging;
    applyLiveDownload(sample, DownloadBytes{60, 100});
    if (sample.phase != InstallPhase::downloading || sample.percent() != 60) return 16;
    Game not_installed{620, L"Niezainstalowana"};
    not_installed.installed = false;
    if (steamActionUrl(not_installed) != L"steam://install/620" ||
        steamActionUrl(Game{10, L"Zainstalowana"}) != L"steam://rungameid/10" ||
        !steamActionUrl(Game{}).empty()) {
        std::cerr << "Installed/uninstalled Steam action routing failed\n";
        return 1;
    }
    Game shortcut{0xFEDCBA98u, L"Skrót"};
    shortcut.kind = GameKind::nonsteam;
    shortcut.shortcut_id = (uint64_t{0xFEDCBA98u} << 32) | 0x02000000ULL;
    if (steamActionUrl(shortcut) != L"steam://rungameid/" + std::to_wstring(shortcut.shortcut_id)) {
        std::cerr << "Non-Steam shortcut Steam URI was incorrect\n";
        return 1;
    }
    {
        // Remembered accounts (loginusers.vdf as Steam writes it in 2026), the next sign-in moved to
        // another one, and the install stub Steam downloads into a picked library.
        using namespace steam_accounts;
        const std::string users =
            "\"users\"\n{\n"
            "\t\"76561198000000001\"\n\t{\n\t\t\"AccountName\"\t\t\"first\"\n\t\t\"PersonaName\"\t\t\"Pierwszy \xC5\x82\"\n"
            "\t\t\"RememberPassword\"\t\t\"1\"\n\t\t\"AutoLogin\"\t\t\"1\"\n\t\t\"timestamp\"\t\t\"1792948338\"\n\t}\n"
            "\t\"76561198000000002\"\n\t{\n\t\t\"AccountName\"\t\t\"second\"\n\t\t\"PersonaName\"\t\t\"Second\"\n"
            "\t\t\"RememberPassword\"\t\t\"1\"\n\t\t\"AutoLogin\"\t\t\"0\"\n\t}\n"
            "\t\"76561198000000003\"\n\t{\n\t\t\"AccountName\"\t\t\"third\"\n\t\t\"RememberPassword\"\t\t\"0\"\n\t}\n}\n";
        const auto accounts = parseLoginUsers(users);
        if (accounts.size() != 3 || accounts[0].steam_id != 76561198000000001ULL || accounts[0].account_name != "first" ||
            accounts[0].persona != L"Pierwszy \u0142" || !accounts[0].auto_login || !accounts[0].remember ||
            accounts[1].auto_login || accounts[2].remember || !accounts[2].persona.empty()) {
            std::cerr << "loginusers.vdf accounts were not read\n";
            return 40;
        }
        const auto moved = withAutoLogin(users, 76561198000000002ULL);
        const auto after = parseLoginUsers(moved);
        if (after.size() != 3 || after[0].auto_login || !after[1].auto_login || after[1].account_name != "second" ||
            moved.find("\"timestamp\"\t\t\"1792948338\"") == std::string::npos || !withAutoLogin(users, 76561198000000099ULL).empty()) {
            std::cerr << "The next Steam sign-in did not move to the chosen account\n";
            return 41;
        }
        // A block without AutoLogin (the older format) gets one.
        const auto third = parseLoginUsers(withAutoLogin(users, 76561198000000003ULL));
        if (third.size() != 3 || !third[2].auto_login || third[0].auto_login) { std::cerr << "AutoLogin was not added\n"; return 42; }
        if (installDirectoryName(L"Half-Life: Alyx?", 546560) != L"Half-Life Alyx" || installDirectoryName(L" ..", 7) != L"app7") {
            std::cerr << "Install folder names were not Windows-safe\n";
            return 43;
        }
        const auto stub = installManifest(620, L"Portal 2", L"C:\\Steam\\steam.exe", 76561198000000002ULL);
        if (stub.find("\"StateFlags\"\t\t\"1026\"") == std::string::npos || stub.find("\"LauncherPath\"\t\t\"C:\\\\Steam\\\\steam.exe\"") == std::string::npos ||
            stub.find("\"LastOwner\"\t\t\"76561198000000002\"") == std::string::npos || stub.find("\"installdir\"\t\t\"Portal 2\"") == std::string::npos) {
            std::cerr << "The Steam install stub was not as Steam writes it\n";
            return 44;
        }
        // The stub reads back as a game Steam is downloading, owned by the account; a second one is refused.
        const auto root = fs::temp_directory_path() / ("launchme-steam-stub-" + std::to_string(GetCurrentProcessId()));
        fs::remove_all(root);
        fs::create_directories(root / "steamapps");
        if (!writeInstallManifest(root, 620, L"Portal 2", L"C:\\Steam\\steam.exe", 76561198000000002ULL) ||
            writeInstallManifest(root, 620, L"Portal 2", L"C:\\Steam\\steam.exe", 76561198000000002ULL)) {
            std::cerr << "The Steam install stub was not written once\n";
            fs::remove_all(root);
            return 45;
        }
        std::ifstream written(root / "steamapps" / "appmanifest_620.acf", std::ios::binary);
        const std::string text{std::istreambuf_iterator<char>(written), {}};
        written.close();
        fs::remove_all(root);
        if (text != stub) { std::cerr << "The Steam install stub on disk differs\n"; return 46; }
    }
    Game platform{0xF0000002u, L"Switch"};
    platform.kind = GameKind::platform;
    if (!steamActionUrl(platform).empty()) {
        std::cerr << "Platform tile must not open a Steam game URI\n";
        return 1;
    }
    {
        // Ciche uruchamianie Steam: rungameid (games and shortcuts) goes through steam.exe -silent;
        // installs, downloads, a missing steam.exe and odd targets stay as they were.
        const std::filesystem::path exe = L"C:\\Steam\\steam.exe";
        const GameAction play{L"steam://rungameid/10", {}, {}, ActionKind::play};
        const auto silent = silentSteamAction(play, exe);
        const auto shortcut_url = steamActionUrl(shortcut);
        const auto silent_shortcut = silentSteamAction(GameAction{shortcut_url, {}, {}, ActionKind::play}, exe);
        if (silent.target != exe.wstring() || silent.arguments != L"-silent -- \"steam://rungameid/10\"" ||
            silent.working_directory != L"C:\\Steam" || !silent.keep_focus || silent.kind != ActionKind::play ||
            silent_shortcut.arguments != L"-silent -- \"" + shortcut_url + L"\"" || !silent_shortcut.keep_focus) {
            std::cerr << "Silent Steam launch did not go through steam.exe -silent\n";
            return 30;
        }
        const auto unchanged = [&](const GameAction& action, const std::filesystem::path& with) {
            const auto result = silentSteamAction(action, with);
            return result.target == action.target && result.arguments == action.arguments && !result.keep_focus;
        };
        if (!unchanged(play, {}) || !unchanged(GameAction{L"steam://install/620", {}, {}, ActionKind::install}, exe) ||
            !unchanged(GameAction{L"steam://nav/downloads", {}, {}, ActionKind::open_client}, exe) ||
            !unchanged(GameAction{L"steam://rungameid/", {}, {}, ActionKind::play}, exe) ||
            !unchanged(GameAction{L"steam://rungameid/10\" -evil", {}, {}, ActionKind::play}, exe) ||
            !unchanged(GameAction{L"C:\\Games\\game.exe", {}, {}, ActionKind::play}, exe)) {
            std::cerr << "Silent Steam launch rewrote an action it must keep\n";
            return 31;
        }
        // "Opcje uruchamiania…": steam.exe -applaunch with the game's options (silent first when on);
        // silent mode then leaves the action alone. Other games, installs and empty options stay.
        const auto options = steamOptionsAction(play, exe, 10, L"  -dx11\t-skipintro ", false);
        const auto quiet = silentSteamAction(steamOptionsAction(play, exe, 10, L"-dx11", true), exe);
        const auto kept = [&](const GameAction& action, uint32_t app, const std::wstring& line) {
            const auto result = steamOptionsAction(action, exe, app, line, false);
            return result.target == action.target && result.arguments == action.arguments && !result.keep_focus;
        };
        if (options.target != exe.wstring() || options.arguments != L"-applaunch 10 -dx11 -skipintro" || !options.keep_focus ||
            options.working_directory != L"C:\\Steam" || quiet.arguments != L"-silent -applaunch 10 -dx11" ||
            !kept(play, 10, L"  ") || !kept(play, 11, L"-dx11") || !kept(GameAction{L"steam://install/10", {}, {}, ActionKind::install}, 10, L"-dx11") ||
            !kept(GameAction{shortcut_url, {}, {}, ActionKind::play}, 10, L"-dx11")) {
            std::cerr << "Steam launch options did not go through steam.exe -applaunch\n";
            return 32;
        }
        const auto folder = std::filesystem::temp_directory_path() / L"launchme-steam-exe-test";
        std::error_code ec;
        std::filesystem::remove_all(folder, ec);
        std::filesystem::create_directories(folder, ec);
        const bool missing = steamExecutable(folder).empty();
        { std::ofstream file(folder / L"steam.exe"); file << "x"; }
        const bool found = steamExecutable(folder) == folder / L"steam.exe";
        std::filesystem::remove_all(folder, ec);
        if (!missing || !found || !steamExecutable({}).empty()) {
            std::cerr << "steam.exe lookup failed\n";
            return 32;
        }
    }
    if (argc == 2) {
        const auto library = scanSteamLibrary(fs::path(argv[1]));
        std::cout << "public=" << library.visible.size() << " private=" << library.private_games.size() << '\n';
        for (size_t i = 0; i < std::min<size_t>(8, library.visible.size()); ++i)
            std::cout << "recent_app=" << library.visible[i].app_id << '\n';
        return 0;
    }
    const auto root = fs::temp_directory_path() / (L"launchme-steam-test-" + std::to_wstring(GetCurrentProcessId()));
    const auto second = root / L"Druga biblioteka";
    fs::create_directories(root / "steamapps");
    fs::create_directories(second / "steamapps");
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); }
    } cleanup{root};
    {
        std::ofstream folders(root / "steamapps" / "libraryfolders.vdf");
        folders << "\"libraryfolders\"\n{\n\"1\"\n{\n\"path\" \""
                << vdfPath(second) << "\"\n}\n}\n";
    }
    fs::create_directories(root / "config");
    {
        std::ofstream login(root / "config" / "loginusers.vdf");
        login << "\"users\"\n{\n\"76561198000000042\"\n{\n"
              << "\"AutoLogin\" \"1\"\n\"timestamp\" \"100\"\n}\n}\n";
    }
    manifest(root, 10, "Zainstalowana", 4, 0, 300);
    manifest(root, 20, "Grana", 4, 100, 100);
    manifest(root, 30, "Powtorzona", 4, 50, 10);
    manifest(second, 30, "Powtorzona", 4, 180, 10);
    manifest(second, 40, "Niedostepna", 2, 999, 999);
    manifest(second, 50, "Pusta", 4, 999, 999, 0);
    manifest(second, 60, "Steamworks Common Redistributables", 4, 999, 999);
    manifest(second, 480, "Spacewar", 4, 999, 999);

    const auto games = scanInstalledGames(root);
    const auto limited = scanInstalledGames(root, 2);
    if (games.size() != 3 || games[0].app_id != 30 || games[0].last_played != 180 ||
        games[1].app_id != 20 || games[2].app_id != 10 ||
        limited.size() != 2 || limited[0].app_id != 30) {
        std::cerr << "Library scanning, ranking, filtering, or limit failed\n";
        return 1;
    }
    fs::create_directories(root / "config");
    const auto account = root / "userdata" / "39734314" / "config";
    fs::create_directories(account);
    {
        std::ofstream login(root / "config" / "loginusers.vdf");
        login << "\"users\"\n{\n\"76561198000000042\"\n{\n"
              << "\"AutoLogin\" \"1\"\n\"timestamp\" \"100\"\n}\n}\n";
        std::ofstream local(account / "localconfig.vdf");
        local << "\"PrivateApps_39734314\" \"[30]\"\n";
    }
    const auto library = scanSteamLibrary(root);
    if (library.visible.size() != 2 || library.visible[0].app_id != 20 ||
        library.private_games.size() != 1 || library.private_games[0].app_id != 30 ||
        library.private_games[0].visibility != GameVisibility::private_game) {
        std::cerr << "Private games leaked into the public library or disappeared entirely\n";
        return 1;
    }
    const auto download = second / "steamapps" / "appmanifest_70.acf";
    const auto write_progress = [&](uint64_t flags, uint64_t done, uint64_t total, bool closed = true) {
        std::ofstream out(download);
        out << "\"AppState\"\n{\n\"appid\" \"70\"\n\"name\" \"Installing fixture\"\n\"StateFlags\" \"" << flags << "\"\n"
            << "\"BytesDownloaded\" \"" << done << "\"\n\"BytesToDownload\" \"" << total << "\"\n"
            << "\"BytesStaged\" \"30\"\n\"BytesToStage\" \"100\"\n\"SizeOnDisk\" \"100\"\n"
            << "\"UserConfig\"\n{\n\"StateFlags\" \"4\"\n}\n";
        if (closed) out << "}\n";
    };
    const auto find_progress = [&]() -> Game {
        const auto current = scanSteamLibrary(root);
        for (const auto& game : current.visible) if (game.app_id == 70) return game;
        return {};
    };
    write_progress(1048576 + 256, 250, 1000);
    auto installing = find_progress();
    if (installing.installed || installing.installation.phase != InstallPhase::downloading || installing.installation.percent() != 25 ||
        steamActionUrl(installing) != L"steam://nav/downloads") return 2;
    write_progress(2097152 + 4, 1000, 1000);
    installing = find_progress();
    if (installing.installed || installing.installation.phase != InstallPhase::staging || installing.installation.percent() != 30) return 3;
    write_progress(131072 + 4, 1000, 1000);
    installing = find_progress();
    if (installing.installed || installing.installation.phase != InstallPhase::verifying || installing.installation.percent()) return 4;
    write_progress(512 + 1024, 250, 1000);
    installing = find_progress();
    if (installing.installation.phase != InstallPhase::paused || installing.installation.moving()) return 5;
    write_progress(8, 0, 0);
    if (find_progress().installation.phase != InstallPhase::queued || find_progress().installation.percent()) return 6;
    write_progress(1048576, UINT64_MAX, 1);
    if (find_progress().installation.percent()) return 7;
    const auto before_truncation = scanSteamLibrary(root);
    write_progress(4, 1000, 1000, false);
    if (scanSteamLibrary(root).complete) return 8;
    const auto retained = retainUnavailableSteamGames(scanSteamLibrary(root), before_truncation);
    if (std::none_of(retained.visible.begin(), retained.visible.end(), [](const Game& game) { return game.app_id == 70 && game.installation.active(); })) return 11;
    write_progress(4, 1000, 1000);
    installing = find_progress();
    if (!installing.installed || installing.installation.active() || steamActionUrl(installing) != L"steam://rungameid/70") return 9;
    SteamLibrary inaccessible;
    inaccessible.complete = false; inaccessible.unavailable_libraries.push_back(download.parent_path());
    inaccessible.excluded.insert(70);
    inaccessible = retainUnavailableSteamGames(inaccessible, retained);
    if (std::none_of(inaccessible.private_games.begin(), inaccessible.private_games.end(), [](const Game& game) { return game.app_id == 70; })) return 12;
    if (!retainUnavailableSteamGames({}, retained).visible.empty()) return 13;
    write_progress(256 + 1048576, 1, 2);
    { std::ofstream local(account / "localconfig.vdf"); local << "\"PrivateApps_39734314\" \"[30,70]\"\n"; }
    const auto private_download = scanSteamLibrary(root);
    if (private_download.private_games.size() != 2 || std::any_of(private_download.visible.begin(), private_download.visible.end(), [](const Game& g) { return g.app_id == 70; })) return 10;
    {
        std::ofstream login(root / "config" / "loginusers.vdf", std::ios::trunc);
        login << "\"users\"\n{\n}\n";
    }
    const auto unknown = scanSteamLibrary(root);
    if (!unknown.visible.empty() || !unknown.private_games.empty()) {
        std::cerr << "Public or private games exposed with no active Steam account\n";
        return 1;
    }
    // Play time and achievements of the signed-in account from the client's local files.
    {
        const auto stats_root = root / "stats";
        const auto config = stats_root / "userdata" / "42" / "config";
        fs::create_directories(config / "librarycache");
        const uint64_t steam_id = 76561197960265728ULL + 42;
        {
            std::ofstream local(config / "localconfig.vdf");
            local << "\"UserLocalConfigStore\"\n{\n\t\"Software\"\n\t{\n\t\t\"Valve\"\n\t\t{\n\t\t\t\"Steam\"\n\t\t\t{\n"
                  << "\t\t\t\t\"apps\"\n\t\t\t\t{\n\t\t\t\t\t\"1551360\"\n\t\t\t\t\t{\n\t\t\t\t\t\t\"LastPlayed\"\t\t\"1788786221\"\n"
                  << "\t\t\t\t\t\t\"Playtime\"\t\t\"754\"\n\t\t\t\t\t}\n\t\t\t\t\t\"70\"\n\t\t\t\t\t{\n\t\t\t\t\t\t\"cloud\"\n\t\t\t\t\t\t{\n"
                  << "\t\t\t\t\t\t\t\"Playtime\"\t\t\"999\"\n\t\t\t\t\t\t}\n\t\t\t\t\t}\n\t\t\t\t}\n\t\t\t}\n\t\t}\n\t}\n}\n";
        }
        { std::ofstream progress(config / "librarycache" / "achievement_progress.json");
          progress << R"({"nVersion":3,"mapCache":[[1952520,{"appid":1952520,"unlocked":0,"total":21}],[1551360,{"appid":1551360,"unlocked":7,"total":51,"percentage":13.7}]]})"; }
        { std::ofstream page(config / "librarycache" / "70.json");
          page << R"([["achievements",{"version":2,"data":{"vecHighlight":[],"nTotal":30,"nAchieved":12}}],["badge",{"version":1,"data":{}}]])"; }
        const auto portal = readSteamStats(stats_root, steam_id, 1551360); // Real AppIDs are large numbers.
        const auto nested = readSteamStats(stats_root, steam_id, 70);
        const auto other = readSteamStats(stats_root, steam_id + 1, 1551360);
        if (!portal.playtime_minutes || *portal.playtime_minutes != 754 || portal.achievements_unlocked != 7 || portal.achievements_total != 51 ||
            nested.playtime_minutes || nested.achievements_unlocked != 12 || nested.achievements_total != 30 ||
            other.playtime_minutes || other.achievements_total) {
            std::cerr << "Steam play time / achievements were not read from the account's files\n";
            return 20;
        }
        if (formatPlaytime(36) != L"36 min" || formatPlaytime(90) != L"1,5 godz." || formatPlaytime(754) != L"13 godz.") return 21;
        // The achievement list: the client's cached game page, unlocked first (newest), then the most common.
        { std::ofstream page(config / "librarycache" / "1145350.json");
          page << R"([["achievements",{"version":2,"data":{"vecHighlight":[)"
               << R"({"strID":"A","strName":"Old one","strDescription":"First.","bAchieved":true,"rtUnlocked":100,"strImage":"https://shared.steamstatic.com/a/1.jpg","flAchieved":61.3},)"
               << R"({"strID":"B","strName":"New one","strDescription":"Second.","bAchieved":true,"rtUnlocked":200,"strImage":"https://shared.steamstatic.com/a/2.jpg","flAchieved":10}],)"
               << R"("vecUnachieved":[{"strID":"C","strName":"Rare","bAchieved":false,"flAchieved":2.5},{"strID":"D","strName":"Common","bAchieved":false,"flAchieved":57.4},)"
               << R"({"strID":"A","strName":"Duplicate","bAchieved":false}],"vecAchievedHidden":[],"nTotal":50,"nAchieved":2}}]])"; }
        const auto cached = readCachedAchievements(stats_root, steam_id, 1145350);
        if (cached.total != 50 || cached.unlocked != 2 || cached.complete || cached.items.size() != 4 ||
            cached.items[0].name != L"New one" || cached.items[1].id != "A" || cached.items[2].name != L"Common" || cached.items[3].name != L"Rare" ||
            achievementIconFile(cached.items[0].icon) != L"2.jpg" || !achievementIconFile(L"https://x/../evil.exe").empty()) return 22;
        // The full list from the Web API (fake HTTP; the key never leaves the fake).
        std::vector<std::wstring> asked;
        const auto fake = [&](const std::wstring& url, const std::wstring&, size_t) -> HttpResponse {
            asked.push_back(url);
            std::string body;
            if (url.find(L"GetSchemaForGame") != std::wstring::npos && url.find(L"l=polish") != std::wstring::npos)
                body = R"({"game":{"availableGameStats":{"achievements":[{"name":"A","displayName":"Pierwsze","description":"Opis","icon":"https://cdn/a.jpg","hidden":0},)"
                       R"({"name":"S","displayName":"Sekret","description":"","icon":"https://cdn/s.jpg","hidden":1}]}}})";
            else if (url.find(L"GetPlayerAchievements") != std::wstring::npos)
                body = R"({"playerstats":{"success":true,"achievements":[{"apiname":"A","achieved":1,"unlocktime":300},{"apiname":"S","achieved":0,"unlocktime":0}]}})";
            else if (url.find(L"GetGlobalAchievementPercentagesForApp") != std::wstring::npos)
                body = R"({"achievementpercentages":{"achievements":[{"name":"A","percent":"48.5"},{"name":"S","percent":1.2}]}})";
            else return {404, {}};
            return {200, std::vector<uint8_t>(body.begin(), body.end())};
        };
        const auto full = fetchSteamAchievements(L"synthetic", steam_id, 1145350, false, fake);
        if (asked.size() != 3 || !full.complete || full.total != 2 || full.unlocked != 1 || full.items[0].name != L"Pierwsze" ||
            full.items[0].unlocked_at != 300 || std::abs(full.items[0].global_percent - 48.5f) > 0.01f || !full.items[1].hidden) return 23;
        if (!fetchSteamAchievements(L"", steam_id, 1145350, false, fake).items.empty() ||
            !fetchSteamAchievements(L"bad&key", steam_id, 1145350, false, fake).items.empty()) return 24;
        // The client's own files (appcache/stats, binary KeyValues): the whole schema and the account's unlock bits.
        struct Kv {
            std::vector<uint8_t> bytes;
            void name(uint8_t type, const std::string& key) { bytes.push_back(type); bytes.insert(bytes.end(), key.begin(), key.end()); bytes.push_back(0); }
            Kv& begin(const std::string& key) { name(0, key); return *this; }
            Kv& end() { bytes.push_back(8); return *this; }
            Kv& text(const std::string& key, const std::string& value) { name(1, key); bytes.insert(bytes.end(), value.begin(), value.end()); bytes.push_back(0); return *this; }
            Kv& number(const std::string& key, int32_t value) { name(2, key); const auto* raw = reinterpret_cast<const uint8_t*>(&value); bytes.insert(bytes.end(), raw, raw + 4); return *this; }
        };
        Kv schema;
        schema.begin("1145350").begin("stats").begin("3").text("type", "ACHIEVEMENTS").begin("bits")
            .begin("0").text("name", "AchA").begin("display").begin("name").text("english", "Witch").text("polish", "Wiedźma").text("token", "T").end()
                .begin("desc").text("english", "Clear").text("polish", "Ukończ").end().number("hidden", 0).text("icon", "a.jpg").text("icon_gray", "ag.jpg").end().end()
            .begin("1").text("name", "AchB").begin("display").begin("name").text("english", "Second").end().number("hidden", 1)
                .text("icon", "b.jpg").text("icon_gray", "bg.jpg").end().end()
            .begin("2").text("name", "AchC").begin("display").begin("name").text("english", "Third").end().text("icon", "c.jpg").end().end()
            .end().end().end().end().end();
        Kv user;
        user.begin("cache").number("crc", 1).number("PendingChanges", 0).begin("3").number("data", 5)
            .begin("AchievementTimes").number("0", 100).number("2", 200).end().end().end().end();
        const auto stats_folder = stats_root / "appcache" / "stats";
        fs::create_directories(stats_folder);
        { std::ofstream file(stats_folder / "UserGameStatsSchema_1145350.bin", std::ios::binary); file.write(reinterpret_cast<const char*>(schema.bytes.data()), static_cast<std::streamsize>(schema.bytes.size())); }
        { std::ofstream file(stats_folder / "UserGameStats_42_1145350.bin", std::ios::binary); file.write(reinterpret_cast<const char*>(user.bytes.data()), static_cast<std::streamsize>(user.bytes.size())); }
        const auto local = readLocalAchievements(stats_root, steam_id, 1145350, false);
        if (!local.complete || local.total != 3 || local.unlocked != 2 || local.items.size() != 3 ||
            local.items[0].id != "AchC" || local.items[0].unlocked_at != 200 || local.items[1].name != L"Wiedźma" ||
            local.items[1].description != L"Ukończ" || local.items[2].achieved || !local.items[2].hidden ||
            !local.items[2].icon.ends_with(L"/1145350/bg.jpg") || !local.items[1].icon.ends_with(L"/a.jpg")) return 25;
        // The exact counts win over the game page cache (2 / 50 there).
        const auto counted = readSteamStats(stats_root, steam_id, 1145350);
        if (counted.achievements_unlocked != 2 || counted.achievements_total != 3) return 26;
        if (binary_kv::parse(std::vector<uint8_t>{0, 'a'}) || binary_kv::parse(std::vector<uint8_t>{9, 'x', 0})) return 27; // Truncated / unknown type.
        // Bit 31 set: Steam's signed 32-bit "data" is negative (-1 when all 32 are unlocked).
        Kv high_schema;
        high_schema.begin("1145351").begin("stats").begin("1").text("type", "ACHIEVEMENTS").begin("bits")
            .begin("0").text("name", "Low").end()
            .begin("31").text("name", "High").end()
            .begin("30").text("name", "Locked").end()
            .end().end().end().end().end();
        Kv high_user;
        high_user.begin("cache").begin("1").number("data", static_cast<int32_t>(0x80000001u)).end().end().end();
        { std::ofstream file(stats_folder / "UserGameStatsSchema_1145351.bin", std::ios::binary); file.write(reinterpret_cast<const char*>(high_schema.bytes.data()), static_cast<std::streamsize>(high_schema.bytes.size())); }
        { std::ofstream file(stats_folder / "UserGameStats_42_1145351.bin", std::ios::binary); file.write(reinterpret_cast<const char*>(high_user.bytes.data()), static_cast<std::streamsize>(high_user.bytes.size())); }
        const auto high = readLocalAchievements(stats_root, steam_id, 1145351, true);
        if (high.total != 3 || high.unlocked != 2 || std::any_of(high.items.begin(), high.items.end(), [](const SteamAchievement& item) {
                return item.achieved != (item.id != "Locked"); })) return 28;
    }
    // A library listed again with other case, separators or a trailing slash is read once.
    {
        const auto twice = root / L"twice";
        fs::create_directories(twice / "config");
        { std::ofstream login(twice / "config" / "loginusers.vdf");
          login << "\"users\"\n{\n\"76561198000000042\"\n{\n\"AutoLogin\" \"1\"\n\"timestamp\" \"100\"\n}\n}\n"; }
        manifest(twice, 10, "Raz", 4, 5, 5);
        auto upper = vdfPath(twice);
        for (auto& ch : upper) if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
        const auto missing = vdfPath(root / L"Missing Library");
        std::string missing_other = missing + "\\\\";
        for (auto& ch : missing_other) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
        { std::ofstream folders(twice / "steamapps" / "libraryfolders.vdf");
          folders << "\"libraryfolders\"\n{\n\"0\"\n{\n\"path\" \"" << upper << "\"\n}\n\"1\"\n{\n\"path\" \"" << missing
                  << "\"\n}\n\"2\"\n{\n\"path\" \"" << missing_other << "\"\n}\n}\n"; }
        const auto scanned = scanSteamLibrary(twice);
        if (scanned.visible.size() != 1 || scanned.unavailable_libraries.size() != 1) {
            std::cerr << "A Steam library listed twice was scanned twice\n";
            return 29;
        }
    }
    // A preload (another account's, before the release): downloaded, held back as not released.
    // Listed, neither installed nor installing, its action the game's page in Steam.
    {
        const auto preload = root / L"preload";
        fs::create_directories(preload / "config");
        { std::ofstream login(preload / "config" / "loginusers.vdf");
          login << "\"users\"\n{\n\"76561198000000042\"\n{\n\"AutoLogin\" \"1\"\n\"timestamp\" \"100\"\n}\n}\n"; }
        fs::create_directories(preload / "steamapps");
        { std::ofstream out(preload / "steamapps" / "appmanifest_4115450.acf");
          out << "\"AppState\"\n{\n\"appid\" \"4115450\"\n\"name\" \"Preload fixture\"\n\"StateFlags\" \"1026\"\n\"installdir\" \"PBZ\"\n"
              << "\"SizeOnDisk\" \"0\"\n\"buildid\" \"0\"\n\"LastOwner\" \"76561198686086405\"\n\"UpdateResult\" \"25\"\n"
              << "\"BytesToDownload\" \"148293236640\"\n\"BytesDownloaded\" \"148293236640\"\n\"BytesToStage\" \"0\"\n\"BytesStaged\" \"0\"\n"
              << "\"TargetBuildID\" \"26213077\"\n}\n"; }
        const auto scanned = scanSteamLibrary(preload);
        const auto found = std::find_if(scanned.visible.begin(), scanned.visible.end(), [](const Game& game) { return game.app_id == 4115450; });
        if (found == scanned.visible.end() || !found->preloaded || found->installed || found->installation.active() ||
            steamActionUrl(*found) != L"steam://nav/games/details/4115450" || std::wstring(primaryActionLabel(*found)) != L"Wstępnie pobrana") {
            std::cerr << "A finished preload was not listed as preloaded\n";
            return 30;
        }
        // Still downloading (Downloading flag, bytes missing): an ordinary download.
        { std::ofstream out(preload / "steamapps" / "appmanifest_4115450.acf");
          out << "\"AppState\"\n{\n\"appid\" \"4115450\"\n\"name\" \"Preload fixture\"\n\"StateFlags\" \"1049602\"\n\"SizeOnDisk\" \"0\"\n\"buildid\" \"0\"\n"
              << "\"BytesToDownload\" \"1000\"\n\"BytesDownloaded\" \"400\"\n}\n"; }
        const auto downloading = scanSteamLibrary(preload);
        const auto again = std::find_if(downloading.visible.begin(), downloading.visible.end(), [](const Game& game) { return game.app_id == 4115450; });
        if (again == downloading.visible.end() || again->preloaded || again->installation.phase != InstallPhase::downloading) return 31;
    }
    return 0;
}
