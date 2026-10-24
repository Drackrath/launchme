#pragma once
#include "../platform/text_encoding.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <vector>

namespace rom_layout {
using Systems = std::vector<std::pair<std::string, std::string>>; // folder, display name
inline std::string normalized(std::string_view value) {
    std::string out;
    for (unsigned char c : value) {
        if (c >= 'A' && c <= 'Z') c = static_cast<unsigned char>(c + 'a' - 'A');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out += static_cast<char>(c);
    }
    return out;
}
inline bool safeFolder(std::string_view folder) {
    return !folder.empty() && folder.size() <= 40 && std::all_of(folder.begin(), folder.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
    });
}
inline Systems parse(std::string_view text) {
    Systems out; std::istringstream input{std::string(text)}; std::string line;
    while (std::getline(input, line)) {
        const auto colon = line.find(':'); if (colon == line.npos) continue;
        const auto folder = line.substr(0, colon);
        if (!safeFolder(folder)) continue;
        const auto begin = line.find_first_not_of(" \t", colon + 1);
        if (begin == line.npos) continue;
        out.emplace_back(folder, line.substr(begin, line.find_last_not_of(" \t\r") - begin + 1));
    }
    return out;
}
inline Systems load(const std::filesystem::path& root) {
    if (root.empty()) return {};
    std::ifstream in(root / L"systems.txt", std::ios::binary | std::ios::ate);
    const auto size = in ? static_cast<int64_t>(in.tellg()) : -1;
    if (size <= 0 || size > 65536) return {};
    std::string text(static_cast<size_t>(size), '\0'); in.seekg(0);
    return in.read(text.data(), size) ? parse(text) : Systems{};
}
inline std::string system(std::string_view platform, const Systems& actual = {}) {
    const auto wanted = normalized(platform);
    if (wanted.empty()) return {};
    // Exact configured folder wins; never treat a platform supplied by the server as a path.
    for (const auto& [folder, title] : actual) if (normalized(folder) == wanted) return folder;
    static const std::pair<const char*, const char*> standard[] = {
        {"wii","Nintendo Wii"}, {"wiiu","Nintendo Wii U"}, {"gc","Nintendo GameCube"}, {"gc","GameCube"},
        {"switch","Nintendo Switch"}, {"switch2","Nintendo Switch 2"},
        {"psx","Sony PlayStation"}, {"psx","PlayStation"}, {"psx","PS1"},
        {"ps2","Sony PlayStation 2"}, {"ps2","PlayStation 2"}, {"ps3","Sony PlayStation 3"}, {"ps3","PlayStation 3"},
        {"ps4","Sony PlayStation 4"}, {"ps4","PlayStation 4"}, {"psp","Sony PlayStation Portable"}, {"psvita","Sony PlayStation Vita"},
        {"gb","Nintendo Game Boy"}, {"gba","Nintendo Game Boy Advance"}, {"gbc","Nintendo Game Boy Color"},
        {"n3ds","Nintendo 3DS"}, {"n3ds","3DS"}, {"nds","Nintendo DS"}, {"n64","Nintendo 64"},
        {"nes","Nintendo Entertainment System"}, {"snes","Super Nintendo"}, {"snes","Nintendo SNES (Super Nintendo)"},
        {"snes","Super Nintendo Entertainment System"}, {"dreamcast","Sega Dreamcast"}, {"saturn","Sega Saturn"},
        {"megadrive","Sega Mega Drive"}, {"genesis","Sega Genesis"}, {"mastersystem","Sega Master System"}, {"gamegear","Sega Game Gear"},
        {"atari2600","Atari 2600"}, {"atari5200","Atari 5200"}, {"atari7800","Atari 7800"}, {"atarijaguar","Atari Jaguar"},
        {"atarilynx","Atari Lynx"}, {"amiga","Commodore Amiga"}, {"amigacd32","Commodore Amiga CD32"},
        {"xbox","Microsoft Xbox"}, {"xbox360","Microsoft Xbox 360"}, {"neogeo","SNK Neo Geo"},
        {"pcengine","NEC PC Engine"}, {"tg16","NEC TurboGrafx-16"}, {"3do","3DO"}, {"arcade","Arcade"}
    };
    for (const auto& [folder, label] : standard) if (wanted == normalized(folder) || wanted == normalized(label)) {
        if (actual.empty() || std::any_of(actual.begin(), actual.end(), [&](const auto& row) { return row.first == folder; })) return folder;
    }
    std::set<std::string> matches;
    for (const auto& [folder, title] : actual) if (normalized(title) == wanted) matches.insert(folder);
    return matches.size() == 1 ? *matches.begin() : std::string{};
}
inline std::filesystem::path destination(const std::filesystem::path& root, std::string_view platform, const Systems& actual = {}) {
    const auto folder = system(platform, actual);
    return root.empty() || !root.is_absolute() || folder.empty() ? std::filesystem::path{} : root / folder;
}
}
