#include "steam_owned.hpp"
#include "../platform/file_io.hpp"
#include "visibility.hpp"
#include "../model/library_time.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/text_encoding.hpp"
#include "../platform/system.hpp"
#ifdef _WIN32
#include <Windows.h>
#else
#include <cerrno>
#include <csignal>
#include <dlfcn.h>
#endif
#include <algorithm>
#include <charconv>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace {
constexpr uint64_t steam_id_offset = 76561197960265728ULL;
std::vector<uint8_t> bytes(const std::filesystem::path& path, size_t limit) { return file_io::readBytes(path, limit); }
#if defined(_WIN32)
template<class Return, class... Args> Return invoke(void* object, size_t slot, Args... args) {
    using Method = Return(__thiscall*)(void*, Args...);
    return reinterpret_cast<Method>((*static_cast<void***>(object))[slot])(object, args...);
}
DWORD activeValue(const wchar_t* name) {
    DWORD value = 0, size = sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\ActiveProcess", name, RRF_RT_REG_DWORD, nullptr, &value, &size);
    return value;
}
using Module = HMODULE;
void closeModule(Module module) { FreeLibrary(module); }
bool processRunning(uint32_t pid) {
    // The registry can retain the PID after Steam exits. Never try connecting then.
    const auto process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!process) return false;
    const bool running = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return running;
}
#else
// The Itanium C++ ABI passes `this` as the first argument; there is no __thiscall.
template<class Return, class... Args> Return invoke(void* object, size_t slot, Args... args) {
    using Method = Return(*)(void*, Args...);
    return reinterpret_cast<Method>((*static_cast<void***>(object))[slot])(object, args...);
}
// ~/.steam/registry.vdf mirrors the Windows registry: HKCU/Software/Valve/Steam/ActiveProcess.
uint32_t activeValue(const wchar_t* name) {
    const auto home = system_info::environment(L"HOME");
    if (!home) return 0;
    const auto data = bytes(std::filesystem::path(*home) / L".steam" / L"registry.vdf", 1024 * 1024);
    const std::string_view text(reinterpret_cast<const char*>(data.data()), data.size());
    const std::string wanted = text_encoding::utf8(name);
    // Quoted tokens and braces; the path of keys leading to each value.
    std::vector<std::string> path;
    std::string pending;
    bool have_key = false;
    for (size_t at = 0; at < text.size(); ++at) {
        const char ch = text[at];
        if (ch == '{') { path.push_back(pending); have_key = false; }
        else if (ch == '}') { if (!path.empty()) path.pop_back(); have_key = false; }
        else if (ch == '"') {
            const auto end = text.find('"', at + 1);
            if (end == std::string_view::npos) return 0;
            std::string token(text.substr(at + 1, end - at - 1));
            at = end;
            if (!have_key) { pending = std::move(token); have_key = true; continue; }
            have_key = false;
            const bool active = path.size() >= 5 && path[path.size() - 1] == "ActiveProcess" && path[path.size() - 2] == "Steam";
            if (active && pending == wanted) {
                uint32_t value = 0;
                const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
                return parsed.ec == std::errc{} ? value : 0;
            }
        }
    }
    return 0;
}
using Module = void*;
void closeModule(Module module) { dlclose(module); }
bool processRunning(uint32_t pid) { return pid && (::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM); }
#endif
bool sameAccount(uint64_t account, uint32_t pid) {
    return pid && activeValue(L"pid") == pid && account > steam_id_offset &&
           account - steam_id_offset <= UINT32_MAX && activeValue(L"ActiveUser") == account - steam_id_offset;
}
struct Connection {
    Module module = nullptr;
    void* client = nullptr;
    int pipe = 0, user = 0;
    ~Connection() {
        if (client && pipe && user) invoke<void>(client, 4, pipe, user);
        if (client && pipe) invoke<bool>(client, 1, pipe);
        if (module) closeModule(module);
    }
};
bool gameType(std::string_view type) {
    return type == "Game" || type == "game" || type == "Demo" || type == "demo";
}
}

