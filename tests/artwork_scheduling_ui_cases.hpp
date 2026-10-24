// Actual disk worker with network workers disabled: queued slow/missing downloads must not
// stop cached Overview landscapes, backdrop and saved square-cover crop from reaching the UI.
int LauncherUiTest::artworkScheduling(HWND window) {
    using Kind = CoverMessage::Kind;
    Launcher app; app.hwnd_ = window;
    const auto stop = std::unique_ptr<Launcher, std::function<void(Launcher*)>>(&app, [window](Launcher* launcher) {
        launcher->alive_->store(false); launcher->jobs_->wake.notify_all(); launcher->work_.shutdown(std::chrono::seconds(4));
        MSG message{};
        while (PeekMessageW(&message, window, cover_message, cover_message, PM_REMOVE))
            delete reinterpret_cast<CoverMessage*>(message.lParam);
    });
    if (!app.renderer_.initialize(window)) return 3900;
    auto library = std::make_unique<LibraryMessage>(); library->account = 1;
    for (uint32_t i = 0; i < 40; ++i) library->games.push_back(Game{990000 + i, L"Artwork fixture " + std::to_wstring(i)});
    app.onLibrary(std::move(library)); app.openLibrary();
    app.library_state_.options[app.libraryScope()].view = library_view::View::overview;
    app.rebuildCards(false);
    // Rebuild drops the old view's square covers out of foreground; only the panel needs one.
    const auto focus = app.cards_[app.selected_].game;
    for (const auto& job : app.jobs_->jobs)
        if (job.kind == Kind::cover && gameKey(job.game) != gameKey(focus)) return 3901;
    size_t landscapes = 0;
    for (const auto& job : app.jobs_->secondary) if (job.kind == Kind::landscape) ++landscapes;
    if (landscapes < 2 || app.jobs_->focus != gameKey(focus)) return 3902;
    // A focused backdrop beats any amount of ready square-cover network work; retry time remains binding.
    Launcher::JobQueue queue;
    queue.focus = gameKey(focus);
    for (uint32_t i = 0; i < 40; ++i) {
        Launcher::Job job{Game{991000 + i, L"Slow square"}, Kind::cover}; job.cache_checked = true;
        queue.jobs.push_back(std::move(job));
    }
    Launcher::Job hero{focus, Kind::hero}; hero.cache_checked = true; queue.secondary.push_back(hero);
    const auto now = std::chrono::steady_clock::now();
    const auto first = queue.takeReady(false, false, now);
    if (!first || first->kind != Kind::hero || gameKey(first->game) != queue.focus) return 3903;
    hero.ready_at = now + std::chrono::seconds(60); queue.secondary.push_back(hero);
    const auto next = queue.takeReady(false, false, now);
    if (!next || next->kind != Kind::cover) return 3904;
    const auto cache = coverCacheDirectory();
    const auto put = [&](const Game& game, ArtworkKind kind, const std::vector<uint8_t>& bytes) {
        const auto file = gameArtworkPath(game, kind, cache);
        std::filesystem::create_directories(file.parent_path());
        std::ofstream out(file, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    };
    put(focus, ArtworkKind::cover, pngFixture(42, 64, 96));
    const auto cover_file = gameCoverPath(focus, cache);
    if (!saveManualTextCrop(cover_file, {0.5f, 0.64f})) return 3905;
    put(focus, ArtworkKind::hero, pngFixture(60, 96, 54));
    put(focus, ArtworkKind::landscape, pngFixture(80, 92, 43));
    // No network worker exists: many misses stay queued indefinitely while the disk lane advances.
    app.startArtworkWorkers(false);
    const auto end = GetTickCount64() + 4000;
    bool arrived = false;
    while (GetTickCount64() < end) {
        MSG message{};
        while (PeekMessageW(&message, window, cover_message, cover_message, PM_REMOVE))
            app.onCover(std::unique_ptr<CoverMessage>(reinterpret_cast<CoverMessage*>(message.lParam)));
        const auto* art = app.art_.peek(gameKey(focus));
        if (art && art->cover && art->hero && art->landscape) { arrived = true; break; }
        Sleep(5);
    }
    if (!arrived || std::abs(app.art_.peek(gameKey(focus))->focus.y - 0.64f) > 0.0001f) return 3906;
    // Late pre-save results cannot snap the newly saved crop back to the automatic centre.
    app.openCropEditor(focus);
    auto crop = std::make_unique<CropMessage>(); crop->generation = app.crop_token_->load(); crop->image = cover_file;
    crop->bytes = pngFixture(42, 64, 96); crop->width = 64; crop->height = 96; crop->focus = {0.5f, 0.64f};
    app.onCrop(std::move(crop)); app.crop_state_.focus = {0.5f, 0.4f}; app.crop_state_.control = 1;
    app.cropAccept();
    auto late = std::make_unique<CoverMessage>(); late->key = gameKey(focus); late->generation = app.jobs_->generation.load();
    late->kind = Kind::cover; late->bytes = pngFixture(42, 64, 96); late->focus = {0.5f, 0.5f};
    app.onCover(std::move(late));
    const auto saved = manualTextCrop(cover_file);
    if (!saved || std::abs(saved->y - 0.4f) > 0.0001f || std::abs(app.art_.peek(gameKey(focus))->focus.y - 0.4f) > 0.0001f) return 3907;
    // Emulated shortcuts also ride the disk lane (no waiting behind SteamGridDB HTTP), honouring emulation_rom_art.
    Game emulated{0xF9900001u, L"Yoshi's Woolly World"};
    emulated.kind = GameKind::nonsteam;
    emulated.shortcut_id = (uint64_t{0xF9900001u} << 32) | 0x02000000ULL;
    emulated.platform = L"Wii U";
    const auto rom_bytes = pngFixture(110, 64, 96), sgdb_bytes = pngFixture(210, 64, 96);
    const auto rom_cover = cache.parent_path() / L"emulation" / L"covers" / (std::to_wstring(emulated.app_id) + L".img");
    std::filesystem::create_directories(rom_cover.parent_path());
    { std::ofstream out(rom_cover, std::ios::binary); out.write(reinterpret_cast<const char*>(rom_bytes.data()), static_cast<std::streamsize>(rom_bytes.size())); }
    put(emulated, ArtworkKind::cover, sgdb_bytes);
    if (!saveManualTextCrop(rom_cover, {0.5f, 0.72f}) || !saveManualTextCrop(gameCoverPath(emulated, cache), {0.5f, 0.28f})) return 3908;
    if (cachedArtwork(emulated, ArtworkKind::cover, cache, true) != rom_bytes ||
        cachedArtwork(emulated, ArtworkKind::cover, cache, false) != sgdb_bytes) return 3909;
    app.jobs_->rom_art = true;
    app.requestArt(emulated, Kind::poster);
    app.requestArt(emulated, Kind::cover);
    const auto emu_end = GetTickCount64() + 4000;
    bool poster_missed = false, emu_arrived = false;
    while (GetTickCount64() < emu_end) {
        MSG message{};
        while (PeekMessageW(&message, window, cover_message, cover_message, PM_REMOVE)) {
            auto arrived_msg = std::unique_ptr<CoverMessage>(reinterpret_cast<CoverMessage*>(message.lParam));
            if (arrived_msg->key == gameKey(emulated) && arrived_msg->kind == Kind::poster && arrived_msg->cache_miss) poster_missed = true;
            app.onCover(std::move(arrived_msg));
        }
        const auto* art = app.art_.peek(gameKey(emulated));
        if (poster_missed && art && art->cover) { emu_arrived = true; break; }
        Sleep(5);
    }
    const auto* emu_art = app.art_.peek(gameKey(emulated));
    if (!emu_arrived || !emu_art || *emu_art->cover != rom_bytes || std::abs(emu_art->focus.y - 0.72f) > 0.0001f) return 3910;
    // Store and console tiles: logos, platform badges and backgrounds borrowed from their games
    // are on disk too. Without any network worker they must still arrive (they used to wait
    // behind every queued download).
    const auto platforms = cache.parent_path() / L"platforms";
    std::filesystem::create_directories(platforms);
    const auto write = [](const std::filesystem::path& file, const std::vector<uint8_t>& bytes) {
        std::filesystem::create_directories(file.parent_path());
        std::ofstream out(file, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    };
    const auto gog_logo = pngFixture(130, 96, 40), wiiu_logo = pngFixture(150, 96, 40);
    write(platforms / platformLogoFilename(L"GOG"), gog_logo);
    write(platforms / platformLogoFilename(L"Wii U"), wiiu_logo);
    Game gog_game; gog_game.kind = GameKind::store; gog_game.source = GameSource::gog;
    gog_game.source_id = "1207658924"; gog_game.name = L"Backdrop fixture"; gog_game.installed = true;
    const auto gog_hero = pngFixture(140, 96, 54), wiiu_hero = pngFixture(160, 96, 54);
    put(gog_game, ArtworkKind::hero, gog_hero);
    app.public_library_.push_back(gog_game);
    Game gog_tile; gog_tile.kind = GameKind::pc_platform; gog_tile.source = GameSource::gog;
    gog_tile.name = gog_tile.platform = sourceName(GameSource::gog);
    const auto emu_hero = cache.parent_path() / L"emulation" / L"backgrounds" / (std::to_wstring(emulated.app_id) + L".img");
    write(emu_hero, wiiu_hero);
    app.platform_games_[L"Wii U"] = {emulated};
    Game wiiu_tile; wiiu_tile.kind = GameKind::platform; wiiu_tile.name = wiiu_tile.platform = L"Wii U"; wiiu_tile.app_id = 0xF9910000u;
    for (const auto& tile : {gog_tile, wiiu_tile}) {
        if (!app.requestArt(tile, Kind::cover) || !app.requestArt(tile, Kind::hero)) return 3911;
    }
    if (!app.requestArt(gog_game, Kind::badge)) return 3912;
    const auto tiles_end = GetTickCount64() + 4000;
    bool badge = false, tiles = false;
    while (GetTickCount64() < tiles_end && !tiles) {
        MSG message{};
        while (PeekMessageW(&message, window, cover_message, cover_message, PM_REMOVE)) {
            auto arrived_msg = std::unique_ptr<CoverMessage>(reinterpret_cast<CoverMessage*>(message.lParam));
            if (arrived_msg->kind == Kind::badge && arrived_msg->badge == L"GOG" && arrived_msg->bytes == gog_logo) badge = true;
            app.onCover(std::move(arrived_msg));
        }
        const auto* gog = app.art_.peek(gameKey(gog_tile));
        const auto* wiiu = app.art_.peek(gameKey(wiiu_tile));
        tiles = badge && gog && gog->cover && gog->hero && wiiu && wiiu->cover && wiiu->hero;
        if (!tiles) Sleep(5);
    }
    if (!tiles) return 3913;
    const auto* gog = app.art_.peek(gameKey(gog_tile));
    const auto* wiiu = app.art_.peek(gameKey(wiiu_tile));
    if (*gog->cover != gog_logo || *gog->hero != gog_hero || gog->hero_tone != HeroTone::dimmed) return 3914;
    if (*wiiu->cover != wiiu_logo || *wiiu->hero != wiiu_hero || wiiu->hero_tone != HeroTone::dimmed) return 3915;
    return 0;
}
