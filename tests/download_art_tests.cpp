#include "../src/artwork/download_art.hpp"
#include "image_fixture.hpp"
#include <iostream>

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
HttpResponse jsonReply(std::string body) { return {200, {body.begin(), body.end()}}; }
}
int main() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const auto root = std::filesystem::temp_directory_path() / (L"launchme-download-art-" + std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { std::filesystem::path root; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(root, ec); CoUninitialize(); } } cleanup{root};
    try {
        const auto png = pngFixture();
        int calls = 0;
        const ArtworkHttp none = [&](const auto&, const auto&, size_t) { ++calls; return HttpResponse{}; };
        auto pc = download_art::forGame(Game{620, L"Portal 2"});
        check(pc.pc && pc.steam_app_id == 620, "Steam target");
        download_art::Target console{L"Example [0100AAAA0000E000][v65536].zip", L"Nintendo Switch", "0100AAAA0000E000"};
        check(download_art::cleanName(console.name) == L"Example", "release annotations removed from lookup name");
        check(download_art::cleanName(L"Dr. Mario 64") == L"Dr. Mario 64" && download_art::cleanName(L"Game v1.2") == L"Game v1.2", "titles with dots stay whole");
        auto canonical = console; canonical.name = L"Example";
        check(console.key() == canonical.key(), "base and release share thumbnail identity");
        auto other = canonical; other.platform = L"Nintendo Switch 2";
        check(other.key() != canonical.key(), "platform identities remain separate");
        check(download_art::fetch(pc, root, root / "metadata", L"", {}, L"en", none).empty() && calls == 0, "no SGDB key means no HTTP");
        check(download_art::fetch(console, root, root / "metadata", L"sgdb-test", {}, L"en", none).empty() && calls == 0, "console never falls back to SGDB without SS credentials");
        const ArtworkHttp sgdb = [&](const std::wstring& url, const std::wstring& headers, size_t) {
            ++calls;
            if (url.find(L"/grids/steam/620") != url.npos) {
                check(headers.find(L"Bearer test-key") != headers.npos, "SGDB credentials on its API");
                return jsonReply(R"({"data":[{"id":1,"width":600,"height":900,"url":"https://cdn.example/portrait.png"},{"id":2,"width":512,"height":512,"url":"https://cdn.example/square.png"}]})");
            }
            check(url == L"https://cdn.example/square.png" && headers.find(L"Authorization") == headers.npos, "prefer square and do not leak key to image host");
            return HttpResponse{200, png};
        };
        const auto pc_image = download_art::fetch(pc, root, root / "metadata", L"test-key", {}, L"en", sgdb);
        check(decodableArtwork(pc_image) && calls == 2, "PC square artwork fetched");
        calls = 0;
        check(download_art::fetch(pc, root, root / "metadata", L"", {}, L"en", none) == pc_image && calls == 0, "disk cache works without credentials or network");

        screenscraper::Config ss{L"dev", L"password", L"Launchme", L"json", std::wstring(screenscraper::default_base_url), L"", L""};
        const ArtworkHttp scraper = [&](const std::wstring& url, const std::wstring& headers, size_t) {
            ++calls;
            check(headers.find(L"Bearer") == headers.npos, "no SGDB key on ScreenScraper");
            if (url.find(L"jeuRecherche.php") != url.npos) {
                check(url.find(L"systemeid=225") != url.npos && url.find(L"recherche=Example") != url.npos, "lookup by game name on the exact console, not unfinished ROM bytes");
                return jsonReply(R"json({"response":{"jeu":{"id":"42","noms":[{"region":"wor","text":"Example"}],"medias":[{"type":"box-2D","region":"wor","format":"png","url":"https://api.screenscraper.fr/api2/mediaJeu.php?jeuid=42&media=box-2D"}]}}})json");
            }
            check(url.find(L"mediaJeu.php") != url.npos && url.find(L"devid=dev") != url.npos, "authenticated ScreenScraper media");
            return HttpResponse{200, png};
        };
        const auto console_image = download_art::fetch(console, root, root / "metadata", L"unused", ss, L"en", scraper);
        check(decodableArtwork(console_image) && calls == 2, "ScreenScraper console box fetched");
        calls = 0;
        check(download_art::fetch(canonical, root, root / "metadata", L"", {}, L"en", none) == console_image && calls == 0, "base/update/DLC reuse the same persisted picture");
        auto unknown = console; unknown.platform = L"Unknown console";
        check(download_art::fetch(unknown, root, root / "metadata", L"key", ss, L"en", none).empty() && calls == 0, "unknown console is not searched across systems");
        auto ambiguous = console; ambiguous.name = L"Ambiguous";
        const ArtworkHttp duplicates = [&](const auto&, const auto&, size_t) {
            ++calls;
            return jsonReply(R"({"response":{"jeux":[{"id":"10","noms":[{"region":"wor","text":"Ambiguous"}]},{"id":"11","noms":[{"region":"wor","text":"Ambiguous"}]}]}})");
        };
        check(download_art::fetch(ambiguous, root, root / "metadata", L"", ss, L"en", duplicates).empty() && calls == 1, "ambiguous title not assigned a random cover");
        calls = 0;
        download_art::Target kirby{L"Kirbys Return to Dream Land", L"Nintendo Wii", "KIRBY"};
        const ArtworkHttp regional = [&](const std::wstring& url, const auto&, size_t) {
            ++calls;
            if (url.find(L"jeuRecherche.php") != url.npos) {
                check(url.find(L"systemeid=16") != url.npos, "Kirby is searched on Wii, not Switch");
                if (url.find(L"Kirby%27s") == url.npos) return jsonReply(R"({"response":{"jeux":[]}})");
                return jsonReply(R"({"response":{"jeu":{"id":"8773","noms":[{"region":"eu","text":"Kirby's Adventure Wii"},{"region":"us","text":"Kirby's Return to Dream Land"}],"medias":[{"type":"box-2D","region":"eu","format":"png","url":"https://api.screenscraper.fr/api2/mediaJeu.php?jeuid=8773"}]}}})");
            }
            return HttpResponse{200, png};
        };
        check(decodableArtwork(download_art::fetch(kirby, root, root / "metadata", L"", ss, L"en", regional)) && calls == 3,
              "apostrophe retry plus regional alias reaches the exact game's cover");
        calls = 0; pc.steam_app_id = 621;
        const ArtworkHttp limited = [&](const auto&, const auto&, size_t) { ++calls; return HttpResponse{429}; };
        check(download_art::fetch(pc, root, root / "metadata", L"test-key", {}, L"en", limited).empty(), "SGDB rate limited");
        pc.steam_app_id = 622;
        check(download_art::fetch(pc, root, root / "metadata", L"test-key", {}, L"en", limited).empty() && calls == 1, "other download thumbnails respect the shared cooldown");
        std::cout << "Download artwork routing, keys, identity and cache passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