std::optional<std::vector<uint32_t>> steamAppInfoIds(std::span<const uint8_t> data) {
    // Valve appcache v27/v28/v29. Each record's size counts bytes after its size
    // field, including metadata and the KV payload. v29 adds a string-table offset.
    if (data.size() < 8) return {};
    const auto u32 = [&](size_t at) { uint32_t n; memcpy(&n, data.data() + at, 4); return n; };
    const auto magic = u32(0);
    if (magic != 0x07564427 && magic != 0x07564428 && magic != 0x07564429) return {};
    size_t cursor = 8, end = data.size();
    if (magic == 0x07564429) {
        if (data.size() < 16) return {};
        uint64_t table; memcpy(&table, data.data() + 8, 8);
        if (table < 20 || table > data.size()) return {};
        end = static_cast<size_t>(table); cursor = 16;
    }
    std::vector<uint32_t> ids;
    while (cursor <= end && end - cursor >= 4) {
        const auto id = u32(cursor); cursor += 4;
        if (!id) {
            if (cursor != end) return {};
            std::sort(ids.begin(), ids.end()); ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
            return ids;
        }
        if (end - cursor < 4 || ids.size() >= 200000) return {};
        const auto size = u32(cursor); cursor += 4;
        const size_t header = magic == 0x07564427 ? 40 : 60;
        if (size <= header || size > end - cursor) return {};
        if (id < 0x80000000u) ids.push_back(id);
        cursor += size;
    }
    return {}; // Torn/unknown cache must not erase a previous ownership snapshot.
}

std::optional<std::vector<Game>> clientSteamOwnedGames(const std::filesystem::path& root, uint64_t account) {
#if !(defined(_WIN32) && defined(_M_X64)) && !(defined(__linux__) && defined(__x86_64__))
    (void)root; (void)account; return {};
#else
    const auto pid = activeValue(L"pid");
    if (!root.is_absolute() || !sameAccount(account, pid) || activeSteamId(root) != account) return {};
    if (!processRunning(pid)) return {};
    const auto candidates = steamAppInfoIds(bytes(root / L"appcache" / L"appinfo.vdf", 128 * 1024 * 1024));
    if (!candidates) return {};
    Connection connection;
#ifdef _WIN32
    connection.module = LoadLibraryExW((root / L"steamclient64.dll").c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!connection.module) return {};
    using Factory = void* (__cdecl*)(const char*, int*);
    const auto factory = reinterpret_cast<Factory>(GetProcAddress(connection.module, "CreateInterface"));
#else
    connection.module = dlopen((root / L"linux64" / L"steamclient.so").c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!connection.module) return {};
    using Factory = void* (*)(const char*, int*);
    const auto factory = reinterpret_cast<Factory>(dlsym(connection.module, "CreateInterface"));
#endif
    if (!factory || !(connection.client = factory("SteamClient021", nullptr))) return {};
    connection.pipe = invoke<int>(connection.client, 0);
    if (!connection.pipe) return {};
    connection.user = invoke<int>(connection.client, 2, connection.pipe);
    if (!connection.user) return {};
    auto* apps = invoke<void*>(connection.client, 15, connection.user, connection.pipe, "STEAMAPPS_INTERFACE_VERSION008");
    auto* metadata = invoke<void*>(connection.client, 15, connection.user, connection.pipe, "STEAMAPPS_INTERFACE_VERSION001");
    if (!apps || !metadata) return {};
    // Versioned public ABIs: ISteamApps008::BIsSubscribedApp (6),
    // GetEarliestPurchaseUnixTime (8), ISteamApps001::GetAppData (0).
    // No unversioned/private vtable slots.
    std::vector<Game> result;
    for (const auto id : *candidates) {
        if (id == 480 || !invoke<bool>(apps, 6, id)) continue;
        char type[64]{}, name[2048]{};
        const int type_size = invoke<int>(metadata, 0, id, "common/type", type, static_cast<int>(sizeof(type)));
        // Subscriptions also include depots/configuration stubs without common/type.
        if (type_size <= 0 || type_size >= static_cast<int>(sizeof(type))) continue;
        if (!gameType(type)) continue;
        const int name_size = invoke<int>(metadata, 0, id, "common/name", name, static_cast<int>(sizeof(name)));
        if (name_size <= 0 || name_size >= static_cast<int>(sizeof(name))) return {};
        Game game; game.app_id = id; game.name = text_encoding::wide(name); game.installed = false;
        game.added_at = library_time::seconds(invoke<uint32_t>(apps, 8, id));
        if (game.name.empty()) return {};
        if (!steamTool(game)) result.push_back(std::move(game));
    }
    if (!sameAccount(account, pid) || activeSteamId(root) != account) return {};
    return result;
#endif
}

