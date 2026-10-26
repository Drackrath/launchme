// RetroAchievements: which console a ROM is for, its hash (rcheevos' rules for cartridges, as in
// SuccessStory), the game ID by hash or by title from the console's game list
// (API_GetGameList with h=1, api-docs.retroachievements.org) and the user's progress.
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz.h>

#include "achievements.hpp"
#include "../../src/platform/text_encoding.hpp"

#include <algorithm>
#include <cstdio>
#include <cwctype>
#include <fstream>

namespace achievements {
using namespace plugins::wire;
namespace {
std::string lowered(std::string text) {
    for (auto& c : text) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return text;
}
bool has(const std::string& text, std::string_view word) { return text.find(word) != std::string::npos; }
std::vector<uint8_t> readAll(const std::filesystem::path& file, size_t limit) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    if (ec || !size || size > limit) return {};
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    std::ifstream in(file, std::ios::binary);
    if (!in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) return {};
    return bytes;
}
// A zipped ROM: its first file that is not a folder or a text file.
std::vector<uint8_t> unzipFirst(const std::vector<uint8_t>& zipped, std::string& name) {
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, zipped.data(), zipped.size(), 0)) return {};
    std::vector<uint8_t> out;
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&zip); ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&zip, i, &stat) || stat.m_is_directory || stat.m_uncomp_size == 0 || stat.m_uncomp_size > 64 * 1024 * 1024) continue;
        const auto file = lowered(stat.m_filename);
        if (file.ends_with(".txt") || file.ends_with(".nfo") || file.ends_with(".diz")) continue;
        out.resize(static_cast<size_t>(stat.m_uncomp_size));
        if (!mz_zip_reader_extract_to_mem(&zip, i, out.data(), out.size(), 0)) out.clear();
        name = stat.m_filename;
        break;
    }
    mz_zip_reader_end(&zip);
    return out;
}
uint32_t le32(const std::vector<uint8_t>& b, size_t at) {
    return at + 4 <= b.size() ? b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (static_cast<uint32_t>(b[at + 3]) << 24) : 0;
}
std::string normalizedTitle(std::string_view title) {
    // Letters and digits of every word but the articles: "Legend of Zelda, The: A Link to the Past"
    // and "The Legend of Zelda - A Link to the Past" alike. Dump tags in brackets do not count:
    // "Castlevania - Symphony of the Night (USA) [Disc 1]".
    const std::wstring wide = text_encoding::wideLenient(title);
    std::wstring out, word;
    int depth = 0;
    const auto flush = [&] { if (word != L"the" && word != L"a" && word != L"an") out += word; word.clear(); };
    for (const auto c : wide) {
        if (c == L'(' || c == L'[') { flush(); ++depth; continue; }
        if ((c == L')' || c == L']') && depth) { --depth; continue; }
        if (depth) continue;
        if (std::iswalnum(c)) word += static_cast<wchar_t>(std::towlower(c));
        else if (c != L'\'') flush();
    }
    flush();
    return text_encoding::utf8Lenient(out);
}
// The system folders of ES-DE and EmuDeck (roms\<system>\…): the most certain console of a ROM.
// 0: a system RetroAchievements has no achievements for.
std::optional<int> folderConsole(const std::filesystem::path& rom) {
    static const std::pair<std::string_view, int> folders[]{
        {"nes", 7}, {"fds", 7}, {"famicom", 7}, {"snes", 3}, {"sfc", 3}, {"snesna", 3}, {"n64", 2}, {"gb", 4}, {"gbc", 6}, {"gba", 5},
        {"nds", 18}, {"genesis", 1}, {"megadrive", 1}, {"mastersystem", 11}, {"gamegear", 15}, {"segacd", 9}, {"megacd", 9},
        {"sega32x", 10}, {"saturn", 39}, {"dreamcast", 40}, {"pcengine", 8}, {"tg16", 8}, {"pcenginecd", 76}, {"tg-cd", 76},
        {"psx", 12}, {"ps2", 21}, {"psp", 41}, {"gc", 16}, {"gamecube", 16}, {"atari2600", 25}, {"atari7800", 51}, {"atarilynx", 13},
        {"lynx", 13}, {"atarijaguar", 17}, {"jaguar", 17}, {"ngp", 14}, {"ngpc", 14}, {"wonderswan", 53}, {"wonderswancolor", 53},
        {"virtualboy", 28}, {"colecovision", 44}, {"pokemini", 24}, {"msx", 29}, {"msx2", 29}, {"arcade", 27}, {"mame", 27},
        {"fbneo", 27}, {"neogeo", 27}, {"neogeocd", 56}, {"3do", 43}, {"sg-1000", 33}, {"intellivision", 45}, {"vectrex", 46},
        // Systems without RetroAchievements: their games never go to the web API.
        {"wii", 0}, {"wiiu", 0}, {"switch", 0}, {"n3ds", 0}, {"3ds", 0}, {"ps3", 0}, {"ps4", 0}, {"psvita", 0}, {"xbox", 0}, {"xbox360", 0},
    };
    auto folder = rom.parent_path();
    for (int level = 0; level < 3 && folder.has_filename(); ++level, folder = folder.parent_path()) {
        const auto name = lowered(text_encoding::utf8Lenient(folder.filename().wstring()));
        for (const auto& [system, console] : folders) if (name == system) return console;
    }
    return std::nullopt;
}
// A zipped ROM's console by the extension of the file inside ("game.zip" holding "game.sfc").
std::filesystem::path zippedName(const std::filesystem::path& rom) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(rom, ec);
    if (ec || !size || size > 64 * 1024 * 1024) return {};
    FILE* file = nullptr;
    if (_wfopen_s(&file, rom.c_str(), L"rb") || !file) return {};
    struct Close { FILE* f; ~Close() { std::fclose(f); } } close{file};
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_cfile(&zip, file, size, 0)) return {}; // Reads only the central directory.
    std::string name;
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&zip) && name.empty(); ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&zip, i, &stat) || stat.m_is_directory) continue;
        const auto inner = lowered(stat.m_filename);
        if (!inner.ends_with(".txt") && !inner.ends_with(".nfo") && !inner.ends_with(".diz")) name = stat.m_filename;
    }
    mz_zip_reader_end(&zip);
    return std::filesystem::path(text_encoding::wideLenient(name));
}
// rcheevos hashes these systems' whole cartridge (or set name); the rest are discs, hashed from
// their tracks and executables, and found by title here.
bool cartridge(int console) {
    static constexpr int cartridges[]{1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 13, 14, 15, 17, 18, 24, 25, 27, 28, 29, 33, 44, 45, 46, 51, 53};
    return std::find(std::begin(cartridges), std::end(cartridges), console) != std::end(cartridges);
}
}

