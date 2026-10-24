#include "../src/platform/steam_library_sync.hpp"
#include "../src/library/steam.hpp"
#include "../src/library/visibility.hpp"
#include "../src/library/steam_sync_state.hpp"
#include "../src/library/custom_games.hpp"
#include <Windows.h>
#include <algorithm>
#include <iostream>

int wmain(int argc, wchar_t** argv) {
    const auto root = findSteamRoot();
    const auto account = activeSteamId(root);
    const auto result = steam_sync::exchange(root, account, {});
    std::cout << "Steam sync: " << (result.snapshot ? "connected" : result.error) << '\n';
    if (result.snapshot) std::cout << "Collections=" << result.snapshot->collections.size()
        << " hidden=" << result.snapshot->hidden.size() << " cloud_pending=" << result.cloud_pending << '\n';
    if (!result.snapshot) return 1;
    if (argc == 3 && std::wstring_view(argv[1]) == L"--migrate-collection") {
        // Explicit maintenance command: close Launchme first so its asynchronous writer cannot race this save.
        const auto directory = userConfigDirectory(), file = directory / L"library-state.ini", settings_file = directory / L"settings.ini";
        auto loaded = loadLibraryState(file);
        if (!loaded) return 2;
        auto& state = *loaded;
        std::vector<std::string> matches;
        for (const auto& [id, name] : state.collections.names) if (id.starts_with("local:") && name == argv[2]) matches.push_back(id);
        if (matches.size() != 1) { std::cout << "Expected one matching local collection; no changes\n"; return 3; }
        if (std::any_of(result.snapshot->collections.begin(), result.snapshot->collections.end(), [&](const auto& c) { return c.name == argv[2]; })) {
            std::cout << "A same-named Steam collection already exists; no changes\n"; return 4;
        }
        const auto random = custom_games::newId({});
        if (random.empty()) return 5;
        const auto id = "user-collections.uc-launchme-" + random;
        auto settings = loadAppSettings(settings_file);
        if (!steam_sync::moveCollection(state, settings, account, matches[0], id)) return 6;
        const auto suffix = L".before-steam-" + std::to_wstring(GetTickCount64());
        std::error_code ec;
        if (!std::filesystem::copy_file(file, file.wstring() + suffix, std::filesystem::copy_options::none, ec) || ec) return 7;
        if (!std::filesystem::copy_file(settings_file, settings_file.wstring() + suffix, std::filesystem::copy_options::none, ec) || ec) return 8;
        if (!saveLibraryState(state, file) || !saveAppSettings(settings, settings_file)) return 9;
        std::vector<uint32_t> expected;
        const auto scope = std::to_string(account) + ':' + id;
        for (const auto& [key, included] : state.collections.members.at(scope))
            if (included && key.starts_with("steam:")) expected.push_back(static_cast<uint32_t>(std::stoul(key.substr(6))));
        for (int pass = 0; pass < 20; ++pass) {
            auto changes = steam_sync::pending(state, account);
            std::erase_if(changes, [&](const auto& c) { return c.collection != id; });
            if (changes.empty()) break;
            changes.resize(std::min<size_t>(changes.size(), 16));
            auto synced = steam_sync::exchange(root, account, changes);
            if (!synced.snapshot || synced.applied.empty()) { std::cout << "Migration queued; Steam: " << synced.error << '\n'; return 10; }
            for (const auto& c : synced.applied) steam_sync::acknowledge(state, account, c);
            if (!saveLibraryState(state, file)) return 11;
        }
        const auto confirmed = steam_sync::exchange(root, account, {});
        if (!confirmed.snapshot) return 12;
        const auto c = std::find_if(confirmed.snapshot->collections.begin(), confirmed.snapshot->collections.end(), [&](const auto& c) { return c.id == id; });
        if (c == confirmed.snapshot->collections.end() || c->name != argv[2] || !std::all_of(expected.begin(), expected.end(), [&](uint32_t app) {
            return std::find(c->app_ids.begin(), c->app_ids.end(), app) != c->app_ids.end();
        })) return 13;
        std::cout << "Migrated and verified Steam collection; members=" << expected.size() << '\n';
    }
    return 0;
}