void mergeSteamOwnedGames(SteamLibrary& library, const std::vector<Game>& owned) {
    std::set<uint32_t> present;
    std::map<uint32_t, uint64_t> mine;
    for (const auto& game : owned) mine.emplace(game.app_id, game.added_at);
    for (auto* list : {&library.visible, &library.private_games})
        for (auto& game : *list) {
            present.insert(game.app_id);
            // Installed by another account, but this one owns it too: no account switch to play it.
            if (const auto found = mine.find(game.app_id); found != mine.end()) {
                game.steam_owner = 0;
                game.added_at = found->second;
            }
        }
    for (auto game : owned) {
        if (!game.app_id || steamTool(game) || !present.insert(game.app_id).second) continue;
        game.installed = false; game.installation = {};
        game.visibility = library.excluded.contains(game.app_id) ? GameVisibility::private_game : GameVisibility::public_game;
        (game.visibility == GameVisibility::public_game ? library.visible : library.private_games).push_back(std::move(game));
    }
    std::sort(library.visible.begin(), library.visible.end(), steamLibraryOrder);
    std::sort(library.private_games.begin(), library.private_games.end(), steamLibraryOrder);
}

const std::vector<Game>& SteamOwnedGames::read(uint64_t account, bool force) {
    const auto now = std::chrono::steady_clock::now();
    cached(account);
    if (account <= steam_id_offset || (!force && now < next_)) return games_;
    next_ = now + std::chrono::seconds(30);
    auto fresh = fetch_(root_, account);
    answered_ = fresh.has_value();
    if (fresh) {
        std::map<uint32_t, uint64_t> dates;
        for (const auto& game : games_) dates.emplace(game.app_id, game.added_at);
        for (auto& game : *fresh) if (!game.added_at)
            if (const auto found = dates.find(game.app_id); found != dates.end()) game.added_at = found->second;
        games_ = std::move(*fresh);
        std::string content = "steam-owned-v2 " + std::to_string(account) + '\n';
        for (const auto& game : games_) {
            const auto name = text_encoding::utf8(game.name);
            if (name.find_first_of("\r\n\t") == std::string::npos)
                content += std::to_string(game.app_id) + '\t' + std::to_string(game.added_at) + '\t' + name + '\n';
        }
        atomic_file::write(cache_ / (std::to_wstring(account) + L".txt"), content);
    }
    return games_;
}

const std::vector<Game>& SteamOwnedGames::cached(uint64_t account) {
    if (account_ != account) {
        account_ = account; games_.clear(); next_ = {};
        if (account > steam_id_offset) {
            const auto content = bytes(cache_ / (std::to_wstring(account) + L".txt"), 8 * 1024 * 1024);
            std::istringstream input(std::string(content.begin(), content.end()));
            std::string line;
            if (std::getline(input, line) && (line == "steam-owned-v1 " + std::to_string(account) || line == "steam-owned-v2 " + std::to_string(account))) {
                const bool dates = line.starts_with("steam-owned-v2 ");
                std::set<uint32_t> seen;
                while (std::getline(input, line)) {
                    const auto tab = line.find('\t');
                    if (tab == std::string::npos) continue;
                    uint32_t id = 0;
                    const auto parsed = std::from_chars(line.data(), line.data() + tab, id);
                    if (parsed.ec != std::errc{} || parsed.ptr != line.data() + tab || !id || id == 480 || id >= 0x80000000u) continue;
                    Game game; game.app_id = id; game.installed = false;
                    const auto name_at = dates ? line.find('\t', tab + 1) : tab;
                    if (name_at == std::string::npos) continue;
                    if (dates) game.added_at = library_time::seconds(std::string_view(line).substr(tab + 1, name_at - tab - 1));
                    game.name = text_encoding::wide(line.substr(name_at + 1));
                    if (!game.name.empty() && !steamTool(game) && seen.insert(id).second) games_.push_back(std::move(game));
                }
            }
        }
    }
    return games_;
}
