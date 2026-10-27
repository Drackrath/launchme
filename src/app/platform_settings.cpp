// PC settings: one store row on the overview; its account/library actions behind the cog.
#include "launcher.hpp"
#include <algorithm>

using namespace settings_ui;
namespace {
template<class State> bool signing(State state) {
    return state == State::preparing || state == State::signing_in || state == State::verifying;
}
template<class State> bool working(State state) { return signing(state) || state == State::syncing; }
std::wstring syncedAt(uint64_t seconds) {
    if (!seconds) return {};
    const time_t time = static_cast<time_t>(seconds); tm local{}; localtime_s(&local, &time);
    wchar_t text[64]{}; wcsftime(text, std::size(text), L"%Y-%m-%d %H:%M", &local);
    return tr(L"Ostatnia synchronizacja: ") + std::wstring(text);
}
}

void Launcher::pcSettingsRows() {
    const auto installed = [&](GameSource source) {
        return std::count_if(store_library_.begin(), store_library_.end(), [&](const auto& game) { return game.source == source && game.installed; });
    };
    const auto library = [&](GameSource source) {
        if (pc_scanning_) return std::wstring(tr(L"Skanowanie…"));
        const auto local = tr(L"Zainstalowane: ") + std::to_wstring(installed(source));
        if ((source == GameSource::ubisoft && ubisoft_account_) || (source == GameSource::ea && ea_import_.snapshot)) {
            size_t owned = 0;
            for (const auto* list : {&store_library_, &store_private_library_})
                owned += std::count_if(list->begin(), list->end(), [&](const auto& game) { return game.source == source; });
            return tr(L"Biblioteka: ") + std::to_wstring(owned) + L" · " + local;
        }
        if (source == GameSource::battlenet) return battlenet_available_ ? tr(L"Lokalne instalacje: ") + std::to_wstring(installed(source)) : std::wstring(tr(L"Lokalne instalacje: stan niedostępny"));
        return local;
    };
    const auto account = [&](GameSource source) {
        if (source == GameSource::gog)
            return gog_state_.state == gog::State::disconnected && gog_state_.failure == gog::Failure::none ? std::wstring(tr(L"Konto niepołączone")) : gog::statusText(gog_state_);
        if (source == GameSource::epic)
            return epic_state_.state == epic::State::disconnected && epic_state_.failure == epic::Failure::none ? std::wstring(tr(L"Konto niepołączone")) : epic::statusText(epic_state_);
        if (source == GameSource::ea) return ea_import_.busy ? std::wstring(tr(L"Importowanie…")) : ea_import_.awaiting ?
            std::wstring(tr(L"Oczekiwanie na dane logowania")) : ea_import_.snapshot ? library(source) : std::wstring(tr(L"Konto niepołączone"));
        return library(source);
    };
    if (!settings_state_.platform()) {
        // DRM-free folders first: the user's own library, not a store account.
        Row drm{Id::pc_platform, Control::platform, tr(L"Katalog gier DRM-free"), drmSummary(), tr(L"Opcje"),
                settings_.enabled_pc_sources.contains(sourceSlug(GameSource::drmfree))};
        drm.source = GameSource::drmfree; settings_rows_.push_back(std::move(drm));
        for (const auto source : pc_sources) {
            auto summary = account(source);
            if (source == GameSource::gog || source == GameSource::epic ||
                (source == GameSource::ea && (!ea_import_.snapshot || ea_import_.busy || ea_import_.awaiting))) summary += L" · " + library(source);
            if (source == GameSource::battlenet && battlenet_import_.snapshot)
                summary += L" · " + std::wstring(tr(L"Gry konta: ")) + std::to_wstring(battlenet_import_.snapshot->games.size());
            Row row{Id::pc_platform, Control::platform, sourceName(source), std::move(summary), tr(L"Opcje"), settings_.enabled_pc_sources.contains(sourceSlug(source))};
            row.source = source; settings_rows_.push_back(std::move(row));
        }
        return;
    }

    const auto source = *settings_state_.platform();
    if (source == GameSource::drmfree) { drmSettingsRows(); return; }
    const auto add = [&](Row row) { row.source = source; settings_rows_.push_back(std::move(row)); };
    if (source == GameSource::gog || source == GameSource::epic) {
        const bool is_gog = source == GameSource::gog;
        const bool busy = is_gog ? working(gog_state_.state) : working(epic_state_.state);
        const bool connected = is_gog ? !gog_state_.account.empty() : !epic_state_.account.empty();
        const bool callback = is_gog ? gog_state_.state == gog::State::signing_in : epic_state_.state == epic::State::signing_in;
        add({Id::pc_connect, Control::action, tr(L"Konto"), account(source), busy ? tr(L"Anuluj") : connected ? tr(L"Zmień konto") : tr(L"Połącz")});
        if (callback) add({is_gog ? Id::gog_callback : Id::epic_callback, Control::secret,
            is_gog ? tr(L"Adres powrotu") : tr(L"Kod logowania"),
            is_gog ? tr(L"Wklej tutaj cały adres z przeglądarki po logowaniu.") : tr(L"Wklej authorizationCode lub całą odpowiedź JSON po logowaniu."),
            is_gog ? tr(L"Wklej adres") : tr(L"Wklej kod")});
        const auto sync = syncedAt(is_gog ? gog_state_.synced_at : epic_state_.synced_at);
        add({connected ? (is_gog ? Id::gog_refresh : Id::epic_refresh) : Id::pc_refresh, Control::action, tr(L"Biblioteka"),
            library(source) + (sync.empty() ? L"" : L" · " + sync), tr(L"Odśwież")});
        const auto prefix = std::string(sourceSlug(source)) + ':';
        const auto excluded = std::count_if(library_state_.excluded_games.begin(), library_state_.excluded_games.end(),
            [&](const auto& entry) { return entry.first.starts_with(prefix); });
        add({Id::pc_excluded, Control::action, tr(L"Wykluczone gry"),
            tr(L"Niewidoczne także w wyszukiwaniu i pod Ctrl+H.") + std::wstring(L" · ") + std::to_wstring(excluded), tr(L"Zarządzaj")});
        if (connected) add({is_gog ? Id::gog_logout : Id::epic_logout, Control::action, tr(L"Połączenie konta"),
            tr(L"Usuń lokalną sesję i bibliotekę kontową."), tr(L"Wyloguj")});
        return;
    }
    if (source == GameSource::ea) {
        add({Id::pc_connect, Control::action, tr(L"Konto"), account(source), ea_import_.busy ? tr(L"Importowanie…") :
            ea_import_.awaiting ? tr(L"Anuluj") : ea_import_.snapshot ? tr(L"Zmień konto") : tr(L"Połącz")});
        if (ea_import_.awaiting) add({Id::ea_callback, Control::secret, tr(L"Dane logowania"),
            tr(L"Zaloguj się na ea.com, odśwież kartę z access_token, skopiuj całą stronę i wklej tutaj."), tr(L"Wklej odpowiedź")});
        add({ea_import_.snapshot ? Id::ea_refresh : Id::pc_refresh, Control::action, tr(L"Biblioteka"),
            ea_import_.snapshot ? syncedAt(ea_import_.snapshot->synced_at) : library(source), tr(L"Odśwież")});
        if (ea_import_.snapshot) add({Id::ea_remove, Control::action, tr(L"Zapisana biblioteka"), tr(L"Usuń zapisaną bibliotekę konta."), tr(L"Usuń")});
    } else if (source == GameSource::battlenet) {
        const auto& state = battlenet_import_;
        const auto description = state.snapshot ? tr(L"Zaimportowano: ") + std::to_wstring(state.snapshot->games.size()) +
            (state.snapshot->synced_at ? L" · " + syncedAt(state.snapshot->synced_at) : std::wstring{}) : std::wstring(tr(L"Nie zaimportowano"));
        add({Id::battlenet_import, Control::action, tr(L"Gry konta"), description,
            state.busy ? tr(L"Importowanie…") : state.awaiting ? tr(L"Anuluj") : state.snapshot ? tr(L"Odśwież") : tr(L"Importuj")});
        if (state.awaiting) add({Id::battlenet_callback, Control::secret, tr(L"Lista gier"),
            tr(L"Po zalogowaniu odśwież kartę games-and-subs, skopiuj całą stronę i wklej tutaj."), tr(L"Wklej listę")});
        add({Id::pc_refresh, Control::action, tr(L"Zainstalowane gry"), library(source), tr(L"Odśwież")});
        if (state.snapshot) add({Id::battlenet_remove, Control::action, tr(L"Zapisana biblioteka"), tr(L"Usuń zaimportowaną listę gier."), tr(L"Usuń")});
    } else if (source == GameSource::multimc) {
        // Every instance is a game; MultiMC itself is portable, so it can be pointed at by hand.
        add({Id::pc_refresh, Control::action, tr(L"Instancje"), library(source), tr(L"Odśwież")});
        std::wstring found;
        for (const auto& client : pc_clients_) if (client.source == source && !client.executable.empty()) found = client.executable.native();
        auto description = settings_.multimc_path.empty() ? (found.empty() ? std::wstring(tr(L"Nie znaleziono Prism Launcher ani MultiMC")) :
            tr(L"Znaleziono: ") + found) : settings_.multimc_path.native();
        add({Id::multimc_path, Control::action, tr(L"Launcher"), std::move(description),
             settings_.multimc_path.empty() ? tr(L"Wskaż…") : tr(L"Wyczyść")});
    } else {
        add({Id::pc_refresh, Control::action, tr(L"Biblioteka"), library(source), tr(L"Odśwież")});
    }
    const bool client = std::any_of(pc_clients_.begin(), pc_clients_.end(), [&](const auto& entry) { return entry.source == source && !entry.executable.empty(); });
    add({Id::pc_client, Control::action, tr(L"Klient"), {}, client ? tr(L"Otwórz klienta") : tr(L"Pobierz klienta")});
}

