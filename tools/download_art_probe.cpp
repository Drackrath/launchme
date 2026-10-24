#include "../src/artwork/download_art.hpp"
#include "../src/platform/user_config.hpp"
#include "../src/platform/text_encoding.hpp"
#include "../src/plugins/json.hpp"
#include <fstream>
#include <iostream>

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) { std::cerr << "Usage: download_art_probe <platform> <title> [catalog.json]\n"; return 2; }
    const std::wstring platform = argv[1], title = argv[2];
    if (argc > 3) {
        std::ifstream input(argv[3], std::ios::binary | std::ios::ate);
        const auto length = input ? static_cast<int64_t>(input.tellg()) : -1;
        if (length > 0 && length < 256ll * 1024 * 1024) {
            std::string bytes(static_cast<size_t>(length), '\0'); input.seekg(0); input.read(bytes.data(), length);
            if (const auto root = json::parse(bytes, bytes.size())) for (const auto& section : plugins::wire::field(*root, "sections").elements)
                for (const auto& item : plugins::wire::field(section, "items").elements) {
                    const auto name = text_encoding::wide(plugins::wire::text(item, "name"));
                    if (gridTitleScore(title, name) < 880) continue;
                    std::cout << "catalog section=" << plugins::wire::text(section, "title") << " id=" << plugins::wire::text(section, "id")
                        << " name=" << plugins::wire::text(item, "name") << " platform=" << plugins::wire::text(item, "platform") << '\n';
                }
        }
    }
    const auto ss = screenscraper::load();
    std::cout << "ScreenScraper configured=" << ss.ready() << '\n';
    if (!ss.ready()) return 3;
    const auto normalized = download_art::normalizePlatform(platform);
    const auto systems = screenscraper::systemIds(normalized, {});
    std::cout << "platform=" << text_encoding::utf8(normalized) << " system=" << (systems.size() == 1 ? systems[0] : 0) << '\n';
    if (systems.size() != 1) return 4;
    const ArtworkHttp observe = [&](const std::wstring& url, const std::wstring& headers, size_t max) {
        const auto answer = httpGetPrivate(url, headers, max);
        const bool lookup = url.find(L"jeuRecherche.php") != url.npos;
        std::cout << (lookup ? "search" : "media") << " HTTP=" << answer.status << " bytes=" << answer.body.size() << '\n';
        if (lookup) {
            const auto found = screenscraper::parse(answer.status, std::string_view(reinterpret_cast<const char*>(answer.body.data()), answer.body.size()), ss.output != L"json", L"pl");
            std::cout << "lookup status=" << static_cast<int>(found.status) << " candidates=" << found.games.size() << '\n';
            for (const auto& game : found.games) std::cout << "candidate id=" << game.id << " name=" << text_encoding::utf8(game.name)
                << " score=" << gridTitleScore(title, game.name) << " media=" << game.media.size() << '\n';
            if (found.games.empty() && found.game) std::cout << "candidate id=" << found.game->id << " name=" << text_encoding::utf8(found.game->name)
                << " score=" << gridTitleScore(title, found.game->name) << " media=" << found.game->media.size() << '\n';
        }
        return answer;
    };
    HttpBudget budget(60000);
    const auto bytes = download_art::fetch({title, platform, ""}, coverCacheDirectory(), userConfigDirectory() / L"metadata", L"", ss, L"pl", observe);
    std::cout << "cover bytes=" << bytes.size() << " decodable=" << decodableArtwork(bytes) << '\n';
    return bytes.empty() ? 1 : 0;
}
