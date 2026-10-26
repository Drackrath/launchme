int LauncherUiTest::organization(HWND window) {
    Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute; app.settings_.accept_action = AppSettings::AcceptAction::launch; // These cases pick cards to launch them.
    app.settings_.launch_animation = false;
    if (!app.renderer_.initialize(window)) return 3000;
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[32768]{};
        if (!GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, static_cast<DWORD>(std::size(folder)))) return true;
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    Game steam{620, L"Portal 2"}, gog; gog.name = L"Other game"; gog.kind = GameKind::store; gog.source = GameSource::gog; gog.source_id = "123";
    app.settings_.enabled_pc_sources.insert("gog"); app.store_library_ = {gog};
    auto library = std::make_unique<LibraryMessage>(); library->account = 11; library->games = {steam};
    library->collections = {{L"Steam puzzles", {620}, "user-collections.puzzles"}};
    app.onLibrary(std::move(library));
    const auto shelf = [&](const std::string& id) -> const Launcher::Category* {
        for (const auto& c : app.categories_) if (c.id == id) return &c;
        return nullptr;
    };
    if (shelf("user-collections.favorite")) return 3001;
    app.openDetails(gog);
    const auto view = app.renderer_.viewport();
    const auto star = details_layout::Layout{view.width, view.height}.rect(app.detailsActions(), app.favoriteDetailAction());
    app.click((star.left + 10) * view.scale, (star.top + 10) * view.scale, false);
    if (!app.isFavorite(gog) || !shelf("user-collections.favorite")) return 3002;
    if (!capture(L"organization-favorite.png")) return 3020;
    app.launch(); // Focus remains on the star: toggles off, never dispatches a game.
    if (app.isFavorite(gog) || shelf("user-collections.favorite")) return 3003;
    app.openCollectionManager(gog);
    const auto choose = [&](const std::string& key) {
        const auto it = std::find_if(app.library_dialog_.rows.begin(), app.library_dialog_.rows.end(), [&](const auto& r) { return r.key == key; });
        if (it == app.library_dialog_.rows.end()) return false;
        app.library_dialog_.focus = static_cast<int>(it - app.library_dialog_.rows.begin()); app.libraryDialogKey(VK_RETURN, false); return true;
    };
    if (!choose("user-collections.puzzles") || !shelf("user-collections.puzzles") || shelf("user-collections.puzzles")->games.size() != 2) return 3004;
    if (!capture(L"organization-membership.png")) return 3021;
    app.buildShelves(); // A rescan preserves Steam plus the local overlay.
    if (shelf("user-collections.puzzles")->games.size() != 2) return 3005;
    if (!choose("@new")) return 3006;
    for (wchar_t ch : std::wstring(L"Moja kolekcja")) app.onCharacter(ch);
    if (!choose("@type-local") || !choose("@save")) return 3006;
    const auto id = app.library_dialog_.collection;
    if (!id.starts_with("local:") || !shelf(id) || shelf(id)->games.size() != 1) return 3007;
    app.back(); app.openCollectionManager();
    if (!choose(id) || !choose("@rename")) return 3008;
    app.onCharacter(L'X'); app.libraryDialogKey(VK_RETURN, false); app.libraryDialogKey(VK_RETURN, false);
    if (!shelf(id) || shelf(id)->title != L"X") return 3009;
    app.paint();
    if (!choose("@delete") || !choose("@delete") || shelf(id)) return 3010;
    app.back();
    if (app.libraryDialogOpen()) return 3011;
    // Proven non-Steam activity accrues locally; Steam's own playtime stays its authority.
    app.beginGameSession(gog); app.beginGameSession(steam);
    const auto activity = [&](uint64_t now) {
        auto message = std::make_unique<GameActivityMessage>(); message->generation = app.game_activity_jobs_->generation.load();
        message->observed_at = now; message->activities = {{gameKey(gog), GameActivity::running}, {gameKey(steam), GameActivity::running}};
        app.onGameActivity(std::move(message));
    };
    for (uint64_t now = 1000; now <= 61000; now += 500) activity(now);
    if (app.localPlaytime(gog) != 1 || app.localPlaytime(steam) != 0) return 3012;
    app.openDetails(gog); app.paint();
    app.game_sessions_.clear();
    gog.name = L"Portal 2"; gog.play_action.target = L"gog-test://launch";
    app.store_library_ = {gog}; app.buildShelves(); app.openLibrary();
    if (app.cards_.size() != 1 || app.cards_[0].game.version_group.empty() || !activePlatformName(app.cards_[0].game).empty()) return 3013;
    app.accept();
    if (!app.libraryDialogOpen() || app.library_dialog_.page != library_manager_ui::Page::versions || app.library_dialog_.versions.size() != 2) return 3014;
    app.library_dialog_.focus = 2; app.libraryDialogKey(VK_RETURN, false);
    if (!app.library_dialog_.remember) return 3015;
    app.library_dialog_.focus = 1; app.paint();
    if (!capture(L"organization-versions.png")) return 3022;
    app.libraryDialogKey(VK_RETURN, false);
    if (app.libraryDialogOpen() || !app.pending_launch_ || app.pending_game_.source != GameSource::gog) {
        std::cerr << "Version pick: dialog=" << app.libraryDialogOpen() << " pending=" << app.pending_launch_ << " source=" << sourceSlug(app.pending_game_.source) << '\n';
        return 3016;
    }
    app.pending_launch_ = false; app.accept();
    if (app.libraryDialogOpen() || !app.pending_launch_ || app.pending_game_.source != GameSource::gog) return 3017;
    app.pending_launch_ = false; app.openDetails(app.cards_[0].game);
    app.detail_action_ = app.versionsDetailAction(); app.launch();
    if (!app.libraryDialogOpen()) return 3018;
    app.library_dialog_.focus = 0; app.libraryDialogKey(VK_RETURN, false);
    if (app.detail_games_[0].source != GameSource::steam || !app.detail_version_selected_ || app.pending_launch_) return 3019;
    if (!capture(L"organization-details-versions.png")) return 3023;
    // Actual Win32 window style and the first System control, on the test window only.
    const int system = static_cast<int>(settings_ui::Category::system);
    app.openSettings(); app.settings_state_.selectCategory(system); app.buildSettingsRows();
    // Aktualizacje come first; the window's row heads "Okno i język".
    const auto windowed = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& r) { return r.id == settings_ui::Id::windowed; });
    if (app.settings_rows_.empty() || app.settings_rows_[0].id != settings_ui::Id::update || windowed == app.settings_rows_.end()) return 3024;
    const auto style = GetWindowLongPtrW(window, GWL_STYLE); RECT original{}; GetWindowRect(window, &original);
    app.fullscreen_ = true; app.previous_rect_ = {100, 100, 1380, 820};
    app.settings_state_.options = true; app.settings_state_.selected[system] = static_cast<int>(windowed - app.settings_rows_.begin()); app.settingsActivate();
    if (!app.settings_.windowed || app.fullscreen_ || (GetWindowLongPtrW(window, GWL_STYLE) & WS_OVERLAPPEDWINDOW) != WS_OVERLAPPEDWINDOW) return 3025;
    app.toggleFullscreen();
    if (app.settings_.windowed || !app.fullscreen_ || !(GetWindowLongPtrW(window, GWL_STYLE) & WS_POPUP)) return 3026;
    SetWindowLongPtrW(window, GWL_STYLE, style);
    SetWindowPos(window, nullptr, original.left, original.top, original.right - original.left, original.bottom - original.top, SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(window, SW_HIDE);
    // New collection type is explicit. Steam collections are usable offline, before native creation.
    app.openCollectionManager(steam);
    if (!choose("@new") || !app.library_dialog_.steam_collection || app.library_dialog_.rows.size() != 3 ||
        !app.library_dialog_.rows[0].radio || !app.library_dialog_.rows[0].checked || app.library_dialog_.rows[1].checked) return 3027;
    for (wchar_t ch : std::wstring(L"Offline Steam collection")) app.onCharacter(ch);
    if (!capture(L"collection-type-steam.png")) return 3028;
    if (!choose("@type-local") || app.library_dialog_.steam_collection || app.library_dialog_.rows[0].checked || !app.library_dialog_.rows[1].checked) return 3029;
    if (!capture(L"collection-type-local.png")) return 3030;
    if (!choose("@type-steam") || !choose("@save")) return 3031;
    const auto steam_id = app.library_dialog_.collection;
    const auto steam_scope = std::to_string(app.account_) + ':' + steam_id;
    if (!steam_id.starts_with("user-collections.uc-launchme-") || !shelf(steam_id) || shelf(steam_id)->games.size() != 1 ||
        !app.library_state_.collections.creates.contains(steam_scope)) return 3032;
    const auto offline = loadLibraryState(userConfigDirectory() / L"library-state.ini");
    if (!offline || !offline->collections.creates.contains(steam_scope) || !offline->collections.members.at(steam_scope).at("steam:620")) return 3033;
    auto unavailable = std::make_unique<SteamSyncMessage>(); unavailable->result.account = app.account_; unavailable->result.error = "unavailable";
    app.onSteamSync(std::move(unavailable));
    if (!shelf(steam_id) || !app.library_state_.collections.creates.contains(steam_scope)) return 3034;
    app.back(); app.openCategoryLibrary(steam_id);
    if (app.cards_.size() != 1 || app.cards_[0].game.app_id != 620) return 3035;
    // Existing local collections can be converted without selecting their games again.
    app.openCollectionManager(steam); if (!choose("@new")) return 3036;
    for (wchar_t ch : std::wstring(L"Move fixture")) app.onCharacter(ch);
    if (!choose("@type-local") || !choose("@save")) return 3037;
    const auto local = app.library_dialog_.collection;
    app.back(); app.openCollectionManager(); if (!choose(local) || !choose("@steam")) return 3038;
    const auto moved = app.library_dialog_.collection;
    if (app.library_state_.collections.names.contains(local) || !moved.starts_with("user-collections.uc-launchme-") ||
        !shelf(moved) || shelf(moved)->games.size() != 1) return 3039;
    app.back();
    return 0;
}
