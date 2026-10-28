#include "../src/artwork/image_compact.hpp"
#include "../src/artwork/screenscraper_art.hpp"
#include "../src/library/screenscraper.hpp"
#include "../src/platform/user_config.hpp"
#include "../src/store/steam_trailer.hpp"
#include "../src/artwork/cover_cache.hpp"
#include "disc_fixture.hpp"

#include <Windows.h>
#ifdef _WIN32
#include <wincodec.h>
#include <wrl/client.h>
#else
#include "../src/artwork/image_codec.hpp"
#endif

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
namespace ss = screenscraper;
HttpResponse text(uint32_t status, std::string_view body) {
    return {status, std::vector<uint8_t>(body.begin(), body.end()), 0};
}

// As ES-DE reads it (Data/jeu, nom@region, synopsis@langue, media@type/region/format).
constexpr std::string_view xml_game =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Data>\n<header><APIversion>2.0</APIversion></header>\n"
    "<ssuser><id>player</id><niveau>1</niveau><maxrequestspermin>40</maxrequestspermin><requeststoday>12</requeststoday>"
    "<requestskotoday>2</requestskotoday><maxrequestsperday>20000</maxrequestsperday><maxrequestskoperday>2000</maxrequestskoperday></ssuser>\n"
    "<jeu id=\"9180\" romid=\"1\" notgame=\"false\">\n<noms><nom region=\"us\">Desert Strike: Return to the Gulf</nom>"
    "<nom region=\"eu\">Desert Strike : Return to the Gulf</nom><nom region=\"ss\">Desert Strike</nom></noms>\n"
    "<synopsis><synopsis langue=\"fr\">Le premier jeu.</synopsis><synopsis langue=\"en\">The first game &amp; more&#8230;</synopsis></synopsis>\n"
    "<editeur id=\"1\">Electronic Arts</editeur><developpeur id=\"2\">Electronic Arts</developpeur>\n"
    "<joueurs>1</joueurs><note>16</note>\n<dates><date region=\"us\">1992-02-01</date><date region=\"eu\">1993</date></dates>\n"
    "<genres><genre id=\"1\" principale=\"1\" langue=\"fr\">Tir</genre><genre id=\"1\" principale=\"1\" langue=\"en\">Shooter</genre>"
    "<genre id=\"2\" langue=\"en\">Flight</genre></genres>\n<medias>\n"
    "<media type=\"box-2D\" parent=\"jeu\" region=\"wor\" format=\"png\">https://neoclone.screenscraper.fr/api2/mediaJeu.php?devid=secretdev&amp;devpassword=secretpw&amp;softname=x&amp;ssid=player&amp;sspassword=userpw&amp;systemeid=64&amp;jeuid=9180&amp;media=box-2D(wor)</media>\n"
    "<media type=\"box-2D\" parent=\"jeu\" region=\"eu\" format=\"png\">https://neoclone.screenscraper.fr/api2/mediaJeu.php?devid=secretdev&amp;devpassword=secretpw&amp;softname=x&amp;systemeid=64&amp;jeuid=9180&amp;media=box-2D(eu)</media>\n"
    "<media type=\"ss\" parent=\"jeu\" region=\"wor\" format=\"png\">https://neoclone.screenscraper.fr/api2/mediaJeu.php?devid=secretdev&amp;media=ss(wor)</media>\n"
    "<media type=\"fanart\" parent=\"jeu\" format=\"jpg\">http://neoclone.screenscraper.fr/api2/mediaJeu.php?devid=secretdev&amp;media=fanart</media>\n"
    "<media type=\"wheel\" parent=\"jeu\" region=\"eu\" format=\"jpg\">https://neoclone.screenscraper.fr/api2/mediaJeu.php?media=wheel(eu)</media>\n"
    "<media type=\"wheel-hd\" parent=\"jeu\" region=\"us\" format=\"png\">https://neoclone.screenscraper.fr/api2/mediaJeu.php?media=wheel-hd(us)</media>\n"
    "</medias>\n</jeu>\n</Data>\n";

constexpr std::string_view json_game = R"json({"header":{"APIversion":"2.0"},"response":{
  "ssuser":{"id":"player","maxrequestspermin":"20","requeststoday":"5","maxrequestsperday":"10000"},
  "jeu":{"id":"3","noms":[{"region":"wor","text":"Sonic The Hedgehog 2"}],
    "synopsis":[{"langue":"en","text":"Sonic is back."}],"dates":[{"region":"wor","text":"1992-11-21"}],
    "developpeur":{"id":"4","text":"Sonic Team"},"editeur":{"id":"3","text":"SEGA"},"joueurs":{"text":"1-2"},"note":{"text":"17"},
    "genres":[{"id":"7","noms":[{"langue":"en","text":"Platform"},{"langue":"fr","text":"Plateforme"}]}],
    "medias":[{"type":"box-2D","region":"us","format":"png","url":"https://neoclone.screenscraper.fr/api2/mediaJeu.php?devid=d&devpassword=p&media=box-2D(us)"}]}}})json";
}

