#include "../src/artwork/esde_art.hpp"
#include "../src/library/esde_gamelist.hpp"

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
void writeImage(const std::filesystem::path& file, bool png, char fill) {
    std::ofstream image(file, std::ios::binary);
    if (png) { image.put(static_cast<char>(0x89)); image.put('P'); }
    else { image.put(static_cast<char>(0xff)); image.put(static_cast<char>(0xd8)); }
    for (int i = 0; i < 256; ++i) image.put(fill);
}
}

int main() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() /
        (L"launchme-esde-art-" + std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{root};
    const auto media = root / "media" / "ps2" / "fanart";
    const auto lists = root / "gamelists" / "ps2";
    fs::create_directories(media);
    fs::create_directories(lists);
    { std::ofstream xml(lists / "gamelist.xml");
      xml << "<gameList><game><path>./Resident Evil - Code Veronica (Europe).chd</path>"
             "<name>Resident Evil Code Veronica</name></game></gameList>"; }
    writeImage(media / "Resident Evil - Code Veronica (Europe).jpg", false, 'x');
    const auto found = esdeArtFromMedia(root / "media", root / "gamelists", L"ps2",
                                        L"Resident Evil Code Veronica", EmuArtKind::background);
    const auto unrelated = esdeArtFromMedia(root / "media", root / "gamelists", L"ps2",
                                            L"Unrelated", EmuArtKind::background);
    if (found.size() != 258 || !unrelated.empty()) {
        std::cerr << "ES-DE display name to ROM stem or fanart lookup failed\n";
        return 1;
    }

    // Metadata as ES-DE writes it after a ScreenScraper scrape.
    const auto list = esde::parseGamelist(
        "<?xml version=\"1.0\"?>\n<gameList>\n"
        "<game id=\"1\" source=\"ScreenScraper.fr\">\n<path>./Ratchet &amp; Clank (Europe).chd</path>\n"
        "<name>Ratchet &amp; Clank</name>\n<desc>Ratchet&apos;s first&#x0A;adventure &#8211; caf&#233;.</desc>\n"
        "<rating>0.8</rating>\n<releasedate>20021108T000000</releasedate>\n<developer>Insomniac Games</developer>\n"
        "<publisher>Sony</publisher>\n<genre>Platform</genre>\n<players>1</players>\n</game>\n"
        "<game><path>./Ratchet &amp; Clank (USA).chd</path><name>Ratchet &amp; Clank</name>"
        "<releasedate>20020101T000000</releasedate><rating>7</rating></game>\n"
        "<game><path>./God of War.ps3</path><name>God of War</name></game>\n"
        "<game><path>./discs/Final Fantasy X (Disc 1).chd</path><name>Final Fantasy X</name></game>\n"
        "<folder><path>./discs</path><name>discs</name></folder>\n</gameList>\n");
    if (list.size() != 4) return 2;
    const auto& ratchet = list[0];
    if (ratchet.name != L"Ratchet & Clank" || ratchet.details.description != L"Ratchet's first\nadventure \u2013 caf\u00e9." ||
        ratchet.details.release_date != L"2002-11-08" || ratchet.details.developer != L"Insomniac Games" ||
        ratchet.details.publisher != L"Sony" || ratchet.details.genres != L"Platform" || ratchet.players != L"1" ||
        ratchet.rating != 0.8 || ratchet.stem() != L"Ratchet & Clank (Europe)") return 3;
    if (list[1].details.release_date != L"2002" || list[1].rating != -1) return 4; // Year-only date, out-of-range score.

    // The ROM file decides, as in ES-DE: two regions share one display name.
    const auto* usa = esde::match(list, L"K:\\Emulation\\roms\\ps2\\Ratchet & Clank (USA).chd", L"Ratchet & Clank");
    const auto* europe = esde::match(list, L"K:/Emulation/ROMS/PS2/ratchet & clank (europe).CHD", L"Something else");
    if (!usa || usa != &list[1] || !europe || europe != &list[0]) return 5;
    // Subfolders and folder games (RPCS3 starts EBOOT.BIN inside Game.ps3).
    const auto* ffx = esde::match(list, L"D:\\roms\\ps2\\discs\\Final Fantasy X (Disc 1).chd", L"");
    const auto* gow = esde::match(list, L"D:\\roms\\ps3\\God of War.ps3\\PS3_GAME\\USRDIR\\EBOOT.BIN", L"");
    if (ffx != &list[3] || gow != &list[2]) return 6;
    // A file name is matched whole, never as a suffix of another name; without a ROM match the title decides.
    if (esde::match(list, L"D:\\roms\\ps2\\Final Fantasy X (Disc 1).chd", L"") ||
        esde::match(list, L"D:\\roms\\ps2\\Not Ratchet & Clank (USA).chd", L"") ||
        esde::match(list, L"D:\\roms\\ps2\\Unknown.chd", L"God of War") != &list[2]) return 7;

    // ES-DE systems: the platform's folders, then the ROM's folder below "roms".
    const auto retro = esde::systems(L"Retro", L"K:\\Emulation\\roms\\SNES\\Super Metroid.sfc");
    const auto ps2 = esde::systems(L"PS2", L"K:\\Emulation\\roms\\ps2\\Game.chd");
    const auto unsafe = esde::systems(L"PC", L"C:\\Games\\game.dat");
    if (retro != std::vector<std::wstring>{L"snes"} || ps2 != std::vector<std::wstring>{L"ps2"} ||
        unsafe != std::vector<std::wstring>{L"games"}) return 8;

    // Library: the ROM wins over a title match, also in another candidate system.
    fs::create_directories(root / "gamelists" / "snes");
    { std::ofstream xml(root / "gamelists" / "snes" / "gamelist.xml");
      xml << "<gameList><game><path>./Super Metroid (Japan, USA) (En,Ja).sfc</path><name>Super Metroid</name>"
             "<desc>Samus.</desc></game></gameList>"; }
    esde::Library library(root / "gamelists");
    const auto metroid = library.find(L"Retro", L"K:\\Emulation\\roms\\snes\\Super Metroid (Japan, USA) (En,Ja).sfc", L"SM");
    if (!metroid || metroid->system != L"snes" || metroid->entry.details.description != L"Samus.") return 9;
    if (library.find(L"Retro", L"K:\\Emulation\\roms\\snes\\Zelda.sfc", L"Zelda")) return 10;

    // Identified by ROM: the media named after the ROM, never another title's file; logos must be PNG.
    fs::create_directories(root / "media" / "snes" / "covers");
    fs::create_directories(root / "media" / "snes" / "marquees");
    writeImage(root / "media" / "snes" / "covers" / "Super Metroid (Japan, USA) (En,Ja).png", true, 'c');
    writeImage(root / "media" / "snes" / "covers" / "Super Metroid (Europe).png", true, 'e');
    writeImage(root / "media" / "snes" / "marquees" / "Super Metroid (Japan, USA) (En,Ja).png", true, 'm');
    const auto cover = esdeArtFromMedia(root / "media", root / "gamelists", L"snes", L"SM", EmuArtKind::cover,
                                        L"K:\\roms\\snes\\Super Metroid (Japan, USA) (En,Ja).sfc");
    const auto logo = esdeArtFromMedia(root / "media", root / "gamelists", L"snes", L"SM", EmuArtKind::logo,
                                       L"K:\\roms\\snes\\Super Metroid (Japan, USA) (En,Ja).sfc");
    if (cover.size() != 258 || cover[2] != 'c' || logo.size() != 258 || logo[2] != 'm') return 11;
    writeImage(root / "media" / "snes" / "marquees" / "Super Metroid (Japan, USA) (En,Ja).png", false, 'j');
    if (!esdeArtFromMedia(root / "media", root / "gamelists", L"snes", L"SM", EmuArtKind::logo,
                          L"K:\\roms\\snes\\Super Metroid (Japan, USA) (En,Ja).sfc").empty()) return 12;
    return 0;
}
