// Included after LauncherUiTest: Launchme's own updates (src/app/updates.cpp) with a fake GitHub,
// a package built here and a recorded restart. A newer release brings one notification (none for a
// skipped version, unless asked), System → Aktualizacje's row and the desktop top bar's icon; one
// click downloads, checks the SHA-256, unpacks and swaps the files in a folder of the test's own,
// then "restarts". A game in progress, a damaged package and a failed restart are refused or
// rolled back. The next start removes the old files and shows Co nowego once.
// Captures: updates-*.png.
int LauncherUiTest::updates(HWND window) {
    using Phase = Launcher::UpdateState::Phase;
    using Modal = Launcher::Modal;
    const auto root = userConfigDirectory() / L"update-fixture";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    const auto app_dir = root / L"app";
    std::filesystem::create_directories(app_dir / L"plugin-packages");
    if (!atomic_file::write(app_dir / L"launchme.exe", std::string("old exe")) ||
        !atomic_file::write(app_dir / L"plugin-packages" / L"launchme.store.zip", std::string("private"))) return 9200;
    // The release's package: built here, its SHA-256 measured.
    const auto package = root / L"package.zip";
    {
        mz_zip_archive zip{};
        if (!mz_zip_writer_init_heap(&zip, 0, 0)) return 9201;
        const std::pair<const char*, std::string> entries[]{{"launchme/launchme.exe", "new exe"}, {"launchme/plugin-packages/launchme.howlongtobeat.zip", "hltb"}};
        for (const auto& [name, content] : entries)
            if (!mz_zip_writer_add_mem(&zip, name, content.data(), content.size(), static_cast<mz_uint>(MZ_DEFAULT_LEVEL))) return 9201;
        void* data = nullptr; size_t size = 0;
        if (!mz_zip_writer_finalize_heap_archive(&zip, &data, &size)) return 9201;
        const std::string bytes(static_cast<char*>(data), size);
        mz_zip_writer_end(&zip);
        if (!atomic_file::write(package, bytes)) return 9201;
    }
    const auto sha = updater::sha256File(package);
    const auto size = std::filesystem::file_size(package);
    std::string answer_digest = sha;
    const auto release = [&](const std::string& tag) {
        return R"({"tag_name":"v)" + tag + R"(","html_url":"https://github.com/uxandai/launchme/releases/tag/v)" + tag +
               R"(","body":"### Install\n\n1. Unpack.\n\n### New\n\n- **Achievement card**: name, date and rarity.\n\nSHA-256: `x`\n","assets":[{"name":"launchme-)" + tag +
               R"(-win64.zip","browser_download_url":"https://github.com/uxandai/launchme/releases/download/v)" + tag + "/launchme-" + tag +
               R"(-win64.zip","size":)" + std::to_string(size) + R"(,"digest":"sha256:)" + answer_digest + R"("}]})";
    };

    Launcher app;
    app.hwnd_ = window; app.execute_ = fakeExecute;
    app.settings_.launch_animation = false;
    app.overlay_backend_.foreground = [window] { return window; };
    if (!app.renderer_.initialize(window)) return 9202;
    auto library = std::make_unique<LibraryMessage>(); library->account = 1;
    library->games = {Game{9201, L"Update fixture"}};
    app.onLibrary(std::move(library)); app.showCategory(0);
    app.app_version_ = L"0.1.7";
    app.update_app_dir_ = app_dir;
    std::string latest = "0.1.8";
    int asked = 0;
    app.update_get_ = [&](const std::wstring& url) {
        ++asked;
        if (url != updater::latest_url) return HttpResponse{404};
        const auto body = release(latest);
        return HttpResponse{200, std::vector<uint8_t>(body.begin(), body.end())};
    };
    bool damaged = false;
    app.update_download_ = [&](const updater::Release& r, const std::filesystem::path& part, const std::function<void(uint64_t)>& progress) {
        if (r.package_url.find(L"launchme-" + r.version + L"-win64.zip") == std::wstring::npos) return false;
        progress(r.package_size / 2);
        std::error_code ec;
        if (!std::filesystem::copy_file(package, part, std::filesystem::copy_options::overwrite_existing, ec)) return false;
        if (damaged) { std::ofstream out(part, std::ios::binary | std::ios::app); out << "x"; }
        progress(r.package_size);
        return true;
    };
    std::vector<std::filesystem::path> restarted;
    bool restart_works = true;
    app.update_restart_ = [&](const std::filesystem::path& exe) { restarted.push_back(exe); return restart_works; };
    const auto pump = [&](auto done) {
        const auto end = GetTickCount64() + 8000;
        while (!done() && GetTickCount64() < end) {
            MSG message{};
            while (PeekMessageW(&message, window, remote_message, remote_message, PM_REMOVE))
                app.onRemote(std::unique_ptr<RemoteMessage>(reinterpret_cast<RemoteMessage*>(message.lParam)));
            Sleep(5);
        }
        return done();
    };
    const auto settled = [&] { return app.update_.phase != Phase::checking && app.update_.phase != Phase::downloading; };
    const auto captureOf = [&](Launcher& shown, const wchar_t* name) {
        wchar_t folder[4096]{};
        if (!GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096)) { shown.paint(); return true; }
        return shown.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { shown.paint(); });
    };
    const auto capture = [&](const wchar_t* name) { return captureOf(app, name); };
    const auto row = [&]() -> const settings_ui::Row* {
        const auto found = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& r) { return r.id == settings_ui::Id::update; });
        return found == app.settings_rows_.end() ? nullptr : &*found;
    };

    // On by default; the same version is no update.
    if (!app.settings_.update_check || app.updateAvailable()) return 9203;
    latest = "0.1.7";
    app.checkForUpdate(false);
    if (!pump(settled) || app.updateAvailable() || !app.notifications_.empty()) return 9204;
    // A newer one: one notification pointing to System, the row, the desktop icon.
    latest = "0.1.8";
    app.checkForUpdate(false);
    if (!pump(settled) || !app.updateAvailable() || app.update_.available->version != L"0.1.8" || app.update_.available->sha256 != sha) return 9205;
    if (app.notifications_.size() != 1 || app.notifications_.front().title.find(L"0.1.8") == std::wstring::npos ||
        app.notifications_.front().subtitle.find(L"System") == std::wstring::npos) return 9206;
    app.checkForUpdate(false);
    if (!pump(settled) || app.notifications_.size() != 1) return 9207; // Once per version.
    const auto tools = app.desktopTools();
    if (tools.empty() || tools.front() != desktop_ui::Tool::update) return 9208;
    app.openSettings(); app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::system)); app.buildSettingsRows();
    if (!row() || app.settings_rows_.front().id != settings_ui::Id::update || row()->title.find(L"0.1.8") == std::wstring::npos ||
        row()->value != L"Aktualizuj") return 9209;
    if (!capture(L"updates-settings.png")) return 9210;
    { // The desktop window: the accent icon first on the top bar, its tooltip the row's.
        Launcher desk;
        desk.hwnd_ = window; desk.execute_ = fakeExecute;
        desk.settings_.launch_animation = false; desk.settings_.tour_done = true; desk.settings_.glance = desk.settings_.companion = false;
        desk.overlay_backend_.foreground = [window] { return window; };
        if (!desk.renderer_.initialize(window)) return 9233;
        desk.desktop_mode_ = true; desk.audio_.setDesktop(true);
        auto games = std::make_unique<LibraryMessage>(); games->account = 1;
        games->games = {Game{9201, L"Update fixture"}};
        desk.onLibrary(std::move(games)); desk.showCategory(0);
        desk.app_version_ = L"0.1.7"; desk.update_.available = app.update_.available;
        const auto bar = desk.desktopTools();
        if (bar.empty() || bar.front() != desktop_ui::Tool::update || desk.desktopTooltip({desktop_ui::Target::tool, 0}).find(L"0.1.8") == std::wstring::npos) return 9234;
        desk.desktop_hover_ = {desktop_ui::Target::tool, 0};
        if (!captureOf(desk, L"updates-desktop.png")) return 9235;
    }
    // A skipped version gets no notification; asking ("Sprawdź teraz") still says so.
    app.notifications_.clear(); app.update_.announced.clear(); app.update_.available.reset();
    app.settings_.update_skipped = L"0.1.8";
    app.checkForUpdate(false);
    if (!pump(settled) || !app.updateAvailable() || !app.notifications_.empty()) return 9211;
    app.update_.announced.clear();
    app.checkForUpdate(true);
    if (!pump(settled) || app.notifications_.size() != 1) return 9212;
    app.settings_.update_skipped.clear(); app.notifications_.clear();

    // Not under a game: it would restart Launchme.
    Game game{9201, L"Update fixture"};
    app.game_sessions_.try_emplace(gameKey(game), 0).first->second.observe(GameActivity::running, 1);
    app.updateAction();
    if (app.update_.phase != Phase::idle || app.notifications_.empty() || app.notifications_.front().kind != NotificationKind::error) return 9213;
    app.game_sessions_.clear(); app.notifications_.clear();

    // A damaged package (another SHA-256): refused, nothing swapped.
    damaged = true;
    app.updateAction();
    if (app.update_.phase != Phase::downloading || app.updateFraction() < 0) return 9214;
    if (!pump(settled) || app.update_.phase != Phase::failed || app.update_.error.empty() || !restarted.empty()) return 9215;
    { std::ifstream in(app_dir / L"launchme.exe", std::ios::binary); std::string text((std::istreambuf_iterator<char>(in)), {}); if (text != "old exe") return 9216; }
    app.buildSettingsRows();
    if (!row() || row()->value != L"Spróbuj ponownie") return 9217;
    // The new EXE does not start: the old files come back.
    damaged = false; restart_works = false; app.notifications_.clear();
    app.updateAction();
    if (!pump(settled) || app.update_.phase != Phase::failed || restarted.size() != 1) return 9218;
    { std::ifstream in(app_dir / L"launchme.exe", std::ios::binary); std::string text((std::istreambuf_iterator<char>(in)), {}); if (text != "old exe") return 9219; }
    if (std::filesystem::exists(app_dir / L"plugin-packages" / L"launchme.howlongtobeat.zip") || std::filesystem::exists(userConfigDirectory() / L"whats-new.txt")) return 9220;
    // One click: downloaded, checked, swapped in, the new EXE started, this one closing.
    restart_works = true; restarted.clear();
    app.updateAction();
    if (!pump(settled) || app.update_.phase != Phase::installing || restarted.size() != 1 || restarted.front() != app_dir / L"launchme.exe" || !app.closing_) return 9221;
    {
        std::ifstream in(app_dir / L"launchme.exe", std::ios::binary); std::string text((std::istreambuf_iterator<char>(in)), {});
        std::ifstream old(app_dir / (std::wstring(L"launchme.exe") + updater::old_suffix), std::ios::binary); std::string before((std::istreambuf_iterator<char>(old)), {});
        std::ifstream store(app_dir / L"plugin-packages" / L"launchme.store.zip", std::ios::binary); std::string kept((std::istreambuf_iterator<char>(store)), {});
        if (text != "new exe" || before != "old exe" || kept != "private") return 9222;
    }
    if (!std::filesystem::exists(userConfigDirectory() / L"whats-new.txt")) return 9223;

    // The new version's first start: the old files go, Co nowego once, over everything else.
    Launcher next;
    next.hwnd_ = window; next.execute_ = fakeExecute;
    next.settings_.launch_animation = false;
    next.overlay_backend_.foreground = [window] { return window; };
    if (!next.renderer_.initialize(window)) return 9224;
    auto again = std::make_unique<LibraryMessage>(); again->account = 1;
    again->games = {Game{9201, L"Update fixture"}};
    next.onLibrary(std::move(again)); next.showCategory(0);
    next.app_version_ = L"0.1.8";
    next.update_app_dir_ = app_dir;
    next.initializeUpdates();
    if (std::filesystem::exists(app_dir / (std::wstring(L"launchme.exe") + updater::old_suffix)) || std::filesystem::exists(app_dir / updater::work_folder)) return 9225;
    if (!next.whatsNewOpen() || next.topModal() != Modal::whats_new || next.whats_new_.version != L"0.1.8" ||
        next.whats_new_.text.find(L"Achievement card") == std::wstring::npos || next.whats_new_.text.find(L"Install") != std::wstring::npos) return 9226;
    if (std::filesystem::exists(userConfigDirectory() / L"whats-new.txt")) return 9227; // Once.
    for (int i = 0; i < 40; ++i) next.whats_new_.text += L"\n\u2022 Another change in this version, line " + std::to_wstring(i); // Long enough to scroll.
    next.paint();
    next.whatsNewKey(VK_DOWN);
    if (next.whats_new_.content <= 0) return 9232;
    if (next.whats_new_.scroll <= 0) return 9228;
    if (!captureOf(next, L"updates-whats-new.png")) return 9229;
    {
        const auto view = next.renderer_.viewport();
        const auto ok = update_ui::WhatsNewLayout{view.width, view.height}.ok();
        next.click((ok.left + ok.right) * 0.5f * view.scale, (ok.top + ok.bottom) * 0.5f * view.scale, false);
    }
    if (next.whatsNewOpen() || next.topModal() != Modal::none) return 9230;
    // A file another version wrote is not this version's news.
    atomic_file::write(userConfigDirectory() / L"whats-new.txt", std::string("0.1.9\nlater"));
    Launcher third;
    third.hwnd_ = window; third.app_version_ = L"0.1.8"; third.update_app_dir_ = app_dir;
    third.initializeUpdates();
    if (third.whatsNewOpen() || std::filesystem::exists(userConfigDirectory() / L"whats-new.txt")) return 9231;
    std::filesystem::remove_all(root, ignored);
    return 0;
}
