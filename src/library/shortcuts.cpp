#include "shortcuts.hpp"
#include "visibility.hpp"
#include "emulation_platforms.hpp"
#include "../platform/text_encoding.hpp"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <fstream>
#include <unordered_map>

namespace {
constexpr uint64_t steam_id_offset = 76561197960265728ULL;

std::wstring wide(std::string_view text) { return text_encoding::wideLegacy(text); }

bool platformMarker(std::wstring_view source, std::wstring_view marker) {
    if (marker.empty()) return false;
    if (marker != L"wii") return source.find(marker) != std::wstring_view::npos;
    // Wii is a complete word/folder, never the prefix of Wii U or WiiU.
    for (size_t at = source.find(marker); at != std::wstring_view::npos; at = source.find(marker, at + marker.size())) {
        const auto end = at + marker.size();
        if ((at && std::iswalnum(source[at - 1])) || (end < source.size() && std::iswalnum(source[end]))) continue;
        const auto next = source.find_first_not_of(L" \t", end);
        if (next != std::wstring_view::npos && source[next] == L'u' &&
            (next + 1 == source.size() || !std::iswalnum(source[next + 1]))) continue;
        return true;
    }
    return false;
}

class BinaryVdf {
public:
    explicit BinaryVdf(std::vector<uint8_t> bytes) : bytes_(std::move(bytes)) {}

    std::vector<ShortcutGame> shortcuts(size_t* parse_error) {
        uint8_t type = 0;
        std::string root;
        if (!byte(type) || type != 0 || !string(root) || root != "shortcuts") {
            if (parse_error) *parse_error = position_;
            return {};
        }
        while (position_ < bytes_.size() && byte(type) && type != 8) {
            std::string index;
            if (!string(index)) { if (parse_error) *parse_error = position_; return {}; }
            if (type != 0) { if (!skip(type, 1)) { if (parse_error) *parse_error = position_; return {}; } continue; }
            ShortcutGame shortcut;
            std::wstring exe, options, tags;
            if (!entry(shortcut, exe, options, tags, 1)) { if (parse_error) *parse_error = position_; return {}; }
            shortcut.game.platform = shortcutPlatform(exe, options, shortcut.game.name, tags);
            shortcut.game.rom = shortcutRom(exe, options);
            if (exe.size() >= 2 && exe.front() == L'"' && exe.back() == L'"') exe = exe.substr(1, exe.size() - 2);
            const std::filesystem::path executable(exe);
            if (executable.is_absolute() && CompareStringOrdinal(executable.extension().c_str(), -1, L".exe", -1, TRUE) == CSTR_EQUAL) shortcut.game.executable = executable;
            if (shortcut.game.app_id && !shortcut.game.name.empty()) {
                shortcut.game.kind = GameKind::nonsteam;
                shortcut.game.shortcut_id = (uint64_t(shortcut.game.app_id) << 32) | 0x02000000ULL;
                result_.push_back(std::move(shortcut));
            }
        }
        return result_;
    }

private:
    bool byte(uint8_t& value) {
        if (position_ >= bytes_.size()) return false;
        value = bytes_[position_++];
        return true;
    }
    bool string(std::string& value) {
        const size_t start = position_;
        const auto finish = std::find(bytes_.begin() + start, bytes_.end(), uint8_t{0});
        if (finish == bytes_.end() || static_cast<size_t>(finish - bytes_.begin()) - start > 64 * 1024) return false;
        value.assign(reinterpret_cast<const char*>(bytes_.data() + start),
                     static_cast<size_t>(finish - bytes_.begin()) - start);
        position_ = static_cast<size_t>(finish - bytes_.begin()) + 1;
        return true;
    }
    bool integer(uint32_t& value) {
        if (bytes_.size() - position_ < 4) return false;
        value = 0;
        for (unsigned i = 0; i < 4; ++i) value |= uint32_t(bytes_[position_++]) << (i * 8);
        return true;
    }
    bool skip(uint8_t type, int depth) {
        if (depth > 16) return false;
        if (type == 0) {
            uint8_t child = 0;
            while (byte(child) && child != 8) {
                std::string key;
                if (!string(key) || !skip(child, depth + 1)) return false;
            }
            return child == 8;
        }
        if (type == 1) { std::string value; return string(value); }
        const size_t size = type == 7 ? 8 : type == 5 ? 0 : 4;
        if (type == 5) { // UTF-16 wide string, null terminated.
            while (bytes_.size() - position_ >= 2) {
                const auto low = bytes_[position_], high = bytes_[position_ + 1];
                position_ += 2;
                if (!low && !high) return true;
            }
            return false;
        }
        if (type < 2 || type > 7 || bytes_.size() - position_ < size) return false;
        position_ += size;
        return true;
    }
    bool entry(ShortcutGame& shortcut, std::wstring& exe, std::wstring& options,
               std::wstring& tags, int depth) {
        uint8_t type = 0;
        while (byte(type) && type != 8) {
            std::string key;
            if (!string(key)) return false;
            for (auto& ch : key) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (type == 1) {
                std::string value;
                if (!string(value)) return false;
                if (key == "appname") shortcut.game.name = wide(value);
                else if (key == "exe") exe = wide(value);
                else if (key == "launchoptions") options = wide(value);
            } else if (type == 2) {
                uint32_t value = 0;
                if (!integer(value)) return false;
                if (key == "appid") shortcut.game.app_id = value;
                else if (key == "ishidden") shortcut.hidden = value != 0;
                else if (key == "lastplaytime") shortcut.game.last_played = value;
            } else if (type == 0 && key == "tags") {
                uint8_t child = 0;
                while (byte(child) && child != 8) {
                    std::string tag_index;
                    if (!string(tag_index)) return false;
                    if (child == 1) {
                        std::string tag;
                        if (!string(tag)) return false;
                        tags += L' ' + wide(tag);
                    } else if (!skip(child, depth + 1)) return false;
                }
                if (child != 8) return false;
            } else if (!skip(type, depth + 1)) return false;
        }
        return type == 8;
    }
    std::vector<uint8_t> bytes_;
    size_t position_ = 0;
    std::vector<ShortcutGame> result_;
};
} // namespace

