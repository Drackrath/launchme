#include "../src/library/pc_platforms.hpp"
#include <Windows.h>
#include <fstream>
#include <iostream>
#include <algorithm>

namespace {
std::string escaped(const std::filesystem::path& path) {
    const auto text = path.generic_string();
    return text;
}
std::string proto(unsigned id, const std::string& value) {
    std::string result(1, static_cast<char>((id << 3) | 2));
    size_t size = value.size();
    do { result += static_cast<char>((size & 127) | (size > 127 ? 128 : 0)); size >>= 7; } while (size);
    return result + value;
}
}

int main() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / (L"launchme-pc-tests-" + std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{root};
    PcDiscovery input;
    for (const auto source : pc_sources) {
        const auto folder = root / sourceSlug(source);
        fs::create_directories(folder);
        std::ofstream(folder / "client.exe") << "fixture";
        input.clients.push_back({source, folder / "client.exe"});
    }
    const auto gog = root / "gog", ea = root / "ea", ubi = root / "ubisoft", epic = root / "epic", battle = root / "battlenet";
    std::ofstream(gog / "goggame-123.info") << R"({"gameId":"123","rootGameId":"123","name":"GOG Fixture"})";
    input.installs.push_back({GameSource::gog, "123", L"Old title", gog});
    std::ofstream(gog / "goggame-124.info") << R"({"gameId":"124","rootGameId":"123","name":"DLC"})";
    input.installs.push_back({GameSource::gog, "124", L"DLC", gog});
    fs::create_directories(ea / "__Installer");
    std::ofstream(ea / "__Installer" / "installerdata.xml") << "<DiPManifest><contentIDs><contentID>OFB-EAST:55</contentID></contentIDs></DiPManifest>";
    input.installs.push_back({GameSource::ea, {}, L"EA Fixture", ea});
    input.installs.push_back({GameSource::ubisoft, "456", {}, fs::path(ubi.wstring() + L"\\")});
    input.xbox.push_back({"Fixture.Game_8wekyb3d8bbwe", L"Xbox Fixture", L"Game", root / "xbox"});
    input.xbox.push_back({"Fixture.Evil_8wekyb3d8bbwe", L"Injected", L"Game\" /evil", root / "xbox"}); // Not an application ID.
    input.xbox.push_back({"Fixture.Gone_8wekyb3d8bbwe", L"Removed", L"Game", root / "xbox-removed"});  // No folder.
    input.battlenet_database = root / "product.db";
    const auto installed = proto(1, "fenris") + proto(2, "fenris") + proto(3, proto(1, escaped(battle))) +
        proto(4, proto(1, std::string("\x08\x01\x10\x01\x18\x01", 6)));
    std::ofstream(input.battlenet_database, std::ios::binary) << proto(1, installed) << proto(1, proto(1, "unknown") + proto(2, "unknown"));
    input.epic_manifests = root / "manifests";
    fs::create_directories(input.epic_manifests);
    std::ofstream(epic / "game.exe") << "fixture";
    const auto epic_manifest = std::string(R"({"AppName":"EpicFixture","CatalogNamespace":"space","CatalogItemId":"item","DisplayName":"Epic Fixture","LaunchExecutable":"game.exe","InstallLocation":")") + escaped(epic) + R"(","AppCategories":["games"]})";
    std::ofstream(input.epic_manifests / "game.item") << "\xef\xbb\xbf" << epic_manifest;
    std::ofstream(input.epic_manifests / "duplicate.item") << epic_manifest;
    std::ofstream(input.epic_manifests / "unfinished.item") << R"({"AppName":"Incomplete","bIsIncompleteInstall":true})";
    auto result = scanNativePcLibraries(input);
    if (result.visible.size() != pc_sources.size()) { std::cerr << "Expected one game per native source, got " << result.visible.size() << '\n'; return 1; }
    for (const auto source : pc_sources) {
        const auto game = std::find_if(result.visible.begin(), result.visible.end(), [&](const Game& g) { return g.source == source; });
        if (game == result.visible.end() || game->app_id || !primaryGameAction(*game)) return 2;
        const auto connection = connectPcClient(source, input.clients);
        if (connection.kind != ActionKind::open_client || !connection.arguments.empty() || connection.target.starts_with(L"http")) return 3;
        if (source == GameSource::epic && game->play_action.target != L"com.epicgames.launcher://apps/space%3Aitem%3AEpicFixture?action=launch&silent=true") return 4;
        if (source == GameSource::ea && game->play_action.target != L"origin2://game/launch?offerIds=OFB-EAST%3A55") return 5;
        if (source == GameSource::battlenet && game->play_action.arguments != L"--exec=\"launch Fen\"") return 6;
        if (source == GameSource::xbox && (game->play_action.target != L"shell:AppsFolder\\Fixture.Game_8wekyb3d8bbwe!Game" ||
            game->name != L"Xbox Fixture" || game->install_directory != root / "xbox" ||
            connection.target != L"shell:AppsFolder\\Microsoft.GamingApp_8wekyb3d8bbwe!Microsoft.Xbox.App")) return 13;
    }
    if (connectPcClient(GameSource::xbox, {}).target != L"ms-windows-store://pdp/?ProductId=9MV0B5HZVK9Z") return 14;
    const auto catalogs = root / "catalogs";
    fs::create_directories(catalogs);
    std::ofstream(catalogs / "gog.json") << R"({"version":1,"games":[{"id":"123","name":"Private fixture","installed":false,"visibility":"private"}]})";
    std::ofstream(catalogs / "battlenet.json") << R"({"version":1,"games":[{"id":"fen","name":"Hidden fixture","installed":true,"visibility":"hidden"}]})";
    const auto merged = mergePcLibraries(result, catalogs);
    if (merged.visible.size() != pc_sources.size() - 2 || merged.private_games.size() != 2 || !merged.private_games[0].installed) return 7;
    std::ofstream(catalogs / "gog.json", std::ios::trunc) << "{";
    const auto broken = mergePcLibraries(result, catalogs);
    if (broken.visible.size() != pc_sources.size() - 2 || broken.private_games.size() != 1 || broken.errors.empty()) return 8;
    input.clients.clear();
    fs::create_directories(gog / "bin");
    std::ofstream(gog / "bin" / "game.exe") << "fixture";
    std::ofstream(gog / "goggame-123.info", std::ios::trunc) << R"({"gameId":"123","name":"Offline game","playTasks":[{"isPrimary":true,"type":"FileTask","path":"bin/game.exe","workingDir":"bin","arguments":"--windowed"}]})";
    const auto offline = scanNativePcLibraries(input);
    const auto game = std::find_if(offline.visible.begin(), offline.visible.end(), [](const Game& g) { return g.source == GameSource::gog; });
    if (game == offline.visible.end() || fs::path(game->play_action.target).lexically_normal() != (gog / "bin" / "game.exe").lexically_normal() ||
        game->play_action.working_directory != gog / "bin" || game->play_action.arguments != L"--windowed") return 9;
    if (!connectPcClient(GameSource::epic, {}).target.starts_with(L"https://store.epicgames.com/")) return 10;
    if (connectPcClient(GameSource::gog, {}).target != L"https://www.gog.com/account") return 11;
    // Epic applications that are not games (the Twinmotion trial) never reach the library.
    Game trial; trial.source = GameSource::epic; trial.kind = GameKind::store; trial.source_id = "twinmotiontrial"; trial.name = L"Twinmotion 2023.1 Trial";
    Game owned_game = trial; owned_game.source_id = "Fortnite"; owned_game.name = L"Fortnite";
    StoreLibrary local; local.visible.push_back(trial);
    const auto without_apps = mergeEpicLibrary(local, {trial, owned_game}, "account");
    if (without_apps.visible.size() != 1 || without_apps.visible[0].name != L"Fortnite" || !epicExcludedTitle(L"TWINMOTION") || epicExcludedTitle(L"Twine")) return 12;
    std::cout << "Native PC manifests/registry inputs, store actions, offline GOG and visibility overrides passed\n";
}
