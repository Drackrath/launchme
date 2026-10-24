// Pad: Y opens the game's menu; X shows the on-screen keyboard over the screen's text field.
int LauncherUiTest::onscreenKeyboard(HWND window) {
    // Layout model: rows wrap horizontally, vertical moves keep the column.
    osk::State model; osk::open(model);
    if (osk::focused(model).character != L'a') return 3800;
    for (int i = 0; i < 11; ++i) osk::move(model, 1, 0);
    if (osk::focused(model).character != L'a' || osk::move(model, 0, 0)) return 3801;
    osk::move(model, 0, 1);
    if (osk::focused(model).kind != osk::Kind::shift) return 3802;
    osk::move(model, 0, 1);
    if (osk::focused(model).kind != osk::Kind::layer || osk::move(model, 0, 1)) return 3803;
    osk::focus(model, 1, 5); osk::move(model, 0, 1); osk::move(model, 0, 1);
    if (osk::focused(model).kind != osk::Kind::space) return 3804;
    osk::move(model, 0, -1);
    if (osk::focused(model).character != L'b') return 3805; // The column under "g" is kept through the space bar.

    Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute;
    app.settings_.launch_animation = false;
    app.overlay_backend_.foreground = [window] { return window; };
    if (!app.renderer_.initialize(window)) return 3810;
    auto library = std::make_unique<LibraryMessage>(); library->account = 1;
    library->games = {Game{3901, L"Portal"}, Game{3902, L"Xenon"}};
    library->games[0].last_played = 20; library->games[1].last_played = 10;
    app.onLibrary(std::move(library));
    app.showCategory(0); app.focusCard(0);
    PadReading pad{true, L"keyboard-fixture"}; app.controller(pad); // Connection: swallowed.
    const auto press = [&](WORD button) { pad.buttons = button; app.controller(pad); pad.buttons = 0; app.controller(pad); };

    // Y: the focused game's menu.
    press(XINPUT_GAMEPAD_Y);
    if (!app.menu_open_) return 3811;
    press(XINPUT_GAMEPAD_Y);
    if (app.menu_open_) return 3812;

    // X on Home: search with the keyboard; A types, Y a space, B erases, B on empty leaves the search.
    press(XINPUT_GAMEPAD_X);
    if (app.screen_ != ScreenMode::search || !app.keyboardOpen() || !app.textFieldActive()) return 3813;
    osk::focus(app.keyboard_, 0, 9); press(XINPUT_GAMEPAD_A); // "p"
    osk::focus(app.keyboard_, 2, 0); press(XINPUT_GAMEPAD_A); // Shift, one letter.
    osk::focus(app.keyboard_, 0, 8); press(XINPUT_GAMEPAD_A); press(XINPUT_GAMEPAD_A); // "O", then "o"
    if (app.search_query_ != L"pOo" || app.keyboard_.shift) return 3814;
    press(XINPUT_GAMEPAD_Y);
    if (app.search_query_ != L"pOo ") return 3815;
    // The other layer: digits and Polish letters, upper case with shift too.
    osk::focus(app.keyboard_, 3, 0); press(XINPUT_GAMEPAD_A);
    osk::focus(app.keyboard_, 2, 0); press(XINPUT_GAMEPAD_A);
    osk::focus(app.keyboard_, 1, 1); press(XINPUT_GAMEPAD_A);
    osk::focus(app.keyboard_, 0, 1); press(XINPUT_GAMEPAD_A);
    if (app.search_query_ != L"pOo Ć2") return 3816;
    {
        auto captures = userConfigDirectory();
        wchar_t folder[4096]{};
        if (const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096); count && count < 4096) captures = folder;
        app.keyboard_progress_ = 1;
        if (!app.renderer_.captureForTest(captures / L"onscreen-keyboard.png", [&] { app.paint(); })) return 3817;
    }
    for (int i = 0; i < 6; ++i) press(XINPUT_GAMEPAD_B);
    if (!app.search_query_.empty() || !app.keyboardOpen()) return 3818;
    press(XINPUT_GAMEPAD_B); // Backspace on an empty search leaves it, and the keyboard with it.
    if (app.screen_ == ScreenMode::search || app.keyboardOpen()) return 3819;

    // X in the library: its search field; Start ("Gotowe") applies the filter and hides the keyboard.
    app.openLibrary();
    press(XINPUT_GAMEPAD_X);
    if (!app.libraryTyping() || !app.keyboardOpen()) return 3820;
    osk::focus(app.keyboard_, 2, 2); press(XINPUT_GAMEPAD_A); // "x"
    if (app.library_controls_.query != L"x") return 3821;
    press(XINPUT_GAMEPAD_START);
    if (app.keyboardOpen() || app.libraryTyping() || app.library_controls_.query != L"x") return 3822;
    // X again with the field closed reopens it; X while shown hides only the keyboard.
    press(XINPUT_GAMEPAD_X);
    if (!app.keyboardOpen() || !app.libraryTyping()) return 3823;
    press(XINPUT_GAMEPAD_X);
    if (app.keyboardOpen() || !app.libraryTyping()) return 3824;
    // A click on a key types it, as the pad does.
    press(XINPUT_GAMEPAD_X);
    const auto view = app.renderer_.viewport();
    const auto key = osk::Layout{view.width, view.height}.key(app.keyboard_, 0, 0); // "q"
    app.click((key.left + 5) * view.scale, (key.top + 5) * view.scale, false);
    if (app.library_controls_.query != L"xq" || !app.keyboardOpen()) return 3825;
    // LB/RB move the insertion point; typing and B work there.
    press(XINPUT_GAMEPAD_LEFT_SHOULDER);
    if (app.library_controls_.caret() != 1 || app.keyboardPreviewCaret() != 1) return 3826;
    osk::focus(app.keyboard_, 1, 0); press(XINPUT_GAMEPAD_A); // "a" between the letters.
    if (app.library_controls_.query != L"xaq" || app.library_controls_.caret() != 2) return 3827;
    press(XINPUT_GAMEPAD_B);
    if (app.library_controls_.query != L"xq" || app.library_controls_.caret() != 1) return 3828;
    press(XINPUT_GAMEPAD_LEFT_SHOULDER); press(XINPUT_GAMEPAD_B); // At the start: nothing before it.
    if (app.library_controls_.query != L"xq" || app.library_controls_.caret() != 0) return 3829;
    press(XINPUT_GAMEPAD_RIGHT_SHOULDER); press(XINPUT_GAMEPAD_RIGHT_SHOULDER); press(XINPUT_GAMEPAD_RIGHT_SHOULDER);
    if (app.library_controls_.caret() != 2) return 3830;
    // The home search too, and a new search starts at its end again.
    app.closeKeyboard(); app.showCategory(0);
    press(XINPUT_GAMEPAD_X);
    osk::focus(app.keyboard_, 0, 0); press(XINPUT_GAMEPAD_A); press(XINPUT_GAMEPAD_A); // "qq"
    press(XINPUT_GAMEPAD_LEFT_SHOULDER);
    osk::focus(app.keyboard_, 0, 1); press(XINPUT_GAMEPAD_A); // "w"
    if (app.search_query_ != L"qwq" || app.searchCaret() != 2) return 3831;
    {
        auto captures = userConfigDirectory();
        wchar_t folder[4096]{};
        if (const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096); count && count < 4096) captures = folder;
        app.keyboard_progress_ = 1; app.ambient_time_ = 0.1f; // A visible caret.
        if (!app.renderer_.captureForTest(captures / L"onscreen-keyboard-caret.png", [&] { app.paint(); })) return 3836;
    }
    app.closeKeyboard(); app.back(); app.openSearch();
    if (!app.search_query_.empty() || app.searchCaret() != 0 || app.search_after_) return 3832;
    app.back();

    // A field entered from the pad brings the keyboard up by itself, once per entry; from the
    // keyboard or mouse it does not.
    app.autoKeyboard(); // Settle the "previous frame" state.
    app.pad_input_ = true; app.openSearch(); app.autoKeyboard();
    if (!app.keyboardOpen()) return 3833;
    app.closeKeyboard(); app.autoKeyboard();
    if (app.keyboardOpen()) return 3834; // Hidden with X: stays hidden in the same field.
    app.back(); app.autoKeyboard();
    app.noteDesktopInput(); app.openSearch(); app.autoKeyboard();
    if (app.keyboardOpen()) return 3835;
    app.back(); app.autoKeyboard();
    return 0;
}
