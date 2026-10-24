int LauncherUiTest::runningCandidates(HWND window) {
    Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute;
    if (!app.renderer_.initialize(window)) return 1780;
    Game game; game.source = GameSource::custom; game.kind = GameKind::store;
    game.source_id = "0123456789abcdef"; game.name = L"Moved game";
    game.executable = L"C:\\Fixture\\old\\game.exe"; game.install_directory = L"C:\\Fixture\\old";
    app.custom_library_ = {game}; app.buildShelves();
    const auto initial = app.running_version_;
    app.custom_library_[0].executable = L"C:\\Fixture\\new\\game.exe";
    app.buildShelves();
    const auto executable_changed = app.running_version_;
    if (!initial || executable_changed == initial || app.running_scan_->candidates[0].executable != app.custom_library_[0].executable) return 1781;
    app.custom_library_[0].install_directory = L"C:\\Fixture\\new"; app.buildShelves();
    if (app.running_version_ == executable_changed || app.running_scan_->candidates[0].install_directory != app.custom_library_[0].install_directory) return 1782;
    const auto moved = app.running_version_;
    app.buildShelves();
    if (app.running_version_ != moved) return 1783;
    auto stale = std::make_unique<RunningGamesMessage>(); stale->version = initial; stale->games = {game};
    app.onRunningGames(std::move(stale));
    if (!app.game_sessions_.empty()) return 1784;
    const auto current = app.running_scan_->candidates[0];
    auto report = std::make_unique<RunningGamesMessage>(); report->version = moved; report->games = {current};
    app.onRunningGames(std::move(report));
    if (app.game_sessions_.size() != 1 || app.trackedGame(gameKey(game)).executable != current.executable ||
        app.running_version_ != moved) return 1785; // last_played did not invalidate the new snapshot.

    app.public_library_[0].name = L"Renamed game";
    app.public_library_[0].account_id = "updated-account";
    app.public_library_[0].visibility = GameVisibility::private_game;
    app.updateRunningCandidates();
    const auto& updated = app.running_scan_->candidates[0];
    if (app.running_version_ == moved || updated.name != L"Renamed game" || updated.account_id != "updated-account" ||
        updated.visibility != GameVisibility::private_game) return 1786;

    // The game in play started a .exe that a store entry lists too (a "Moje gry" entry for a GOG
    // installation): that process is no second game, so it gets no session and no play history.
    Game store_copy; store_copy.source = GameSource::gog; store_copy.kind = GameKind::store; store_copy.source_id = "1541066319";
    store_copy.name = L"Store copy"; store_copy.executable = L"c:/fixture/NEW/Game.exe"; store_copy.install_directory = L"C:\\Fixture\\new";
    Game unrelated = store_copy; unrelated.source_id = "777"; unrelated.name = L"Unrelated";
    unrelated.executable = L"C:\\Fixture\\other\\other.exe"; unrelated.install_directory = L"C:\\Fixture\\other";
    const auto history_before = app.library_state_.last_played.size();
    auto shared = std::make_unique<RunningGamesMessage>(); shared->version = app.running_version_; shared->games = {store_copy, unrelated};
    app.onRunningGames(std::move(shared));
    if (app.game_sessions_.contains(gameKey(store_copy)) || !app.game_sessions_.contains(gameKey(unrelated)) ||
        app.game_sessions_.size() != 2 || app.library_state_.last_played.size() != history_before + 1) return 1787;
    return 0;
}
