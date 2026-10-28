// Xbox account: the sign-in, library sync and installs of Launchme's Xodus-based helper, which is
// downloaded only when asked (Biblioteka → Xbox, or "Połącz"). Games the Xbox app installed keep
// working without any of it.
#include "launcher.hpp"
#include "../platform/text_encoding.hpp"
#include <algorithm>
#include <shlobj.h>

void Launcher::initializeXbox() {
    if (!xbox_service_) xbox_service_ = xbox_account::makeService(userConfigDirectory());
    xbox_state_.helper = xbox_account::helperInstalled(userConfigDirectory());
    xbox_service_->submit(xbox_account::Command::restore);
}

bool Launcher::xboxOwnInstall(const Game& game) const {
    return settings_.xbox_own_install && game.source == GameSource::xbox && game.xbox_own_install && !game.account_id.empty() &&
           xbox_account::validProductId(game.xbox_product);
}

void Launcher::xboxCommand(xbox_account::Command command) {
    if (!xbox_service_) return;
    if (command == xbox_account::Command::sync && xbox_service_->busy()) return;
    xbox_service_->submit(command);
    if (command == xbox_account::Command::connect || command == xbox_account::Command::download) {
        xbox_enable_on_connect_ = command == xbox_account::Command::connect;
        xbox_state_.state = xbox_account::State::preparing; xbox_state_.failure = xbox_account::Failure::none;
    } else if (command == xbox_account::Command::sync) xbox_state_.state = xbox_account::State::syncing;
    else if (command == xbox_account::Command::cancel) {
        xbox_enable_on_connect_ = false;
        xbox_state_.state = xbox_state_.account.empty() ? xbox_account::State::disconnected : xbox_account::State::connected;
    } else if (command == xbox_account::Command::logout) {
        xbox_enable_on_connect_ = false;
        if (gog_install_ui_.game.source == GameSource::xbox && !gog_install_ui_.snapshot.busy()) gog_install_ui_ = {};
    }
    buildSettingsRows();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::pollXbox() {
    if (!xbox_service_) return;
    if (auto next = xbox_service_->take()) {
        const bool changed = next->account != xbox_state_.account || next->synced_at != xbox_state_.synced_at || next->games != xbox_state_.games;
        xbox_state_ = std::move(*next);
        if (xbox_enable_on_connect_ && !xbox_state_.account.empty()) {
            xbox_enable_on_connect_ = false;
            if (settings_.enabled_pc_sources.insert(sourceSlug(GameSource::xbox)).second) settingsChanged(true);
        }
        if (xbox_state_.state == xbox_account::State::error && xbox_state_.failure == xbox_account::Failure::expired)
            showNotification(NotificationKind::error, L"Xbox", xbox_account::statusText(xbox_state_));
        if (changed) { pc_token_->fetch_add(1); refreshPcLibraries(); }
        if (settingsOpen()) buildSettingsRows();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    if (auto next = xbox_service_->takeInstall(); next && gog_install_ui_.game.source == GameSource::xbox) {
        if (next->product == gog_install_ui_.game.source_id) {
            const auto previous_state = gog_install_ui_.snapshot.state;
            gog_install_ui_.snapshot = std::move(*next); applyGogInstallProgress();
            const auto& status = gog_install_ui_.snapshot;
            if (status.state == gog::InstallState::completed) {
                completed_pc_installs_.insert(gameKey(gog_install_ui_.game));
                if (previous_state != gog::InstallState::completed) downloadFinished(gog_install_ui_.game.name, &gog_install_ui_.game);
            }
            if (!status.busy()) { pc_token_->fetch_add(1); refreshPcLibraries(); }
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void Launcher::openXboxInstall(const Game& game) {
    if (!xbox_service_ || !xboxConnected() || !xboxOwnInstall(game)) return;
    const bool other = gog_install_ui_.game.source != GameSource::xbox || gog_install_ui_.game.source_id != game.source_id;
    if ((gog_service_ && gog_service_->installBusy()) || (epic_service_ && epic_service_->busy() && gog_install_ui_.game.source == GameSource::epic) ||
        (amazon_service_ && amazon_service_->busy() && gog_install_ui_.game.source == GameSource::amazon) || (gog_install_ui_.snapshot.busy() && other)) {
        showNotification(NotificationKind::error, L"Xbox", tr(L"Najpierw dokończ lub anuluj bieżące pobieranie.")); return;
    }
    dismissMenu(); closeQuick(); closeFriends(); closeCategoryMenu(); cancelEnterHold();
    if (other || gog_install_ui_.snapshot.state == gog::InstallState::none) {
        gog_install_ui_ = {}; gog_install_ui_.game = game;
        PWSTR path = nullptr; std::filesystem::path root = userConfigDirectory();
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &path))) { root = path; CoTaskMemFree(path); }
        auto& request = gog_install_ui_.request; request.product = game.source_id;
        request.install_directory = root / L"Games" / (L"Xbox-" + xbox_account::folderName(game.name, game.xbox_product));
        request.owner_window = reinterpret_cast<uintptr_t>(hwnd_);
        gog_install_ui_.snapshot.product = game.source_id; gog_install_ui_.snapshot.state = gog::InstallState::ready;
        gog_install_ui_.row = 1;
    }
    gog_install_ui_.open = true; InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::xboxInstallAccept() {
    if (!xbox_service_) return;
    auto& ui = gog_install_ui_;
    if (ui.snapshot.state == gog::InstallState::verifying) return;
    if (ui.snapshot.busy()) xbox_service_->cancelOperation();
    else if (ui.snapshot.state == gog::InstallState::completed) gogInstallKey(VK_ESCAPE, false);
    else if (xbox_service_->install(ui.game, ui.request)) {
        downloadQueued(ui.game.name);
        ui.snapshot.state = gog::InstallState::loading; ui.snapshot.error = gog::InstallError::none;
    }
}
