// Second-pad couch navigation, per-device toasts and the asynchronous census.
// Every connection is a fixture; no real hardware or game actions are required.
int LauncherUiTest::controllerConnections(HWND window) {
    const devices::Controller xbox{L"xbox-fixture", L"Xbox Series X|S", L"Bluetooth", devices::Picture::xbox, 85};
    const devices::Controller dualsense{L"dualsense-fixture", L"DualSense", L"USB", devices::Picture::dualsense, 45};
    const devices::Controller third{L"third-fixture", L"8BitDo Pro 2", L"USB", devices::Picture::eightbitdo_pro};
    const auto library = [](const wchar_t* name) {
        auto result = std::make_unique<LibraryMessage>(); result->account = 1;
        for (uint32_t id = 9100; id < 9103; ++id) {
            Game game{id, L"Couch fixture " + std::to_wstring(id)}; game.last_played = 100;
            result->games.push_back(std::move(game));
        }
        SteamCollection collection{name, {9101, 9102}}; collection.id = "couch-fixture";
        result->collections.push_back(std::move(collection));
        return result;
    };
    const auto receive = [](Launcher& app, std::vector<devices::Controller> pads) {
        auto message = std::make_unique<ControllersMessage>(); message->controllers = std::move(pads);
        app.onControllers(std::move(message));
    };
    const auto onCouch = [](const Launcher& app) {
        if (app.screen_ != ScreenMode::shelves) return false;
        if (app.xmbActive()) {
            const auto& tile = app.xmb_top_[app.xmb_column_];
            return tile.kind == GameKind::category && app.categories_[tile.app_id].id == "couch-fixture";
        }
        return app.categories_[app.category_].id == "couch-fixture";
    };
    const auto leaveCouch = [](Launcher& app) {
        if (app.xmbActive()) app.xmbFocus(0, -1);
        else if (app.flixActive()) app.flixFocus(0, 0);
        else app.showCategory(0);
    };
    for (const auto layout : {AppSettings::HomeLayout::shelves, AppSettings::HomeLayout::xmb, AppSettings::HomeLayout::flix, AppSettings::HomeLayout::metro}) {
        Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute;
        if (!app.renderer_.initialize(window)) return 1800;
        app.settings_.home_layout = layout; app.settings_.show_categories = true;
        // Two pads can be known before Steam/collections finish loading. A saved
        // library bookmark must not win over the automatic couch category.
        receive(app, {xbox, dualsense});
        if (!app.couch_category_pending_ || !app.cards_.empty()) return 1801;
        LibraryBookmark bookmark; bookmark.account = 1; bookmark.place = LibraryPlace::library;
        bookmark.category = "recent"; bookmark.selection = "steam:9100";
        app.library_state_.bookmark = bookmark; app.restore_pending_ = true;
        app.onLibrary(library(layout == AppSettings::HomeLayout::xmb ? L" CoUcH " : L"KaNaPa"));
        if (!onCouch(app) || app.couch_category_pending_ || app.restore_pending_) return 1802;
        leaveCouch(app);
        receive(app, {dualsense, xbox}); // Same controllers in another enumeration order.
        receive(app, {dualsense, xbox, third}); // The third pad does not re-pin the category.
        if (onCouch(app)) return 1803;
        receive(app, {xbox}); receive(app, {xbox, dualsense});
        if (!onCouch(app)) return 1804; // Disconnecting below two rearms it.
        // A hotplug while editing Settings is handled after the screen is closed.
        leaveCouch(app); receive(app, {xbox}); app.openSettings();
        receive(app, {xbox, dualsense});
        if (!app.settingsOpen() || !app.couch_category_pending_) return 1805;
        app.settings_state_.options = app.settings_state_.editing = false; app.back(); app.focusCouchCategory();
        if (!onCouch(app)) return 1806;
        // A second pad removed before returning cancels the deferred jump.
        leaveCouch(app); receive(app, {xbox}); app.openSettings();
        receive(app, {xbox, dualsense}); receive(app, {xbox});
        app.settings_state_.options = app.settings_state_.editing = false; app.back(); app.focusCouchCategory();
        if (onCouch(app) || app.couch_category_pending_) return 1807;
        // Hidden and absent collections are never resurrected by the shortcut.
        app.settings_.hidden_collections.insert(collectionKey(1, app.collections_source_.front()));
        app.buildShelves(); app.showCategory(0);
        receive(app, {xbox, dualsense});
        if (app.categories_.size() != 1 || app.couch_category_pending_) return 1808;
        receive(app, {xbox}); app.settings_.hidden_collections.clear();
        app.onLibrary(library(L"Other collection")); receive(app, {xbox, dualsense});
        if (app.category_ != 0 || app.couch_category_pending_) return 1809;
    }
    // Exercise the real worker/message route with an injectable enumerator; first
    // connection, second connection and removal each carry that pad's own picture.
    Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute;
    if (!app.renderer_.initialize(window)) return 1810;
    app.onLibrary(library(L"couch"));
    const auto count = std::make_shared<std::atomic_int>(1);
    const auto battery = std::make_shared<std::atomic_int>(85);
    app.read_controllers_ = [count, battery, xbox, dualsense] {
        std::vector<devices::Controller> pads{xbox};
        pads[0].battery = battery->load() < 0 ? std::nullopt : std::optional<int>{battery->load()};
        if (count->load() >= 2) pads.push_back(dualsense);
        return pads;
    };
    app.initializeControllerMonitor();
    const auto drain = [&] {
        MSG message{};
        while (PeekMessageW(&message, window, controllers_message, controllers_message, PM_REMOVE))
            app.onControllers(std::unique_ptr<ControllersMessage>(reinterpret_cast<ControllersMessage*>(message.lParam)));
    };
    const auto await = [&](size_t expected) {
        const auto deadline = GetTickCount64() + 3000;
        while (GetTickCount64() < deadline) {
            drain();
            if (app.connected_controllers_ == expected) return true;
            Sleep(10);
        }
        return false;
    };
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        const auto length = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
        if (!length || length >= 4096) return true;
        for (int i = 0; i < 35; ++i) {
            if (!app.notifications_.empty()) app.notifications_.front().age = 0.5f;
            app.last_frame_ -= std::chrono::milliseconds(50); app.tick();
        }
        if (app.notifications_.empty()) return false;
        app.notifications_.front().age = 1;
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    if (!await(1) || app.notifications_.size() != 1 || app.notifications_.back().controller_picture != devices::Picture::xbox ||
        app.notifications_.back().device_id != xbox.id || app.notifications_.back().battery != 85 ||
        !capture(L"controller-connected-xbox.png")) return 1811;
    receive(app, {xbox});
    if (app.notifications_.size() != 1) return 1812;
    // Battery-only census changes reach the UI but never become new connection notifications.
    for (const int level : {45, 15, 0, 100, -1, 85}) {
        const auto expected = level < 0 ? std::nullopt : std::optional<int>{level};
        const float age = app.notifications_.front().age;
        battery->store(level); app.controllerDeviceChanged(nullptr);
        const auto deadline = GetTickCount64() + 3000;
        do { drain(); if (app.connected_pads_.front().battery == expected) break; Sleep(10); } while (GetTickCount64() < deadline);
        if (app.connected_pads_.front().battery != expected || app.notifications_.size() != 1 ||
            app.notifications_.front().battery != expected || app.notifications_.front().age != age) return 1815;
        if (!capture((L"controller-battery-" + std::to_wstring(level) + L".png").c_str())) return 1816;
    }
    app.notifications_.clear(); count->store(2); app.controllerDeviceChanged(nullptr);
    if (!await(2) || !onCouch(app) || app.notifications_.size() != 1 || app.notifications_.back().subtitle != L"DualSense" ||
        app.notifications_.back().controller_picture != devices::Picture::dualsense || !capture(L"controller-connected-dualsense.png")) return 1813;
    app.notifications_.clear(); count->store(1); app.controllerDeviceChanged(nullptr);
    if (!await(1) || app.notifications_.size() != 1 || app.notifications_.back().connected ||
        app.notifications_.back().battery || app.notifications_.back().controller_picture != devices::Picture::dualsense ||
        !capture(L"controller-disconnected-dualsense.png")) return 1814;
    // Two controllers with the same picture must still keep their own battery reading.
    app.notifications_.clear();
    auto second = xbox; second.id = L"second-xbox"; second.battery = 10;
    receive(app, {xbox, second});
    auto first = xbox; first.battery = 75; second.battery = 20;
    receive(app, {first, second});
    if (app.notifications_.size() != 1 || app.notifications_.front().device_id != second.id || app.notifications_.front().battery != 20) return 1817;
    return 0;
}
