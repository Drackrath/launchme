#include "steam.hpp"
#include "../platform/unicode_text.hpp"
#include "visibility.hpp"
#include "../platform/text_encoding.hpp"
#include "../platform/system.hpp"

#ifdef _WIN32
#include <Windows.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <charconv>

namespace {
std::wstring wide(std::string_view text) { return text_encoding::wideLegacy(text); }

// VDF escapes backslashes and quotes. Reading one key/value line avoids accidentally
// picking up fields from nested depot and user-config objects in a manifest.
std::optional<std::pair<std::string, std::string>> pairOnLine(std::string_view line) {
    size_t position = 0;
    auto quoted = [&]() -> std::optional<std::string> {
        while (position < line.size() && (line[position] == ' ' || line[position] == '\t')) ++position;
        if (position == line.size() || line[position++] != '"') return std::nullopt;
        std::string value;
        while (position < line.size()) {
            const char ch = line[position++];
            if (ch == '"') return value;
            if (ch == '\\' && position < line.size() && (line[position] == '\\' || line[position] == '"')) {
                value += line[position++];
            } else {
                value += ch;
            }
        }
        return std::nullopt;
    };
    auto key = quoted();
    if (!key) return std::nullopt;
    auto value = quoted();
    if (!value) return std::nullopt;
    return std::pair{*key, *value};
}

uint64_t number(const std::string& text) {
    uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size() ? value : 0;
}

std::filesystem::path pathFromUtf8(const std::string& text) {
    return std::filesystem::path(wide(text));
}

// One Steam library folder may be listed twice with other case or separators (registry
// "c:/program files (x86)/steam", libraryfolders.vdf "C:\\Program Files (x86)\\Steam").
bool sameFolder(const std::filesystem::path& a, const std::filesystem::path& b) {
    const auto text = [](const std::filesystem::path& path) {
        auto result = path.lexically_normal().wstring();
        while (result.size() > 3 && (result.back() == L'\\' || result.back() == L'/')) result.pop_back();
        return result;
    };
    const auto left = text(a), right = text(b);
    return unicode_text::equalIgnoreCase(left, right);
}

std::vector<std::filesystem::path> libraries(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> result{root};
    std::ifstream input(root / "steamapps" / "libraryfolders.vdf", std::ios::binary);
    std::string line;
    while (std::getline(input, line)) {
        const auto field = pairOnLine(line);
        if (field && field->first == "path") result.push_back(pathFromUtf8(field->second));
    }
    return result;
}

std::optional<Game> manifest(const std::filesystem::path& path, bool& complete) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() <= 0 || input.tellg() > 2 * 1024 * 1024) { complete = false; return {}; }
    input.seekg(0);
    Game game;
    game.manifest_path = path;
    uint64_t state = 0;
    uint64_t size = 0;
    uint64_t download_done = 0, download_total = 0, stage_done = 0, stage_total = 0;
    int depth = 0; bool root = false, has_state = false;
    std::string line;
    while (std::getline(input, line)) {
        const auto first = line.find_first_not_of(" \t\r");
        if (first != std::string::npos && line[first] == '{') { ++depth; root = true; continue; }
        if (first != std::string::npos && line[first] == '}') { if (--depth < 0) { complete = false; return {}; } continue; }
        if (depth != 1) continue;
        const auto field = pairOnLine(line);
        if (!field) continue;
        const auto& [key, value] = *field;
        if (key == "appid") { const auto id = number(value); game.app_id = id <= UINT32_MAX ? static_cast<uint32_t>(id) : 0; }
        else if (key == "name") game.name = wide(value);
        else if (key == "LastPlayed") game.last_played = number(value);
        else if (key == "LastUpdated") game.last_updated = number(value);
        else if (key == "LastOwner") game.steam_owner = number(value);
        else if (key == "StateFlags") { state = number(value); has_state = true; }
        else if (key == "SizeOnDisk") size = number(value);
        else if (key == "BytesDownloaded") download_done = number(value);
        else if (key == "BytesToDownload") download_total = number(value);
        else if (key == "BytesStaged") stage_done = number(value);
        else if (key == "BytesToStage") stage_total = number(value);
        else if (key == "installdir") {
            const auto directory = pathFromUtf8(value);
            if (!directory.empty() && !directory.has_root_path() &&
                std::none_of(directory.begin(), directory.end(), [](const auto& part) { return part == L".."; }))
                game.install_directory = (path.parent_path() / L"common" / directory).lexically_normal();
        }
    }
    if (depth != 0 || !root || !has_state || !game.app_id || game.name.empty()) { complete = false; return {}; }
    // Steam AppState flags and byte counters describe different phases. A
    // finished download is not evidence that staging/validation has completed.
    if (!(state & 2048)) {
        if (state & 512) game.installation.phase = InstallPhase::paused;
        else if (state & 131072) game.installation.phase = InstallPhase::verifying;
        else if (state & 1048576) game.installation.phase = InstallPhase::downloading;
        else if (state & (2097152 | 4194304)) game.installation.phase = InstallPhase::staging;
        else if (state & 524288) game.installation.phase = InstallPhase::allocating;
        else if (state & (256 | 1024 | 1048576 | 262144)) game.installation.phase = InstallPhase::downloading;
        else if (state & 8) game.installation.phase = InstallPhase::queued;
    }
    if (game.installation.phase == InstallPhase::staging) { game.installation.done = stage_done; game.installation.total = stage_total; }
    else if (game.installation.phase == InstallPhase::downloading || game.installation.phase == InstallPhase::paused || game.installation.phase == InstallPhase::queued) {
        game.installation.done = download_done; game.installation.total = download_total;
    }
    // Steam keeps FullyInstalled (4) while it updates or validates an installed game; a new
    // installation has it only once it is complete (content_log: "Fully Installed,Update Running").
    game.installation.update = game.installation.active() && (state & 4);
    game.installed = (state & 4) && size && !(state & 2048) && !game.installation.active();
    if (game.app_id == 480 || (!game.installed && !game.installation.active()) || steamTool(game)) return std::nullopt;
    return game;
}
} // namespace

