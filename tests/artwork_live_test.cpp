#include "../src/artwork/cover_cache.hpp"
#include "../src/artwork/platform_logo.hpp"
#include "../src/artwork/esde_art.hpp"

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>

// Manual network smoke test. Intentionally not registered with CTest: it needs
// SteamGridDB access and must keep ordinary builds deterministic/offline.
int main() {
    namespace fs = std::filesystem;
    constexpr uint32_t unmapped_app_id = 4290000000u;
    const auto root = fs::temp_directory_path() /
                       (L"launchme-sgdb-live-" + std::to_wstring(GetCurrentProcessId()));
    const auto cache = root / L"covers";
    struct Cleanup {
        fs::path folder;
        ~Cleanup() { std::error_code ec; fs::remove_all(folder, ec); }
    } cleanup{root};
    fs::create_directories(cache);
    const auto art = coverForSteamApp(unmapped_app_id, L"Silent Hill: Townfall", cache,
                                      cache / L"no-steam-installed-here");
    uint32_t game_id = 0;
    std::ifstream mapping(cache / L"4290000000.sgdbid");
    mapping >> game_id;
    if (game_id != 5436385 || art.size() < 128) {
        std::cerr << "SGDB name lookup or image download failed: game=" << game_id
                  << " bytes=" << art.size() << '\n';
        return 1;
    }
    std::cout << "Resolved SteamGridDB game " << game_id << " and downloaded "
              << art.size() << " bytes of artwork\n";
    Game external;
    external.source = GameSource::gog;
    external.source_id = "10"; // Synthetic catalog record; artwork only, never launch.
    external.kind = GameKind::store;
    external.name = L"Silent Hill: Townfall";
    const auto external_art = artworkForGame(external, ArtworkKind::cover, cache, {});
    if (external_art.size() < 128 || !fs::exists(gameCoverPath(external, cache)) ||
        external_art != artworkForGame(external, ArtworkKind::cover, cache, {})) {
        std::cerr << "External name-only artwork lookup or namespaced cache failed\n";
        return 1;
    }
    std::cout << "External source: name lookup and isolated cache, " << external_art.size() << " bytes\n";
    const auto screenshot = storeScreenshotForSteamApp(620); // Portal 2: first Steam store screenshot.
    if (screenshot.size() < 128) {
        std::cerr << "Steam store screenshot fallback failed\n";
        return 1;
    }
    std::cout << "Steam screenshot fallback: " << screenshot.size() << " bytes\n";
    size_t logos = 0;
    for (const auto* platform : {L"GameCube", L"Wii", L"Wii U", L"Switch", L"PS2", L"Wii / GameCube", L"PC",
                                L"Steam", L"GOG", L"Battle.net", L"EA Play", L"Ubisoft Connect", L"Epic Games", L"Xbox"}) {
        const auto logo = platformLogo(platform, coverCacheDirectory().parent_path() / L"platforms");
        if (logo.size() < 128) { std::cerr << "Platform PNG download failed\n"; return 1; }
        ++logos;
    }
    std::cout << "Monochrome Gaming Logos PNG count: " << logos << '\n';
    bool fallback = true;
    const auto backdrop = platformBackground(L"PS2", coverCacheDirectory().parent_path() / L"platforms", fallback);
    if (std::filesystem::exists(coverCacheDirectory().parent_path() / L"platforms" / L"backgrounds" / L"ps2.jpg") &&
        (fallback || backdrop.size() < 128)) {
        std::cerr << "Local PS2 console background was not preferred\n";
        return 1;
    }
    const auto esde_background = esdeArtForShortcut(0xFA000001u, L"PS2",
        L"Resident Evil Code : Veronica X", EmuArtKind::background,
        coverCacheDirectory().parent_path() / L"emulation");
    std::cout << "ES-DE scraped PS2 fanart: " << esde_background.size() << " bytes\n";
    return 0;
}