int main() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / (L"launchme-screenscraper-id-" + std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{root};
    fs::create_directories(root / "roms" / "amiga");
    SetEnvironmentVariableW(L"LAUNCHME_CONFIG_DIR", (root / "config").wstring().c_str());
    // No real ES-DE installation may answer: its media would win over the sandbox's.
    SetEnvironmentVariableW(L"APPDATA", (root / "appdata").wstring().c_str());
    SetEnvironmentVariableW(L"USERPROFILE", (root / "profile").wstring().c_str());
    fs::create_directories(root / "config");
    for (const auto& field : ss::fields) SetEnvironmentVariableW(field.environment, nullptr);

    // Systems: ES-DE's ScreenScraper IDs, from the platform and the ROM folder.
    if (ss::systemId(L"ps2") != 58 || ss::systemId(L"AMIGA") != 64 || ss::systemId(L"switch") != 225 || ss::systemId(L"pc") != 0) return 1;
    if (ss::systemIds(L"Retro", L"K:\\Emulation\\roms\\snes\\Super Metroid.sfc") != std::vector<uint32_t>{4} ||
        ss::systemIds(L"Wii / GameCube", L"K:\\roms\\Zelda.rvz") != std::vector<uint32_t>{16, 13} ||
        !ss::systemIds(L"PC", L"C:\\Games\\game.dat").empty()) return 2;

    // ROM file: name, size and MD5 ("abc"); above the limit the size alone; folder games by folder name.
    const auto rom_path = root / "roms" / "amiga" / "DesertStrike_v4.2_0112.lha";
    { std::ofstream rom(rom_path, std::ios::binary); rom << "abc"; }
    const auto rom = ss::describeRom(rom_path);
    const auto unhashed = ss::describeRom(rom_path, 0);
    const auto folder = ss::describeRom(L"D:\\roms\\ps3\\God of War.ps3\\PS3_GAME\\USRDIR\\EBOOT.BIN");
    if (!rom || rom->name != L"DesertStrike_v4.2_0112.lha" || rom->size != 3 || rom->md5 != L"900150983cd24fb0d6963f7d28e17f72" ||
        !unhashed || !unhashed->md5.empty() || unhashed->size != 3 || !folder || folder->name != L"God of War.ps3" || !folder->md5.empty()) return 3;

    ss::Config config{L"dev id", L"dev&pw", L"Launchme", L"xml", std::wstring(ss::default_base_url), L"player", L"user pw"};
    const auto url = ss::gameInfoUrl(config, 64, *rom);
    if (url != L"https://api.screenscraper.fr/api2/jeuInfos.php?devid=dev%20id&devpassword=dev%26pw&softname=Launchme&output=xml"
               L"&ssid=player&sspassword=user%20pw&systemeid=64&romnom=DesertStrike_v4.2_0112.lha"
               L"&md5=900150983cd24fb0d6963f7d28e17f72&romtaille=3") { std::wcerr << url << L'\n'; return 4; }
    if (ss::gameInfoUrl(config, 64, *unhashed).find(L"romtaille") != std::wstring::npos) return 4; // Size goes with the hash (as ES-DE).

    // XML answer: names by region (eu first), descriptions by language (pl, then en), credentials stripped.
    const auto xml = ss::parse(200, xml_game, true, L"pl");
    if (xml.status != ss::Status::found || !xml.game) return 5;
    const auto& desert = *xml.game;
    if (desert.id != 9180 || desert.name != L"Desert Strike : Return to the Gulf" || desert.details.description != L"The first game & more\u2026" ||
        desert.details.release_date != L"1993" || desert.details.developer != L"Electronic Arts" || desert.details.publisher != L"Electronic Arts" ||
        desert.details.genres != L"Shooter, Flight" || desert.players != L"1" || desert.note != 16 || desert.media.size() != 6) return 6;
    for (const auto& media : desert.media)
        if (media.url.find(L"secret") != std::wstring::npos || media.url.find(L"userpw") != std::wstring::npos || media.url.find(L"softname") != std::wstring::npos) return 7;
    if (xml.quota.max_per_minute != 40 || xml.quota.requests_today != 12 || xml.quota.max_per_day != 20000 || xml.quota.exhausted()) return 8;

    // Slots: the European box first, fanart before screenshots, logos only as PNG.
    const auto* cover = ss::pickMedia(desert, ss::ArtSlot::cover);
    const auto* background = ss::pickMedia(desert, ss::ArtSlot::background);
    const auto* logo = ss::pickMedia(desert, ss::ArtSlot::logo);
    if (!cover || cover->region != L"eu" || !background || background->type != L"fanart" || !logo || logo->type != L"wheel-hd") return 9;

    // JSON answer (output=json).
    const auto json = ss::parse(200, json_game, false, L"en");
    if (json.status != ss::Status::found || !json.game || json.game->name != L"Sonic The Hedgehog 2" || json.game->details.developer != L"Sonic Team" ||
        json.game->details.release_date != L"1992-11-21" || json.game->details.genres != L"Platform" || json.game->note != 17 ||
        json.game->players != L"1-2" || json.game->media.size() != 1 || json.game->media[0].url.find(L"devpassword") != std::wstring::npos ||
        json.quota.max_per_minute != 20) return 10;

    // Errors: misses are answers about the ROM, everything else stops the pass.
    if (ss::parse(404, "Erreur : Jeu non trouvée !", true, L"en").status != ss::Status::missing ||
        ss::parse(200, "Erreur : Rom/Iso/Dossier non trouv\xc3\xa9" "e !", true, L"en").status != ss::Status::missing ||
        ss::parse(200, "Erreur de login : V\xc3\xa9rifier vos identifiants d\xc3\xa9veloppeur !", true, L"en").status != ss::Status::rejected ||
        ss::parse(403, "", true, L"en").status != ss::Status::rejected || ss::parse(430, "", true, L"en").status != ss::Status::quota ||
        ss::parse(431, "", true, L"en").status != ss::Status::quota || ss::parse(429, "", true, L"en").status != ss::Status::throttled ||
        ss::parse(426, "", true, L"en").status != ss::Status::closed || ss::parse(423, "", true, L"en").status != ss::Status::closed ||
        ss::parse(500, "", true, L"en").status != ss::Status::invalid ||
        ss::parse(200, "<Data><jeu id=\"1\"><noms><nom region=\"ss\">ZZZ(notgame):Fichier</nom></noms></jeu></Data>", true, L"en").status != ss::Status::missing ||
        ss::parse(200, "<Data><jeu id=\"1\"><noms><nom", true, L"en").status != ss::Status::invalid) return 11;

    // Credentials only ever go to ScreenScraper hosts over HTTPS.
    const auto media_url = ss::mediaUrl(config, L"http://neoclone.screenscraper.fr/api2/mediaJeu.php?media=fanart&devid=old");
    if (media_url != L"https://neoclone.screenscraper.fr/api2/mediaJeu.php?media=fanart&devid=dev%20id&devpassword=dev%26pw&softname=Launchme"
                     L"&ssid=player&sspassword=user%20pw" ||
        !ss::mediaUrl(config, L"https://evil.example/mediaJeu.php?x=1").empty() ||
        !ss::mediaUrl(config, L"https://screenscraper.fr.evil.example/x").empty()) return 12;

    // Lookup through the injected transport.
    std::wstring requested;
    const auto found = ss::lookup(config, 64, *rom, L"en", [&](const std::wstring& address, const std::wstring&, size_t) {
        requested = address; return text(200, xml_game); });
    if (found.status != ss::Status::found || requested != url) return 13;
    config.dev_id.clear();
    if (ss::lookup(config, 64, *rom, L"en", [](const std::wstring&, const std::wstring&, size_t) { return text(200, xml_game); }).status != ss::Status::rejected) return 14;
    config.dev_id = L"dev id";

    // The cache: a stable fingerprint, no credentials, round trip.
    Game game;
    game.kind = GameKind::nonsteam; game.app_id = 0xFA000001u; game.shortcut_id = (uint64_t{0xFA000001u} << 32) | 0x02000000ULL;
    game.name = L"Desert Strike"; game.platform = L"Amiga"; game.rom = rom_path;
    ss::Cached entry{ss::fingerprint(game, *rom), 1234, desert};
    const auto serialized = ss::serialize(entry);
    if (serialized.find("secret") != std::string::npos || serialized.find("userpw") != std::string::npos) return 15;
    const auto restored = ss::deserialize(serialized);
    if (!restored || restored->checked != 1234 || restored->fingerprint != entry.fingerprint || !restored->game ||
        restored->game->name != desert.name || restored->game->details.description != desert.details.description ||
        restored->game->media.size() != desert.media.size() || restored->game->note != 16) return 16;
    const auto missing = ss::deserialize(ss::serialize({L"x|1", 5, {}}));
    if (!missing || missing->game || missing->checked != 5) return 17;

    // Artwork: only from a cached identification, with the current credentials, kept beside ES-DE's copies.
    const auto config_root = root / "config";
    for (const auto& [field, value] : {std::pair{ss::Field::dev_id, L"dev id"}, {ss::Field::dev_password, L"dev&pw"}})
        if (!ss::save(field, value)) return 18;
    std::vector<uint8_t> png(300, 'x'); png[0] = 0x89; png[1] = 'P'; png[2] = 'N'; png[3] = 'G';
    int downloads = 0;
    const auto transport = [&](const std::wstring& address, const std::wstring&, size_t) {
        ++downloads;
        if (!address.starts_with(L"https://neoclone.screenscraper.fr/") || address.find(L"devid=dev%20id") == std::wstring::npos) return text(403, "");
        if (address.find(L"box-2D(eu)") != std::wstring::npos) return HttpResponse{200, png, 0};
        return text(200, "NOMEDIA");
    };
    if (!screenscraperArt(game, EmuArtKind::cover, config_root, transport).empty() || downloads) return 19; // Nothing cached yet.
    if (!ss::saveCached(game, entry, config_root / "metadata")) return 20;
    const auto art = screenscraperArt(game, EmuArtKind::cover, config_root, transport);
    if (art != png || !fs::exists(esdeArtCachePath(game.app_id, EmuArtKind::cover, config_root / "emulation"))) return 21;
    if (!screenscraperArt(game, EmuArtKind::background, config_root, transport).empty()) return 22; // "NOMEDIA" is no image.
    std::string stored;
    { std::ifstream cache(config_root / "metadata" / (gameCacheKey(game) + ".screenscraper.json"), std::ios::binary);
      stored.assign(std::istreambuf_iterator<char>(cache), std::istreambuf_iterator<char>()); }
    if (stored.find("dev%20id") != std::string::npos || stored.find("secret") != std::string::npos) return 23;

    // Videos: ES-DE first (none in this sandbox), then a download into the ES-DE layout; a failed
    // download is not repeated on every focus.
    const auto videos = root / "videos";
    if (downloadedVideoPath(game, videos) != videos / L"amiga" / L"videos" / L"DesertStrike_v4.2_0112.mp4") return 24;
    auto with_video = entry;
    with_video.game->media.push_back({L"video-normalized", L"", L"mp4", L"https://neoclone.screenscraper.fr/api2/mediaVideoJeu.php?media=video-normalized"});
    if (!ss::saveCached(game, with_video, config_root / "metadata")) return 25;
    downloads = 0;
    const auto write = [](const fs::path& file, std::string_view bytes) {
        fs::create_directories(file.parent_path()); std::ofstream(file, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    };
    const auto no_media = [&](const std::wstring& address, const fs::path& file) {
        ++downloads;
        if (address.find(L"devid=dev%20id") == std::wstring::npos) return HttpResponse{403};
        write(file, "NOMEDIA"); return HttpResponse{200};
    };
    if (!gameVideoFile(game, config_root, videos, no_media).empty() || downloads != 1 || fs::exists(downloadedVideoPath(game, videos))) return 26;
    if (!gameVideoFile(game, config_root, videos, no_media).empty() || downloads != 1) return 27; // Remembered.
    fs::remove(downloadedVideoPath(game, videos).wstring() + L".none");
    // An interrupted download leaves its part for the next attempt and is not a failure.
    const auto interrupted = [&](const std::wstring&, const fs::path& file) { ++downloads; write(file.wstring() + L".part", "partial"); return HttpResponse{}; };
    if (!gameVideoFile(game, config_root, videos, interrupted).empty() || fs::exists(downloadedVideoPath(game, videos).wstring() + L".none")) return 31;
    fs::remove(downloadedVideoPath(game, videos).wstring() + L".part");
    std::string mp4(4096, '\0'); std::memcpy(mp4.data() + 4, "ftyp", 4);
    downloads = 1;
    const auto video_transport = [&](const std::wstring&, const fs::path& file) { ++downloads; write(file, mp4); return HttpResponse{200}; };
    if (gameVideoFile(game, config_root, videos, video_transport) != downloadedVideoPath(game, videos) || downloads != 2) return 28;
    if (gameVideoFile(game, config_root, videos, video_transport) != downloadedVideoPath(game, videos) || downloads != 2) return 29; // Kept.
    downloads = 2;

    // jeuRecherche.php: every game found, best first.
    const auto searched = ss::parse(200, "<Data><jeux><jeu id=\"1\"><noms><nom region=\"wor\">Zelda</nom></noms></jeu>"
                                         "<jeu id=\"2\"><noms><nom region=\"eu\">Zelda II</nom></noms><joueurs>1</joueurs><note>14</note></jeu></jeux></Data>", true, L"en");
    if (searched.status != ss::Status::found || searched.games.size() != 2 || searched.games[1].name != L"Zelda II" ||
        searched.games[1].details.score != L"14/20" || searched.games[1].details.players != L"1" || searched.game->id != 1) return 32;
    // ssuserInfos.php: the account check behind "Sprawdź połączenie".
    const auto account = ss::parseAccount(200, "<Data><ssuser><id>player</id><niveau>1</niveau><maxthreads>1</maxthreads>"
                                               "<requeststoday>5</requeststoday><maxrequestsperday>20000</maxrequestsperday></ssuser></Data>");
    if (account.status != ss::Status::found || account.user != L"player" || account.threads != 1 || account.quota.requests_today != 5 ||
        ss::parseAccount(200, "Erreur de login : V\xc3\xa9rifier vos identifiants d\xc3\xa9veloppeur !").status != ss::Status::rejected ||
        ss::parseAccount(430, "").status != ss::Status::quota) return 33;
    // A manual pick survives the cache round trip.
    auto manual = entry; manual.manual = true;
    if (const auto back = ss::deserialize(ss::serialize(manual)); !back || !back->manual || back->game->details.score != L"16/20") return 34;
    // Multi-disc playlists: the first disc is the ROM ScreenScraper knows.
    fs::create_directories(root / "roms" / "psx");
    { std::ofstream list(root / "roms" / "psx" / "FF7.m3u"); list << "# discs\r\nFF7 (Disc 1).chd\r\nFF7 (Disc 2).chd\r\n"; }
    { std::ofstream disc(root / "roms" / "psx" / "FF7 (Disc 1).chd", std::ios::binary); disc << "abc"; }
    const auto playlist = ss::describeRom(root / "roms" / "psx" / "FF7.m3u");
    if (!playlist || playlist->name != L"FF7 (Disc 1).chd" || playlist->md5 != L"900150983cd24fb0d6963f7d28e17f72") return 35;
    // Steam trailers: the highlighted MP4, 480p, over HTTPS.
    if (steamTrailerUrl(620, R"json({"620":{"success":true,"data":{"movies":[{"id":1,"highlight":false,"mp4":{"480":"http://a.test/1.mp4"}},
        {"id":2,"highlight":true,"mp4":{"480":"http://b.test/2.mp4","max":"https://b.test/2max.mp4"}}]}}})json") != L"https://b.test/2.mp4" ||
        !steamTrailerUrl(620, R"json({"620":{"success":true,"data":{"movies":[{"id":1,"dash_h264":"https://x/y.mpd"}]}}})json").empty()) return 36;

    // Current store pages: DASH only. The video track (tallest at most 480 lines) becomes one fMP4.
    if (steamTrailerDashUrl(620, R"json({"620":{"data":{"movies":[{"id":1,"highlight":true,"dash_h264":"https://v.test/t/dash_h264.mpd?t=1"}]}}})json") !=
        L"https://v.test/t/dash_h264.mpd?t=1") return 37;
    const auto segments = dashVideoSegments(R"(<?xml version='1.0'?><MPD type="static" mediaPresentationDuration="PT0M7.5S"><Period>
        <AdaptationSet id="0" contentType="video">
          <Representation id="0" width="940" height="528"><SegmentTemplate timescale="1000000" duration="3000000" initialization="d/init-stream$RepresentationID$.m4s" media="d/chunk-stream$RepresentationID$-$Number%05d$.m4s" startNumber="1"></SegmentTemplate></Representation>
          <Representation id="1" width="854" height="480"><SegmentTemplate timescale="1000000" duration="3000000" initialization="d/init-stream$RepresentationID$.m4s" media="d/chunk-stream$RepresentationID$-$Number%05d$.m4s" startNumber="1"></SegmentTemplate></Representation>
          <Representation id="2" width="640" height="360"><SegmentTemplate timescale="1000000" duration="3000000" initialization="d/init-stream$RepresentationID$.m4s" media="d/chunk-stream$RepresentationID$-$Number%05d$.m4s" startNumber="1"></SegmentTemplate></Representation>
        </AdaptationSet>
        <AdaptationSet id="1" contentType="audio"><Representation id="3"><SegmentTemplate timescale="1000000" duration="3000000" initialization="d/init-stream$RepresentationID$.m4s" media="d/a.m4s"></SegmentTemplate></Representation></AdaptationSet>
        </Period></MPD>)", L"https://v.test/t/dash_h264.mpd?t=1");
    if (segments != std::vector<std::wstring>{L"https://v.test/t/d/init-stream1.m4s", L"https://v.test/t/d/chunk-stream1-00001.m4s",
                                             L"https://v.test/t/d/chunk-stream1-00002.m4s", L"https://v.test/t/d/chunk-stream1-00003.m4s"}) return 38;

    // Trailer clips: from second 10 for 30 s (3 s segments → segments 4..13 of 36).
    const auto clip = dashVideoSegments(R"(<MPD mediaPresentationDuration="PT1M45.6S"><Period><AdaptationSet contentType="video">
        <Representation id="1" height="480"><SegmentTemplate timescale="1000000" duration="3000000" initialization="d/i$RepresentationID$.m4s" media="d/c$RepresentationID$-$Number%05d$.m4s" startNumber="1"></SegmentTemplate></Representation>
        </AdaptationSet></Period></MPD>)", L"https://v.test/t/m.mpd");
    if (clip.size() != 11 || clip[1] != L"https://v.test/t/d/c1-00004.m4s" || clip.back() != L"https://v.test/t/d/c1-00013.m4s") return 39;
    // Segments joined: styp/sidx dropped, decode times moved to start at 0.
    {
        const auto box = [](std::string_view type, std::vector<uint8_t> body) {
            std::vector<uint8_t> out(8);
            const auto size = static_cast<uint32_t>(body.size() + 8);
            for (int i = 0; i < 4; ++i) out[i] = static_cast<uint8_t>(size >> (24 - 8 * i));
            std::memcpy(out.data() + 4, type.data(), 4);
            out.insert(out.end(), body.begin(), body.end());
            return out;
        };
        const auto tfdt = [&](uint64_t time) {
            std::vector<uint8_t> body{1, 0, 0, 0};
            for (int i = 0; i < 8; ++i) body.push_back(static_cast<uint8_t>(time >> (56 - 8 * i)));
            return box("tfdt", body);
        };
        const auto segment = [&](uint64_t time) {
            auto out = box("styp", {1, 2, 3, 4});
            const auto sidx = box("sidx", {0, 0, 0, 0}); out.insert(out.end(), sidx.begin(), sidx.end());
            const auto moof = box("moof", box("traf", tfdt(time))); out.insert(out.end(), moof.begin(), moof.end());
            const auto mdat = box("mdat", {9, 9}); out.insert(out.end(), mdat.begin(), mdat.end());
            return out;
        };
        std::optional<uint64_t> origin;
        const auto first = rebaseDashSegment(segment(9000000), origin), second = rebaseDashSegment(segment(12000000), origin);
        const auto time = [](const std::vector<uint8_t>& bytes) { // moof(8) traf(8) tfdt header(8) + version(4).
            uint64_t value = 0;
            for (int i = 0; i < 8; ++i) value = value << 8 | bytes[28 + i];
            return value;
        };
        if (first.size() != 8 + 8 + 20 + 10 || std::memcmp(first.data() + 4, "moof", 4) != 0 || time(first) != 0 || time(second) != 3000000) return 40;
    }
    // Trailer space: the oldest-played clips go first.
    {
        const auto folder = root / "trailers";
        fs::create_directories(folder);
        for (int i = 0; i < 3; ++i) {
            const auto file = folder / (std::to_wstring(i) + L".clip.mp4");
            std::ofstream(file, std::ios::binary) << std::string(1024 * 1024, 'x');
            fs::last_write_time(file, fs::file_time_type::clock::now() - std::chrono::hours(3 - i));
        }
        trimTrailers(folder, 2 * 1024 * 1024 + 10);
        if (fs::exists(folder / L"0.clip.mp4") || !fs::exists(folder / L"1.clip.mp4") || !fs::exists(folder / L"2.clip.mp4")) return 41;
    }
    // Artwork compaction: a 3840 px opaque PNG hero becomes a 2560 px JPEG; small or transparent ones stay PNG.
    {
        const auto png = [](UINT width, UINT height, bool alpha) {
            std::vector<uint8_t> pixels(size_t(width) * height * 4);
            uint32_t noise = 12345; // Photo-like detail: PNG cannot squeeze it, as with real heroes.
            for (size_t i = 0; i < pixels.size(); i += 4) {
                const auto x = (i / 4) % width, y = (i / 4) / width;
                noise = noise * 1664525u + 1013904223u;
                pixels[i] = static_cast<uint8_t>(x / 16 + (noise >> 28)); pixels[i + 1] = static_cast<uint8_t>(y / 8 + (noise >> 26 & 15)); pixels[i + 2] = static_cast<uint8_t>((x + y) / 32);
                pixels[i + 3] = alpha && x < width / 2 ? 0 : 255;
            }
#ifndef _WIN32
            return image_codec::encodePng({image_codec::Format::png, width, height, std::move(pixels)});
#else
            Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
            CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
            Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
            factory->CreateBitmapFromMemory(width, height, GUID_WICPixelFormat32bppBGRA, width * 4, static_cast<UINT>(pixels.size()), pixels.data(), &bitmap);
            Microsoft::WRL::ComPtr<IStream> stream; CreateStreamOnHGlobal(nullptr, TRUE, &stream);
            Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder; factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
            encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
            Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame; encoder->CreateNewFrame(&frame, nullptr);
            frame->Initialize(nullptr); frame->SetSize(width, height); frame->WriteSource(bitmap.Get(), nullptr); frame->Commit(); encoder->Commit();
            STATSTG stat{}; stream->Stat(&stat, STATFLAG_NONAME);
            std::vector<uint8_t> bytes(static_cast<size_t>(stat.cbSize.QuadPart));
            LARGE_INTEGER start{}; stream->Seek(start, STREAM_SEEK_SET, nullptr);
            ULONG read = 0; stream->Read(bytes.data(), static_cast<ULONG>(bytes.size()), &read);
            return bytes;
#endif
        };
#ifdef _WIN32
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#endif
        const auto hero = png(3840, 1240, false), logo = png(3000, 1000, true);
        const auto compact = compactImage(hero, ImageRole::background);
        const auto compact_logo = compactImage(logo, ImageRole::logo);
        if (compact.empty() || compact[0] != 0xff || compact.size() >= hero.size() ||
            compact_logo.empty() || compact_logo[0] != 0x89) return 42;
        ImageRole role{};
        if (!imageRoleFor(L"C:\\x\\heroes\\620.v2.hd.img", role) || role != ImageRole::background ||
            !imageRoleFor(L"C:\\x\\stores\\gog\\k\\logo.v2.img", role) || role != ImageRole::logo ||
            !imageRoleFor(L"C:\\x\\artwork\\selections\\k\\poster\\5.img", role) || role != ImageRole::poster ||
            imageRoleFor(L"C:\\x\\artwork\\thumbnails\\cover\\5.img", role) || imageRoleFor(L"C:\\x\\covers\\620.focus", role)) return 43;
    }

    // Optical-media artwork: exact platform/name, transparent scans, independent cache and manual picks.
    {
        const auto scan = discFixture(), square = discFixture(false);
        if (!discImage(scan) || discImage(square) || discImage({1, 2, 3})) return 50;
        Game pc{620, L"Disc Fixture"};
        auto cartridge = game; cartridge.platform = L"Switch"; cartridge.rom = L"D:\\roms\\switch\\Game.nsp";
        if (discSystemId(pc) != 138 || discSystemId(cartridge) != 0) return 51;
        const std::string item = R"json({"id":"42","noms":[{"region":"us","text":"Disc Fixture"}],"medias":[{"type":"support-2D","region":"eu","format":"png","url":"https://api.screenscraper.fr/api2/mediaJeu.php?systemeid=138&jeuid=42&media=support-2D(eu)"}]})json";
        int searches = 0, details = 0, images = 0;
        const auto disc_get = [&](const std::wstring& url, const std::wstring&, size_t) {
            if (url.find(L"mediaJeu.php") != url.npos) { ++images; return HttpResponse{200, scan}; }
            if (url.find(L"systemeid=138") == url.npos) return text(400, "wrong platform");
            if (url.find(L"jeuRecherche.php") != url.npos) { ++searches; return text(200, "{\"response\":{\"jeux\":[" + item + "]}}"); }
            if (url.find(L"jeuInfos.php") != url.npos && url.find(L"gameid=42") != url.npos && url.find(L"romnom=") == url.npos) {
                ++details; return text(200, "{\"response\":{\"jeu\":" + item + "}}");
            }
            return text(400, "unexpected route");
        };
        const auto cache = config_root / L"covers";
        const auto fetched = discArtwork(pc, cache, disc_get);
        if (fetched.bytes != scan || fetched.retryable || searches != 1 || details != 1 || images != 1) return 52;
        if (discArtwork(pc, cache, disc_get).bytes.empty() || searches != 1 || images != 1) return 53;
        if (gameArtworkPath(pc, ArtworkKind::disc, cache) == gameArtworkPath(pc, ArtworkKind::hero, cache) ||
            !discImage(cachedArtwork(pc, ArtworkKind::disc, cache))) return 54;
        if (!cacheArtworkVariant(pc, ArtworkKind::disc, 987, scan, cache) || !selectArtworkVariant(pc, ArtworkKind::disc, 987, cache) ||
            discArtwork(pc, cache, disc_get).bytes != scan || images != 1) return 55;
        // Fuzzy titles and an ambiguous exact name never acquire another game's scan.
        auto wrong = pc; wrong.app_id = 621; wrong.name = L"Disc Fixture Remastered";
        if (!screenscraperDiscGallery(wrong, config_root, disc_get).media.empty() || details != 1) return 56;
        const int before_miss = searches;
        if (!screenscraperDiscGallery(wrong, config_root, disc_get).media.empty() || searches != before_miss) return 57;
        auto ambiguous = pc; ambiguous.app_id = 622;
        const auto duplicate = [&](const auto&, const auto&, size_t) {
            return text(200, "{\"response\":{\"jeux\":[" + item + R"(,{"id":"43","noms":[{"region":"us","text":"Disc Fixture"}]}]}})");
        };
        if (screenscraperDiscGallery(ambiguous, config_root, duplicate).error.empty()) return 58;
        int rejected_requests = 0;
        if (!screenscraperDiscGallery(cartridge, config_root, [&](const auto&, const auto&, size_t) { ++rejected_requests; return text(200, "{}"); }).media.empty() || rejected_requests) return 59;
        // Transport failures remain retryable; quota/refusal handling belongs to the shared gate.
        auto offline = pc; offline.app_id = 623;
        const auto failed = screenscraperDiscGallery(offline, config_root, [](const auto&, const auto&, size_t) { return text(503, "unavailable"); });
        if (!failed.retryable || ss::loadCached(offline, config_root / L"metadata" / L"discs")) return 60;
        // A cached ROM identification can supply discs even while its drive is disconnected.
        auto ps1 = game; ps1.platform = L"PS1"; ps1.rom = L"Z:\\roms\\psx\\Disc Fixture.chd"; ps1.app_id = 0xFA000002u; ps1.shortcut_id = 0;
        ss::GameInfo identified; identified.id = 99; identified.name = L"Disc Fixture";
        identified.media.push_back({L"support-2D", L"jp", L"png", L"https://api.screenscraper.fr/api2/mediaJeu.php?systemeid=57&jeuid=99&media=support-2D(jp)"});
        ss::saveCached(ps1, {L"offline", ss::unixMs(), identified, true}, config_root / L"metadata");
        const auto local = screenscraperDiscGallery(ps1, config_root, [&](const auto&, const auto&, size_t) { ++rejected_requests; return text(500, "unexpected"); });
        if (local.media.size() != 1 || rejected_requests || discSystemId(ps1) != 57) return 61;
        // A user's pick made during the HTTP download cannot be overwritten on disk by the automatic result.
        auto racing = pc; racing.app_id = 624;
        bool picked = false;
        const auto racing_get = [&](const std::wstring& url, const std::wstring& headers, size_t max) {
            if (url.find(L"mediaJeu.php") != url.npos) {
                picked = cacheArtworkVariant(racing, ArtworkKind::disc, 888, square, cache) &&
                    selectArtworkVariant(racing, ArtworkKind::disc, 888, cache);
            }
            return disc_get(url, headers, max);
        };
        if (discArtwork(racing, cache, racing_get).bytes != scan || !picked ||
            selectedArtworkId(racing, ArtworkKind::disc, cache) != 888 || cachedArtwork(racing, ArtworkKind::disc, cache) != square) return 62;
        auto throttled_game = pc; throttled_game.app_id = 625;
        int throttled_calls = 0;
        const auto limited = [&](const auto&, const auto&, size_t) { ++throttled_calls; return text(429, "rate limited"); };
        if (!screenscraperDiscGallery(throttled_game, config_root, limited).retryable ||
            !screenscraperDiscGallery(throttled_game, config_root, limited).retryable || throttled_calls != 1) return 63;
    }

    // A pause (refusal, quota) holds every caller, not only the startup pass.
    ss::pause(config_root / "metadata", 60 * 1000);
    fs::remove(esdeArtCachePath(game.app_id, EmuArtKind::cover, config_root / "emulation"));
    if (!ss::paused(config_root / "metadata") || !screenscraperArt(game, EmuArtKind::cover, config_root, transport).empty() || downloads != 2) return 30;
    return 0;
}
