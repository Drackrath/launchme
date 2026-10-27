// End-to-end native UI with an injected Aurelia protocol, no service calls or real account actions.
int LauncherUiTest::aureliaIntegration(HWND window) {
    using C = aurelia::Command; using A = aurelia_ui::Action; using P = aurelia_ui::Page;
    Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute;
    app.settings_.companion = app.settings_.glance = app.settings_.launch_animation = false;
    app.settings_.tour_done = true; app.settings_.aurelia = false;
    const auto settings = parseAppSettings("aurelia=1\naurelia_intro_done=1\naurelia_standalone=902\naurelia_standalone=../evil\n");
    if (!settings.aurelia || !settings.aurelia_intro_done || settings.aurelia_standalone.size() != 1 || !settings.aurelia_standalone.contains("902")) return 10350;
    app.overlay_backend_.foreground = [window] { return window; };
    if (!app.renderer_.initialize(window)) return 10300;
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{}; const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
        app.notifications_.clear();
        if (!count || count >= 4096) { app.paint(); return true; }
        std::filesystem::create_directories(folder);
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    const auto pump = [&] {
        MSG m{}; while (PeekMessageW(&m, window, remote_message, remote_message, PM_REMOVE))
            app.onRemote(std::unique_ptr<RemoteMessage>(reinterpret_cast<RemoteMessage*>(m.lParam)));
    };
    const auto wait = [&](const auto& ready) {
        for (const auto until = GetTickCount64() + 5000; GetTickCount64() < until;) { pump(); if (ready()) return true; Sleep(5); }
        return false;
    };
    struct Fake {
        std::mutex mutex; std::vector<aurelia::Request> calls;
        std::atomic_bool send_failure{false}, download{false}, stop_download{false}, helper_failure{false};
        std::atomic_int helper_calls{0};
        std::atomic_bool logged_in{true}, bad_health{false};
        std::atomic_uint64_t account{76561198000000001ULL};
        int count(C c) { std::lock_guard lock(mutex); return static_cast<int>(std::count_if(calls.begin(), calls.end(), [c](const auto& r) { return r.command == c; })); }
        aurelia::Request last(C c) { std::lock_guard lock(mutex); for (auto it = calls.rbegin(); it != calls.rend(); ++it) if (it->command == c) return *it; return {}; }
    };
    auto fake = std::make_shared<Fake>();
    app.aurelia_transport_ = [fake](const aurelia::Request& r, const aurelia::Cancel& cancel, const aurelia::Event& event) {
        { std::lock_guard lock(fake->mutex); fake->calls.push_back(r); }
        const auto ok = [](std::string_view s) { return aurelia::Reply{true, false, *json::parse(s)}; };
        switch (r.command) {
        case C::health: return fake->bad_health ? ok(R"({"unexpected":true})") : ok(std::string("{\"logged_in\":") +
            (fake->logged_in ? "true" : "false") + ",\"steam_id\":" + std::to_string(fake->account.load()) + ",\"account\":\"Aurelia fixture\"}");
        case C::library: return ok(R"([{"app_id":902,"name":"The Moonlit Archive","is_installed":true,"is_owned":true,"install_path":"D:\\SteamLibrary\\Fixture"},{"app_id":906,"name":"Library worker fixture","is_installed":false,"is_owned":true}])");
        case C::available: return ok(R"({"app_id":902,"available":true,"pinned":true})");
        case C::launch_targets: return ok(R"({"app_id":902,"targets":[{"platform":"windows","steam_mode":"none","available":true}]})");
        case C::play: return ok(R"({"app_id":902,"status":"finished"})");
        case C::downgrade: return ok(R"({"app_id":902,"status":"downgraded","pinned":true})");
        case C::cloud_list: return ok(R"({"app_id":902,"files":[{"filename":"save/slot-1.dat","size":8192},{"filename":"settings.ini","size":123}]})");
        case C::cloud_sync: return r.text.empty() ? ok(R"({"status":"conflicts","conflicts":[{"filename":"slot-1.dat"}]})") : ok(R"({"status":"ok","conflicts":[],"skipped":[],"failed":[]})");
        case C::dlc: return ok(R"({"dlc":[{"app_id":903,"name":"The Moonlit Archive","owned":true,"installed":true,"disabled":false},{"app_id":904,"name":"Beyond the Fog","owned":true,"installed":false,"disabled":false},{"app_id":905,"name":"Soundtrack","owned":false,"installed":false,"disabled":false}]})");
        case C::manifests: return ok(R"([{"depot_id":903,"depot_name":"Windows content","branch":"public","manifest_id":8593343465227540543}])");
        case C::branches: return ok(R"({"branches":["public","beta"]})");
        case C::libraries: return ok(R"({"libraries":[{"path":"D:\\SteamLibrary","free_bytes":900000000000}]})");
        case C::workshop_browse: return ok(R"({"items":[{"id":12345678901234567,"title":"Moonlit paths"}]})");
        case C::friends: return ok(R"([{"steam_id":76561198000000002,"persona_name":"Luna","persona_state":1,"game_name":"The Moonlit Archive"}])");
        case C::chat_history: return ok(R"([{"timestamp":100,"from_self":false,"message":"Got time for a new adventure?"},{"timestamp":101,"from_self":true,"message":"Yes! Just checking my cloud saves."}])");
        case C::chat_live:
            if (event) { event(*json::parse(R"({"event":"ready"})")); event(*json::parse(R"({"event":"message","timestamp":102,"text":"Great. See you in a minute!"})")); }
            while (!cancel()) Sleep(5);
            return aurelia::Reply{false, true};
        case C::chat_send: return fake->send_failure ? aurelia::Reply{false, false, {}, L"Fixture offline"} : ok(R"({"sent":true,"server_timestamp":103})");
        case C::install:
            fake->download = true;
            if (event) event(*json::parse(R"({"event":"progress","state":"downloading","bytes_downloaded":500,"total_bytes":1000})"));
            while (!cancel() && !fake->stop_download) Sleep(5);
            fake->download = false;
            return aurelia::Reply{false, true, {}, L"Cancelled"};
        case C::cancel_install: fake->stop_download = true; return ok(R"({"event":"stopping"})");
        case C::downloads: return fake->download ? ok(R"([{"app_id":902,"name":"The Moonlit Archive","downloaded_bytes":500,"total_bytes":1000,"is_downloading":true}])") : ok("[]");
        default: return ok(R"({"status":"ok"})");
        }
    };
    app.aurelia_version_ = L"v0.1.40";
    app.aurelia_get_ = [](const std::wstring& url, const std::wstring&, size_t) {
        if (url != aurelia::latest_url) return HttpResponse{};
        const auto body = std::string(R"({"tag_name":"v0.1.40","assets":[{"name":"aurelia_windows_x86_64.exe","browser_download_url":"https://github.com/Drackrath/Aurelia/releases/download/v0.1.40/aurelia_windows_x86_64.exe","size":8693760,"digest":"sha256:)") + std::string(64, 'a') + R"("}]})";
        return HttpResponse{200, std::vector<uint8_t>(body.begin(), body.end())};
    };
    app.aurelia_installer_ = [fake](const aurelia::Release& r, const aurelia::Cancel&, const std::function<void(uint64_t, uint64_t)>& progress) {
        ++fake->helper_calls;
        if (r.version != L"v0.1.40" || r.size != 8693760) return aurelia::Reply{false, false, {}, L"Bad release"};
        progress(r.size, r.size);
        return fake->helper_failure ? aurelia::Reply{false, false, {}, L"Fixture digest mismatch"} : aurelia::Reply{true};
    };
    Game game{902, L"The Moonlit Archive"}; game.installed = true; game.last_played = 100;
    auto library = std::make_unique<LibraryMessage>(); library->account = 76561198000000001ULL; library->games = {game};
    app.onLibrary(std::move(library)); app.showCategory(0); app.arrived_ = 2; app.detail_ = 1;
    for (auto& card : app.cards_) card.opacity = 1;
    app.openAurelia(nullptr, true);
    if (!wait([&] { return !app.aurelia_ui_.busy; }) || app.topModal() != Launcher::Modal::aurelia) return 10301;
    if (!capture(L"aurelia-onboarding-tv.png")) return 10302;
    dispatched.clear(); app.aureliaActivate({{}, {}, {}, A::repository});
    if (dispatched != std::vector<std::wstring>{L"https://github.com/Drackrath/Aurelia"}) return 10351;
    app.installAurelia();
    if (!wait([&] { return !app.aurelia_helper_busy_; }) || fake->helper_calls != 1 || app.aurelia_version_ != L"v0.1.40") return 10347;
    fake->helper_failure = true; app.installAurelia();
    if (!wait([&] { return !app.aurelia_helper_busy_; }) || app.aurelia_setup_status_ != L"Fixture digest mismatch" || app.aurelia_version_ != L"v0.1.40") return 10348;
    app.aureliaActivate({{}, {}, {}, A::toggle});
    if (!app.settings_.aurelia || app.aurelia_active_ || app.aurelia_ui_.status.empty()) return 10303; // Restart, no half-switched services.
    app.closeAurelia();
    if (!app.settings_.aurelia_intro_done || app.topModal() != Launcher::Modal::none) return 10304;
    app.aurelia_active_ = app.aurelia_authenticated_ = true; app.aurelia_account_ = app.account_;
    if (!app.menuRows().aurelia) return 10305;
    app.openAurelia(&game); if (!wait([&] { return !app.aurelia_ui_.busy; })) return 10306;
    if (!capture(L"aurelia-game-tv.png")) return 10307;
    app.aureliaActivate({{}, {}, {}, A::steam});
    if (!app.settings_.aurelia_standalone.contains("902")) return 10308;
    app.aureliaActivate({{}, {}, {}, A::steam});
    app.aureliaPage(P::cloud); if (!wait([&] { return !app.aurelia_ui_.busy; })) return 10309;
    if (app.aurelia_cloud_[902].state != aurelia::CloudState::files) return 10310;
    app.aureliaActivate({{}, {}, {}, A::sync});
    if (!wait([&] { return !app.aurelia_ui_.busy; }) || app.aurelia_cloud_[902].state != aurelia::CloudState::conflicts) return 10311;
    if (!capture(L"aurelia-cloud-conflict-tv.png")) return 10312;
    app.aureliaActivate({L"Use cloud saves", L"Replace local saves", {}, A::resolve_cloud});
    if (!app.aurelia_ui_.confirmation || fake->count(C::cloud_sync) != 1) return 10313;
    app.modalKey(VK_ESCAPE, false); if (app.aurelia_ui_.confirmation || fake->count(C::cloud_sync) != 1) return 10314;
    app.aureliaActivate({L"Use cloud saves", {}, {}, A::resolve_cloud}, true);
    if (!wait([&] { return !app.aurelia_ui_.busy; }) || app.aurelia_cloud_[902].state != aurelia::CloudState::synced) return 10315;
    app.aureliaPage(P::dlc); if (!wait([&] { return !app.aurelia_ui_.busy; })) return 10316;
    if (app.aurelia_ui_.rows.size() != 4 || app.aurelia_ui_.rows[3].action != A::none) return 10317;
    if (!capture(L"aurelia-dlc-tv.png")) return 10318;
    app.aureliaActivate(app.aurelia_ui_.rows[1]);
    if (!wait([&] { return !app.aurelia_ui_.busy; }) || fake->last(C::disable_dlc).app != 903) return 10319;
    app.aureliaPage(P::versions); if (!wait([&] { return !app.aurelia_ui_.busy; })) return 10320;
    app.aurelia_ui_.input = L"903 8593343465227540543"; app.aureliaSubmit();
    if (!app.aurelia_ui_.confirmation || fake->count(C::downgrade)) return 10321;
    if (!capture(L"aurelia-downgrade-confirm-tv.png")) return 10322;
    app.aurelia_ui_.hover = -5; app.modalKey(VK_RETURN, false);
    if (!wait([&] { return !app.aurelia_ui_.busy; }) || fake->last(C::downgrade).id != 8593343465227540543ULL) return 10323;
    app.aureliaPage(P::versions); if (!wait([&] { return !app.aurelia_ui_.busy; }) || !capture(L"aurelia-versions-tv.png")) return 10324;
    app.aureliaPage(P::workshop); if (!wait([&] { return !app.aurelia_ui_.busy; })) return 10325;
    app.aureliaActivate(app.aurelia_ui_.rows[1]);
    if (!wait([&] { return !app.aurelia_ui_.busy; }) || fake->last(C::workshop_subscribe).id != 12345678901234567ULL) return 10326;
    app.aureliaPage(P::game); if (!wait([&] { return !app.aurelia_ui_.busy; })) return 10327;
    app.aureliaOperation({C::install, 902}, game);
    if (!wait([&] { return fake->download && app.aurelia_downloads_.contains(902) && app.aurelia_downloads_[902].progress.percent() == 50; })) return 10328;
    app.closeAurelia(); std::vector<downloads_ui::Row> rows;
    if (app.collectDownloads(rows).size() != 1 || rows.front().store != L"Aurelia") return 10329;
    app.openAurelia(&game); if (!wait([&] { return !app.aurelia_ui_.busy; })) return 10330;
    app.aureliaActivate({{}, {}, {}, A::cancel});
    if (!wait([&] { return app.aurelia_operations_.empty(); }) || !fake->stop_download) return 10331;
    app.aureliaPage(P::friends); if (!wait([&] { return !app.aurelia_ui_.busy; })) return 10332;
    app.aureliaActivate(app.aurelia_ui_.rows.back());
    if (!wait([&] { return app.aurelia_ui_.live && app.aurelia_ui_.messages.size() == 3; }) || app.aurelia_ui_.friend_name != L"Luna") return 10333;
    app.aurelia_ui_.input = L"--help & hello, Luna!"; app.aureliaSubmit();
    if (!wait([&] { return !app.aurelia_ui_.sending; }) || !app.aurelia_ui_.input.empty() || fake->last(C::chat_send).text != L"--help & hello, Luna!") return 10334;
    if (!capture(L"aurelia-chat-tv.png")) return 10335;
    fake->send_failure = true; app.aurelia_ui_.input = L"Keep my draft"; app.aureliaSubmit();
    if (!wait([&] { return !app.aurelia_ui_.sending; }) || app.aurelia_ui_.input != L"Keep my draft") return 10336;
    // Modal input cannot leak into the library. Pad keyboard uses the very same message field.
    const auto selection = app.selected_; app.onCharacter(L'!');
    if (app.aurelia_ui_.input.back() != L'!' || app.selected_ != selection || !app.textFieldActive()) return 10337;
    app.openKeyboard(); if (!app.keyboard_.open) return 10338;
    app.keyboardBackspace(); app.closeKeyboard();
    if (app.aurelia_ui_.input != L"Keep my draft") return 10339;
    app.closeAurelia(); if (!app.aurelia_chat_cancel_->load()) return 10340;
    app.aureliaOpenChat(76561198000000002ULL, L"Luna");
    if (!wait([&] { return app.aurelia_ui_.live; }) || app.aurelia_ui_.input != L"Keep my draft") return 10354;
    app.closeAurelia();
    // A stale page result cannot reopen or replace the next page.
    app.openAurelia(&game); app.aureliaPage(P::dlc); app.aureliaPage(P::versions);
    if (!wait([&] { return !app.aurelia_ui_.busy; }) || app.aurelia_ui_.page != P::versions) return 10341;
    app.closeAurelia();
    app.desktop_mode_ = true; app.screen_ = ScreenMode::shelves; app.buildDesktopSidebar(); app.rebuildCards(true);
    app.openAurelia(); if (!wait([&] { return !app.aurelia_ui_.busy; }) || !capture(L"aurelia-settings-desktop.png")) return 10342;
    app.aureliaOpenChat(76561198000000002ULL, L"Luna");
    if (!wait([&] { return app.aurelia_ui_.live; }) || !capture(L"aurelia-chat-desktop.png")) return 10343;
    app.closeAurelia(); app.aurelia_cloud_[902] = {aurelia::CloudState::conflicts, 2, 1};
    if (!capture(L"aurelia-cloud-card-desktop.png")) return 10344;
    app.desktop_mode_ = false; app.openDetails(game); app.details_loading_ = false;
    if (!capture(L"aurelia-cloud-card-tv.png")) return 10345;
    // Disabling is persisted, but the running session remains coherent until restart.
    app.toggleAurelia(); if (app.settings_.aurelia || !app.aurelia_active_) return 10346;
    app.startAureliaLibraryWorker();
    if (!wait([&] { return app.installed_library_.size() == 2 && fake->count(C::library) > 0; })) return 10349;
    // The integration is additive: normal Play still dispatches Steam, per-game opt-in dispatches Aurelia.
    dispatched.clear(); Game routed{902, L"Routing fixture"};
    app.launchGame(routed); app.finishLaunch();
    if (dispatched != std::vector<std::wstring>{L"steam://rungameid/902"} || fake->count(C::play)) return 10352;
    app.game_sessions_.clear(); app.game_activity_jobs_->active.clear();
    app.settings_.aurelia_standalone.insert("902"); dispatched.clear();
    app.launchGame(routed);
    if (!wait([&] { return !app.aurelia_playing_.contains(902); }) || !dispatched.empty() || fake->last(C::play).steam) return 10353;
    app.settings_.aurelia_auto_cloud.insert("902");
    const auto before_play = fake->count(C::play);
    app.launchGame(routed);
    if (!wait([&] { return !app.aurelia_playing_.contains(902) && !app.aurelia_ui_.busy; }) || fake->count(C::play) != before_play ||
        app.aurelia_cloud_[902].state != aurelia::CloudState::conflicts) return 10355;
    app.closeAurelia();
    fake->logged_in = false; app.steam_rescan_->wake();
    if (!wait([&] { return !app.aurelia_authenticated_ && app.aurelia_setup_status_.find(L"offline") != std::wstring::npos; }) || app.installed_library_.size() != 2) return 10356;
    fake->bad_health = true; app.steam_rescan_->wake();
    if (!wait([&] { return app.aurelia_setup_status_ == tr(L"Nieprawidłowa odpowiedź Aurelii"); }) || app.account_ != 76561198000000001ULL || app.installed_library_.size() != 2) return 10357;
    fake->bad_health = false; fake->logged_in = true; app.steam_rescan_->wake();
    if (!wait([&] { return app.aurelia_authenticated_; })) return 10358;
    app.aureliaOpenChat(76561198000000002ULL, L"Luna");
    if (!wait([&] { return app.aurelia_ui_.live; })) return 10359;
    app.aurelia_ui_.input = L"Private account draft";
    fake->account = 76561198000000003ULL; app.steam_rescan_->wake();
    if (!wait([&] { return app.account_ == 76561198000000003ULL; }) || !app.aurelia_chat_drafts_.empty() || app.aurelia_ui_.open) return 10360;
    return 0;
}