std::wstring shortcutPlatform(const std::wstring& executable, const std::wstring& options,
                              const std::wstring& title, const std::wstring& tags) {
    std::wstring source = executable + L' ' + options + L' ' + tags + L' ' + title;
    for (auto& ch : source) ch = ch == L'/' ? L'\\' : static_cast<wchar_t>(std::towlower(ch));
    for (const auto& platform : emulation_platforms)
        for (const auto marker : platform.markers)
            if (platformMarker(source, marker)) return std::wstring(platform.name);
    return std::wstring(emulation_platforms.back().name);
}

std::filesystem::path shortcutRom(const std::wstring& executable, const std::wstring& options) {
    // Emulator and launcher files never are the game itself.
    constexpr std::wstring_view tools[] = {L".exe", L".dll", L".ps1", L".bat", L".cmd", L".lnk", L".vbs",
                                           L".ini", L".cfg", L".json", L".xml", L".yml", L".txt", L".log"};
    std::wstring found;
    const auto scan = [&](const auto& self, std::wstring_view command, int depth) -> void {
        for (size_t at = 0; at < command.size();) {
            if (std::iswspace(command[at])) { ++at; continue; }
            std::wstring token;
            bool quoted = false;
            if (command[at] == L'"' || command[at] == L'\'') { // "K:\roms\ps2\A B.chd" or PowerShell 'K:\...'
                const auto quote = command[at];
                const auto end = command.find(quote, at + 1);
                token = command.substr(at + 1, end == std::wstring_view::npos ? std::wstring_view::npos : end - at - 1);
                at = end == std::wstring_view::npos ? command.size() : end + 1;
                quoted = true;
            } else {
                size_t end = at;
                while (end < command.size() && !std::iswspace(command[end])) ++end;
                token = command.substr(at, end - at);
                at = end;
            }
            while (!token.empty() && (token.front() == L'{' || token.front() == L'&')) token.erase(0, 1);
            while (!token.empty() && (token.back() == L'}' || token.back() == L';')) token.pop_back();
            const bool drive = token.size() > 3 && std::iswalpha(token[0]) && token[1] == L':' && (token[2] == L'\\' || token[2] == L'/');
            const bool unc = token.size() > 3 && token[0] == L'\\' && token[1] == L'\\';
            if (!drive && !unc) {
                // PowerShell wrappers quote a whole script line: -Command "& {launcher.ps1 'rom'}".
                if (quoted && depth < 2 && token.find_first_of(L" \t") != std::wstring::npos) self(self, token, depth + 1);
                continue;
            }
            const auto extension = std::filesystem::path(token).extension().wstring();
            if (extension.size() < 2) continue;
            if (std::any_of(std::begin(tools), std::end(tools), [&](std::wstring_view tool) {
                    return CompareStringOrdinal(extension.c_str(), -1, tool.data(), static_cast<int>(tool.size()), TRUE) == CSTR_EQUAL; })) continue;
            found = std::move(token); // The last file argument: emulator options come first.
        }
    };
    scan(scan, executable + L' ' + options, 0);
    if (found.empty()) return {};
    for (auto& ch : found) if (ch == L'/') ch = L'\\';
    return std::filesystem::path(found);
}

