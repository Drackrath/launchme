#include "../src/ui/launch_animation.hpp"

int LauncherUiTest::launchAnimation(HWND window) {
    Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute;
    app.foreground_ = [](HWND) { return false; }; // Losing focus must not extend the animation.
    app.overlay_backend_.foreground = [window] { return window; };
    if (!app.renderer_.initialize(window)) return 2500;
    auto library = std::make_unique<LibraryMessage>(); library->account = 1;
    library->games = {Game{620, L"Portal 2"}, Game{400, L"Portal"}};
    app.onLibrary(std::move(library));
    app.openSettings();
    // Biblioteka → Uruchamianie: the picture is always on, without a switch of its own.
    app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::library)); app.buildSettingsRows();
    if (std::any_of(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& item) { return item.title == L"Animacja uruchamiania gry"; }) ||
        !app.settings_.launch_animation || !loadAppSettings().launch_animation) return 2501;
    app.showCategory(0); app.openLibrary(); app.focusCard(0);
    const auto game = app.cards_[0].game;

    dispatched.clear(); app.launch();
    if (!app.launchAnimationActive() || !app.pending_launch_ || app.launch_animation_title_ != game.name || app.launch_artwork_key_ != gameKey(game)) return 2508;
    const auto started = *app.launch_animation_started_;
    app.tick(); // Launch immediately, rather than waiting for the animation to finish.
    if (app.pending_launch_ || dispatched.size() != 1 || !app.launchAnimationActive() || app.launch_progress_ != 0) return 2509;
    app.launch(); app.click(200, 200, true);
    PadReading pad{}; pad.connected = true;
    app.controller(pad); pad.buttons = XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_DPAD_RIGHT; app.controller(pad);
    if (dispatched.size() != 1 || app.selected_ != 0 || app.quick_open_) return 2510;
    app.tickLaunchAnimation(started + std::chrono::milliseconds(800));
    if (!app.launchAnimationActive() || app.screen_ != ScreenMode::library) return 2511;
    app.tickLaunchAnimation(started + std::chrono::seconds(5)); // About ten seconds, not a blink.
    if (!app.launchAnimationActive()) return 2523;
    using launch_animation::seconds;
    if (seconds < 8 || launch_animation::opacity(0) != 0 || launch_animation::opacity(0.2f) <= 0 || launch_animation::opacity(0.2f) >= 1 ||
        launch_animation::opacity(seconds * 0.5f) != 1 || launch_animation::opacity(seconds) != 0 ||
        launch_animation::zoom(1) >= launch_animation::zoom(6) || launch_animation::zoom(0) != 1 ||
        std::abs(launch_animation::zoom(seconds) - (1 + launch_animation::zoom_extra)) > 0.001f ||
        launch_animation::logo(0) != 0 || launch_animation::logo(3) != 1) return 2519;

    auto captures = userConfigDirectory() / L"launch-captures";
    wchar_t folder[4096]{};
    if (const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096); count && count < 4096) captures = folder;
    std::filesystem::create_directories(captures);
    app.renderer_.setCover(0, pngFixture(100, 180, 180), {});
    app.launch_animation_started_ = std::chrono::steady_clock::now() - std::chrono::milliseconds(250);
    if (!app.renderer_.captureForTest(captures / L"cover-launch-enter.png", [&] { app.paint(); })) return 2520;
    app.launch_animation_started_ = std::chrono::steady_clock::now() - std::chrono::milliseconds(4000);
    if (!app.renderer_.captureForTest(captures / L"cover-launch.png", [&] { app.paint(); })) return 2512;
    app.launch_animation_started_ = started;
    // Back hides it early, but not the press that started the game.
    if (app.dismissLaunchAnimation(started + std::chrono::milliseconds(300)) || !app.launchAnimationActive()) return 2521;
    if (!app.dismissLaunchAnimation(started + std::chrono::seconds(2))) return 2522;
    if (app.launchAnimationActive() || app.screen_ != ScreenMode::shelves || dispatched.size() != 1) return 2513;
    // Left alone, it ends on its own after its time.
    app.focusCard(0); app.game_sessions_.clear();
    app.launch(); app.tick();
    if (!app.launchAnimationActive()) return 2524;
    app.tickLaunchAnimation(*app.launch_animation_started_ + std::chrono::milliseconds(static_cast<int>(seconds * 1000) + 50));
    if (app.launchAnimationActive() || app.screen_ != ScreenMode::shelves) return 2525;
    dispatched.resize(1);

    // Failure reveals the error right away, without leaving a fake loading screen.
    app.game_sessions_.clear(); app.focusCard(0);
    app.execute_ = [](HWND, const GameAction&) -> INT_PTR { return 31; };
    app.launch(); app.finishLaunch();
    if (app.launchAnimationActive() || app.notifications_.empty() || app.audio_.music_target_ <= 0) return 2515;
    app.execute_ = fakeExecute; app.settings_.launch_animation = false;
    app.launch();
    if (app.launchAnimationActive() || !app.pending_launch_) return 2516;
    app.finishLaunch();
    if (app.launchAnimationActive() || dispatched.size() != 2) return 2517;
    // Installation/client actions use their existing feedback even with the effect enabled.
    app.settings_.launch_animation = true; app.game_sessions_.clear();
    app.cards_[app.selected_].game.installed = false;
    app.launch();
    if (app.launchAnimationActive() || !app.pending_launch_) return 2518;
    app.finishLaunch();

    // Biblioteka → Uruchamianie → Ciche uruchamianie Steam: off by default. On, a Steam game goes
    // through steam.exe -silent, without foreground permission and with a longer focus handoff.
    app.settings_.launch_animation = false; app.game_sessions_.clear(); app.cancelGameFocus();
    app.openSettings();
    app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::library)); app.buildSettingsRows();
    const auto silent_row = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(),
        [](const auto& item) { return item.id == settings_ui::Id::steam_silent_launch; });
    if (silent_row == app.settings_rows_.end() || silent_row->checked || app.settings_.steam_silent_launch) return 5104;
    app.settings_state_.enter(static_cast<int>(app.settings_rows_.size()));
    app.settings_state_.selected[app.settings_state_.category] = static_cast<int>(silent_row - app.settings_rows_.begin());
    app.settingsActivate(); app.saveSettings();
    if (!app.settings_.steam_silent_launch || !loadAppSettings().steam_silent_launch) return 5105;
    app.showCategory(0); app.openLibrary(); app.focusCard(0);
    static std::vector<GameAction> silent_actions;
    silent_actions.clear();
    app.steam_executable_ = [] { return std::filesystem::path(L"C:\\Steam\\steam.exe"); };
    app.execute_ = [](HWND, const GameAction& action) -> INT_PTR { silent_actions.push_back(action); return 42; };
    const auto url = L"steam://rungameid/" + std::to_wstring(app.cards_[app.selected_].game.app_id);
    app.launch(); app.finishLaunch();
    if (silent_actions.size() != 1 || silent_actions[0].target != L"C:\\Steam\\steam.exe" ||
        silent_actions[0].arguments != L"-silent -- \"" + url + L"\"" || !silent_actions[0].keep_focus ||
        app.launch_focus_until_ < GetTickCount64() + 30000) return 5106;
    // steam.exe refused: the plain steam:// URL still starts the game, with the usual handoff.
    app.game_sessions_.clear(); app.cancelGameFocus(); silent_actions.clear();
    app.execute_ = [](HWND, const GameAction& action) -> INT_PTR { silent_actions.push_back(action); return action.keep_focus ? 2 : 42; };
    app.launch(); app.finishLaunch();
    if (silent_actions.size() != 2 || silent_actions[1].target != url || silent_actions[1].keep_focus ||
        app.launch_focus_until_ > GetTickCount64() + 16000) return 5107;
    app.game_sessions_.clear(); app.cancelGameFocus(); app.execute_ = fakeExecute;
    app.settings_.steam_silent_launch = false; app.saveSettings();
    return 0;
}