void Launcher::openPlatformSettings(GameSource source) {
    const auto found = std::find(pc_sources.begin(), pc_sources.end(), source);
    const bool drm = source == GameSource::drmfree;
    if (!drm && found == pc_sources.end()) return;
    if (settings_state_.category != static_cast<int>(settings_ui::Category::library))
        settings_state_.selectCategory(static_cast<int>(settings_ui::Category::library));
    settings_state_.closeAll(); // Another platform's page replaces this one; Back leads to the overview.
    drm_confirm_.clear();
    settings_ui::Row opener{Id::pc_platform, Control::platform};
    opener.source = source;
    settings_state_.open(Page::platform, {}, static_cast<int>(source), RowRef::of(opener));
    // Overview rows: DRM-free first, then the stores in pc_sources order (if the row is not found again).
    settings_state_.pages.back().parent_row = drm ? 0 : 1 + static_cast<int>(found - pc_sources.begin());
    clearSecretInput(); settings_motion_.valid = false; settingsPreviewFocus(); buildSettingsRows(); audio_.context();
}

bool Launcher::pcSettingsActivate(const Row& row) {
    if (drmSettingsActivate(row)) return true;
    if (row.id == Id::pc_platform) {
        if (settings_state_.editing) openPlatformSettings(row.source);
        else {
            const auto key = sourceSlug(row.source);
            if (!settings_.enabled_pc_sources.erase(key)) settings_.enabled_pc_sources.insert(key);
            settingsChanged(true); audio_.context();
        }
    } else if (row.id == Id::pc_connect) {
        if (row.source == GameSource::gog) gogCommand(working(gog_state_.state) ? gog::Command::cancel : gog::Command::connect);
        else if (row.source == GameSource::epic) epicCommand(working(epic_state_.state) ? epic::Command::cancel : epic::Command::connect);
        else if (row.source == GameSource::ea) { if (ea_import_.awaiting) cancelWebImport(row.source); else startWebImport(row.source); }
    } else if (row.id == Id::pc_excluded) openExcludedGames(row.source);
    else if (row.id == Id::pc_refresh) refreshPcLibraries();
    else if (row.id == Id::multimc_path) {
        if (!settings_.multimc_path.empty()) { setMultimcPath({}); return true; }
        file_browser::Request request;
        request.mode = file_browser::Mode::file;
        request.title = tr(L"Wskaż Prism Launcher lub MultiMC");
        request.extensions = {L".exe"};
        request.filter_label = tr(L"Programy (.exe)");
        openFileBrowser(std::move(request), [this](const std::filesystem::path& picked) { setMultimcPath(picked); });
    }
    else if (row.id == Id::pc_client) {
        const auto action = connectPcClient(row.source, pc_clients_);
        if (execute_(hwnd_, action) <= 32) showNotification(NotificationKind::error, tr(L"Nie można otworzyć klienta"), sourceName(row.source));
        else if (!action.target.starts_with(L"https://")) { settings_.enabled_pc_sources.insert(sourceSlug(row.source)); settingsChanged(true); }
    }
    // Store accounts: sync and sign-out, the website imports.
    else if (row.id == Id::gog_refresh) gogCommand(gog::Command::sync);
    else if (row.id == Id::gog_logout) gogCommand(gog::Command::logout);
    else if (row.id == Id::epic_refresh) epicCommand(epic::Command::sync);
    else if (row.id == Id::epic_logout) epicCommand(epic::Command::logout);
    else if (row.id == Id::ea_refresh) startWebImport(GameSource::ea);
    else if (row.id == Id::ea_remove) clearWebImport(GameSource::ea);
    else if (row.id == Id::battlenet_import) {
        if (battlenet_import_.awaiting) cancelWebImport(GameSource::battlenet); else startWebImport(GameSource::battlenet);
    }
    else if (row.id == Id::battlenet_remove) clearWebImport(GameSource::battlenet);
    // Sign-in codes and pasted store pages: an empty field opens, the second Enter submits it.
    else if (row.id == Id::gog_callback || row.id == Id::epic_callback || row.id == Id::ea_callback || row.id == Id::battlenet_callback) {
        if (settings_state_.editing) {
            if (row.id == Id::gog_callback) gogCommand(gog::Command::complete);
            else if (row.id == Id::epic_callback) epicCommand(epic::Command::complete);
            else submitWebImport(row.id == Id::ea_callback ? GameSource::ea : GameSource::battlenet);
            return true;
        }
        clearSecretInput();
        settings_secret_selected_ = true;
        settings_state_.editing = true;
        audio_.context();
        InvalidateRect(hwnd_, nullptr, FALSE);
    } else return false;
    return true;
}

// MultiMC / Prism picked by hand: only one of their EXEs is accepted; the scan starts at once and
// the source is switched on (the user asked for its instances).
void Launcher::setMultimcPath(const std::filesystem::path& picked) {
    if (!picked.empty() && !multimc::flavorOf(picked)) {
        showNotification(NotificationKind::error, tr(L"To nie jest Prism Launcher ani MultiMC"), picked.native());
        return;
    }
    settings_.multimc_path = picked;
    if (!picked.empty()) settings_.enabled_pc_sources.insert(sourceSlug(GameSource::multimc));
    settingsChanged(true);
    refreshPcLibraries();
    if (settingsOpen()) buildSettingsRows();
    audio_.context();
}

void Launcher::cancelPlatformSignIn(GameSource source) {
    if (source == GameSource::gog && signing(gog_state_.state)) gogCommand(gog::Command::cancel);
    else if (source == GameSource::epic && signing(epic_state_.state)) epicCommand(epic::Command::cancel);
    else if ((source == GameSource::ea || source == GameSource::battlenet) && webImport(source).awaiting) cancelWebImport(source);
}