// RA's console IDs (API_GetConsoleIDs): by the ROM's system folder, the platform's words, else
// the ROM's extension (a zip's by the file inside).
int retroConsole(const std::string& platform, const std::filesystem::path& rom) {
    if (const auto folder = folderConsole(rom)) return *folder;
    // Whole words: "nes" is not in "genesis".
    std::string p = " ";
    for (const char c : lowered(platform)) p += (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ? c : ' ';
    p += ' ';
    struct Word { std::string_view word; int console; };
    static constexpr Word words[]{
        {"game boy advance", 5}, {"gameboy advance", 5}, {"gba", 5}, {"game boy color", 6}, {"gameboy color", 6}, {"gbc", 6},
        {"game boy", 4}, {"gameboy", 4}, {"super nintendo", 3}, {"snes", 3}, {"super famicom", 3}, {"famicom disk", 7},
        {"nintendo entertainment", 7}, {"famicom", 7}, {"nes", 7}, {"nintendo 64", 2}, {"n64", 2}, {"nintendo ds", 18}, {"nds", 18},
        {"genesis", 1}, {"mega drive", 1}, {"megadrive", 1}, {"32x", 10}, {"sega cd", 9}, {"mega cd", 9}, {"master system", 11},
        {"game gear", 15}, {"saturn", 39}, {"dreamcast", 40}, {"pc engine", 8}, {"turbografx", 8}, {"pc-engine", 8},
        {"playstation portable", 41}, {"psp", 41}, {"playstation 2", 21}, {"ps2", 21}, {"playstation", 12}, {"ps1", 12}, {"psx", 12},
        {"gamecube", 16}, {"atari 2600", 25}, {"atari 7800", 51}, {"lynx", 13}, {"jaguar", 17}, {"neo geo pocket", 14}, {"neogeo pocket", 14},
        {"wonderswan", 53}, {"virtual boy", 28}, {"colecovision", 44}, {"pokemon mini", 24}, {"msx", 29}, {"arcade", 27}, {"mame", 27}, {"fbneo", 27},
    };
    for (const auto& [word, console] : words) {
        std::string padded = " ";
        for (const char c : word) padded += c == '-' ? ' ' : c;
        if (has(p, padded + " ")) return console;
    }
    static const std::pair<std::string_view, int> extensions[]{
        {".nes", 7}, {".fds", 7}, {".sfc", 3}, {".smc", 3}, {".gb", 4}, {".gbc", 6}, {".gba", 5}, {".n64", 2}, {".z64", 2}, {".v64", 2},
        {".nds", 18}, {".md", 1}, {".gen", 1}, {".smd", 1}, {".32x", 10}, {".sms", 11}, {".gg", 15}, {".pce", 8}, {".a26", 25}, {".a78", 51},
        {".lnx", 13}, {".ngp", 14}, {".ngc", 14}, {".ws", 53}, {".wsc", 53}, {".vb", 28}, {".col", 44}, {".min", 24}};
    const auto byExtension = [&](const std::filesystem::path& file) {
        const auto extension = lowered(file.extension().string());
        for (const auto& [ext, console] : extensions) if (extension == ext) return console;
        return 0;
    };
    const auto extension = lowered(rom.extension().string());
    if (extension != ".zip") return byExtension(rom);
    if (const int console = byExtension(rom.stem())) return console; // "game.sfc.zip"
    return byExtension(zippedName(rom));
}

std::string retroHash(int console, const std::filesystem::path& rom) {
    if (rom.empty()) return {};
    // Arcade: the set's file name, not its contents.
    if (console == 27) { const auto set = rom.stem().string(); return md5Hex(std::vector<uint8_t>(set.begin(), set.end())); }
    if (!cartridge(console)) return {};
    auto bytes = readAll(rom, 64 * 1024 * 1024);
    if (bytes.empty()) return {};
    std::string inner = rom.filename().string();
    if (lowered(rom.extension().string()) == ".zip") {
        bytes = unzipFirst(bytes, inner);
        if (bytes.empty()) return {};
    }
    const auto skip = [&](size_t count) { bytes.erase(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(std::min(count, bytes.size()))); };
    switch (console) {
    case 7: // iNES and FDS headers.
        if (bytes.size() > 16 && ((bytes[0] == 'N' && bytes[1] == 'E' && bytes[2] == 'S' && bytes[3] == 0x1A) ||
                                  (bytes[0] == 'F' && bytes[1] == 'D' && bytes[2] == 'S' && bytes[3] == 0x1A))) skip(16);
        break;
    case 3: if (bytes.size() % 8192 == 512) skip(512); break; // A copier header.
    case 8: if (bytes.size() % 131072 == 512) skip(512); break;
    case 13: if (bytes.size() > 64 && bytes[0] == 'L' && bytes[1] == 'Y' && bytes[2] == 'N' && bytes[3] == 'X') skip(64); break;
    case 51: if (bytes.size() > 128 && std::string_view(reinterpret_cast<const char*>(bytes.data()) + 1, 9) == "ATARI7800") skip(128); break;
    case 2: // Big-endian (.z64) whatever the dump's byte order.
        if (bytes.size() >= 4 && bytes[0] == 0x37 && bytes[1] == 0x80) {
            for (size_t i = 0; i + 1 < bytes.size(); i += 2) std::swap(bytes[i], bytes[i + 1]);
        } else if (bytes.size() >= 4 && bytes[0] == 0x40 && bytes[1] == 0x12) {
            for (size_t i = 0; i + 3 < bytes.size(); i += 4) { std::swap(bytes[i], bytes[i + 3]); std::swap(bytes[i + 1], bytes[i + 2]); }
        }
        break;
    case 18: { // Nintendo DS: the header, both binaries and the icon.
        if (bytes.size() < 0x160) return {};
        const auto arm9 = le32(bytes, 0x20), arm9_size = le32(bytes, 0x2C), arm7 = le32(bytes, 0x30), arm7_size = le32(bytes, 0x3C), icon = le32(bytes, 0x68);
        const auto in = [&](uint64_t at, uint64_t size) { return at + size <= bytes.size(); };
        if (!in(arm9, arm9_size) || !in(arm7, arm7_size) || arm9_size + arm7_size > 16 * 1024 * 1024) return {};
        std::vector<uint8_t> parts(bytes.begin(), bytes.begin() + 0x160);
        parts.insert(parts.end(), bytes.begin() + arm9, bytes.begin() + arm9 + arm9_size);
        parts.insert(parts.end(), bytes.begin() + arm7, bytes.begin() + arm7 + arm7_size);
        if (icon && in(icon, 0xA00)) parts.insert(parts.end(), bytes.begin() + icon, bytes.begin() + icon + 0xA00);
        else parts.insert(parts.end(), 0xA00, 0);
        return md5Hex(parts);
    }
    default: break;
    }
    return md5Hex(bytes);
}

namespace {
std::optional<uint32_t> gameId(const json::Value& game) {
    const auto& id = field(game, "ID");
    if (id.type != json::Value::Type::number || id.numeric < 1 || id.numeric > UINT32_MAX) return {};
    return static_cast<uint32_t>(id.numeric);
}
}

bool retroGameList(std::string_view body) {
    const auto root = json::parse(body, 32 * 1024 * 1024);
    return root && root->type == json::Value::Type::array;
}

std::optional<uint32_t> retroGameByHash(std::string_view game_list, const std::string& hash) {
    const auto wanted = lowered(hash);
    if (wanted.size() != 32) return {};
    const auto root = json::parse(game_list, 32 * 1024 * 1024);
    if (!root || root->type != json::Value::Type::array) return {};
    for (const auto& game : root->elements)
        for (const auto& known : field(game, "Hashes").elements)
            if (known.type == json::Value::Type::string && lowered(known.text) == wanted) return gameId(game);
    return {};
}

std::optional<uint32_t> retroGameByTitle(std::string_view game_list, const std::string& title) {
    const auto root = json::parse(game_list, 32 * 1024 * 1024);
    if (!root || root->type != json::Value::Type::array) return {};
    const auto wanted = normalizedTitle(title);
    if (wanted.empty()) return {};
    for (const auto& game : root->elements) {
        const auto name = text(game, "Title");
        // "~Hack~ …", "~Homebrew~ …" and "… [Subset - Bonus]" are other sets than the game itself.
        if (name.starts_with('~') || name.find("[Subset") != std::string::npos) continue;
        bool match = normalizedTitle(name) == wanted;
        // "Pokemon - Red Version | Blue Version": either part.
        for (size_t start = 0; !match && start < name.size();) {
            const auto end = name.find_first_of("|", start);
            match = normalizedTitle(name.substr(start, end == std::string::npos ? std::string::npos : end - start)) == wanted;
            start = end == std::string::npos ? name.size() : end + 1;
        }
        if (match) if (const auto id = gameId(game)) return id;
    }
    return {};
}

std::optional<List> parseRetroProgress(std::string_view body) {
    const auto root = json::parse(body, 8 * 1024 * 1024);
    if (!root || root->type != json::Value::Type::object) return {};
    const auto& achievements = field(*root, "Achievements");
    if (achievements.type != json::Value::Type::object && achievements.type != json::Value::Type::array) return {};
    double players = field(*root, "NumDistinctPlayers").numeric;
    if (players <= 0) players = field(*root, "NumDistinctPlayersCasual").numeric;
    List list; list.source = "RetroAchievements";
    std::vector<std::pair<int, Item>> ordered;
    const auto add = [&](const json::Value& a) {
        Item item;
        const auto& id = field(a, "ID");
        item.id = id.text.empty() ? text(a, "ID") : id.text;
        if (item.id.empty()) return;
        item.name = text(a, "Title"); item.description = text(a, "Description");
        auto earned = text(a, "DateEarnedHardcore");
        if (earned.empty()) earned = text(a, "DateEarned");
        item.achieved = !earned.empty();
        item.unlocked_at = item.achieved ? parseIsoTime(earned) : 0;
        const auto badge = text(a, "BadgeName");
        if (!badge.empty() && std::all_of(badge.begin(), badge.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }))
            item.icon = "https://media.retroachievements.org/Badge/" + badge + (item.achieved ? ".png" : "_lock.png");
        if (players > 0) item.percent = std::min(100.0, field(a, "NumAwarded").numeric * 100.0 / players);
        ordered.emplace_back(static_cast<int>(field(a, "DisplayOrder").numeric), std::move(item));
    };
    if (achievements.type == json::Value::Type::object) for (const auto& [key, a] : achievements.properties) add(a);
    else for (const auto& a : achievements.elements) add(a);
    std::stable_sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.first != b.first ? a.first < b.first : a.second.id < b.second.id; });
    for (auto& [order, item] : ordered) list.items.push_back(std::move(item));
    return list;
}
}
