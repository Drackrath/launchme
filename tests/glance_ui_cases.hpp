#include <xinput.h>

int LauncherUiTest::glanceHub(HWND window, bool live) {
    Launcher app;
    app.hwnd_ = window; app.execute_ = fakeExecute;
    app.browse_ = [](const std::wstring& url) { dispatched.push_back(url); };
    app.overlay_backend_.foreground = [window] { return window; };
    app.settings_.launch_animation = false;
    if (!app.renderer_.initialize(window)) return 6000;
    auto library = std::make_unique<LibraryMessage>(); library->account = 1;
    const uint32_t live_ids[] = {570,730,440,620,4000,646570,1145360,1091500};
    const wchar_t* live_names[] = {L"Dota 2",L"Counter-Strike 2",L"Team Fortress 2",L"Portal 2",L"Garry's Mod",L"Slay the Spire",L"Hades",L"Cyberpunk 2077"};
    for (uint32_t i = 0; i < 8; ++i) { Game game{live ? live_ids[i] : 100+i,live ? live_names[i] : L"Game "+std::to_wstring(i)}; game.last_played = 1000-i; library->games.push_back(game); }
    app.onLibrary(std::move(library)); app.showCategory(0); app.navigate(2);
    app.snapCardsForTransition(); app.detail_ = 1;
    for (size_t i = 0; i < app.cards_.size(); ++i) app.renderer_.setCover(i,pngFixture(static_cast<uint8_t>(30+i*20),64,64),{});
    app.renderer_.setHero(app.selected_,pngFixture(90,320,180));
    const auto capture = [&](const wchar_t* name, bool settled = true) {
        if (settled) for (int i = 0; i < 40; ++i) app.tickGlance(0.025f);
        wchar_t folder[4096]{};
        if (!GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR",folder,4096)) return true;
        return app.renderer_.captureForTest(std::filesystem::path(folder)/name,[&] { app.paint(); });
    };
    if (!app.glanceAvailable() || !capture(L"glance-home.png")) return 6001;
    app.paint();
    if (!app.renderer_.glance_frosted_for_test) return 6100;
    app.renderer_.setGlass(0); app.paint();
    if (app.renderer_.glance_frosted_for_test) return 6101;
    app.renderer_.setGlass(0.7f);
    // Neither vertical direction adds the shortcut to the shelf's focus order.
    ViewContext hints;
    app.inputHints(hints); if (hints.hints.empty()) return 6002;
    app.changeCategory(-1);
    if (app.glanceOpen() || !app.quick_open_) return 6003;
    app.inputHints(hints); if (!hints.hints.empty() || app.glanceAvailable()) return 6004;
    app.quick_progress_ = 1;
    if (!capture(L"glance-bottom-menu.png")) return 6005;
    app.closeQuick(); app.quick_progress_ = 0.4f;
    app.inputHints(hints); if (!hints.hints.empty()) return 6006;
    if (!app.glanceAvailable(true) || app.glanceAvailable() || app.glance_shortcut_progress_!=0) return 6120;
    app.tickGlance(0.075f);
    if (app.glance_shortcut_progress_<=0 || app.glance_shortcut_progress_>=1 || !capture(L"glance-menu-fade-quarter.png",false)) return 6121;
    const auto quarter = app.glance_shortcut_progress_;
    app.tickGlance(0.075f);
    if (app.glance_shortcut_progress_<=quarter || app.glance_shortcut_progress_>=1 || !capture(L"glance-menu-fade-half.png",false)) return 6122;
    app.quick_open_ = true; app.tickGlance(0.016f);
    if (app.glance_shortcut_progress_!=0) return 6123; // Reopening midway resets the next fade-in.
    app.quick_open_ = false;
    app.quick_progress_ = 0;
    app.inputHints(hints); if (hints.hints.empty()) return 6007;
    app.changeCategory(1); if (app.glanceOpen()) return 6008;
    app.showCategory(0); app.navigate(2-static_cast<int>(app.selected_));
    const auto selected = app.selected_;

    std::string feed = "{\"new_releases\":{\"items\":[";
    for (int i = 0; i < 9; ++i) {
        if (i) feed += ',';
        feed += "{\"id\":"+std::to_string(700+i)+",\"name\":\"Nowa przygoda "+std::to_string(i+1)+"\",\"large_capsule_image\":\"https://shared.akamai.steamstatic.com/fixture.jpg\"}";
    }
    feed += "]},\"coming_soon\":{\"items\":[]}}";
    const auto fixture = pngFixture(135,320,180);
    const DWORD ui_thread = GetCurrentThreadId();
    const auto wrong_thread = std::make_shared<std::atomic_bool>(false);
    if (!live) app.glance_get_ = [feed,fixture,ui_thread,wrong_thread](const std::wstring& url,const auto&,size_t) {
        if (GetCurrentThreadId() == ui_thread) wrong_thread->store(true);
        if (url.find(L"/search/results/")!=url.npos) {
            const std::string empty=R"({"success":1,"start":0,"total_count":0,"results_html":""})";
            return HttpResponse{200,{empty.begin(),empty.end()}};
        }
        if (glance::imageUrl(url)) return HttpResponse{200,fixture};
        Sleep(25);
        if (const auto at = url.find(L"appid="); at != url.npos) {
            const auto id = std::stoul(url.substr(at+6));
            const std::string json = "{\"appnews\":{\"appid\":"+std::to_string(id)+",\"newsitems\":[{\"appid\":"+std::to_string(id)+
                ",\"gid\":\""+std::to_string(id)+"01\",\"title\":\"Nowa aktualizacja i wydarzenie w Twojej grze\",\"url\":\"https://store.steampowered.com/news/app/"+
                std::to_string(id)+"\",\"date\":1700000000,\"feedname\":\"steam_community_announcements\"}]}}";
            return HttpResponse{200,{json.begin(),json.end()}};
        }
        if (url.find(L"/api/") != url.npos) return HttpResponse{200,{feed.begin(),feed.end()}};
        const std::string rss = R"(<rss><channel><item><title>Jak powstają światy gier: rozmowa z twórcami</title><link>https://www.eurogamer.pl/fixture</link><pubDate>Tue, 13 Oct 2026 08:59:39 +0000</pubDate></item></channel></rss>)";
        return HttpResponse{200,{rss.begin(),rss.end()}};
    };
    const auto wait = [&] {
        const auto end = GetTickCount64()+(live ? 45000 : 5000);
        do {
            MSG message{};
            while (PeekMessageW(&message,window,glance_message,glance_message,PM_REMOVE))
                app.onGlance(std::unique_ptr<GlanceMessage>(reinterpret_cast<GlanceMessage*>(message.lParam)));
            if (!app.glance_busy_) return !wrong_thread->load();
            Sleep(5);
        } while (GetTickCount64()<end);
        return false;
    };
    const auto view = app.renderer_.viewport(); const auto tile = glance_ui::shortcut(view.width, 0, app.glanceTop());
    app.click((tile.left+30)*view.scale,(tile.top+30)*view.scale,false);
    if (!app.glanceOpen() || !app.glance_.loading || !capture(L"glance-loading.png") || !wait()) return 6009;
    if (app.glance_.section != static_cast<int>(glance::Section::owned) || app.glance_.feed.items.empty()) return 6102;
    if (!capture(L"glance-personal.png")) return 6103;
    if (live) {
        // Explicit manual capture only; CTest always uses the offline fixtures below.
        for (int section = 0; section < glance::section_count; ++section) {
            app.glanceSection(section);
            if (!wait() || app.glance_.feed.unavailable || app.glance_.feed.items.empty()) return 6050+section;
            app.glance_.focus = 0;
            if (!capture((L"glance-live-"+std::to_wstring(section)+L".png").c_str())) return 6060+section;
            if (section==static_cast<int>(glance::Section::upcoming)) {
                const auto recent=std::find_if(app.glance_.feed.items.begin(),app.glance_.feed.items.end(),[](const auto& item) { return item.release.recent; });
                if (recent!=app.glance_.feed.items.end()) {
                    app.glance_.focus=static_cast<int>(recent-app.glance_.feed.items.begin());
                    app.glance_.scroll=glance_ui::Layout{view.width,view.height}.reveal(app.glance_.focus,static_cast<int>(app.glance_.feed.items.size()),app.glance_.scroll,&app.glance_);
                    if (!capture(L"glance-live-recent.png")) return 6065;
                }
            }
        }
        app.back(); return 0;
    }
    if (app.glance_.feed.games_total!=8 || app.glance_.feed.games_checked!=8 || app.glance_.feed.items[0].app_id!=100) return 6104;
    if (app.glance_.card_heights[0] >= glance_ui::Layout::card_height || app.glance_.card_heights[0] < 240) return 6114;
    app.glance_hover_ = true;
    app.tickGlance(0.025f); // A hub does not animate the hidden shortcut.
    if (app.glance_hover_motion_.value != 0) return 6115;
    app.glance_hover_ = false;
    // Non-Steam and hidden entries cannot enter the source roster; new owners never reuse an aggregate.
    Game external{900,L"Other store"}; external.source = GameSource::gog;
    Game hidden{901,L"Hidden"}; hidden.visibility = GameVisibility::private_game;
    app.installed_library_.push_back(external); app.installed_library_.push_back(hidden);
    if (app.glanceGames().size()!=8) return 6105;
    app.installed_library_.pop_back(); app.installed_library_.pop_back();
    app.glanceStepSection(1); // Personalized tab is first, then the original Steam releases.
    if (!wait() || app.glance_.section!=0) return 6106;
    if (app.glance_.content_age!=0 || !app.tickGlance(0.08f) || app.glance_.appearance(0)<=0 || app.glance_.appearance(0)>=1) return 6107;
    if (!capture(L"glance-enter-mid.png",false)) return 6108;
    if (app.glance_.feed.items.size()!=9 || app.glance_.loading) return 6010;
    app.changeCategory(1); // Tabs -> first article.
    if (app.glance_.focus!=0 || !capture(L"glance-hub.png")) return 6011;
    app.navigate(1); app.accept();
    if (dispatched.empty() || dispatched.back()!=L"https://store.steampowered.com/app/701/" || app.pending_launch_) return 6012;
    app.changeCategory(1); app.changeCategory(1);
    if (app.glance_.focus!=7 || app.glance_.scroll<=0) return 6013;
    if (!app.tickGlance(0.016f) || app.glance_.drawScroll()>=app.glance_.scroll || app.glance_.drawScroll()<=0) return 6109;
    const glance_ui::Layout layout{view.width,view.height};
    const auto moving_card = layout.visualCard(3,app.glance_);
    if (layout.hit((moving_card.left+moving_card.right)/2,(moving_card.top+moving_card.bottom)/2,app.glance_)!=3) return 6110;
    if (!capture(L"glance-scroll-mid.png",false)) return 6111;
    app.settings_.screen_transitions = false; app.tickGlance(0.016f);
    if (app.glance_.drawScroll()!=app.glance_.scroll || app.glance_.appearance(0)!=1) return 6112;
    app.settings_.screen_transitions = true;
    const auto generation = app.glance_generation_;
    app.glanceSection(1); app.glanceSection(2); app.glanceSection(3); // Only the newest pending tab matters.
    if (!wait() || app.glance_.section!=3 || app.glance_.feed.items.size()!=1 || app.glance_.feed.items[0].source!=L"Eurogamer.pl") return 6014;
    auto late = std::make_unique<GlanceMessage>(); late->generation = generation; late->feed.items.push_back({L"Old",L"https://www.eurogamer.pl/old"});
    app.onGlance(std::move(late));
    if (app.glance_.feed.items[0].title == L"Old" || !capture(L"glance-news.png")) return 6015;
    app.glanceSection(1); if (!wait()) return 6130;
    auto timeline=std::make_unique<GlanceMessage>(); timeline->generation=app.glance_generation_; timeline->section=1;
    SYSTEMTIME calendar_now{}; GetLocalTime(&calendar_now);
    const auto calendar_today=std::chrono::sys_days(std::chrono::year(calendar_now.wYear)/calendar_now.wMonth/calendar_now.wDay);
    const int days[]={1,7,29,0,-3,-6};
    for (int i=0;i<6;++i) {
        glance::Item item; item.title=L"Premiera gry "+std::to_wstring(i+1); item.url=L"https://store.steampowered.com/app/"+std::to_wstring(800+i)+L"/";
        const std::chrono::year_month_day day{calendar_today+std::chrono::days(days[i])};
        item.release.precision=glance::ReleasePrecision::day; item.release.year=int(day.year()); item.release.month=unsigned(day.month()); item.release.day=unsigned(day.day());
        item.release.available=true; item.release.recent=i>=3; timeline->feed.items.push_back(std::move(item));
    }
    app.onGlance(std::move(timeline));
    for (size_t i=0;i<6;++i) app.renderer_.setGlanceThumbnail(i,shareImage(fixture));
    if (!app.glance_.timeline() || glance_ui::Layout{view.width,view.height}.columns(&app.glance_)!=1 || !capture(L"glance-release-timeline.png")) return 6131;
    app.renderer_.resetGlanceThumbnails(6);
    if (!capture(L"glance-timeline-stars.png")) return 6140;
    app.changeCategory(1); app.changeCategory(1);
    if (app.glance_.focus!=1) return 6132;
    app.accept(); if (dispatched.back()!=L"https://store.steampowered.com/app/801/") return 6133;
    for (int i=0;i<4;++i) app.changeCategory(1);
    if (app.glance_.focus!=5 || app.glance_.scroll<=0 || !capture(L"glance-release-recent.png")) return 6134;
    app.back(); if (app.glanceOpen() || app.selected_!=selected) return 6016;

    // R2 fires once, never on a held reconnection or after returning while still held.
    PadReading pad{true,L"fixture-dualsense",pad_right_trigger}; pad.name = L"DualSense";
    app.controller(pad); if (app.glanceOpen()) return 6017;
    pad.buttons = 0; app.controller(pad); pad.buttons = pad_right_trigger; app.controller(pad);
    if (!app.glanceOpen() || !wait()) return 6018;
    app.back(); app.controller(pad); if (app.glanceOpen()) return 6019;
    pad.buttons = 0; app.controller(pad);
    if (!capture(L"glance-home-pad.png")) return 6020;
    app.quick_open_ = true; pad.buttons = pad_right_trigger; app.controller(pad);
    if (app.glanceOpen()) return 6021;
    app.quick_open_ = false;
    app.openSettings(); app.settings_state_.category = static_cast<int>(settings_ui::Category::system); app.buildSettingsRows();
    const auto row = std::find_if(app.settings_rows_.begin(),app.settings_rows_.end(),[](const auto& r) { return r.id==settings_ui::Id::glance; });
    if (row == app.settings_rows_.end() || row->section != tr(L"Funkcje")) return 6022; // System → Funkcje, first of its section.
    app.settings_state_.options = true; app.settings_state_.selected[app.settings_state_.category] = static_cast<int>(row-app.settings_rows_.begin());
    app.settingsActivate(); app.saveSettings();
    if (app.settings_.glance || loadAppSettings().glance) return 6023;
    app.showCategory(0); app.openGlance(); if (app.glanceOpen() || app.glanceAvailable()) return 6024;
    // XMB: G still opens it; its tile is one of the row's (home_widgets_ui_cases.hpp), nothing floats.
    app.settings_.glance = true; app.settings_.home_layout = AppSettings::HomeLayout::xmb;
    if (!app.glanceAvailable() || app.homeWidgetSpots().glance) return 6025;
    app.settings_.home_layout = AppSettings::HomeLayout::shelves;
    app.power_menu_ = 0; if (app.glanceAvailable()) return 6026; app.power_menu_.reset();
    app.keyboard_.open = true; if (app.glanceAvailable()) return 6027; app.keyboard_.open = false;
    app.openGlance(); app.glanceSection(2); app.back();
    if (!wait() || app.glanceOpen() || !app.glance_cancel_->load()) return 6028;
    // A temporary device panel must not eat a feed response or leave a stuck spinner on return.
    app.openGlance(); app.screen_ = ScreenMode::devices; app.device_return_ = ScreenMode::glance;
    if (!wait() || app.glance_.loading || app.glance_.feed.items.empty()) return 6029;
    app.back(); if (!app.glanceOpen()) return 6030;
    app.back();
    app.openGlance(); app.back();
    auto account = std::make_unique<LibraryMessage>(); account->account = 2; account->games = {Game{999,L"New account game"}};
    app.onLibrary(std::move(account));
    if (!wait() || !app.glance_.feed.items.empty() || app.glanceGames().size()!=1 || app.glanceGames()[0].app_id!=999) return 6113;
    // Browser focus owns the pad; B closes it without moving the underlying Glance selection.
    app.openGlance(); if (!wait()) return 6135;
    app.glance_.focus=0;
    app.browser_=std::make_unique<BrowserWindow>(); app.browser_->testPage(L"",true);
    BrowserWindow::Options browser_options; browser_options.activate=false;
    if (!app.browser_->open(window,L"https://launchme.test/",userConfigDirectory()/L"browser-test",browser_options)) return 6136;
    const auto browser_window=app.browser_->handle();
    app.overlay_backend_.foreground=[browser_window] { return browser_window; };
    pad.buttons=0; app.controller(pad); pad.buttons=XINPUT_GAMEPAD_DPAD_DOWN; app.controller(pad);
    if (app.glance_.focus!=0) return 6137;
    pad.buttons=XINPUT_GAMEPAD_B; app.controller(pad);
    MSG closed{}; while (PeekMessageW(&closed,browser_window,0,0,PM_REMOVE)) { TranslateMessage(&closed); DispatchMessageW(&closed); }
    if (app.browser_->isOpen() || !app.glanceOpen() || app.glance_.focus!=0 || !IsWindowEnabled(window)) return 6138;
    app.browser_.reset(); app.back();
    app.settings_dirty_ = false;
    return 0;
}
