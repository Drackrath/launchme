#include "../src/library/store_adapter.hpp"
#include "../src/library/steam.hpp"
#include "../src/library/collections.hpp"
#include "../src/library/search.hpp"
#include "../src/artwork/cover_cache.hpp"
#include "../src/artwork/platform_logo.hpp"

#include <Windows.h> // Linux: tests/linux_compat (GetCurrentProcessId, GetEnvironmentVariableW).

#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>

namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; ++failures; }
}
constexpr auto catalog = R"({"version":1,"games":[
 {"id":"10","name":"Public game","installed":true,"visibility":"public","last_played":"1720000000"},
 {"id":"20","name":"Hidden game","installed":true,"visibility":"hidden"},
 {"id":"30","name":"Private game","installed":false,"visibility":"private"},
 {"id":"40","name":"Uninstalled game","installed":false,"visibility":"public"}]})";
}

int main() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / (L"launchme-store-tests-" + std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{root};
    fs::create_directories(root);
    std::set<std::string> identities;
    std::set<fs::path> cover_paths;
    Game steam{10, L"Steam game"};
    identities.insert(gameKey(steam));
    cover_paths.insert(gameCoverPath(steam, root / "covers"));
    std::vector<Game> public_games{steam};
    for (const auto source : {GameSource::gog, GameSource::battlenet, GameSource::ea, GameSource::ubisoft}) {
        const auto adapter = makeStoreAdapter(source);
        check(adapter && adapter->source() == source, "Wrong adapter source");
        auto library = parseStoreCatalog(source, catalog);
        check(library.errors.empty() && library.visible.size() == 2 && library.private_games.size() == 2,
              "Catalog visibility/installation parsing failed");
        if (library.visible.size() != 2) continue;
        auto game = library.visible.front();
        check(game.app_id == 0 && game.source_id == "10" && game.last_played == 1720000000,
              "Store ID or timestamp was truncated/coerced to Steam");
        check(identities.insert(gameKey(game)).second, "Store identity collision");
        check(cover_paths.insert(gameCoverPath(game, root / "covers")).second, "Store cache collision");
        game.app_id = 10; // Even malformed legacy data must not leak into Steam actions/collections.
        check(steamActionUrl(game).empty(), "External game launched through Steam");
        check(!adapter->action(steam), "Adapter accepted another store's game");
        public_games.push_back(game);
        const auto png = platformLogoFilename(activePlatformName(game));
        check(png.starts_with(L"publisher_") && png.ends_with(L".png"), "Missing store badge");
        auto all_games = library.visible;
        all_games.insert(all_games.end(), library.private_games.begin(), library.private_games.end());
        check(searchPublicGames(all_games, L"game").size() == 2, "Search leaked private/hidden entries");
        check(searchPublicGames(all_games, L"game", true).size() == all_games.size(), "Ctrl+H search missed private/hidden entries");
        std::ofstream(root / (std::string(sourceSlug(source)) + ".json")) << catalog;
    }
    {
        // DRM-free has no published mark: a locally drawn padlock + word as a real PNG (860x300),
        // without a download; the tiles crop logos to their ink.
        const auto logo = platformLogo(L"DRM-free", root / "platforms");
        const auto be32 = [&](size_t at) { return (uint32_t(logo[at]) << 24) | (uint32_t(logo[at + 1]) << 16) | (uint32_t(logo[at + 2]) << 8) | logo[at + 3]; };
        check(logo.size() > 1000 && logo[0] == 0x89 && logo[1] == 'P' && logo[2] == 'N' && logo[3] == 'G' &&
              be32(16) == 860 && be32(20) == 300, "DRM-free logo was not drawn");
        wchar_t captures[4096]{};
        if (GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", captures, 4096))
            std::ofstream(fs::path(captures) / L"drm-free-logo.png", std::ios::binary).write(reinterpret_cast<const char*>(logo.data()), static_cast<std::streamsize>(logo.size()));
    }
    const auto shelves = publicSteamShelves({SteamCollection{L"Only Steam", {10}}}, public_games);
    check(shelves.size() == 1 && shelves.front().games.size() == 1 &&
          shelves.front().games.front().source == GameSource::steam, "External store entered a Steam collection");
    const auto scan = scanStoreLibraries(root);
    check(scan.visible.size() == 8 && scan.private_games.size() == 8 && scan.errors.empty(), "Adapter scan/merge failed");
    check(scanStoreLibraries(root / "missing").visible.empty(), "Missing catalog fabricated games");

    for (const auto* invalid : {
        R"({"version":2,"games":[]})",
        R"({"version":1,"games":[{"id":"10","name":"Game","installed":true}]})",
        R"({"version":1,"games":[{"id":10,"name":"Game","installed":true,"visibility":"public"}]})",
        R"({"version":1,"games":[{"id":"../10","name":"Game","installed":true,"visibility":"public"}]})",
        R"({"version":1,"games":[{"id":"010","name":"Game","installed":true,"visibility":"public"}]})",
        R"({"version":1,"games":[{"id":"10","name":"Game","installed":"false","visibility":"public"}]})",
        R"({"version":1,"games":[{"id":"10","name":"Game","installed":true,"visibility":"public"},{"id":"10","name":"Hidden","installed":true,"visibility":"hidden"}]})",
        R"({"version":1,"games":[{"id":"10","name":"Game","installed":true,"visibility":"public","install_dir":"relative"}]})",
        R"({"version":1,"games":[{"id":"10","name":"Bad\u0000title","installed":true,"visibility":"public"}]})",
        "{"
    }) {
        const auto rejected = parseStoreCatalog(GameSource::gog, invalid);
        check(!rejected.errors.empty() && rejected.visible.empty() && rejected.private_games.empty(),
              "Malformed/ambiguous catalog was not rejected atomically");
    }
    std::ofstream(root / "ea.json", std::ios::trunc) << "{";
    const auto partial = scanStoreLibraries(root);
    check(partial.errors.size() == 1 && partial.visible.size() == 6 && partial.private_games.size() == 6,
          "One broken adapter suppressed other libraries");

    const auto gog = parseStoreCatalog(GameSource::gog, R"({"version":1,"client":"C:\\GOG Galaxy\\GalaxyClient.exe","games":[
      {"id":"9007199254740993","name":"GOG game","installed":true,"visibility":"public","install_dir":"D:\\GOG Games\\Example\\"}]})");
    check(gog.visible.size() == 1, "GOG catalog failed");
    if (!gog.visible.empty()) {
        auto game = gog.visible.front();
        const auto play = primaryGameAction(game);
        check(play.target == L"C:\\GOG Galaxy\\GalaxyClient.exe" &&
              play.arguments == L"/launchViaAutostart /gameId=9007199254740993 /command=runGame /path=\"D:\\GOG Games\\Example\\\\\"",
              "GOG numeric precision or Windows argument quoting failed");
        game.installed = false;
        check(primaryGameAction(game).target == L"goggalaxy://openGameView/9007199254740993" &&
              primaryGameAction(game).kind == ActionKind::open_client, "GOG installation should open Galaxy game view");
    }
    const auto bnet = parseStoreCatalog(GameSource::battlenet, R"({"version":1,"client":"C:\\Battle.net\\Battle.net.exe","games":[
      {"id":"WoW","name":"Bnet game","installed":true,"visibility":"public"}]})");
    check(bnet.visible.size() == 1 && primaryGameAction(bnet.visible.front()).arguments == L"--exec=\"launch WoW\"",
          "Battle.net product launch plan failed");
    if (!bnet.visible.empty()) {
        auto uninstalled = bnet.visible.front();
        uninstalled.installed = false;
        const auto action = primaryGameAction(uninstalled);
        check(action.kind == ActionKind::open_client && action.arguments.empty(),
              "Uninstalled Battle.net game must not invoke launch");
    }
    const auto ea = parseStoreCatalog(GameSource::ea, R"({"version":1,"client":"C:\\EA Desktop\\EADesktop.exe","games":[
      {"id":"OFB-EAST:123","name":"EA game","installed":true,"visibility":"public"}]})");
    check(ea.visible.size() == 1 && primaryGameAction(ea.visible.front()).target == L"origin2://game/launch?offerIds=OFB-EAST%3A123",
          "EA offer ID or URI encoding failed");
    if (!ea.visible.empty()) {
        auto uninstalled = ea.visible.front();
        uninstalled.installed = false;
        check(primaryGameAction(uninstalled).target == L"C:\\EA Desktop\\EADesktop.exe" &&
              primaryGameAction(uninstalled).kind == ActionKind::open_client, "EA install must open the client");
    }
    auto unavailable = parseStoreCatalog(GameSource::battlenet, catalog).visible.front();
    check(!primaryGameAction(unavailable), "Missing Battle.net client invented a play action");
    unavailable.installed = false;
    check(!primaryGameAction(unavailable) && std::wstring(primaryActionLabel(unavailable)) == L"Otwórz klienta",
          "Missing client was represented as an install action");
    auto ubisoft = parseStoreCatalog(GameSource::ubisoft, catalog).visible.front();
    check(primaryGameAction(ubisoft).target == L"uplay://launch/10/0", "Ubisoft launch URI failed");
    ubisoft.installed = false;
    check(primaryGameAction(ubisoft).target == L"uplay://install/10" &&
          primaryGameAction(ubisoft).kind == ActionKind::install, "Ubisoft install URI failed");
    check(primaryGameAction(steam).target == L"steam://rungameid/10", "Steam launch regression");
    steam.installed = false;
    check(primaryGameAction(steam).target == L"steam://install/10", "Steam install regression");
    Game shortcut{10, L"Shortcut"};
    shortcut.kind = GameKind::nonsteam;
    shortcut.shortcut_id = 9876543210;
    check(gameKey(shortcut) != gameKey(steam) && gameCoverPath(shortcut, root) != gameCoverPath(steam, root),
          "Shortcut and Steam artwork collided");
    check(primaryGameAction(shortcut).target == L"steam://rungameid/9876543210", "Shortcut launch ID truncated");
    for (auto kind : {GameKind::all, GameKind::back, GameKind::platform}) {
        steam.kind = kind;
        check(!primaryGameAction(steam), "Navigation tile produced a launch action");
    }
    check(activePlatformName(steam) == steam.platform, "Platform badge regression");
    steam.kind = GameKind::all;
    check(activePlatformName(steam).empty(), "All tile received a platform badge");
    return failures ? 1 : 0;
}