bool steamTool(const Game& game) {
    // 1716750 Ubisoft Connect PC Client, 4099820 Ubisoft Connect Services, 4874400 FramePacer.
    for (const uint32_t id : {1716750u, 4099820u, 4874400u}) if (game.app_id == id) return true;
    const auto is = [&](const wchar_t* name) { return unicode_text::equalIgnoreCase(game.name, name); };
    const auto starts = [&](std::wstring_view prefix) { return unicode_text::startsWithIgnoreCase(game.name, prefix); };
    return is(L"Steamworks Common Redistributables") || is(L"framepacer") || starts(L"Steam Linux Runtime") ||
           starts(L"Proton ") || starts(L"Ubisoft Connect");
}

std::filesystem::path findSteamRoot() {
    if (auto value = system_info::environment(L"STEAM_DIR", 4095); value && !value->empty()) return *value;
#ifndef _WIN32
    // Native (~/.steam/steam links to the real one), then the Flatpak and Snap packages.
    const auto home = std::filesystem::path(system_info::environment(L"HOME").value_or(L""));
    const auto data = system_info::environment(L"XDG_DATA_HOME");
    std::vector<std::filesystem::path> candidates{home / L".steam" / L"steam", home / L".steam" / L"root",
        data ? std::filesystem::path(*data) / L"Steam" : home / L".local" / L"share" / L"Steam",
        home / L".var" / L"app" / L"com.valvesoftware.Steam" / L".local" / L"share" / L"Steam",
        home / L".var" / L"app" / L"com.valvesoftware.Steam" / L"data" / L"Steam",
        home / L"snap" / L"steam" / L"common" / L".local" / L"share" / L"Steam"};
    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (!home.empty() && std::filesystem::is_directory(candidate / L"steamapps", ec)) return std::filesystem::weakly_canonical(candidate, ec);
    }
    return home.empty() ? std::filesystem::path{} : home / L".local" / L"share" / L"Steam";
