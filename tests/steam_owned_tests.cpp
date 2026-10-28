#include "../src/library/steam_owned.hpp"
#include "../src/platform/atomic_file.hpp"
#include <Windows.h>
#include <iostream>

namespace {
void u32(std::vector<uint8_t>& bytes, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes.push_back(static_cast<uint8_t>(value >> (8 * i)));
}
std::vector<uint8_t> appinfo(uint32_t version) {
    std::vector<uint8_t> data; u32(data, 0x07564400 | version); u32(data, 1);
    if (version == 0x29) { u32(data, 0); u32(data, 0); }
    for (auto id : {3393110u, 10u, 3393110u}) {
        u32(data, id); const unsigned header = version == 0x27 ? 40 : 60;
        u32(data, header + 2); data.resize(data.size() + header, 0); data.push_back(8); data.push_back(8);
    }
    u32(data, 0);
    if (version == 0x29) {
        const auto offset = static_cast<uint32_t>(data.size());
        for (unsigned i = 0; i < 4; ++i) data[8 + i] = static_cast<uint8_t>(offset >> (8 * i));
        u32(data, 0); // Empty key string table, unused when enumerating candidates.
    }
    return data;
}
}
int main() {
    for (auto version : {0x27u, 0x28u, 0x29u}) {
        auto data = appinfo(version);
        auto ids = steamAppInfoIds(data);
        if (!ids || *ids != std::vector<uint32_t>{10, 3393110}) return 1;
        // A truncated record or overflow-sized record never becomes an empty library.
        data.resize(30);
        if (steamAppInfoIds(data)) return 2;
        data = appinfo(version);
        const size_t size_at = version == 0x29 ? 20 : 12;
        for (size_t i = size_at; i < size_at + 4; ++i) data[i] = 0xff;
        if (steamAppInfoIds(data)) return 3;
    }
    if (steamAppInfoIds(std::vector<uint8_t>(20, 0))) return 4;
    Game aion{3393110, L"AION 2"}; aion.installed = false;
    aion.added_at = 1700000000;
    Game hidden{20, L"Private"}; hidden.installed = false;
    Game installed{10, L"Installed"}; installed.last_played = 123;
    Game downloading{30, L"Downloading"}; downloading.installed = false;
    downloading.installation = {InstallPhase::downloading, 42, 100};
    SteamLibrary scan; scan.visible = {installed, downloading}; scan.excluded = {20};
    Game owned_install{10, L"Old name"}; owned_install.added_at = 1600000000;
    mergeSteamOwnedGames(scan, {aion, hidden, owned_install, Game{30, L"Old name"}, aion});
    if (scan.visible.size() != 3 || scan.private_games.size() != 1 || scan.visible[0].last_played != 123 ||
        scan.private_games[0].visibility != GameVisibility::private_game) return 5;
    for (const auto& game : scan.visible) {
        if (game.app_id == 3393110 && (game.installed || steamActionUrl(game) != L"steam://install/3393110")) return 6;
        if (game.app_id == 30 && game.installation.done != 42) return 7;
        if (game.app_id == 10 && game.added_at != 1600000000) return 15;
    }
    const auto cache = std::filesystem::temp_directory_path() / (L"launchme-owned-" + std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup{cache};
    constexpr uint64_t account = 76561197960265770ULL;
    bool online = true, empty = false; int calls = 0;
    const auto fetch = [&](const auto&, uint64_t id) -> std::optional<std::vector<Game>> {
        ++calls;
        if (!online) return {};
        return id == account && !empty ? std::vector<Game>{aion} : std::vector<Game>{};
    };
    SteamOwnedGames owned({}, cache, fetch);
    if (owned.read(account).size() != 1 || calls != 1) return 8;
    if (owned.read(account).size() != 1 || calls != 1) return 9;
    online = false;
    if (owned.read(account, true).size() != 1) return 10;
    SteamOwnedGames restarted({}, cache, fetch);
    if (restarted.read(account).size() != 1 || !restarted.read(account + 1).empty() || !restarted.read(0).empty()) return 11;
    if (restarted.read(account).size() != 1) return 12;
    if (restarted.cached(account)[0].added_at != 1700000000) return 16;
    // Acquisition facts are retained when an otherwise successful refresh cannot supply a date.
    online = true; aion.added_at = 0;
    if (restarted.read(account, true)[0].added_at != 1700000000) return 17;
    online = true; empty = true;
    if (!restarted.read(account, true).empty()) return 13;
    online = false;
    SteamOwnedGames after_removal({}, cache, fetch);
    if (!after_removal.read(account).empty()) return 14;
    // Existing v1 snapshots load without treating their title as a timestamp.
    atomic_file::write(cache / (std::to_wstring(account) + L".txt"), "steam-owned-v1 " + std::to_string(account) + "\n10\tOld cached title\n");
    SteamOwnedGames legacy({}, cache, fetch);
    if (legacy.cached(account).size() != 1 || legacy.cached(account)[0].name != L"Old cached title" || legacy.cached(account)[0].added_at) return 18;
    // Another account's same game carries that account's acquisition date.
    SteamOwnedGames other({}, cache, [&](const auto&, uint64_t id) -> std::optional<std::vector<Game>> {
        Game copy = aion; copy.added_at = id == account ? 1600000000 : 1700000000; return std::vector<Game>{copy};
    });
    if (other.read(account)[0].added_at != 1600000000 || other.read(account + 1)[0].added_at != 1700000000) return 19;
    std::cout << "Steam owned candidates, merge, offline cache and account isolation passed\n";
    return 0;
}
