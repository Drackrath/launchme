int LauncherUiTest::settingsDesign(HWND window) {
    Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute;
    if (!app.renderer_.initialize(window)) return 1800;
    auto library = std::make_unique<LibraryMessage>(); library->account = 1; library->games = {Game{620, L"Portal 2"}};
    app.onLibrary(std::move(library)); app.openSettings();
    const auto view = app.renderer_.viewport();
    const auto layout = settings_ui::Layout::of(view.width, view.height, app.settings_state_, app.settings_rows_);
    const int big = layout.layout_index;
    // Interfejs starts with "Tryb TV": the tall layout row first.
    if (big != 0 || app.settings_rows_[0].id != settings_ui::Id::home_layout || app.settings_rows_[big].section.empty()) return 1801;
    const auto large = layout.row(big, 0);
    if (large.bottom - large.top < 250 || layout.row(big + 1, 0).top <= large.bottom) return 1801;
    const auto baseline = app.settings_.home_layout;
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[32768]{};
        if (!GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, static_cast<DWORD>(std::size(folder)))) return true;
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    for (int mode = 0; mode < 3; ++mode) {
        const auto tile = layout.layoutOption(big, 0, mode);
        if (tile.left < large.left || tile.right > large.right || tile.bottom > large.bottom || tile.top <= large.top + 60) return 1802;
        const auto x = (tile.left + tile.right) * 0.5f * view.scale, y = (tile.top + tile.bottom) * 0.5f * view.scale;
        app.mouseMove(x, y);
        const auto started = app.settings_preview_.started;
        app.mouseMove(x + 1, y + 1);
        if (app.settings_preview_.option != mode || app.settings_preview_.started != started || app.settings_.home_layout != baseline || app.settings_dirty_) return 1803;
        app.settings_preview_.started = GetTickCount64() - 600;
        if (!capture((L"settings-preview-" + std::to_wstring(mode) + L"-horizontal.png").c_str())) return 1804;
        app.settings_preview_.started = GetTickCount64() - 1250;
        if (!capture((L"settings-preview-" + std::to_wstring(mode) + L"-vertical.png").c_str())) return 1805;
        app.settings_preview_.started = GetTickCount64() - 2200;
        app.tick();
        const auto finished = app.settings_preview_.started;
        app.mouseMove(x + 2, y + 2);
        if (app.settings_preview_.tick(GetTickCount64()) || app.settings_preview_.seconds(GetTickCount64()) != 2 || app.settings_preview_.started != finished) return 1806;
        app.mouseLeave(); if (app.settings_preview_.option != -1) return 1807;
        app.mouseMove(x, y); if (app.settings_preview_.started == finished || !app.settings_preview_.tick(GetTickCount64())) return 1808;
        app.mouseLeave();
    }
    // Keyboard/pad focus uses the same preview state. Moving to another row stops it.
    app.settings_state_.options = true; app.settings_state_.selected[0] = big; app.settingsPreviewFocus();
    if (app.settings_preview_.option != 0) return 1809;
    app.settingsAdjust(1);
    if (app.settings_preview_.option != 1 || app.settings_.home_layout != AppSettings::HomeLayout::xmb) return 1810;
    app.settingsMove(1); if (app.settings_preview_.option != -1) return 1811;
    app.settingsMove(100);
    const auto xmb_rows = settings_ui::Layout::of(view.width, view.height, app.settings_state_, app.settings_rows_);
    const auto last = xmb_rows.row(app.settings_state_.row(), app.settings_state_.offset());
    if (last.bottom > xmb_rows.listBottom() + 0.1f || last.top < xmb_rows.listTop() ||
        xmb_rows.hitRow(last.left + 10, last.top + 10, static_cast<int>(app.settings_rows_.size()), app.settings_state_.offset()) != app.settings_state_.row()) return 1812;
    for (auto category : {settings_ui::Category::library, settings_ui::Category::media, settings_ui::Category::audio,
                          settings_ui::Category::controllers, settings_ui::Category::companion, settings_ui::Category::system}) {
        app.settings_state_.selectCategory(static_cast<int>(category)); app.buildSettingsRows(); app.settingsPreviewFocus();
        app.settings_motion_.valid = false; // Capture the settled category, not an in-flight rail transition.
        if (app.settings_preview_.option != -1) return 1813;
        // Every category is split into titled sections, its first row opening one.
        if (app.settings_rows_.empty() || app.settings_rows_[0].section.empty()) return 1816;
        // Dźwięk and Kontrolery open with their tiles: the two sources, the pads and "Sparuj kontroler".
        if (category == settings_ui::Category::audio && (app.settings_rows_[0].control != settings_ui::Control::sources ||
            app.settings_rows_[0].tiles.size() != 2)) return 1817;
        if (category == settings_ui::Category::controllers && (app.settings_rows_[0].control != settings_ui::Control::pads ||
            app.settings_rows_[0].tiles.empty() || !app.settings_rows_[0].tiles.back().pair)) return 1818;
        const auto plain = settings_ui::Layout::of(view.width, view.height, static_cast<int>(category));
        if (plain.row(1, 0).bottom - plain.row(1, 0).top != settings_ui::Layout::row_height) return 1814;
        if (!capture((L"settings-unified-" + std::to_wstring(static_cast<int>(category)) + L".png").c_str())) return 1815;
    }
    return 0;
}