#else
    wchar_t buffer[4096]{};
    DWORD bytes = sizeof(buffer);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath",
                     RRF_RT_REG_SZ, nullptr, buffer, &bytes) == ERROR_SUCCESS) return buffer;
    bytes = sizeof(buffer);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath",
                     RRF_RT_REG_SZ, nullptr, buffer, &bytes) == ERROR_SUCCESS) return buffer;
    return L"C:\\Program Files (x86)\\Steam";
#endif
}

std::vector<std::filesystem::path> steamLibraryFolders(const std::filesystem::path& steam_root) {
    std::vector<std::filesystem::path> seen;
    for (const auto& library : libraries(steam_root))
        if (std::none_of(seen.begin(), seen.end(), [&](const auto& other) { return sameFolder(library, other); })) seen.push_back(library);
    return seen;
}

SteamLibrary scanSteamLibrary(const std::filesystem::path& steam_root) {
    // Without a known active account there is no trustworthy hidden/private list.
    // Fail closed rather than exposing another user's games in a public shelf.
    if (!activeSteamId(steam_root)) return {};
    const auto excluded = excludedSteamApps(steam_root);
    bool complete = true;
    SteamLibrary result;
    result.excluded.insert(excluded.begin(), excluded.end());
    std::map<uint32_t, Game> unique;
    for (const auto& library : steamLibraryFolders(steam_root)) {
        std::error_code ec;
        std::filesystem::directory_iterator iter(library / "steamapps", ec);
        if (ec) { complete = false; result.unavailable_libraries.push_back(library / "steamapps"); continue; }
        for (std::filesystem::directory_iterator end; !ec && iter != end; iter.increment(ec)) {
            const auto& entry = *iter;
            if (!entry.is_regular_file(ec)) continue;
            const auto name = entry.path().filename().wstring();
            if (!name.starts_with(L"appmanifest_") || entry.path().extension() != L".acf") continue;
            bool readable = true;
            auto game = manifest(entry.path(), readable);
            if (!readable) {
                complete = false;
                // path::string() throws for names outside the ANSI code page; the poller has no handler.
                const auto stem = text_encoding::utf8(entry.path().stem().wstring());
                const auto id = stem.size() > 12 ? number(stem.substr(12)) : 0; // After "appmanifest_".
                if (id && id <= UINT32_MAX) result.unavailable_apps.insert(static_cast<uint32_t>(id));
            }
            if (game && (!unique.contains(game->app_id) ||
                          game->last_played > unique[game->app_id].last_played)) unique[game->app_id] = *game;
        }
        if (ec) { complete = false; result.unavailable_libraries.push_back(library / "steamapps"); }
    }
    std::vector<Game> games;
    for (auto& [id, game] : unique) games.push_back(std::move(game));
    std::sort(games.begin(), games.end(), steamLibraryOrder);
    result.complete = complete;
    for (auto& game : games) {
        if (excluded.contains(game.app_id)) {
            game.visibility = GameVisibility::private_game;
            result.private_games.push_back(std::move(game));
        } else result.visible.push_back(std::move(game));
    }
    return result;
}

