int LauncherUiTest::gameFocus(HWND window) {
    Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute;
    const Game first{6100, L"Delayed game window"}, second{6200, L"Second game"};
    HWND foreground = window;
    app.overlay_backend_.foreground = [&] { return foreground; };
    bool allow = false;
    int attempts = 0;
    std::vector<GameProcess> received;
    app.quick_resume_.focus = [&](const auto& processes) { ++attempts; received = processes; return allow; };
    const auto deliver = [&](uint64_t id, std::vector<GameProcess> processes) {
        auto message = std::make_unique<GameActivityMessage>();
        message->generation = app.game_activity_jobs_->generation.load(); message->observed_at = GetTickCount64();
        message->focus_request = id; message->focus_processes = std::move(processes);
        app.onGameActivity(std::move(message));
    };
    app.beginGameSession(first);
    const auto initial = app.launch_focus_id_;
    deliver(initial, {}); // The store has launched, but there is no game window yet.
    if (attempts || app.launch_focus_key_ != gameKey(first)) return 1900;
    deliver(initial, {{1234, 42}}); // Window creation/activation may still be pending.
    if (attempts != 1 || app.launch_focus_key_.empty()) return 1901;
    allow = true; deliver(initial, {{1234, 42}});
    if (attempts != 2 || !app.launch_focus_key_.empty() || !app.game_activity_jobs_->focus_key.empty() ||
        received != std::vector<GameProcess>{{1234, 42}}) return 1902;

    app.beginGameSession(second);
    const auto next = app.launch_focus_id_;
    deliver(initial, {{1234, 42}}); // A late result cannot activate the previous game.
    if (attempts != 2 || app.launch_focus_key_ != gameKey(second)) return 1903;
    deliver(next, {{5678, 43}});
    if (attempts != 3 || !app.launch_focus_key_.empty()) return 1904;

    app.beginGameSession(first);
    foreground = reinterpret_cast<HWND>(static_cast<UINT_PTR>(0x5678));
    deliver(app.launch_focus_id_, {{1234, 42}}); // User Alt+Tabbed elsewhere.
    if (attempts != 3 || !app.launch_focus_key_.empty()) return 1905;
    foreground = window;
    app.beginGameSession(first); app.noteUserInput();
    deliver(app.launch_focus_id_, {{1234, 42}}); // New launcher input cancels the handoff too.
    if (attempts != 3 || !app.launch_focus_key_.empty()) return 1906;
    app.beginGameSession(first); app.launch_focus_until_ = GetTickCount64() - 1;
    deliver(app.launch_focus_id_, {{1234, 42}});
    if (attempts != 3 || !app.launch_focus_key_.empty()) return 1907;
    app.beginGameSession(first); app.game_sessions_.erase(gameKey(first));
    deliver(app.launch_focus_id_, {{1234, 42}});
    if (attempts != 3 || !app.launch_focus_key_.empty()) return 1908;
    const Game external{6300, L"Started elsewhere"};
    auto discovery = std::make_unique<RunningGamesMessage>();
    discovery->version = app.running_version_; discovery->games = {external};
    app.onRunningGames(std::move(discovery));
    if (attempts != 3 || !app.launch_focus_key_.empty()) return 1909;
    return 0;
}
