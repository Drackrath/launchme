// Linux: how a game runs through Proton and on the Steam Deck (src/net/protondb.hpp): ProtonDB's tier
// and Valve's Deck rating for a Steam game, or a Windows game whose umu-database ID is a Steam app
// (umu-<app ID>). Shown in the desktop window's details pane and on the game's Proton / Wine page.
#include "launcher.hpp"

#ifndef _WIN32
#include "../i18n/i18n.hpp"
#include "../library/compat_tools.hpp"
#include "../library/launch_options_store.hpp"
#include "../net/http.hpp"
#include "../platform/user_config.hpp"

#include <ctime>
#endif

#ifdef _WIN32
std::wstring Launcher::protonDbLabel(const Game&) { return {}; }
void Launcher::openProtonDb(const Game&) {}
#else
std::wstring Launcher::protonDbLabel(const Game& game) {
    const auto key = gameKey(game);
    if (key.empty() || !isGame(game)) return {};
    if (const auto found = protondb_.find(key); found != protondb_.end()) return protondb::label(found->second.summary, i18n::english());
    if (!protondb_requested_.insert(key).second) return {};
    // Which Steam app it is: its own, else the one umu-database names for the store's game.
    const uint32_t steam = game.source == GameSource::steam ? game.app_id : 0;
    const auto launch = !steam && compatGame(game) ? launch_options_store::compatLaunch(game) : nullptr;
    if (!steam && !launch) return {};
    const auto alive = alive_; const auto hwnd = hwnd_;
    work_.start([alive, hwnd, key, steam, launch] {
        uint32_t app = steam;
        if (!app && launch) app = launch->steam_app_id ? launch->steam_app_id : protondb::steamAppOf(compat_tools::gameId(*launch, compat_tools::systemRoots().config));
        if (!app || !alive->load()) return;
        const auto folder = userConfigDirectory() / L"protondb";
        const auto now = static_cast<uint64_t>(std::time(nullptr));
        auto summary = protondb::cached(folder, app, now);
        if (!summary) {
            summary = protondb::fetch(app, [](const std::wstring& url) { return httpGet(url, L"", 256 * 1024); });
            if (summary) protondb::store(folder, app, *summary, now);
        }
        if (!summary || !alive->load()) return;
        auto message = std::make_unique<RemoteMessage>();
        message->run = [key, app, summary = *summary](Launcher& launcher) {
            launcher.protondb_[key] = {app, summary};
            if (launcher.settingsOpen() && launcher.compatSettingsPage()) launcher.buildSettingsRows();
            InvalidateRect(launcher.hwnd_, nullptr, FALSE);
        };
        if (PostMessageW(hwnd, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    });
    return {};
}

void Launcher::openProtonDb(const Game& game) {
    const auto found = protondb_.find(gameKey(game));
    if (found == protondb_.end() || !found->second.app) return;
    const auto page = protondb::page(found->second.app);
    if (execute_(hwnd_, GameAction{page, {}, {}, ActionKind::open_client}) <= 32)
        showNotification(NotificationKind::error, tr(L"Nie można otworzyć strony"), page);
}
#endif
