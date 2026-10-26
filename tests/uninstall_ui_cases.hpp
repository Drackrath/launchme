// Odinstaluj: last and red in the game's menu and in Details' "…"; Steam's own dialog; GOG's Inno
// uninstaller silently after Launchme's question; no real registry or uninstaller is touched.
int LauncherUiTest::uninstallGames(HWND window) {
    using context_menu::Item;
    Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute;
    if (!app.renderer_.initialize(window)) return 9400;
    app.settings_.enabled_pc_sources = {"gog"}; app.settings_.show_uninstalled = true;
    Game gog; gog.kind = GameKind::store; gog.source = GameSource::gog; gog.source_id = "1207658924";
    gog.name = L"GOG uninstall fixture"; gog.install_directory = L"C:\\Synthetic GOG game"; gog.last_played = 100;
    gog.play_action = {L"C:\\Synthetic GOG game\\game.exe"};
    Game shortcut = gog; shortcut.kind = GameKind::nonsteam; shortcut.source = GameSource::steam; shortcut.source_id.clear();
    shortcut.name = L"Shortcut fixture"; shortcut.app_id = 0;
    app.store_library_ = {gog};
    auto library = std::make_unique<LibraryMessage>(); library->account = 11; library->games = {Game{620, L"Portal 2"}};
    app.onLibrary(std::move(library));
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[32768]{};
        if (!GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, static_cast<DWORD>(std::size(folder)))) return true;
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    std::vector<GameAction> started;
    std::atomic_int finished{0};
    UninstallResult outcome = UninstallResult::done;
    app.installed_programs_ = [] {
        return std::vector<InstalledProgram>{{L"1207658924_is1", L"GOG uninstall fixture", L"GOG.com",
            L"\"C:\\Synthetic GOG game\\unins000.exe\"", L"C:\\Synthetic GOG game"}};
    };
    app.silent_uninstall_ = [&](const GameAction& action, void*, const std::function<bool()>&) {
        started.push_back(action); ++finished; return outcome;
    };
    const auto pump = [&] {
        for (const auto end = GetTickCount64() + 4000; GetTickCount64() < end; Sleep(2)) {
            MSG message{};
            if (PeekMessageW(&message, window, store_change_message, store_change_message, PM_REMOVE)) {
                app.onStoreChange(std::unique_ptr<StoreChangeMessage>(reinterpret_cast<StoreChangeMessage*>(message.lParam)));
                return true;
            }
        }
        return false;
    };
    const auto toast = [&](const wchar_t* title) {
        return std::any_of(app.notifications_.begin(), app.notifications_.end(), [&](const Notification& n) { return n.title == tr(title) || n.subtitle == tr(title); });
    };
    const auto frame = [&](int ms) { app.last_frame_ -= std::chrono::milliseconds(ms); app.tick(); };
    const auto settle = [&] { for (int i = 0; i < 40; ++i) frame(50); };
    const auto menuOn = [&](const Game& game) {
        app.dismissMenu(); app.openPcLibrary(game.source); settle();
        for (size_t i = 0; i < app.cards_.size(); ++i) if (gameKey(app.cards_[i].game) == gameKey(game)) {
            app.focusCard(i); settle(); app.openContextMenu(); settle(); return app.menu_open_;
        }
        return false;
    };
    // The game's menu: Odinstaluj is its last row.
    if (!menuOn(gog)) return 9401;
    auto rows = app.menuRows();
    if (!rows.uninstall || context_menu::row(Item::uninstall, rows) != context_menu::rows(rows) - 1) return 9402;
    if (!capture(L"menu-uninstall.png")) return 9403;
    // Details' "…": artwork, launch options, folder, then Odinstaluj, drawn as many rows as it navigates.
    app.dismissMenu(); app.openDetails(gog);
    app.detail_action_ = 1; app.enterDown(false); app.enterUp();
    rows = app.menuRows();
    if (!app.menu_open_ || !rows.detail_options || context_menu::row(Item::uninstall, rows) != context_menu::rows(rows) - 1) return 9404;
    app.menu_progress_ = 1;
    if (!capture(L"details-options-uninstall.png")) return 9405;
    // GOG: Launchme asks, defaulting to the folder; nothing has run yet.
    app.menu_selected_ = context_menu::row(Item::uninstall, rows); app.menuAccept();
    if (!app.exit_prompt_ || *app.exit_prompt_ != 1 || app.prompt_kind_ != Launcher::PromptKind::uninstall || app.menu_open_ ||
        app.uninstall_silent_.target != L"C:\\Synthetic GOG game\\unins000.exe" ||
        app.uninstall_silent_.arguments != L"/VERYSILENT /SUPPRESSMSGBOXES /NORESTART" || !started.empty()) return 9407;
    if (!capture(L"uninstall-prompt.png")) return 9408;
    // Cancel leaves everything as it was.
    app.exitPromptKey(VK_ESCAPE, false);
    if (app.exit_prompt_ || app.prompt_kind_ != Launcher::PromptKind::exit || !started.empty()) return 9409;
    // Odinstaluj: the silent uninstaller on a worker, then "Gra odinstalowana".
    app.read_pc_library_ = [](const PcLibraryRequest&, const std::function<bool()>&) { return PcLibraryMessage{}; };
    app.runtime_started_ = true; app.pc_initial_ready_ = true;
    app.uninstallGame(gog); app.exitPromptMove(-1); app.exitPromptAccept();
    if (!toast(L"Odinstalowywanie…") || !pump() || started.size() != 1 || started[0].arguments.find(L"/VERYSILENT") == std::wstring::npos ||
        !toast(L"Gra odinstalowana")) return 9410;
    // UAC declined: no toast about it; a failed uninstaller says so.
    app.notifications_.clear(); outcome = UninstallResult::refused;
    app.uninstallGame(gog); app.exitPromptMove(-1); app.exitPromptAccept();
    if (!pump() || started.size() != 2 || toast(L"Gra odinstalowana") || toast(L"Nie udało się odinstalować gry")) return 9411;
    outcome = UninstallResult::failed;
    app.uninstallGame(gog); app.exitPromptMove(-1); app.exitPromptAccept();
    if (!pump() || started.size() != 3 || !toast(L"Nie udało się odinstalować gry")) return 9412;
    // No registered uninstaller: the store's client, never a guessed command.
    app.installed_programs_ = [] { return std::vector<InstalledProgram>{}; };
    const auto before = dispatched.size();
    app.uninstallGame(gog);
    if (app.exit_prompt_ || started.size() != 3) return 9413;
    if (dispatched.size() != before + 1 || dispatched.back() != L"https://www.gog.com/account") return 9414;
    // Steam: its own dialog through steam://uninstall.
    app.dismissMenu(); app.openLibrary();
    Game portal{620, L"Portal 2"};
    if (!menuOn(portal) || !app.menuRows().uninstall) return 9415;
    app.menu_selected_ = context_menu::row(Item::uninstall, app.menuRows()); app.menuAccept();
    if (dispatched.empty() || dispatched.back() != L"steam://uninstall/620" || app.exit_prompt_) return 9416;
    // Removing a shortcut from the list is not uninstalling, nor is a game that is not installed.
    Game missing = gog; missing.installed = false;
    if (app.canUninstall(shortcut) || app.canUninstall(missing)) return 9417;
    app.runtime_started_ = false;
    return 0;
}
