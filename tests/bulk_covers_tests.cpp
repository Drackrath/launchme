#include "../src/artwork/bulk_covers.hpp"
#include "../src/artwork/image_compact.hpp"
#include "../src/artwork/esde_art.hpp"
#include "../src/net/artwork_limits.hpp"
#include "../src/platform/atomic_file.hpp"
#include "../src/library/screenscraper.hpp"
#include <Windows.h>
#include <fstream>
#include <iostream>
#include <set>
#include "image_fixture.hpp"

using namespace bulk_covers;
int main() {
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 15;
    struct Com { ~Com() { CoUninitialize(); } } com;
    ArtworkLimit limit;
    limit.reserve(100);
    if (limit.delay(100) != 1100 || limit.delay(1200)) return 1;
    limit.observe(1200, 429, 120000, {}, 0);
    limit.observe(1300, 200, 0, 0, 300000);
    if (limit.delay(1300) != 300000) return 2;
    limit.observe(1400, 200, 0, 10, 0);
    if (limit.delay(1400) != 299900) return 3; // A late reply never shortens a pause.
    const auto root = std::filesystem::temp_directory_path() / (L"launchme-bulk-" + std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { std::filesystem::path p; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(p, ec); } } cleanup{root};
    const auto file = root / L"existing.img";
    atomic_file::write(file, "original bytes");
    {
        MissingImagesOnly missing;
        const std::vector<uint8_t> replacement(128, 42);
        if (writeCachedImage(file, replacement)) return 4;
        if (!writeCachedImage(root / L"new.img", replacement)) return 5;
    }
    std::ifstream input(file, std::ios::binary);
    if (std::string(std::istreambuf_iterator<char>{input}, {}) != "original bytes") return 6;

    Game one{1,L"One"}, two{2,L"Two"}, hidden{3,L"Hidden"}; hidden.visibility = GameVisibility::private_game;
    std::set<std::pair<uint32_t,ArtworkKind>> files{{1,ArtworkKind::cover}};
    int fetched = 0; bool ready = false;
    Progress final;
    Control control; // Explicit confirmation before any network request.
    Operations ops{
        [&](const Game& game, ArtworkKind kind) { return files.contains({game.app_id,kind}); },
        [&](const Game& game, ArtworkKind kind) -> Attempt { ++fetched; files.insert({game.app_id,kind}); return {Result::saved}; }
    };
    run({one,one,two,hidden},control,ops,[&](const Progress& p) {
        final=p;
        if(p.phase==Phase::ready) {
            ready=true;
            if(fetched) std::terminate();
            // Another worker fills this slot after the plan; the pre-download check must catch it.
            files.insert({2,ArtworkKind::cover}); control.start=true;
        }
    });
    if(!ready || fetched!=4 || final.total!=6 || final.existing!=2 || final.downloaded!=4 || final.phase!=Phase::finished ||
        !files.contains({1,ArtworkKind::landscape}) || !files.contains({2,ArtworkKind::landscape})) return 7;
    Control restart; restart.start=true; fetched=0;
    run({one,two},restart,ops,[&](const Progress& p){final=p;});
    if(fetched || final.existing!=6) return 8;

    Control cancel; cancel.start=true; int attempts=0;
    run({one,two},cancel,{[](const Game&,ArtworkKind){return false;},[&](const Game&,ArtworkKind)->Attempt{
        ++attempts; return {Result::retry,43200,L"Daily quota"};
    }},[&](const Progress& p){final=p; if(p.phase==Phase::waiting) cancel.cancel=true;});
    if(attempts!=1 || final.phase!=Phase::cancelled || final.completed()!=0) return 9;
    Control pause; pause.start=true; pause.pause=true; fetched=0;
    run({one},pause,{[](const Game&,ArtworkKind){return false;},ops.fetch},[&](const Progress& p){final=p; if(p.phase==Phase::paused) pause.cancel=true;});
    if(fetched || final.phase!=Phase::cancelled) return 10;
    Control classify; classify.start=true; attempts=0; bool eta=false;
    run({one,two,Game{4,L"Four"}},classify,{[](const Game&,ArtworkKind){return false;},[&](const Game&,ArtworkKind)->Attempt{
        return {++attempts%2 ? Result::missing : Result::failed};
    }},[&](const Progress& p){final=p; eta |= p.eta_seconds.has_value();});
    if(final.missing!=5 || final.failed!=4 || final.completed()!=9 || !eta) return 11;
    SetEnvironmentVariableW(L"LAUNCHME_CONFIG_DIR", root.c_str());
    {
        // A previous pass left covers and posters; hero is only a fallback, never a completed landscape.
        const auto cache = root / L"covers";
        Game steam{9000,L"Steam fixture"}, gog, epic, manual;
        gog.kind = GameKind::store; gog.source = GameSource::gog; gog.source_id = "9000"; gog.name = L"GOG fixture";
        epic = gog; epic.source = GameSource::epic; epic.name = L"Epic fixture";
        manual = gog; manual.source_id = "manual";
        const std::vector<Game> games{steam, gog, epic, manual};
        const auto portrait = pngFixture(20, 60, 90), horizontal = pngFixture(80, 92, 43);
        for (const auto& game : games) for (const auto kind : {ArtworkKind::cover, ArtworkKind::poster, ArtworkKind::hero})
            if (!writeCachedImage(gameArtworkPath(game, kind, cache), portrait)) return 16;
        if (!cacheArtworkVariant(manual, ArtworkKind::landscape, 77, horizontal, cache) ||
            !selectArtworkVariant(manual, ArtworkKind::landscape, 77, cache)) return 17;
        auto real = operations(cache, {}, true);
        for (const auto& game : {steam, gog, epic}) if (real.exists(game, ArtworkKind::landscape)) return 18;
        std::set<std::string> downloaded;
        Operations fill{real.exists, [&](const Game& game, ArtworkKind kind) -> Attempt {
            if (kind != ArtworkKind::landscape) return {Result::failed};
            downloaded.insert(gameKey(game));
            return {writeCachedImage(gameArtworkPath(game, kind, cache), horizontal) ? Result::saved : Result::failed};
        }};
        Control pass; pass.start = true;
        run(games, pass, fill, [&](const Progress& p) { final = p; });
        if (final.total != 12 || final.existing != 9 || final.downloaded != 3 || final.failed ||
            downloaded != std::set<std::string>{gameKey(steam), gameKey(gog), gameKey(epic)}) return 19;
        for (const auto& game : games) {
            if (cachedArtwork(game, ArtworkKind::landscape, cache) != horizontal ||
                cachedArtwork(game, ArtworkKind::poster, cache) != portrait) return 20;
        }
        run(games, pass, fill, [&](const Progress& p) { final = p; });
        if (final.existing != 12 || final.downloaded) return 21;
        // Emulated landscape uses wide fanart, never the portrait cover copied to a landscape slot.
        Game rom; rom.app_id = 0xf0000001; rom.kind = GameKind::nonsteam; rom.platform = L"PS2"; rom.name = L"ROM fixture";
        if (!writeCachedImage(esdeArtCachePath(rom.app_id, EmuArtKind::cover, root / L"emulation"), portrait) ||
            !writeCachedImage(esdeArtCachePath(rom.app_id, EmuArtKind::background, root / L"emulation"), horizontal)) return 22;
        if (real.fetch(rom, ArtworkKind::landscape).result != Result::saved ||
            cachedArtwork(rom, ArtworkKind::landscape, cache) != horizontal) return 23;
        if (!writeCachedImage(gameCoverPath(rom, cache), horizontal)) return 24;
        if (cachedArtwork(rom, ArtworkKind::cover, cache, true) != portrait ||
            cachedArtwork(rom, ArtworkKind::cover, cache, false) != horizontal) return 25;
        const auto picked = pngFixture(77, 120, 120);
        if (!cacheArtworkVariant(rom, ArtworkKind::cover, 777, picked, cache) ||
            !selectArtworkVariant(rom, ArtworkKind::cover, 777, cache) ||
            cachedArtwork(rom, ArtworkKind::cover, cache, true) != picked ||
            cachedArtwork(rom, ArtworkKind::cover, cache, false) != picked) return 26;
    }
    screenscraper::Quota quota; quota.requests_today = 1; quota.max_per_day = 1;
    screenscraper::observeQuota(quota);
    int requests = 0;
    auto response = screenscraper::gated([&] { ++requests; return HttpResponse{200}; }, false);
    if (requests || response.status != 430 || !screenscraper::paused(root / L"metadata")) return 12;
    response = screenscraper::gated([&] { ++requests; return HttpResponse{200}; }, false);
    if (requests || response.status != 429) return 13;
    // An explicit account check can refresh quota, without bypassing ordinary download gating.
    response = screenscraper::gated([&] { ++requests; return HttpResponse{200}; }, false, true);
    if (requests != 1 || response.status != 200) return 14;
    std::cout << "Missing-only, race-safe writes, deduplication, quota/cancel/pause and ETA passed\n";
    return 0;
}
