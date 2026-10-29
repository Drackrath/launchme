// Kopie zapisów: a game's saves backed up and restored through Ludusavi (src/library/ludusavi.hpp):
// after each game when Settings → Library → Kopie zapisów says so, and from the game's menu (Zapisy).
// Ludusavi finds the game by its Steam or GOG ID, else its title, and keeps the backups in the folder
// of its own configuration; Wine prefixes of Windows games on Linux are named to it.
#include "launcher.hpp"
#include "../library/ludusavi.hpp"
#include "../platform/helper_process.hpp"
#include "../platform/user_config.hpp"
#ifndef _WIN32
#include "../library/launch_options_store.hpp"
#endif

namespace {
std::wstring sizeLabel(uint64_t bytes) {
    if (bytes >= 1024 * 1024) return std::to_wstring((bytes + 512 * 1024) / (1024 * 1024)) + L" MB";
    return std::to_wstring((bytes + 512) / 1024) + L" KB";
}
}

std::filesystem::path Launcher::ludusaviProgram() const { return ludusavi::find(userConfigDirectory()); }

void Launcher::installLudusavi() {
    if (ludusavi_busy_) return;
    ludusavi_busy_ = true;
    showNotification(NotificationKind::download, L"Ludusavi", tr(L"Pobieranie…"));
    if (settingsOpen()) buildSettingsRows();
    const auto alive = alive_; const auto hwnd = hwnd_;
    if (!work_.start([alive, hwnd] {
        auto error = ludusavi::install(userConfigDirectory());
        if (!alive->load()) return;
        auto message = std::make_unique<RemoteMessage>();
        message->run = [error = std::move(error)](Launcher& app) {
            app.ludusavi_busy_ = false;
            if (error.empty()) app.showNotification(NotificationKind::download, L"Ludusavi", tr(L"Zainstalowano"));
            else app.showNotification(NotificationKind::error, tr(L"Nie udało się pobrać Ludusavi"), error);
            if (app.settingsOpen()) app.buildSettingsRows();
        };
        if (PostMessageW(hwnd, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    })) ludusavi_busy_ = false;
}

void Launcher::openLudusavi() {
    const auto program = ludusaviProgram();
    if (program.empty()) { installLudusavi(); return; }
    if (execute_(hwnd_, GameAction{program.wstring(), L"gui", program.parent_path(), ActionKind::open_client}) <= 32)
        showNotification(NotificationKind::error, tr(L"Nie można otworzyć programu"), program.wstring());
}

void Launcher::gameSaves(const Game& game, bool restore, bool quiet) {
    const auto key = gameKey(game);
    if (key.empty() || !isGame(game)) return;
    const auto program = ludusaviProgram();
    if (program.empty()) {
        if (!quiet) showNotification(NotificationKind::error, tr(L"Brak Ludusavi"), tr(L"Pobierz go w Ustawieniach → Biblioteka → Kopie zapisów."));
        return;
    }
    ludusavi::Query query;
    if (game.source == GameSource::steam) query.steam_id = game.app_id;
    else if (game.source == GameSource::gog) query.gog_id = game.source_id;
    query.title = game.name;
    std::filesystem::path wine_prefix;
#ifndef _WIN32
    // A Windows game's own prefix (Steam's, Heroic's and Lutris' Ludusavi finds itself).
    if (compatGame(game)) if (const auto launch = launch_options_store::compatLaunch(game)) {
        std::error_code ec;
        wine_prefix = std::filesystem::is_directory(launch->prefix / L"pfx", ec) ? launch->prefix / L"pfx" : launch->prefix;
    }
#endif
    const auto cached = ludusavi_titles_.find(key);
    const std::wstring known = cached == ludusavi_titles_.end() ? std::wstring{} : cached->second;
    const auto name = game.name;
    if (!quiet) showNotification(NotificationKind::download, name, restore ? tr(L"Przywracanie zapisów…") : tr(L"Kopia zapisów…"));
    const auto alive = alive_; const auto hwnd = hwnd_;
    work_.start([alive, hwnd, program, query, wine_prefix, known, key, name, restore, quiet] {
        const auto never = [] { return false; };
        std::wstring title = known;
        if (title.empty()) {
            auto found = ludusavi::parseFind(helper_process::run(program, {}, ludusavi::findArguments(query), {}, never).output);
            if (!found && (query.steam_id || !query.gog_id.empty())) { // An ID it does not know: its title, then.
                ludusavi::Query by_title;
                by_title.title = query.title;
                found = ludusavi::parseFind(helper_process::run(program, {}, ludusavi::findArguments(by_title), {}, never).output);
            }
            if (found) title = *found;
        }
        ludusavi::Outcome outcome;
        if (!title.empty()) outcome = ludusavi::parseOperation(helper_process::run(program, {}, ludusavi::operationArguments(restore, title, wine_prefix), {}, never).output);
        if (!alive->load()) return;
        auto message = std::make_unique<RemoteMessage>();
        message->run = [key, name, title, outcome, restore, quiet](Launcher& app) {
            if (title.empty()) {
                if (!quiet) app.showNotification(NotificationKind::error, name, tr(L"Ludusavi nie zna zapisów tej gry."));
                return;
            }
            app.ludusavi_titles_[key] = title;
            if (!outcome.games) {
                if (!quiet) app.showNotification(NotificationKind::error, name, restore ? tr(L"Brak kopii zapisów tej gry.") : tr(L"Nie znaleziono zapisów tej gry."));
                return;
            }
            const auto detail = std::to_wstring(outcome.files) + tr(L" plików · ") + sizeLabel(outcome.bytes);
            app.showNotification(NotificationKind::download, name, (restore ? tr(L"Przywrócono zapisy: ") : tr(L"Kopia zapisów: ")) + detail);
        };
        if (PostMessageW(hwnd, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    });
}

void Launcher::askRestoreSaves(const Game& game) {
    if (sessionState(game) != SessionState::none) { // Never under a running game.
        showNotification(NotificationKind::error, game.name, tr(L"Zamknij grę przed przywróceniem zapisów."));
        return;
    }
    dismissMenu();
    restore_game_ = game;
    prompt_kind_ = PromptKind::restore_saves;
    exit_prompt_ = 1; // "Najpierw zrób kopię" is the safe default.
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}