std::wstring collectionPlatform(std::wstring_view collection_name) {
    std::wstring name(collection_name);
    for (auto& ch : name) ch = static_cast<wchar_t>(std::towlower(ch));
    // "System - Emulator" and "System (Emulator)": the system part names the platform.
    for (const auto* separator : {L" - ", L" ("})
        if (const auto cut = name.find(separator); cut != std::wstring::npos) name.resize(cut);
    const auto first = name.find_first_not_of(L" \t"), last = name.find_last_not_of(L" \t");
    if (first == std::wstring::npos) return {};
    name = name.substr(first, last - first + 1);
    for (const auto& platform : emulation_platforms)
        for (const auto known : platform.collections)
            if (!known.empty() && name == known) return std::wstring(platform.name);
    // Other wording ("Switch games"): the executable markers, never the PC fallback.
    for (const auto& platform : emulation_platforms)
        for (const auto marker : platform.markers)
            if (platformMarker(name, marker)) return std::wstring(platform.name);
    return {};
}

void applyCollectionPlatforms(std::vector<ShortcutGame>& shortcuts, const std::vector<SteamCollection>& collections) {
    std::unordered_map<uint32_t, std::wstring> assigned;
    for (const auto& collection : collections) {
        if (!srmCollection(collection)) continue;
        const auto platform = collectionPlatform(collection.name);
        if (platform.empty()) continue;
        for (const auto id : collection.app_ids) {
            if (!steamShortcutId(id)) continue;
            const auto [found, inserted] = assigned.emplace(id, platform);
            // A shared Dolphin collection must not hide a later, specific console collection.
            if (!inserted && found->second == L"Wii / GameCube") found->second = platform;
        }
    }
    for (auto& shortcut : shortcuts) {
        const auto found = assigned.find(shortcut.game.app_id);
        if (found == assigned.end()) continue;
        if (found->second == L"Wii / GameCube" &&
            (shortcut.game.platform == L"Wii" || shortcut.game.platform == L"GameCube")) continue;
        shortcut.game.platform = found->second;
    }
}

std::vector<ShortcutGame> scanSteamShortcuts(const std::filesystem::path& steam_root, size_t* parse_error) {
    const uint64_t id = activeSteamId(steam_root);
    if (id <= steam_id_offset) return {};
    const auto file = steam_root / "userdata" / std::to_wstring(id - steam_id_offset) /
                      "config" / "shortcuts.vdf";
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() <= 0 || input.tellg() > 16 * 1024 * 1024) return {};
    const size_t size = static_cast<size_t>(input.tellg());
    input.seekg(0);
    std::vector<uint8_t> bytes(size);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))) return {};
    return BinaryVdf(std::move(bytes)).shortcuts(parse_error);
}
