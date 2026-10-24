#include "../src/artwork/identified_artwork.hpp"
#include "../src/platform/atomic_file.hpp"
#include "image_fixture.hpp"
#include <iostream>

namespace {
HttpResponse reply(uint32_t status, std::string_view text) { return {status, {text.begin(), text.end()}}; }
constexpr auto page = R"({"data":[{"id":81,"width":512,"height":512,"url":"https://cdn.example/good.png"}]})";
}
int main() {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 99;
    const auto root = std::filesystem::temp_directory_path() / (L"launchme-identify-art-" + std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { std::filesystem::path root; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(root, ec); CoUninitialize(); } } cleanup{root};
    Game game; game.kind = GameKind::store; game.source = GameSource::gog; game.source_id = "GOG-is-not-a-Steam-ID"; game.name = L"My local title";
    const auto key = gameKey(game);
    const auto image = pngFixture();
    std::vector<std::wstring> urls;
    bool leaked = false;
    const ArtworkHttp mapped = [&](const auto& address, const auto& headers, size_t) {
        urls.push_back(address);
        if (address.starts_with(L"https://cdn.example/")) {
            leaked |= headers.find(L"Authorization") != headers.npos;
            return address.ends_with(L"bad.png") ? HttpResponse{200, {1, 2, 3}} : HttpResponse{200, image};
        }
        if (address.find(L"/steam/6980?") == address.npos) return reply(400, "{}");
        if (address.find(L"dimensions=512x512,600x900") != address.npos)
            return reply(200, R"({"data":[{"id":80,"url":"https://cdn.example/bad.png"},{"id":81,"url":"https://cdn.example/good.png"}]})");
        return reply(200, page);
    };
    const auto active = [] { return true; };
    if (matchIdentifiedArtwork(L"Heroes of the Lance", {{{33743, false}, L"Advanced Dungeons & Dragons: Heroes of the Lance"},
        {{4, false}, L"Heroes of the Seven Seas"}}).id != 33743) return 12;
    if (matchIdentifiedArtwork(L"Heroes of the Lance", {{{1, false}, L"Franchise: Heroes of the Lance"}, {{2, false}, L"Other: Heroes of the Lance"}}).id ||
        matchIdentifiedArtwork(L"Heroes of the Lance", {{{1, false}, L"DLC: Heroes of the Lance"}}).id ||
        matchIdentifiedArtwork(L"Tomb Raider", {{{1, false}, L"Something: Tomb Raider"}}).id ||
        matchIdentifiedArtwork(L"Heroes of the Lance", {{{1, false}, L"Franchise: Heroes of the Lancer"}}).id) return 13;
    // Same title, two games: RAWG's year picks one; without it nothing is guessed. A lone
    // exact title from another era is a different game.
    const std::vector<GridGame> revenant{{{2024, false}, L"Revenant", 2024}, {{1999, false}, L"Revenant", 1999}};
    if (matchIdentifiedArtwork(L"Revenant", revenant, 1999).id != 1999 || matchIdentifiedArtwork(L"Revenant", revenant, 2024).id != 2024 ||
        matchIdentifiedArtwork(L"Revenant", revenant).id || matchIdentifiedArtwork(L"Revenant", {{{2024, false}, L"Revenant", 2024}}, 1999).id ||
        matchIdentifiedArtwork(L"Revenant", {{{7, false}, L"Revenant", 0}}, 1999).id != 7) return 14;
    const auto cache = root / "mapped" / "covers";
    auto filled = fillIdentifiedArtwork(game, L"Thief 3: Deadly Shadows", 6980, cache, L"synthetic", active, mapped);
    if (filled.images.size() != 5 || filled.missing != 5 || !filled.error.empty() || urls.size() != 11 || leaked) return 1;
    if (gameKey(game) != key || game.app_id || selectedArtworkSource(game, cache).id != 6980 || !selectedArtworkSource(game, cache).steam) return 2;
    for (const auto kind : {ArtworkKind::cover, ArtworkKind::hero, ArtworkKind::logo, ArtworkKind::poster, ArtworkKind::landscape})
        if (selectedArtwork(game, kind, cache) != image || selectedArtworkId(game, kind, cache) != 81) return 3;
    urls.clear();
    filled = fillIdentifiedArtwork(game, L"Thief 3: Deadly Shadows", 6980, cache, L"synthetic", active, mapped);
    if (!filled.images.empty() || filled.missing || !urls.empty()) return 4; // Existing valid artwork is preserved without HTTP.
    // A stale selection marker with corrupt image bytes must not suppress filling an empty tile.
    std::vector<uint8_t> corrupt(256, 0); corrupt[0] = 0x89; corrupt[1] = 'P'; corrupt[2] = 'N'; corrupt[3] = 'G';
    if (!atomic_file::write(artworkVariantPath(game, ArtworkKind::cover, 81, cache), corrupt) || !selectedArtworkId(game, ArtworkKind::cover, cache)) return 5;
    filled = fillIdentifiedArtwork(game, L"Thief 3: Deadly Shadows", 6980, cache, L"synthetic", active, mapped);
    if (filled.images.size() != 1 || filled.images[0].kind != ArtworkKind::cover || urls.size() != 3 || selectedArtwork(game, ArtworkKind::cover, cache) != image) return 6;
    int searches = 0;
    const ArtworkHttp title = [&](const auto& address, const auto&, size_t) {
        if (address.find(L"/steam/") != address.npos) return reply(404, "{}");
        if (address.find(L"/autocomplete/") != address.npos) {
            ++searches;
            return reply(200, R"({"data":[{"id":99,"name":"Control"}]})");
        }
        if (address.find(L"/game/99?") != address.npos) return reply(200, page);
        if (address == L"https://cdn.example/good.png") return HttpResponse{200, image};
        return reply(400, "{}");
    };
    filled = fillIdentifiedArtwork(game, L"Control Ultimate Edition", 0, root / "title" / "covers", L"synthetic", active, title);
    if (filled.images.size() != 5 || !filled.error.empty() || searches != 1 || selectedArtworkSource(game, root / "title" / "covers").id != 99) return 7;
    searches = 0;
    filled = fillIdentifiedArtwork(game, L"Control Ultimate Edition", 123, root / "fallback" / "covers", L"synthetic", active, title);
    if (filled.images.size() != 5 || !filled.error.empty() || searches != 1) return 8; // Steam has no assets, title fallback shared across kinds.
    for (const auto status : {401u, 429u, 503u}) {
        int calls = 0;
        filled = fillIdentifiedArtwork(game, L"Title", 6980, root / std::to_string(status) / "covers", L"synthetic", active,
            [&](const auto&, const auto&, size_t) { ++calls; return reply(status, "{}"); });
        if (!filled.images.empty() || filled.error.empty() || calls != 1) return 9;
    }
    int calls = 0;
    filled = fillIdentifiedArtwork(game, L"Title", 6980, root / "keyless" / "covers", L"", active,
        [&](const auto&, const auto&, size_t) { ++calls; return HttpResponse{}; });
    if (filled.error.empty() || calls) return 10;
    bool running = true;
    const auto cancelled = root / "cancelled" / "covers";
    filled = fillIdentifiedArtwork(game, L"Title", 6980, cancelled, L"synthetic", [&] { return running; },
        [&](const auto& address, const auto&, size_t) {
            if (address.starts_with(L"https://cdn.example/")) { running = false; return HttpResponse{200, image}; }
            return reply(200, page);
        });
    if (!filled.images.empty() || selectedArtworkId(game, ArtworkKind::cover, cancelled)) return 11;
    std::cout << "Identification artwork: Steam mapping, all slots, title fallback, preservation, corrupt/missing images, errors and cancellation passed\n";
}