SteamLibrary retainUnavailableSteamGames(SteamLibrary current, const SteamLibrary& previous) {
    std::set<uint32_t> present;
    for (const auto* list : {&current.visible, &current.private_games}) for (const auto& game : *list) present.insert(game.app_id);
    for (const auto* list : {&previous.visible, &previous.private_games}) for (const auto& old : *list) {
        if (present.contains(old.app_id)) continue;
        const bool unreadable = current.unavailable_apps.contains(old.app_id) ||
            std::any_of(current.unavailable_libraries.begin(), current.unavailable_libraries.end(), [&](const auto& path) {
                return !old.manifest_path.empty() && old.manifest_path.parent_path() == path;
            });
        if (!unreadable) continue; // A readable deletion really does mean uninstalled/cancelled.
        auto game = old;
        // A library folder that is gone (an unplugged drive, a removed network share) keeps its
        // games listed, but not as playable: Play would only make Steam report a missing folder.
        std::error_code ec;
        if (!old.manifest_path.empty() && !std::filesystem::exists(old.manifest_path.parent_path(), ec) && !ec) {
            game.installed = false; game.installation = {};
        }
        game.visibility = current.excluded.contains(game.app_id) ? GameVisibility::private_game : GameVisibility::public_game;
        (game.visibility == GameVisibility::public_game ? current.visible : current.private_games).push_back(std::move(game));
    }
    return current;
}

std::vector<Game> scanInstalledGames(const std::filesystem::path& steam_root, size_t limit) {
    auto games = scanSteamLibrary(steam_root).visible;
    std::erase_if(games, [](const Game& game) { return !game.installed; });
    if (limit && games.size() > limit) games.resize(limit);
    return games;
}

std::wstring steamActionUrl(const Game& game) {
    if (game.source != GameSource::steam || !game.app_id ||
        (game.kind != GameKind::steam && game.kind != GameKind::nonsteam)) return {};
    if (game.kind == GameKind::nonsteam && game.shortcut_id)
        return L"steam://rungameid/" + std::to_wstring(game.shortcut_id);
    if (game.installation.active()) return L"steam://nav/downloads";
    return std::wstring(game.installed ? L"steam://rungameid/" : L"steam://install/") +
           std::to_wstring(game.app_id);
}

std::filesystem::path steamExecutable(const std::filesystem::path& steam_root) {
    if (steam_root.empty()) return {};
    std::error_code ec;
#ifdef _WIN32
    const auto exe = steam_root / L"steam.exe";
#else
    const auto exe = steam_root / L"steam.sh"; // The native client's launcher script.
#endif
    return std::filesystem::is_regular_file(exe, ec) ? exe.lexically_normal() : std::filesystem::path{};
}

GameAction silentSteamAction(GameAction action, const std::filesystem::path& steam_exe) {
    constexpr std::wstring_view prefix = L"steam://rungameid/";
    if (action.kind != ActionKind::play || steam_exe.empty() || !action.arguments.empty() ||
        !action.target.starts_with(prefix) || action.target.size() == prefix.size() ||
        action.target.find_first_not_of(L"0123456789", prefix.size()) != std::wstring::npos) return action;
    // The same form as Steam's registered steam:// handler ("steam.exe" -- "%1"), with -silent first.
    action.arguments = L"-silent -- \"" + action.target + L"\"";
    action.target = steam_exe.wstring();
    action.working_directory = steam_exe.parent_path();
    action.keep_focus = true;
    return action;
}

GameAction steamOptionsAction(GameAction action, const std::filesystem::path& steam_exe, uint32_t app_id,
                              const std::wstring& options, bool silent) {
    if (action.kind != ActionKind::play || steam_exe.empty() || !app_id || !action.arguments.empty() ||
        action.target != L"steam://rungameid/" + std::to_wstring(app_id)) return action;
    std::wstring line;
    for (const wchar_t ch : options) line += ch < L' ' ? L' ' : ch; // One command line.
    const auto first = line.find_first_not_of(L' ');
    if (first == std::wstring::npos) return action;
    line = line.substr(first, line.find_last_not_of(L' ') - first + 1);
    action.arguments = (silent ? L"-silent -applaunch " : L"-applaunch ") + std::to_wstring(app_id) + L' ' + line;
    action.target = steam_exe.wstring();
    action.working_directory = steam_exe.parent_path();
    action.keep_focus = true;
    return action;
}
