// Included after LauncherUiTest: DRM-free folders. Settings → Platformy PC lists them first;
// their page adds folders, scans and corrects games. The folder/file dialogs and ShellExecute
// are fakes, and the scan worker's messages are delivered by hand (no folder walk, no RAWG).
namespace {
std::vector<GameAction> drm_launches;
INT_PTR drmExecute(HWND, const GameAction& action) { drm_launches.push_back(action); return 33; }
} // namespace

int LauncherUiTest::drmFree(HWND window) {
    using namespace settings_ui;
    Launcher app; app.hwnd_ = window; app.execute_ = drmExecute; drm_launches.clear();
    app.boundary_feedback_ = [] {};
    if (!app.renderer_.initialize(window)) return 2100;
    const auto root = userConfigDirectory() / L"drm-fixture" / L"Gry";
    std::filesystem::create_directories(root);
    app.pick_games_folder_ = [&](HWND) { return root; };
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        if (!GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096)) return true;
        app.settings_motion_.valid = false;
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    const auto select = [&](Id id, const std::string& key = {}) {
        const auto row = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [&](const Row& r) { return r.id == id && (key.empty() || r.key == key); });
        if (row == app.settings_rows_.end()) return false;
        app.settings_state_.selected[app.settings_state_.category] = static_cast<int>(row - app.settings_rows_.begin());
        app.settings_state_.options = true; app.settings_state_.editing = false; app.ensureSettingVisible(); return true;
    };
    const auto deliver = [&](DrmFreeMessage::Kind kind, drm_free::Catalog catalog = {}, drm_free::Progress progress = {}) {
        auto message = std::make_unique<DrmFreeMessage>();
        message->generation = app.drm_generation_; message->kind = kind; message->catalog = std::move(catalog); message->progress = progress;
        app.onDrmFree(std::move(message));
    };
    const auto pcs = [&]() -> const Launcher::Category* {
        const auto found = std::find_if(app.categories_.begin(), app.categories_.end(), [](const auto& item) { return item.id == "pc-platforms"; });
        return found == app.categories_.end() ? nullptr : &*found;
    };
    auto steam = std::make_unique<LibraryMessage>(); steam->account = 1; steam->games = {Game{620, L"Portal 2"}};
    app.onLibrary(std::move(steam));

    // The overview starts with the DRM-free folders, above the six stores.
    app.openSettings(); app.settings_state_.selectCategory(static_cast<int>(Category::library));
    app.pc_scanning_ = false; app.buildSettingsRows();
    if (app.settings_rows_.size() <= pc_sources.size() + 1 || app.settings_rows_[0].id != Id::pc_platform ||
        app.settings_rows_[0].source != GameSource::drmfree || app.settings_rows_[0].title != L"Katalog gier DRM-free" ||
        app.settings_rows_[0].checked || app.settings_rows_[1].source != GameSource::gog) return 2101;
    if (!capture(L"drm-free-overview.png")) return 2102;
    const auto view = app.renderer_.viewport();
    const auto layout = Layout::of(view.width, view.height, app.settings_state_, app.settings_rows_);
    const auto cog = layout.pluginCog(0, 0);
    app.settingsClick((cog.left + cog.right) * 0.5f, (cog.top + cog.bottom) * 0.5f);
    if (app.settings_state_.platform() != GameSource::drmfree || app.settings_rows_.size() != 3 || app.settings_rows_[0].id != Id::drm_scan ||
        app.settings_rows_[1].id != Id::drm_background || !app.settings_rows_[1].checked || app.settings_rows_[2].id != Id::drm_add_root) return 2103;
    for (const auto& row : app.settings_rows_) if (row.source != GameSource::drmfree) return 2104;

    // "Dodaj katalog": the folder is saved, the source turns on and a full scan starts.
    if (!select(Id::drm_add_root)) return 2105;
    app.settingsActivate();
    if (app.drm_catalog_.roots != std::vector<std::filesystem::path>{root} || !app.settings_.enabled_pc_sources.contains("drmfree") ||
        !app.drm_scanning_ || app.settings_rows_.size() != 4 || app.settings_rows_[3].id != Id::drm_root || app.settings_rows_[3].title != root.native() ||
        app.settings_rows_[0].value != L"Zatrzymaj") return 2106;
    // The same folder, or one inside it, is refused.
    app.pick_games_folder_ = [&](HWND) { return root / L"Indie"; };
    app.drmAddRoot();
    if (app.drm_catalog_.roots.size() != 1 || app.notifications_.empty()) return 2107;
    app.notifications_.clear();
    deliver(DrmFreeMessage::Kind::progress, {}, {3, 7});
    if (app.settings_rows_[0].description.find(L"3 / 7") == std::wstring::npos) return 2108;

    // The scanned catalog: two games, saved and on the shelves.
    const auto entry = [&](const wchar_t* folder_name, const wchar_t* exe, const wchar_t* name) {
        drm_free::Entry value; value.folder = root / folder_name; value.id = drm_free::folderId(value.folder);
        value.executable = value.folder / exe; value.title = value.name = name; value.found = 1;
        return value;
    };
    auto scanned = app.drm_catalog_;
    scanned.games = {entry(L"Celeste", L"Celeste.exe", L"Celeste"), entry(L"Hollow_Knight", L"hollow_knight.exe", L"Hollow Knight")};
    scanned.scanned = 1790000000;
    const auto stale_generation = app.drm_generation_;
    deliver(DrmFreeMessage::Kind::scanned, scanned);
    if (app.drm_catalog_.games.size() != 2 || app.drm_library_.size() != 2 || app.notifications_.empty() ||
        drm_free::load(userConfigDirectory() / L"drm-free.json").catalog != app.drm_catalog_) return 2109;
    deliver(DrmFreeMessage::Kind::finished);
    if (app.drm_scanning_ || app.settings_rows_[0].value != L"Skanuj teraz") return 2110;
    // A message of an earlier scan never replaces the catalog of a newer one.
    app.startDrmScan(true);
    auto late = std::make_unique<DrmFreeMessage>(); late->generation = stale_generation; late->kind = DrmFreeMessage::Kind::scanned;
    app.onDrmFree(std::move(late));
    if (app.drm_catalog_.games.size() != 2 || !app.drm_scanning_) return 2111;
    deliver(DrmFreeMessage::Kind::finished);
    const auto celeste_id = app.drm_catalog_.games[0].id, hollow_id = app.drm_catalog_.games[1].id;
    // The page shows one "Znalezione gry" row with the total, not the games themselves.
    if (app.settings_rows_.size() != 5 || app.settings_rows_[4].id != Id::drm_games || app.settings_rows_[4].control != Control::count ||
        app.settings_rows_[4].value != L"2" || app.settings_rows_[4].title != L"Znalezione gry") return 2112;
    if (!capture(L"drm-free-page.png")) return 2113;
    const auto* shelf = pcs();
    if (!shelf || std::none_of(shelf->games.begin(), shelf->games.end(), [](const Game& tile) { return tile.source == GameSource::drmfree; }) ||
        std::count_if(app.public_library_.begin(), app.public_library_.end(), [](const Game& game) { return game.source == GameSource::drmfree; }) != 2) return 2114;
    // Entering it: thin rows, one per game.
    if (!select(Id::drm_games)) return 2132;
    app.settingsActivate();
    const auto list_layout = Layout::of(view.width, view.height, app.settings_state_);
    if (!app.settings_state_.at(settings_ui::Page::drm_games) || app.settings_rows_.size() != 2 || app.settings_rows_[0].key != celeste_id ||
        app.settings_rows_[0].description != L"Celeste.exe" || app.settings_rows_[1].title != L"Hollow Knight" || !list_layout.compact ||
        list_layout.rowHeight(0) >= Layout::row_height || app.settings_state_.row() != 0) return 2133;
    if (!capture(L"drm-free-games.png")) return 2134;
    // A click on the second thin row hits that row (geometry shared with drawing).
    const auto second = list_layout.row(1, app.settings_state_.offset());
    if (list_layout.hitRow(second.left + 30, second.top + 10, 2, app.settings_state_.offset()) != 1) return 2135;

    // Game options: "Dopasuj w RAWG…" opens identification over Settings.
    if (!select(Id::drm_game, celeste_id)) return 2115;
    app.settingsActivate();
    if (!app.settings_state_.editing || app.settings_rows_[app.settings_state_.row()].choices.size() != 4 ||
        app.settings_rows_[app.settings_state_.row()].choices[0].label != L"Dopasuj w RAWG…") return 2116;
    if (!capture(L"drm-free-game-options.png")) return 2117;
    app.settings_state_.choice = 0; app.settingsActivate();
    if (!app.identifyOpen() || gameKey(app.identify_state_.game) != "drmfree:" + celeste_id || app.identify_state_.query != L"Celeste" ||
        !app.settingsOpen()) return 2136;
    if (!capture(L"drm-free-identify.png")) return 2137;
    app.back();
    if (app.identifyOpen() || !app.settingsOpen() || !app.settings_state_.at(settings_ui::Page::drm_games)) return 2138;
    // "Zmień plik gry…" opens the picker in the game's folder.
    const auto other = root / L"Celeste" / L"Celeste-OpenGL.exe";
    std::filesystem::path initial;
    app.pick_path_ = [&](HWND, bool folder, const std::filesystem::path& start) { initial = start; return folder ? std::filesystem::path{} : other; };
    if (!select(Id::drm_game, celeste_id)) return 2139;
    app.settingsActivate(); app.settings_state_.choice = 1; app.settingsActivate();
    const auto& celeste = app.drm_catalog_.games[0];
    if (app.settings_state_.editing || initial != root / L"Celeste" || celeste.executable != other || !celeste.manual_executable ||
        app.settings_rows_[app.settings_state_.row()].key != celeste_id || app.settings_rows_[app.settings_state_.row()].choices.size() != 5) return 2118;
    // "Ignoruj" takes Hollow Knight out of the library; it moves to the end of the list.
    if (!select(Id::drm_game, hollow_id)) return 2119;
    app.settingsActivate(); app.settings_state_.choice = 2; app.settingsActivate();
    if (!app.drm_catalog_.games[1].ignored || app.drm_library_.size() != 1 || app.settings_rows_.back().description != L"Ignorowana") return 2120;
    // A scan made on an older snapshot keeps both corrections.
    app.startDrmScan(true);
    deliver(DrmFreeMessage::Kind::scanned, scanned);
    deliver(DrmFreeMessage::Kind::finished);
    if (!app.drm_catalog_.games[1].ignored || app.drm_catalog_.games[0].executable != other || app.drm_library_.size() != 1) return 2121;
    // Back returns to the page with "Znalezione gry" focused; its total counts playable games.
    app.back();
    if (app.settings_state_.at(settings_ui::Page::drm_games) || app.settings_state_.platform() != GameSource::drmfree ||
        app.settings_rows_[app.settings_state_.row()].id != Id::drm_games || app.settings_rows_[app.settings_state_.row()].value != L"1" ||
        app.settings_rows_[app.settings_state_.row()].description.find(L"Ignorowane: 1") == std::wstring::npos) return 2140;

    // The DRM-free tile opens its library; launching runs the chosen file from its folder.
    for (int i = 0; i < 4 && app.settingsOpen(); ++i) app.back();
    if (app.settingsOpen()) return 2122;
    app.openPcLibrary(GameSource::drmfree);
    if (app.screen_ != ScreenMode::library || app.cards_.size() != 1 || app.cards_[0].game.name != L"Celeste") return 2123;
    app.launch(); app.finishLaunch();
    if (drm_launches.size() != 1 || drm_launches[0].target != other.native() || drm_launches[0].working_directory != other.parent_path()) return 2124;
    // "Zidentyfikuj grę" renames it for good: later scans keep the user's choice.
    rawg::Metadata identified; identified.id = 9; identified.name = L"Celeste (2018)";
    app.drmIdentified(app.cards_[0].game, identified);
    if (app.drm_catalog_.games[0].name != L"Celeste (2018)" || !app.drm_catalog_.games[0].manual_identity || app.cards_[0].game.name != L"Celeste (2018)") return 2125;
    app.startDrmScan(false);
    deliver(DrmFreeMessage::Kind::scanned, scanned);
    deliver(DrmFreeMessage::Kind::finished);
    if (app.drm_catalog_.games[0].name != L"Celeste (2018)" || app.drm_catalog_.games[0].rawg_id != 9) return 2126;

    // "Zmień grafikę → Z dysku…": type from the list, then a PNG/JPEG file becomes that artwork.
    const auto image = root / L"Celeste" / L"cover.png";
    { const auto bytes = pngFixture(90, 300, 200, true); std::ofstream(image, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())); }
    std::vector<int> picks;
    app.pick_image_ = [&](HWND) { picks.push_back(1); return image; };
    const auto celeste_game = app.cards_[0].game;
    app.openArtworkBrowser(celeste_game);
    auto& art = app.artwork_state_;
    art.category = static_cast<int>(artwork_ui::kinds.size()) - 1; // "Kafel poziomy", the last type.
    app.artworkMove(0, 1);
    if (!art.local_focus || art.local_open) return 2141;
    app.artworkAccept();
    if (!art.local_open || art.local_kind != art.category || !picks.empty()) return 2142;
    if (!capture(L"artwork-from-disk.png")) return 2143;
    for (int i = 0; i < 6; ++i) app.artworkMove(0, -1); // Up to "Okładka".
    app.artworkAccept();                                  // Chosen; focus moves to "Wskaż plik…".
    if (art.local_kind != 0 || art.local_row != artwork_ui::State::local_pick_row || !art.local_open) return 2144;
    app.back();                                           // Back closes only the panel.
    if (art.local_open || !app.artworkOpen()) return 2145;
    app.artworkAccept(); app.artworkMove(0, -10); app.artworkAccept(); app.artworkAccept();
    if (picks.size() != 1 || art.local_open || !art.applying || art.category != 0) return 2146;
    {
        std::lock_guard lock(app.artwork_jobs_->mutex);
        const auto& queued = app.artwork_jobs_->jobs.front();
        if (queued.type != ArtworkBrowserMessage::Type::local || queued.kind != ArtworkKind::cover || queued.query != image.native()) return 2147;
    }
    // The worker's part: the file stored as a variant of this game, then its answer.
    const uint32_t local_id = 0x80000001u;
    if (!cacheArtworkVariant(celeste_game, ArtworkKind::cover, local_id, pngFixture(90, 300, 200, true), coverCacheDirectory())) return 2148;
    auto applied = std::make_unique<ArtworkBrowserMessage>();
    applied->type = ArtworkBrowserMessage::Type::local; applied->generation = app.artwork_jobs_->generation.load();
    applied->kind = ArtworkKind::cover; applied->asset_id = local_id; applied->bytes = pngFixture(90, 300, 200, true);
    app.onArtworkBrowser(std::move(applied));
    const auto* bound = app.art_.find(gameKey(celeste_game));
    if (art.applying || art.status != L"Zastosowano grafikę z dysku." || !bound || !bound->cover ||
        selectedArtworkId(celeste_game, ArtworkKind::cover, coverCacheDirectory()) != local_id) return 2149;

    // Search field by keyboard: Up from the first type enters it; the selected text is replaced by typing.
    art.local_focus = false; art.gallery = false; art.category = 0;
    app.artworkMove(0, -1);
    if (!app.editingArtworkSearch() || !art.query_selected) return 2150;
    for (const auto ch : std::wstring(L"Revenant")) app.onCharacter(ch);
    if (art.query != L"Revenant" || !art.results_mode || art.query_selected) return 2151;
    // Down without searching leaves the field and brings the gallery back.
    app.artworkMove(0, 1);
    if (app.editingArtworkSearch() || art.results_mode || !art.loading || art.query != L"Revenant") return 2152;
    // "Wyczyść" (also Delete) empties the field and keeps the focus there.
    app.artworkMove(0, -1);
    if (!app.editingArtworkSearch()) return 2153;
    app.clearArtworkQuery();
    if (!art.query.empty() || !app.editingArtworkSearch()) return 2154;
    // Same-titled games: the year known for this game (RAWG) decides the order; rows show years.
    app.rawg_metadata_[gameKey(celeste_game)].details.release_date = L"1999-10-26";
    for (const auto ch : std::wstring(L"Revenant")) app.onCharacter(ch);
    app.searchArtwork();
    auto found = std::make_unique<ArtworkBrowserMessage>();
    found->type = ArtworkBrowserMessage::Type::search; found->generation = app.artwork_jobs_->generation.load();
    found->search.games = {{{5389392, false}, L"Revenant", 2024}, {{5100000, false}, L"Revenant Kingdom", 2020},
                           {{5247473, false}, L"Revenant", 1999}, {{5300000, false}, L"Revenant", 2023}};
    app.onArtworkBrowser(std::move(found));
    if (art.games.size() != 4 || art.games[0].source.id != 5247473 || art.games[1].source.id != 5300000 ||
        art.games[3].source.id != 5100000 || !art.gallery || art.selected != 0) return 2155;
    if (!capture(L"artwork-search-years.png")) return 2156;
    app.closeArtworkBrowser();
    // Emulated shortcuts: "Źródło" under "Z dysku…" switches SteamGridDB → ScreenScraper → ES-DE.
    {
        Game emulated; emulated.kind = GameKind::nonsteam; emulated.app_id = 0xF1000001u;
        emulated.shortcut_id = (uint64_t{0xF1000001u} << 32) | 0x02000000ULL;
        emulated.name = L"Desert Strike"; emulated.platform = L"Amiga";
        app.openArtworkBrowser(emulated);
        if (!art.emulated() || art.provider != ArtworkProvider::steamgriddb) return 2157;
        art.category = artwork_ui::category(ArtworkKind::landscape); // Disc artwork has one fixed provider.
        art.local_focus = true;
        app.artworkMove(0, 1); // "Z dysku…" -> "Źródło" for the landscape gallery.
        if (!art.provider_focus || art.local_focus) return 2158;
        app.artworkAccept();
        {
            std::lock_guard lock(app.artwork_jobs_->mutex);
            if (app.artwork_jobs_->jobs.empty() || app.artwork_jobs_->jobs.front().type != ArtworkBrowserMessage::Type::list ||
                app.artwork_jobs_->jobs.front().provider != ArtworkProvider::screenscraper) return 2159;
        }
        auto listed = std::make_unique<ArtworkBrowserMessage>();
        listed->type = ArtworkBrowserMessage::Type::list; listed->generation = app.artwork_jobs_->generation.load();
        listed->page.provider = ArtworkProvider::screenscraper;
        listed->page.assets = {{0x40000001u, L"https://neoclone.screenscraper.fr/api2/mediaJeu.php?media=fanart",
                                L"https://neoclone.screenscraper.fr/api2/mediaJeu.php?media=fanart", L"ScreenScraper · fanart"}};
        app.onArtworkBrowser(std::move(listed));
        if (art.provider != ArtworkProvider::screenscraper || art.assets.size() != 1 || !art.provider_focus) return 2160;
        const auto loading = app.launch_animation_started_;
        app.launch_animation_started_.reset(); // The earlier launch animation would cover the page.
        if (!capture(L"artwork-source-screenscraper.png")) return 2161;
        app.launch_animation_started_ = loading;
        app.artworkAccept();
        if (art.provider != ArtworkProvider::esde) return 2162;
        app.artworkAccept();
        if (art.provider != ArtworkProvider::steamgriddb) return 2163;
        app.closeArtworkBrowser();
        // Other games have no "Źródło": Down from "Z dysku…" goes to "Popraw kadr", then Back, which
        // Enter presses; Down stops there.
        app.openArtworkBrowser(celeste_game);
        art.category = static_cast<int>(artwork_ui::kinds.size()) - 1;
        app.artworkMove(0, 1); app.artworkMove(0, 1);
        if (art.emulated() || art.provider_focus || !art.crop_focus) return 2164;
        app.artworkMove(0, 1); app.artworkMove(0, 1);
        if (!art.back_focus || art.crop_focus) return 2177;
        app.artworkAccept();
        if (app.artworkOpen()) return 2178;
    }

    // Removing the folder takes two presses and empties the library.
    app.openSettings(); app.openPlatformSettings(GameSource::drmfree);
    if (!select(Id::drm_root)) return 2127;
    app.settingsActivate();
    if (!app.drm_catalog_.roots.size() || app.settings_rows_[app.settings_state_.row()].value != L"Potwierdź") return 2128;
    app.settingsActivate();
    if (!app.drm_catalog_.roots.empty() || !app.drm_catalog_.games.empty() || !app.drm_library_.empty() || app.settings_rows_.size() != 3) return 2129;
    const auto* emptied = pcs();
    if (emptied && std::any_of(emptied->games.begin(), emptied->games.end(), [](const Game& tile) { return tile.source == GameSource::drmfree; })) return 2130;
    app.back();
    if (app.settings_state_.platform() || app.settings_state_.row() != 0) return 2131;
    // Media i dane → "ScreenScraper" opens its own page of API settings; Back returns to that row.
    app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::media)); app.buildSettingsRows();
    if (!select(Id::game_videos) || !select(Id::screenscraper_open)) return 2165;
    const int opener = app.settings_state_.row();
    app.settingsActivate();
    if (!app.settings_state_.at(settings_ui::Page::screenscraper) || app.settings_rows_.size() != screenscraper::fields.size() + 2 || app.settings_rows_.back().id != Id::screenscraper_clear ||
        app.settings_rows_[0].id != Id::screenscraper || app.settings_rows_[0].key != "dev_id") return 2166;
    app.launch_animation_started_.reset(); // The earlier launch animation would cover the page.
    if (!capture(L"settings-screenscraper.png")) return 2167;
    app.back();
    if (app.settings_state_.at(settings_ui::Page::screenscraper) || app.settings_state_.row() != opener || app.settings_rows_[opener].id != Id::screenscraper_open) return 2168;
    // "Zidentyfikuj grę" on an emulated game switches between RAWG and ScreenScraper; other games only link to RAWG.
    {
        Game emulated; emulated.kind = GameKind::nonsteam; emulated.app_id = 0xF1000002u;
        emulated.shortcut_id = (uint64_t{0xF1000002u} << 32) | 0x02000000ULL;
        emulated.name = L"Paper Mario"; emulated.platform = L"Switch";
        app.openIdentifyFor(emulated);
        const auto before = app.identify_state_.source;
        if (!app.identify_state_.emulated) return 2169;
        app.identifySwitchSource();
        if (app.identify_state_.source == before) return 2170;
        if (app.identify_state_.source != identify_ui::State::Source::screenscraper) app.identifySwitchSource();
        if (!capture(L"identify-screenscraper.png")) return 2171;
        app.closeIdentify();
        app.openIdentifyFor(celeste_game);
        if (app.identify_state_.emulated || app.identify_state_.source != identify_ui::State::Source::rawg) return 2172;
        app.closeIdentify();
    }
    // Biblioteka → Folder filmów: downloaded videos in the old folder → asked whether they move along.
    {
        const auto media = userConfigDirectory() / L"media" / L"steam" / L"videos";
        std::filesystem::create_directories(media);
        { std::ofstream video(media / L"620.mp4", std::ios::binary); video << std::string(3 * 1024 * 1024, 'v'); }
        const auto chosen = root / L"Filmy";
        app.changeVideoFolder(chosen);
        if (!app.exit_prompt_ || app.prompt_kind_ != Launcher::PromptKind::move_videos || app.video_move_bytes_ != 3 * 1024 * 1024 ||
            !app.settings_.video_folder.empty()) return 2173;
        app.launch_animation_started_.reset();
        if (!capture(L"move-videos-prompt.png")) return 2174;
        app.exitPromptMove(1); // "Zostaw w starym folderze": only the setting changes.
        app.exitPromptAccept();
        if (app.exit_prompt_ || app.settings_.video_folder != chosen || !std::filesystem::exists(media / L"620.mp4")) return 2175;
        app.changeVideoFolder({}); // Back to the default: nothing of Launchme's in the chosen folder, no question.
        if (app.exit_prompt_ || !app.settings_.video_folder.empty()) return 2176;
    }
    return 0;
}
