// Included after LauncherUiTest: Glance and Towarzyszka on every home layout (src/ui/home_widgets.hpp).
// The default home keeps its right column; Metro gives them the Platformy PC / Emulacja cells; XMB
// puts them in its row right after the recent games; PSfive in its bottom-right corner; Flix as two
// pills beside the platform badge, where she neither greets nor speaks. Focus, Enter, clicks and R2
// open them and Back returns to the same tile. Nothing reaches the network. Captures: home-widgets-*.png.
#include "../src/ui/metro_layout.hpp"

int LauncherUiTest::homeWidgets(HWND window) {
    using home_widgets::Placement;
    using metro_layout::Role;
    Launcher app;
    app.settings_.launch_animation = false;
    app.hwnd_ = window; app.execute_ = fakeExecute;
    app.overlay_backend_.foreground = [window] { return window; };
    app.glance_get_ = [](const std::wstring&, const auto&, size_t) { return HttpResponse{}; };
    if (!app.renderer_.initialize(window)) return 7300;
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
        if (!count || count >= 4096) { app.paint(); return true; }
        app.notifications_.clear();
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    const auto settle = [&] { for (int i = 0; i < 40; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); } };
    // Her greeting and the chat's opening line (offline: no connection) and Glance's empty feeds.
    const auto drain = [&] {
        const auto end = GetTickCount64() + 3000;
        do {
            MSG message{};
            while (PeekMessageW(&message, window, companion_message, companion_message, PM_REMOVE))
                app.onCompanion(std::unique_ptr<CompanionMessage>(reinterpret_cast<CompanionMessage*>(message.lParam)));
            while (PeekMessageW(&message, window, glance_message, glance_message, PM_REMOVE))
                app.onGlance(std::unique_ptr<GlanceMessage>(reinterpret_cast<GlanceMessage*>(message.lParam)));
            if (!app.companion_.busy && !app.glance_busy_) return;
            Sleep(5);
        } while (GetTickCount64() < end);
    };
    // A line of hers in the bubble, fully spoken.
    const auto say = [&] {
        drain();
        auto& bubble = app.companion_.bubble;
        bubble = {};
        bubble.text = L"Glance i ja jesteśmy teraz także tutaj. Kliknij mnie!";
        bubble.reveal = bubble.text.size();
        bubble.alpha = 1;
    };
    const auto layout = [&](AppSettings::HomeLayout home) { app.settings_.home_layout = home; app.showCategory(0); settle(); };
    const auto view = app.renderer_.viewport();
    const auto clickAt = [&](settings_ui::Rect r) { app.click((r.left + r.right) * 0.5f * view.scale, (r.top + r.bottom) * 0.5f * view.scale, false); };

    app.settings_.enabled_pc_sources.insert("gog"); // A Platformy PC page, so Metro has a shortcut to give up.
    auto library = std::make_unique<LibraryMessage>();
    library->account = 1;
    for (uint32_t i = 0; i < 12; ++i) {
        Game game{5300 + i, L"Widget game " + std::to_wstring(i)};
        game.last_played = i < 5 ? 100 + i : 0;
        library->games.push_back(std::move(game));
    }
    SteamCollection collection{L"Ulubione", {5300, 5301, 5302}};
    collection.id = "widgets-0";
    library->collections.push_back(std::move(collection));
    app.onLibrary(std::move(library));
    layout(AppSettings::HomeLayout::shelves);

    // Default home: unchanged — her tile atop the right column, Glance under it.
    {
        const auto spots = app.homeWidgetSpots();
        if (app.homeWidgets() != Placement::column || !spots.glance || !spots.companion ||
            spots.companion->top != companion_ui::tile(view.width).top || spots.glance->top != companion_ui::glance_top ||
            spots.glance->right != glance_ui::shortcut(view.width).right) return 7301;
    }

    // Metro: Towarzyszka and Glance in the bottom row's middle cells; the PC shortcut gives way.
    layout(AppSettings::HomeLayout::metro);
    const auto metroTile = [&](Role wanted) -> std::optional<size_t> {
        if (app.metro_pages_.empty()) return std::nullopt;
        const auto& start = app.metro_pages_[0];
        for (size_t i = start.begin; i < start.begin + start.count; ++i) if (app.metro_tiles_[i].role == wanted) return i;
        return std::nullopt;
    };
    {
        const auto her = metroTile(Role::companion), glance = metroTile(Role::glance);
        if (!app.metroActive() || !her || !glance || metroTile(Role::pc_platforms) || metroTile(Role::emulation)) return 7302;
        const auto first = metro_layout::cell(0, 1, 2), second = metro_layout::cell(0, 2, 2);
        if (app.metro_tiles_[*her].rect.left != first.left || app.metro_tiles_[*her].rect.top != first.top ||
            app.metro_tiles_[*glance].rect.left != second.left || app.cards_[*her].game.kind != GameKind::companion ||
            app.cards_[*glance].game.kind != GameKind::glance) return 7303;
        const auto spots = app.homeWidgetSpots();
        if (app.homeWidgets() != Placement::tiles || spots.glance || spots.companion || !app.glanceAvailable()) return 7304;
        // Enter on Glance's tile opens it; Back returns to the same tile.
        app.metroFocus(*glance); app.launch();
        if (!app.glanceOpen()) return 7305;
        drain(); app.back(); settle();
        if (app.glanceOpen() || !app.metroActive() || app.selected_ != *metroTile(Role::glance)) return 7306;
        // Her tile: Enter opens the chat, Back returns; one click opens it too.
        app.metroFocus(*metroTile(Role::companion)); app.launch();
        if (!app.companionOpen()) return 7307;
        drain(); app.back(); settle();
        if (app.companionOpen() || app.selected_ != *metroTile(Role::companion)) return 7308;
        app.metroClick(*metroTile(Role::glance), false);
        if (!app.glanceOpen()) return 7309;
        drain(); app.back(); settle();
        app.metroFocus(*metroTile(Role::companion)); settle();
        say();
        if (!capture(L"home-widgets-metro.png")) return 7310;
        // Her line beside her tile, left of it (over Biblioteka and Ustawienia).
        const auto& card = app.cards_[*metroTile(Role::companion)];
        const auto bubble = app.renderer_.companionBubble();
        if (bubble.right <= bubble.left || bubble.right > card.x + 1) return 7311;
        // Glance alone: Glance, then the PC shortcut.
        app.settings_.companion = false;
        layout(AppSettings::HomeLayout::metro);
        if (!metroTile(Role::glance) || !metroTile(Role::pc_platforms) || metroTile(Role::companion) ||
            app.metro_tiles_[*metroTile(Role::glance)].rect.left != first.left) return 7312;
        app.settings_.companion = true;
    }

    // XMB: right after the recent games, before "Wszystkie gry".
    layout(AppSettings::HomeLayout::xmb);
    {
        const auto& row = app.xmb_top_;
        const auto at = std::find_if(row.begin(), row.end(), [](const Game& game) { return game.kind == GameKind::glance; });
        if (!app.xmbActive() || at == row.begin() || at == row.end() || at + 2 >= row.end() || !isGame(*(at - 1)) ||
            (at + 1)->kind != GameKind::companion || (at + 2)->kind != GameKind::all) return 7320;
        const auto glance = static_cast<size_t>(at - row.begin());
        app.navigate(static_cast<int>(glance));
        if (app.selected_ != glance || app.xmb_column_ != glance) return 7321;
        app.launch();
        if (!app.glanceOpen()) return 7322;
        drain(); app.back(); settle();
        if (app.glanceOpen() || app.selected_ != glance || app.xmb_column_ != glance) return 7323;
        app.navigate(1);
        app.launch();
        if (!app.companionOpen()) return 7324;
        drain(); app.back(); settle();
        if (app.companionOpen() || app.selected_ != glance + 1) return 7325;
        // Her line beside her tile, right of it (the focused tile and its caption sit left).
        app.navigate(-static_cast<int>(glance + 1)); settle();
        say();
        if (!capture(L"home-widgets-xmb.png")) return 7326;
        const auto& card = app.cards_[glance + 1];
        const auto bubble = app.renderer_.companionBubble();
        if (bubble.right <= bubble.left || bubble.left < card.x + card.size - 1) return 7327;
    }

    // PSfive: both tiles in the bottom-right corner, Glance in the corner, hers left of it.
    layout(AppSettings::HomeLayout::psfive);
    {
        const auto spots = app.homeWidgetSpots();
        if (app.homeWidgets() != Placement::corner || !spots.glance || !spots.companion ||
            spots.glance->right != view.width - home_widgets::corner_right || spots.glance->bottom != view.height - home_widgets::corner_bottom ||
            spots.companion->right != spots.glance->left - home_widgets::corner_gap || spots.companion->bottom != spots.glance->bottom) return 7330;
        CompanionView seen; app.companionView(seen);
        if (!seen.tile || seen.pill || seen.spot.left != spots.companion->left) return 7331;
        say();
        if (!capture(L"home-widgets-psfive.png")) return 7332;
        const auto bubble = app.renderer_.companionBubble();
        if (bubble.right <= bubble.left || bubble.right > spots.companion->left) return 7333;
        clickAt(*spots.glance);
        if (!app.glanceOpen()) return 7334;
        drain(); app.back(); settle();
        clickAt(*spots.companion);
        if (!app.companionOpen()) return 7335;
        drain(); app.back(); settle();
        // R2 opens Glance on every layout (edge only, as on the default home).
        PadReading pad{true, L"fixture-dualsense", 0}; pad.name = L"DualSense";
        app.controller(pad); pad.buttons = pad_right_trigger; app.controller(pad);
        if (!app.glanceOpen()) return 7336;
        drain(); app.back(); pad.buttons = 0; app.controller(pad); settle();
    }

    // Flix: two pills beside the platform badge; no figure, no bubble, no greeting there.
    layout(AppSettings::HomeLayout::flix);
    {
        const auto spots = app.homeWidgetSpots();
        if (app.homeWidgets() != Placement::pills || !spots.glance || !spots.companion ||
            spots.glance->right != view.width - home_widgets::badge_slot || spots.glance->top != home_widgets::pill_top ||
            spots.companion->right != spots.glance->left - home_widgets::pill_gap) return 7340;
        CompanionView seen; app.companionView(seen);
        if (seen.tile || !seen.pill) return 7341;
        app.companion_.greeted = false; app.companion_.age = 5; app.companion_.bubble = {};
        app.tickCompanion(0.016f);
        if (app.companion_.greeted || app.companion_.busy) return 7342;
        app.companion_.greeted = true;
        if (!capture(L"home-widgets-flix.png")) return 7343;
        clickAt(*spots.glance);
        if (!app.glanceOpen()) return 7344;
        drain(); app.back(); settle();
        clickAt(*spots.companion);
        if (!app.companionOpen()) return 7345;
        drain(); app.back(); settle();
    }
    drain();
    return 0;
}
