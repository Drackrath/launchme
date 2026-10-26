// RetroAchievements: which console a ROM is for, its hash (rcheevos' rules for cartridges, as in
// SuccessStory), the game ID by hash or by title, and the user's progress.
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz.h>

#include "achievements.hpp"
#include "../../src/platform/text_encoding.hpp"

#include <algorithm>
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
    // and "The Legend of Zelda - A Link to the Past" alike.
    const std::wstring wide = text_encoding::wideLenient(title);
    std::wstring out, word;
    const auto flush = [&] { if (word != L"the" && word != L"a" && word != L"an") out += word; word.clear(); };
    for (const auto c : wide) {
        if (std::iswalnum(c)) word += static_cast<wchar_t>(std::towlower(c));
        else if (c != L'\'') flush();
    }
    flush();
    return text_encoding::utf8Lenient(out);
}
}

// RA's console IDs: by the platform's words, else by the ROM's extension.
int retroConsole(const std::string& platform, const std::filesystem::path& rom) {
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
    auto extension = lowered(rom.extension().string());
    if (extension == ".zip") extension = lowered(rom.stem().extension().string()); // "game.sfc.zip"
    static const std::pair<std::string_view, int> extensions[]{
        {".nes", 7}, {".fds", 7}, {".sfc", 3}, {".smc", 3}, {".gb", 4}, {".gbc", 6}, {".gba", 5}, {".n64", 2}, {".z64", 2}, {".v64", 2},
        {".nds", 18}, {".md", 1}, {".gen", 1}, {".smd", 1}, {".32x", 10}, {".sms", 11}, {".gg", 15}, {".pce", 8}, {".a26", 25}, {".a78", 51},
        {".lnx", 13}, {".ngp", 14}, {".ngc", 14}, {".ws", 53}, {".wsc", 53}, {".vb", 28}, {".col", 44}, {".min", 24}};
    for (const auto& [ext, console] : extensions) if (extension == ext) return console;
    return 0;
}

std::string retroHash(int console, const std::filesystem::path& rom) {
    if (rom.empty()) return {};
    // Arcade: the set's file name, not its contents.
    if (console == 27) { const auto set = rom.stem().string(); return md5Hex(std::vector<uint8_t>(set.begin(), set.end())); }
    // Discs (PlayStation, Saturn, Sega CD, Dreamcast, PS2, PSP, GameCube) hash their tracks and
    // executables: those are found by title instead.
    if (console == 12 || console == 39 || console == 9 || console == 40 || console == 21 || console == 41 || console == 16) return {};
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

std::optional<uint32_t> parseRetroGameId(std::string_view body) {
    const auto root = json::parse(body);
    if (!root || field(*root, "Success").type != json::Value::Type::boolean || !field(*root, "Success").flag) return {};
    const auto& id = field(*root, "GameID");
    if (id.type != json::Value::Type::number || id.numeric < 1 || id.numeric > UINT32_MAX) return {};
    return static_cast<uint32_t>(id.numeric);
}

std::optional<uint32_t> retroGameByTitle(std::string_view game_list, const std::string& title) {
    const auto root = json::parse(game_list, 16 * 1024 * 1024);
    if (!root || root->type != json::Value::Type::array) return {};
    const auto wanted = normalizedTitle(title);
    if (wanted.empty()) return {};
    for (const auto& game : root->elements) {
        const auto name = text(game, "Title");
        bool match = normalizedTitle(name) == wanted;
        // "Pokemon - Red Version | Blue Version": either part.
        for (size_t start = 0; !match && start < name.size();) {
            const auto end = name.find_first_of("|", start);
            match = normalizedTitle(name.substr(start, end == std::string::npos ? std::string::npos : end - start)) == wanted;
            start = end == std::string::npos ? name.size() : end + 1;
        }
        const auto& id = field(game, "ID");
        if (match && id.type == json::Value::Type::number && id.numeric >= 1 && id.numeric <= UINT32_MAX) return static_cast<uint32_t>(id.numeric);
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
