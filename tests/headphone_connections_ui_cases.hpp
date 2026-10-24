// Read-only hotplug census on its real worker/message route; no device panel is opened.
int LauncherUiTest::headphoneConnections(HWND window) {
    Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute;
    if (!app.renderer_.initialize(window)) return 1850;
    const auto state = std::make_shared<std::atomic_int>(0);
    const auto reads = std::make_shared<std::atomic_int>(0);
    const auto battery = std::make_shared<std::atomic_int>(-1);
    app.read_headphones_ = [state, reads, battery]() -> std::optional<headphones::State> {
        const int value = state->load();
        reads->fetch_add(1);
        if (value < 0) return {};
        return headphones::State{value != 0, value && battery->load() >= 0 ? std::optional<int>{battery->load()} : std::nullopt};
    };
    app.initializeHeadphoneMonitor();
    int received = 0;
    const auto drain = [&] {
        MSG message{};
        while (PeekMessageW(&message, window, headphones_message, headphones_message, PM_REMOVE)) {
            ++received;
            app.onHeadphones(std::unique_ptr<HeadphonesMessage>(reinterpret_cast<HeadphonesMessage*>(message.lParam)));
        }
    };
    const auto await = [&](int expected) {
        const auto deadline = GetTickCount64() + 3000;
        while (GetTickCount64() < deadline) {
            drain();
            if (received >= expected) return true;
            Sleep(10);
        }
        return false;
    };
    if (!await(1) || !app.notifications_.empty()) return 1851; // Disconnected at startup.
    state->store(1); app.devicesChanged();
    if (!await(2) || !app.headphones_connected_ || app.notifications_.size() != 1 ||
        app.notifications_.back().kind != NotificationKind::headphones ||
        app.notifications_.back().title != L"Teraz podłączone" ||
        app.notifications_.back().subtitle != L"Audio-Technica ATH-M50xBT2") return 1852;
    auto duplicate = std::make_unique<HeadphonesMessage>(); duplicate->connected = true;
    app.onHeadphones(std::move(duplicate));
    if (app.notifications_.size() != 1) return 1853;
    // A late battery report must enrich the same toast without restarting its lifetime.
    app.notifications_.front().age = 0.75f;
    battery->store(65); app.devicesChanged();
    if (!await(3) || app.notifications_.size() != 1 || app.notifications_.front().battery != 65 ||
        app.notifications_.front().age != 0.75f) return 1858;
    // A census failure leaves the connection intact and cannot rearm the toast.
    const int before = reads->load();
    state->store(-1);
    const auto deadline = GetTickCount64() + 3000;
    for (int census = 1; census <= 2; ++census) { // Two failed device-change censuses in a row.
        app.devicesChanged();
        while (reads->load() < before + census && GetTickCount64() < deadline) Sleep(10);
    }
    drain();
    if (reads->load() < before + 2 || received != 3 || !app.headphones_connected_ || app.notifications_.size() != 1 ||
        app.notifications_.front().battery != 65) return 1854;
    state->store(0); app.devicesChanged();
    if (!await(4) || app.headphones_connected_ || app.notifications_.size() != 1 || app.notifications_.front().battery) return 1855;
    auto image = std::make_unique<HeadphonesMessage>(); image->image = true; image->bytes = pngFixture();
    app.onHeadphones(std::move(image));
    if (!app.headphone_image_ready_ || app.headphones_connected_ || app.notifications_.size() != 1) return 1856;
    app.notifications_.clear(); state->store(1); app.devicesChanged();
    if (!await(5) || !app.headphones_connected_ || app.notifications_.size() != 1 || app.notifications_.front().battery != 65) return 1857;
    app.notifications_.front().age = 1;
    app.paint(); // Same circular photo / battery-ring layout as controller notifications.
    for (const int level : {65, 40, 10, 0, 100, -1}) {
        auto update = std::make_unique<HeadphonesMessage>(); update->connected = true;
        update->battery = level < 0 ? std::nullopt : std::optional<int>{level};
        app.onHeadphones(std::move(update));
        if (app.notifications_.size() != 1 || app.notifications_.front().battery != (level < 0 ? std::nullopt : std::optional<int>{level}) ||
            app.notifications_.front().age != 1) return 1859;
        wchar_t folder[4096]{};
        if (GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096) &&
            !app.renderer_.captureForTest(std::filesystem::path(folder) / (L"headphones-battery-" + std::to_wstring(level) + L".png"), [&] { app.paint(); })) return 1860;
    }
    return 0;
}
