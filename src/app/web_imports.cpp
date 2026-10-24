// EA and Battle.net libraries copied from the store's website: the browser shows a page
// (EA: a sign-in JSON with a short-lived token; Battle.net: the account's game list), the
// user pastes it into Settings, a worker turns it into a saved list of owned games
// (store/web_import.hpp) and the PC scan merges that list. Refreshing = pasting again.
#include "launcher.hpp"
#include "../library/battlenet.hpp"
#include "../platform/text_encoding.hpp"
#include <algorithm>
#include <chrono>

namespace {
// Battle.net: the sign-in establishes the account site's session, the second page is the list.
constexpr const wchar_t* battlenet_sign_in = L"https://account.battle.net/oauth2/authorization/account-settings";
constexpr const wchar_t* battlenet_games = L"https://account.battle.net/api/games-and-subs";
}

Launcher::WebImportState& Launcher::webImport(GameSource source) { return source == GameSource::ea ? ea_import_ : battlenet_import_; }
const Launcher::WebImportState& Launcher::webImport(GameSource source) const { return source == GameSource::ea ? ea_import_ : battlenet_import_; }

void Launcher::startWebImport(GameSource source) {
    auto& state = webImport(source);
    if (state.busy) return;
    const bool opened = source == GameSource::ea ? execute_(hwnd_, {ea::login_url, {}, {}, ActionKind::open_client}) > 32 :
        execute_(hwnd_, {battlenet_sign_in, {}, {}, ActionKind::open_client}) > 32 && execute_(hwnd_, {battlenet_games, {}, {}, ActionKind::open_client}) > 32;
    if (!opened) {
        showNotification(NotificationKind::error, sourceName(source), tr(L"Nie można otworzyć przeglądarki."));
        return;
    }
    state.awaiting = true;
    buildSettingsRows();
    // Focus the paste field at once, as for the GOG and Epic sign-in codes.
    const auto id = source == GameSource::ea ? settings_ui::Id::ea_callback : settings_ui::Id::battlenet_callback;
    const auto row = std::find_if(settings_rows_.begin(), settings_rows_.end(), [&](const auto& entry) { return entry.id == id; });
    if (settingsOpen() && row != settings_rows_.end()) {
        settings_state_.selected[settings_state_.category] = static_cast<int>(row - settings_rows_.begin());
        settings_state_.editing = settings_state_.options = true;
        clearSecretInput(); settings_secret_selected_ = true;
        ensureSettingVisible();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::cancelWebImport(GameSource source) {
    auto& state = webImport(source);
    state.awaiting = false;
    clearSecretInput(); settings_state_.editing = false;
    buildSettingsRows();
}

void Launcher::submitWebImport(GameSource source) {
    auto& state = webImport(source);
    if (state.busy) return;
    auto pasted = text_encoding::utf8(settings_secret_input_);
    clearSecretInput(); settings_state_.editing = false;
    state.awaiting = false; state.busy = true;
    const auto generation = ++state.generation;
    const auto alive = alive_;
    const auto window = hwnd_;
    work_.start([source, generation, alive, window, importer = ea_importer_, pasted = std::move(pasted)]() mutable {
        auto message = std::make_unique<StoreImportMessage>();
        message->source = source; message->generation = generation;
        if (source == GameSource::ea) {
            HttpBudget budget(90000);
            auto result = importer(pasted);
            message->failure = result.failure;
            message->snapshot = std::move(result.snapshot);
        } else if (auto games = battlenet::parseGamesAndSubs(pasted)) {
            // The page names no account; the marker keeps these entries apart from local installations.
            message->snapshot.account = "battlenet";
            message->snapshot.synced_at = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
            for (auto& game : *games) { game.account_id = message->snapshot.account; message->snapshot.games.push_back(std::move(game)); }
        } else message->invalid = true;
        SecureZeroMemory(pasted.data(), pasted.size());
        if (message->failure == ea::Failure::none && !message->invalid)
            message->save_failed = !web_import::save(userConfigDirectory(), source, message->snapshot);
        if (!alive->load()) return;
        if (PostMessageW(window, store_import_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    });
    buildSettingsRows();
}

void Launcher::clearWebImport(GameSource source) {
    auto& state = webImport(source);
    if (state.busy) return;
    state.awaiting = false; state.busy = true;
    const auto generation = ++state.generation;
    const auto alive = alive_;
    const auto window = hwnd_;
    work_.start([source, generation, alive, window] {
        auto message = std::make_unique<StoreImportMessage>();
        message->source = source; message->generation = generation; message->cleared = true;
        message->save_failed = !web_import::clear(userConfigDirectory(), source);
        if (!alive->load()) return;
        if (PostMessageW(window, store_import_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    });
    buildSettingsRows();
}

void Launcher::onStoreImport(std::unique_ptr<StoreImportMessage> message) {
    auto& state = webImport(message->source);
    if (message->generation != state.generation) return;
    state.busy = false;
    const auto name = sourceName(message->source);
    if (message->cleared) {
        if (!message->save_failed) state.snapshot.reset();
        else showNotification(NotificationKind::error, name, tr(L"Nie udało się usunąć zapisanej biblioteki."));
    } else if (message->invalid) {
        showNotification(NotificationKind::error, name, tr(L"To nie jest lista gier Battle.net. Skopiuj całą stronę po zalogowaniu."));
    } else if (message->failure != ea::Failure::none) {
        showNotification(NotificationKind::error, name, ea::failureText(message->failure));
    } else if (message->save_failed) {
        showNotification(NotificationKind::error, name, tr(L"Nie udało się zapisać biblioteki."));
    } else {
        showNotification(NotificationKind::launch, name, tr(L"Gry w bibliotece: ") + std::to_wstring(message->snapshot.games.size()));
        state.snapshot = std::move(message->snapshot);
        if (settings_.enabled_pc_sources.insert(sourceSlug(message->source)).second) settingsChanged(true);
    }
    pc_token_->fetch_add(1); refreshPcLibraries();
    if (settingsOpen()) buildSettingsRows();
    InvalidateRect(hwnd_, nullptr, FALSE);
}
