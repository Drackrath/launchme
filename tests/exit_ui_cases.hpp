// The close delay changes window lifetime: repeated close/input must never skip
// the animation or launch a pending game. Rendering uses real Direct2D/WIC.
int LauncherUiTest::exitTransition(HWND window) {
    Launcher app;
    app.hwnd_ = window; app.execute_ = fakeExecute;
    if (!app.renderer_.initialize(window)) return 1900;
    app.pending_launch_ = true;
    app.enter_held_ = true;
    const auto launches = dispatched.size();
    if (!app.requestClose() || !app.closing() || app.exitPromptOpen() || app.pending_launch_ || app.enter_held_) return 1901;
    const auto started = app.close_started_;
    if (!app.requestClose() || app.close_started_ != started) return 1902;
    app.tick();
    PadReading pad{}; pad.connected = true; pad.buttons = XINPUT_GAMEPAD_A | pad_guide;
    app.controller(pad);
    if (app.pending_launch_ || app.quickOpen() || dispatched.size() != launches) return 1903;
    MSG close{};
    app.tickClose(started + std::chrono::milliseconds(750));
    if (!app.requestClose() || PeekMessageW(&close, window, WM_CLOSE, WM_CLOSE, PM_REMOVE)) return 1904;
    app.tickClose(started + std::chrono::milliseconds(1000)); // Hidden HWND: completion cannot depend on WM_PAINT.
    if (app.requestClose() || !PeekMessageW(&close, window, WM_CLOSE, WM_CLOSE, PM_REMOVE)) return 1905;
    app.tickClose(started + std::chrono::seconds(2));
    if (PeekMessageW(&close, window, WM_CLOSE, WM_CLOSE, PM_REMOVE)) return 1906;

    auto captures = userConfigDirectory() / L"exit-captures";
    wchar_t folder[4096]{};
    if (const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096); count && count < 4096) captures = folder;
    std::filesystem::create_directories(captures);
    Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.GetAddressOf())))) return 1907;
    float previous_dot_y = 0;
    uint64_t previous_brightness = UINT64_MAX;
    for (const int percent : {25, 65, 100}) {
        Renderer renderer;
        if (!renderer.initialize(window)) return 1908;
        ViewContext context;
        context.exit_progress = 0;
        GameCard card;
        card.game.kind = GameKind::all;
        std::vector<GameCard> cards{card};
        const auto file = captures / (L"exit-" + std::to_wstring(percent) + L".png");
        bool snapshot = false;
        if (!renderer.captureForTest(file, [&] {
            renderer.paint(cards, 0, false, 0, 1, context);
            snapshot = renderer.exitSnapshotForTest();
            context.exit_progress = percent / 100.0f;
            // Mutating the scene cannot replace the captured departing image.
            renderer.paint({}, 0, true, 10, 0, context);
        }) || !snapshot) return 1909;
        std::ifstream input(file, std::ios::binary);
        const std::vector<uint8_t> bytes(std::istreambuf_iterator<char>{input}, {});
        const auto image = decodeImage(wic.Get(), bytes, {});
        if (!image.valid()) return 1910;
        uint64_t brightness = 0, dot_weight = 0, dot_y = 0;
        for (uint32_t y = 0; y < image.height; ++y) for (uint32_t x = 0; x < image.width; ++x) {
            const size_t i = (static_cast<size_t>(y) * image.width + x) * 4;
            const unsigned sum = image.pixels[i] + image.pixels[i + 1] + image.pixels[i + 2];
            brightness += sum;
            if (std::abs(static_cast<int>(x) - static_cast<int>(image.width / 2)) <= 5 && sum > 600) {
                dot_weight += sum; dot_y += static_cast<uint64_t>(y) * sum;
            }
        }
        if (brightness >= previous_brightness || (percent == 100 && brightness != 0)) return 1911;
        previous_brightness = brightness;
        if (percent < 100) {
            if (!dot_weight) return 1912;
            const float position = static_cast<float>(dot_y) / static_cast<float>(dot_weight) / image.height;
            if (position <= previous_dot_y || (percent == 25 && position >= 0.4f) || (percent == 65 && position <= 0.6f)) return 1913;
            previous_dot_y = position;
        }
        // captureForTest recreates the target: a lost snapshot still fades fully to black.
        if (percent == 100 && (!renderer.captureForTest(captures / L"exit-recovered.png", [&] {
            context.exit_progress = 1;
            renderer.paint(cards, 0, false, 0, 1, context);
        }) || renderer.exitSnapshotForTest())) return 1914;
    }

    // Screen transitions (Library, Settings, Details, Downloads, Devices, Search, and Back):
    // fade-in, zoom and 4-edge lens bow over 0.35 s, without triggering on same-screen shelf changes.
    {
        Launcher screens;
        screens.hwnd_ = window; screens.execute_ = fakeExecute;
        if (!screens.renderer_.initialize(window)) return 1920;
        auto library = std::make_unique<LibraryMessage>(); library->account = 1;
        for (uint32_t i = 0; i < 12; ++i) {
            Game game{4000 + i, L"Transition game " + std::to_wstring(i)};
            game.last_played = 100;
            library->games.push_back(game);
        }
        SteamCollection col{L"Favorites", {4000, 4001}}; col.id = "fav";
        library->collections.push_back(col);
        screens.onLibrary(std::move(library));
        for (size_t i = 0; i < screens.public_library_.size(); ++i) {
            auto cover = std::make_unique<CoverMessage>();
            cover->key = gameKey(screens.public_library_[i]);
            cover->generation = screens.jobs_->generation.load();
            cover->bytes = pngFixture(static_cast<uint8_t>(60 + i * 12), 64, 64);
            screens.onCover(std::move(cover));
        }
        screens.screen_motion_for_test_ = true;
        // Same-screen shelf navigation does not start a full-screen transition.
        screens.showCategory(1);
        screens.syncScreenTransition();
        if (screens.screen_transition_ != 1.0f) return 1921;

        // Opening Library starts the transition and immediately places the grid cards at rest inside the screen.
        screens.openLibrary();
        screens.syncScreenTransition();
        if (screens.screen_ != ScreenMode::library || screens.screen_transition_ <= 0 || screens.screen_transition_ >= 0.2f ||
            screens.cards_.empty() || screens.cards_[0].size <= 0 || screens.cards_[0].opacity < 0.99f) return 1922;
        uint64_t prev_lib_brightness = 0;
        for (const int percent : {25, 60, 100}) {
            screens.screen_transition_ = percent / 100.0f;
            const auto file = captures / (L"screen-library-" + std::to_wstring(percent) + L".png");
            bool snap = false, bow = false;
            if (!screens.renderer_.captureForTest(file, [&] {
                screens.paint();
                snap = screens.renderer_.screenSnapshotForTest();
                bow = screens.renderer_.screenBowTargetForTest();
            })) return 1923;
            if (percent < 100 && (!snap || !bow)) return 1924;
            std::ifstream input(file, std::ios::binary);
            const std::vector<uint8_t> bytes(std::istreambuf_iterator<char>{input}, {});
            const auto image = decodeImage(wic.Get(), bytes, {});
            if (!image.valid()) return 1925;
            uint64_t brightness = 0;
            for (size_t i = 0; i < image.pixels.size(); i += 4)
                brightness += image.pixels[i] + image.pixels[i + 1] + image.pixels[i + 2];
            if (brightness <= prev_lib_brightness) return 1926;
            prev_lib_brightness = brightness;
        }

        // Opening Settings starts the transition and settles over ~0.35 s via tick().
        screens.openSettings();
        screens.syncScreenTransition();
        if (screens.screen_ != ScreenMode::settings || screens.screen_transition_ >= 0.2f) return 1927;
        uint64_t prev_set_brightness = 0;
        for (const int percent : {25, 60, 100}) {
            screens.screen_transition_ = percent / 100.0f;
            const auto file = captures / (L"screen-settings-" + std::to_wstring(percent) + L".png");
            bool snap = false, bow = false;
            if (!screens.renderer_.captureForTest(file, [&] {
                screens.paint();
                snap = screens.renderer_.screenSnapshotForTest();
                bow = screens.renderer_.screenBowTargetForTest();
            })) return 1928;
            if (percent < 100 && (!snap || !bow)) return 1929;
            std::ifstream input(file, std::ios::binary);
            const std::vector<uint8_t> bytes(std::istreambuf_iterator<char>{input}, {});
            const auto image = decodeImage(wic.Get(), bytes, {});
            if (!image.valid()) return 1930;
            uint64_t brightness = 0;
            for (size_t i = 0; i < image.pixels.size(); i += 4)
                brightness += image.pixels[i] + image.pixels[i + 1] + image.pixels[i + 2];
            if (brightness <= prev_set_brightness) return 1931;
            prev_set_brightness = brightness;
        }

        // Returning to Home via back() starts the transition and settles after ~0.35 s of ticks.
        screens.back();
        screens.syncScreenTransition();
        if (screens.screen_ != ScreenMode::shelves || screens.screen_transition_ >= 0.2f) return 1932;
        for (int step = 0; step < 5; ++step) {
            screens.last_frame_ = std::chrono::steady_clock::now() - std::chrono::milliseconds(40);
            screens.tick();
        }
        if (screens.screen_transition_ <= 0.4f || screens.screen_transition_ >= 1.0f) return 1933;
        for (int step = 0; step < 10; ++step) {
            screens.last_frame_ = std::chrono::steady_clock::now() - std::chrono::milliseconds(40);
            screens.tick();
        }
        if (screens.screen_transition_ != 1.0f) return 1934;
    }
    return 0;
}
