// Included after LauncherUiTest: desktop mode (src/app/desktop*.cpp, src/ui/desktop_view.hpp). The
// library as sections and a poster grid, search, sorting, the installed filter, marking several
// games, favourites and collections by dragging, the pane's data, Ustawienia as a desktop dialog
// (its rows, a switch, a list and Esc), the power menu's mode row and the window strip.
// Nothing reaches the network. Captures: desktop-*.png.

int LauncherUiTest::desktop(HWND window) {
    using namespace desktop_ui;
    Launcher app;
    app.settings_.launch_animation = false;
    app.settings_.tour_done = true;
    app.settings_.glance = app.settings_.companion = false;
    app.hwnd_ = window; app.execute_ = fakeExecute;
    app.overlay_backend_.foreground = [window] { return window; };
    if (!app.renderer_.initialize(window)) return 7600;
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
        if (!count || count >= 4096) { app.paint(); return true; }
        app.notifications_.clear();
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    const auto settle = [&] { for (int i = 0; i < 40; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); } };
    app.desktop_mode_ = true;
    app.audio_.setDesktop(true);

    auto library = std::make_unique<LibraryMessage>();
    library->account = 1;
    for (uint32_t i = 0; i < 18; ++i) {
        Game game{7600 + i, (i % 3 == 0 ? L"Alpha " : i % 3 == 1 ? L"Beta " : L"Gamma ") + std::to_wstring(i)};
        game.last_played = i < 5 ? 1000 + i : 0;
        library->games.push_back(std::move(game));
    }
    SteamCollection favorites{L"Ulubione", {7600, 7601}};
    favorites.id = "user-collections.favorite";
    SteamCollection shooters{L"Strzelanki", {7602}};
    shooters.id = "desktop-shooters";
    library->collections.push_back(std::move(favorites));
    library->collections.push_back(std::move(shooters));
    app.onLibrary(std::move(library));
    app.showCategory(0);
    settle();
    if (!app.desktopActive() || app.desktopMode() != true) return 7601;

    // Sidebar: the library's sections, a store and the collection; every game in the grid.
    const auto entry = [&](const std::string& id) { return app.desktopEntryIndex(id); };
    if (entry("all") < 0 || entry("recent") < 0 || entry("favorites") < 0 || entry("source:steam") < 0 || entry("collection:desktop-shooters") < 0) return 7602;
    if (app.cards_.size() != 18 || app.desktop_entries_[static_cast<size_t>(entry("favorites"))].count != 2) return 7603;
    if (!capture(L"desktop-library.png")) return 7604;

    // Sections: Ostatnio grane orders by last played; Ulubione keeps its two.
    app.desktopShowSection("recent"); settle();
    if (app.cards_.size() != 5 || app.cards_[0].game.app_id != 7604) return 7605;
    app.desktopShowSection("favorites"); settle();
    if (app.cards_.size() != 2) return 7606;
    app.desktopShowSection("all"); settle();

    // Search filters at once after its pause; the installed filter hides the rest.
    for (const wchar_t c : std::wstring(L"beta")) app.desktopCharacter(c);
    if (!app.desktop_search_focus_) return 7607;
    app.desktopApplyQuery();
    if (app.cards_.size() != 6) return 7608;
    app.desktopKey(VK_ESCAPE, false); // Clears the text, the field keeps the focus…
    if (!app.desktop_query_.empty() || app.cards_.size() != 18 || !app.desktop_search_focus_) return 7609;
    app.desktopKey(VK_ESCAPE, false); // …the second Esc leaves it.
    if (app.desktop_search_focus_ || app.desktop_zone_ != 1) return 7609;
    app.settings_.desktop_installed = true; app.rebuildCards(true);
    if (app.cards_.size() != 18) return 7610; // Every game here is installed.
    app.settings_.desktop_installed = false; app.rebuildCards(true);

    // Keys move the selection along the grid's rows; Ctrl/Shift mark several for the pane's bulk actions.
    app.desktopFocus(0);
    app.desktopMove(1, 0);
    if (app.selected_ != 1) return 7611;
    app.desktopMarkRange(1, 3);
    {
        desktop_ui::View view;
        app.desktopView(view);
        if (view.marked_count != 3 || view.pane.marked != 3) return 7612;
    }
    if (!capture(L"desktop-marked.png")) return 7613;
    app.desktop_marked_.clear();

    // Dropping a game on a collection adds it there; on Ulubione, to the favourites.
    app.desktopFocus(5);
    app.desktopDrop(app.desktop_entries_[static_cast<size_t>(entry("collection:desktop-shooters"))], {app.desktop_keys_[5]});
    app.desktopDrop(app.desktop_entries_[static_cast<size_t>(entry("favorites"))], {app.desktop_keys_[5]});
    {
        desktop_ui::View view;
        app.desktopView(view);
        if (view.drop_flash_entry != entry("favorites") || view.drop_flash <= 0.0f) return 7614;
    }
    settle();
    {
        const auto& game = app.cards_[5].game;
        const auto* shooters_collection = app.desktopCollection("desktop-shooters");
        if (!shooters_collection || !app.desktopMember(game, *shooters_collection) || !app.isFavorite(game)) return 7614;
        desktop_ui::View view;
        app.desktopView(view);
        if (!view.pane.game || !view.pane.favorite || view.pane.collections.empty() || view.drop_flash != 0.0f) return 7615;
    }
    // Micro-interactions: favourite star pulse, refresh spin, section collapse chevron and sidebar active glide.
    app.desktopActivate({Target::pane_favorite, 0});
    {
        desktop_ui::View view;
        app.desktopView(view);
        if (view.pane.favorite || view.pane.favorite_pulse <= 0.0f) return 7615;
    }
    app.desktopActivate({Target::pane_favorite, 0});
    app.desktopRefreshLibraries();
    app.last_frame_ -= std::chrono::milliseconds(50);
    app.tick();
    {
        desktop_ui::View view;
        app.desktopView(view);
        if (!view.refreshing || view.refresh_angle <= 0.0f) return 7615;
    }
    app.desktopShowSection("recent");
    {
        desktop_ui::View view;
        app.desktopView(view);
        if (!view.active_y) return 7615;
    }
    app.desktopShowSection("all");
    app.desktopActivate({Target::sidebar_row, 0}); // Fold Biblioteka, then unfold.
    app.last_frame_ -= std::chrono::milliseconds(50);
    app.tick();
    {
        desktop_ui::View view;
        app.desktopView(view);
        if (view.collapse_progress[0] <= 0.0f || view.collapse_progress[0] >= 1.0f) return 7615;
    }
    app.desktopActivate({Target::sidebar_row, 0});
    settle();

    // Artwork: a poster in hand needs no square cover; one the disk lane misses gets its cover as a
    // stand-in. Queued requests for rows left behind are dropped. Paints between frames wait for
    // the next frame while frames run.
    {
        using Kind = CoverMessage::Kind;
        app.desktopFocus(0);
        const auto queued = [&](const Game& game, Kind kind) {
            std::lock_guard lock(app.jobs_->mutex);
            const auto key = artRequestKey(game, kind);
            for (const auto* queue : {&app.jobs_->jobs, &app.jobs_->secondary})
                if (std::any_of(queue->begin(), queue->end(), [&](const auto& job) { return job.key == key; })) return true;
            return false;
        };
        const auto& with_poster = app.cards_[1].game;
        const auto& missing = app.cards_[2].game;
        if (!queued(with_poster, Kind::poster) || queued(with_poster, Kind::cover) || queued(missing, Kind::cover)) return 7640;
        auto poster = std::make_unique<CoverMessage>(); poster->key = gameKey(with_poster); poster->generation = app.jobs_->generation.load();
        poster->kind = Kind::poster; poster->bytes = pngFixture(60, 60, 90);
        app.onCover(std::move(poster));
        auto miss = std::make_unique<CoverMessage>(); miss->key = gameKey(missing); miss->generation = app.jobs_->generation.load();
        miss->kind = Kind::poster; miss->cache_miss = true;
        app.onCover(std::move(miss));
        if (!app.renderer_.hasPoster(1) || queued(with_poster, Kind::cover) || !queued(missing, Kind::cover)) return 7641;
        // Ordered from the view's middle rows outwards (the pane's own requests first).
        {
            const auto order = app.desktopArtRange().order;
            std::vector<std::string> expected;
            for (const size_t i : order) if (queued(app.cards_[i].game, Kind::poster)) expected.push_back(artRequestKey(app.cards_[i].game, Kind::poster));
            std::vector<std::string> lane;
            std::lock_guard lock(app.jobs_->mutex);
            for (const auto& job : app.jobs_->jobs) if (job.kind == Kind::poster) lane.push_back(job.key);
            if (lane.empty() || lane != expected) return 7642;
        }
        app.last_tick_ms_ = GetTickCount64();
        if (!app.deferPaint() || !app.paint_deferred_) return 7643;
        app.frame_painting_ = true;
        if (app.deferPaint()) return 7644; // The frame's own paint.
        app.frame_painting_ = false; app.paint_deferred_ = false;
        app.last_tick_ms_ = GetTickCount64() - 1000;
        if (app.deferPaint()) return 7645; // No frames run (a modal loop): paint at once.
    }

    // Zmień grafikę from the grid opens on the art the grid shows (Plakat, not the square Okładka
    // of Tryb TV): the picked poster is the card's on return, and Ponów grafiki asks for posters too.
    {
        using Kind = CoverMessage::Kind;
        app.desktopFocus(1);
        const auto game = app.cards_[1].game;
        const auto key = gameKey(game);
        app.openArtworkBrowser(game);
        if (!app.artworkOpen() || app.artwork_state_.kind() != ArtworkKind::poster) return 7646;
        const auto picked = pngFixture(200, 40, 60);
        constexpr uint32_t picked_id = 0x80000123u;
        if (!cacheArtworkVariant(game, ArtworkKind::poster, picked_id, picked, coverCacheDirectory())) return 7647;
        app.artwork_state_.applying = true; // A file from disk, as "Z dysku…" applies it.
        auto applied = std::make_unique<ArtworkBrowserMessage>();
        applied->generation = app.artwork_jobs_->generation.load();
        applied->type = ArtworkBrowserMessage::Type::local; applied->kind = ArtworkKind::poster;
        applied->asset_id = picked_id; applied->bytes = picked;
        app.onArtworkBrowser(std::move(applied));
        if (const auto* art = app.art_.peek(key); !art || !art->poster || *art->poster != picked ||
            selectedArtworkId(game, ArtworkKind::poster, coverCacheDirectory()) != picked_id) return 7648;
        app.closeArtworkBrowser();
        if (!app.desktopActive() || app.selected_ >= app.cards_.size() || gameKey(app.cards_[app.selected_].game) != key ||
            !app.renderer_.hasPoster(app.selected_)) return 7649;
        const auto poster_request = artRequestKey(game, Kind::poster);
        const auto before = app.art_revisions_[poster_request];
        app.retryGameArtwork(game, false);
        if (app.art_revisions_[poster_request] == before) return 7650;
        settle();
    }

    // Enter on a card picks it the default game action's way: its details (Szczegóły), not a launch.
    {
        app.desktopFocus(0); app.desktop_zone_ = 1;
        const auto picked = app.cards_[0].game.app_id;
        app.desktopKey(VK_RETURN, false);
        if (app.screen_ != ScreenMode::details || app.pending_launch_ || app.detail_games_.empty() ||
            app.detail_games_.front().app_id != picked) return 7651;
        app.desktopPageActivate({SheetHit::Kind::close}, false);
        if (app.screen_ != ScreenMode::shelves || !app.desktopActive()) return 7652;
        settle();
    }

    // Ustawienia opens as a desktop dialog: its rows, a switch flipped from its row, Esc closes it.
    app.openSettings(); settle();
    if (!app.desktopSheet() || app.screen_ != ScreenMode::settings) return 7616;
    if (!capture(L"desktop-settings.png")) return 7617;
    app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::appearance));
    app.buildSettingsRows();
    const auto row = [&](settings_ui::Id id) {
        for (int i = 0; i < static_cast<int>(app.settings_rows_.size()); ++i) if (app.settings_rows_[static_cast<size_t>(i)].id == id) return i;
        return -1;
    };
    const int tv_row = row(settings_ui::Id::launch_tv_mode);
    if (tv_row < 0 || app.settings_.launch_tv_mode) return 7618;
    app.desktopSettingsActivate({SheetHit::Kind::row, tv_row});
    if (!app.settings_.launch_tv_mode) return 7619;
    app.desktopSettingsActivate({SheetHit::Kind::toggle, tv_row});
    if (app.settings_.launch_tv_mode) return 7620;
    // A choice opens its list under the field; a pick applies it.
    const int font_row = row(settings_ui::Id::font);
    if (font_row < 0) return 7621;
    app.desktopSettingsActivate({SheetHit::Kind::field, font_row});
    if (!app.settingsPicking()) return 7622;
    if (!capture(L"desktop-settings-list.png")) return 7623;
    app.back();
    if (app.settingsPicking()) return 7624;
    app.desktopSettingsActivate({SheetHit::Kind::close});
    if (app.screen_ != ScreenMode::shelves || !app.desktopActive()) return 7625;

    // Szczegóły as a desktop dialog, then back.
    app.desktopFocus(0);
    app.openDetails(app.cards_[0].game); settle();
    if (!app.desktopSheet() || app.screen_ != ScreenMode::details) return 7626;
    if (!capture(L"desktop-details.png")) return 7627;
    app.desktopPageActivate({SheetHit::Kind::close}, false);
    if (app.screen_ != ScreenMode::shelves) return 7628;

    // The power menu's mode row says where it goes; Tryb TV and back keep the library.
    app.openPowerMenu();
    if (app.power_items_.size() < 3 || app.power_items_[0] != power_menu::Item::tv_mode) return 7629;
    app.closePowerMenu();
    app.setDesktopMode(false);
    if (app.desktopMode() || app.desktopActive()) return 7630;
    settle();
    app.setDesktopMode(true);
    settle();
    if (!app.desktopActive() || app.cards_.size() != 18) return 7631;
    return 0;
}
