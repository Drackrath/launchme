#include <stdexcept>

int LauncherUiTest::pcRefresh(HWND window) {
    struct Control {
        std::atomic_int calls{0}, active{0}, peak{0};
        std::atomic_bool release{false}, fail{false}, block{true};
        std::mutex mutex;
        std::vector<std::string> accounts;
    };
    const auto control = std::make_shared<Control>();
    Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute;
    struct Release { std::shared_ptr<Control> control; ~Release() { control->release = true; } } release{control};
    if (!app.renderer_.initialize(window)) return 1760;
    Game game; game.source = GameSource::gog; game.kind = GameKind::store;
    game.source_id = "123"; game.name = L"Completed installation"; game.account_id = "account-A"; game.installed = false;
    app.settings_.enabled_pc_sources = {"gog"}; app.settings_.show_uninstalled = true;
    app.screen_ = ScreenMode::settings; app.scanning_ = false; app.pc_initial_ready_ = true;
    app.gog_state_.account = game.account_id; app.gog_state_.games = {game};
    app.store_library_ = {game}; app.buildShelves();
    app.read_pc_library_ = [control, game](const PcLibraryRequest& request, const std::function<bool()>&) {
        const int call = ++control->calls, active = ++control->active;
        int previous = control->peak.load();
        while (previous < active && !control->peak.compare_exchange_weak(previous, active)) {}
        struct Finished { std::shared_ptr<Control> control; ~Finished() { --control->active; } } finished{control};
        { std::lock_guard lock(control->mutex); control->accounts.push_back(request.gog_account); }
        if (control->block.exchange(false)) while (!control->release) Sleep(1); // Simulated blocking disk I/O.
        if (control->fail.exchange(false)) throw std::runtime_error("scan fixture failed");
        PcLibraryMessage message; auto found = game;
        found.installed = call > 1;
        if (found.installed) { found.account_id.clear(); found.play_action = {L"C:\\Fixture\\game.exe"}; }
        message.library.visible = {found}; return message;
    };
    const auto pump = [&] {
        MSG message{};
        while (PeekMessageW(&message, window, pc_library_message, pc_library_message, PM_REMOVE))
            app.onPcLibrary(std::unique_ptr<PcLibraryMessage>(reinterpret_cast<PcLibraryMessage*>(message.lParam)));
    };
    const auto wait = [&](const auto& condition) {
        const auto end = GetTickCount64() + 5000;
        do { pump(); if (condition()) return true; Sleep(2); } while (GetTickCount64() < end);
        return false;
    };
    app.runtime_started_ = true; app.refreshPcLibraries();
    if (!wait([&] { return control->calls == 1; })) return 1761;
    const auto stale_generation = app.pc_scan_generation_;
    app.gog_install_ui_.game = game; app.gog_install_ui_.snapshot.product = game.source_id;
    app.gog_install_ui_.snapshot.state = gog::InstallState::downloading;
    auto service = std::make_unique<FakeGog>();
    service->installation = gog::InstallSnapshot{};
    service->installation->product = game.source_id; service->installation->state = gog::InstallState::completed;
    app.gog_service_ = std::move(service);
    app.pollGogInstall();
    for (int i = 0; i < 20; ++i) app.refreshPcLibraries();
    if (!app.pc_refresh_pending_ || app.pc_token_->load() == stale_generation || control->calls != 1) return 1762;
    control->release = true;
    if (!wait([&] { return !app.pc_scanning_ && control->calls == 2; }) || app.pc_refresh_pending_ ||
        app.store_library_.size() != 1 || !app.store_library_[0].installed || app.actionLabel(app.store_library_[0]) != L"Graj") return 1763;
    // An old duplicate cannot undo the completed installation.
    auto stale = std::make_unique<PcLibraryMessage>(); stale->generation = stale_generation; stale->library.visible = {game};
    app.onPcLibrary(std::move(stale));
    if (!app.store_library_[0].installed) return 1764;

    control->fail = true; app.refreshPcLibraries();
    if (!wait([&] { return !app.pc_scanning_; }) || control->calls != 3 || !app.store_library_[0].installed || app.notifications_.empty()) return 1765;
    app.refreshPcLibraries();
    if (!wait([&] { return !app.pc_scanning_; }) || control->calls != 4) return 1766;
    // Other PC stores keep polling even with Battle.net disabled.
    app.next_pc_poll_ = 0; app.pollPcInstallations();
    if (!wait([&] { return !app.pc_scanning_; }) || control->calls != 5) return 1767;

    // An account change during a failing old scan still starts exactly one
    // follow-up, using the latest account rather than the cancelled snapshot.
    control->release = false; control->block = true; control->fail = true;
    app.refreshPcLibraries();
    if (!wait([&] { return control->calls == 6; })) return 1768;
    app.gog_state_.account = "account-B"; ++*app.pc_token_; app.refreshPcLibraries();
    control->release = true;
    if (!wait([&] { return !app.pc_scanning_ && control->calls == 7; }) || control->peak != 1) return 1769;
    { std::lock_guard lock(control->mutex); if (control->accounts.back() != "account-B") return 1770; }
    app.runtime_started_ = false;
    return 0;
}
