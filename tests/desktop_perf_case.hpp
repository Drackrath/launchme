// Included after LauncherUiTest: LAUNCHME_PERF_DESKTOP=1 (with LAUNCHME_PERF_SECONDS) profiles the
// desktop window's poster grid over a large library (LAUNCHME_PERF_GAMES, default 3000). Posters and
// square covers lie in the artwork cache on disk and reach the grid through the actual disk lane
// (no network workers), the renderer's decoder workers and its uploads, as in the app.
// LAUNCHME_PERF_ART_DIR=<folder with posters\ and covers\ *.img> takes real images (hard-linked
// round-robin); without it, synthetic 600×900 JPEGs. Phases, each measured on its own:
//   open    — the grid appears; until every visible poster is shown;
//   wheel   — steady wheel notches down (LAUNCHME_PERF_SECONDS long, 20 notches/s);
//   settle  — after the wheel stops, until every visible poster is shown;
//   drag    — the scrollbar dragged across 2/3 of the library in 1.5 s, then settle;
//   flick   — a free-spinning wheel, 60 notches/s for 2 s, then settle;
//   return  — a jump back into rows passed during `wheel`, until every visible poster is shown.
// Per phase: frame intervals (p50/p95/p99/max, hitches above 1.5 refresh periods), tick and paint
// CPU, bitmap uploads, the visible rows' art/poster coverage while moving, and the time to a
// complete view. Stdout and the optional `<LAUNCHME_PERF_TRACE>.desktop.csv` carry no titles.
#include <functional>
#include <iomanip>

namespace desktop_perf {
struct Phase {
    std::string name;
    std::vector<double> intervals, tick, paint, draw, present, upload, art, posters; // draw: CPU before EndDraw, uploads excluded.
    unsigned uploads = 0, extra_paints = 0; // Paints from queued WM_PAINT between frames (as in the app).
    double extra_paint_ms = 0;
    double complete_ms = -1, art_ms = -1; // Every visible poster / any image shown; -1: not reached.
    uint64_t decoded = 0; double decode_ms = 0;
};
inline double percentile(std::vector<double> values, double p) {
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    return values[static_cast<size_t>((values.size() - 1) * p)];
}
inline double mean(const std::vector<double>& values) {
    return values.empty() ? 0 : std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
}
inline double largest(const std::vector<double>& values) { return values.empty() ? 0 : *std::max_element(values.begin(), values.end()); }
} // namespace desktop_perf

int LauncherUiTest::profileDesktop(HWND window, int seconds) {
    using namespace desktop_perf;
    using Clock = std::chrono::steady_clock;
    Launcher app;
    const auto stop = std::unique_ptr<Launcher, std::function<void(Launcher*)>>(&app, [window](Launcher* launcher) {
        launcher->alive_->store(false); launcher->jobs_->wake.notify_all(); launcher->work_.shutdown(std::chrono::seconds(4));
        MSG message{};
        while (PeekMessageW(&message, window, cover_message, cover_message, PM_REMOVE))
            delete reinterpret_cast<CoverMessage*>(message.lParam);
    });
    app.hwnd_ = window; app.execute_ = fakeExecute;
    app.foreground_ = [](HWND) { return true; };
    app.settings_.launch_animation = false; app.settings_.tour_done = true;
    app.settings_.glance = app.settings_.companion = false;
    app.desktop_mode_ = true;
    if (!app.renderer_.initialize(window)) return 1;
    app.renderer_.startDecoderForTest();
    wchar_t full_ram[4]{};
    app.settings_.low_memory = !(GetEnvironmentVariableW(L"LAUNCHME_PERF_FULL_RAM", full_ram, 4) == 1 && full_ram[0] == L'1');
    // Kompresja grafik as the app has it (on), or LAUNCHME_PERF_BC1=0/1.
    wchar_t bc1[4]{};
    if (GetEnvironmentVariableW(L"LAUNCHME_PERF_BC1", bc1, 4) == 1 && (bc1[0] == L'0' || bc1[0] == L'1')) app.settings_.texture_compression = bc1[0] == L'1';
    app.applyLowMemory();
    app.initializeLibraryPreferences();
    app.renderer_.profile(true);
    app.profiler_.configure();

    // The library and its artwork on disk: every game has a poster and a square cover (with its
    // saved automatic crop, as after the first OCR pass); one in four is not installed.
    wchar_t value[4096]{};
    const uint32_t game_count = GetEnvironmentVariableW(L"LAUNCHME_PERF_GAMES", value, 16) ?
        static_cast<uint32_t>(std::clamp(_wtoi(value), 8, 20000)) : 3000u;
    const auto cache = coverCacheDirectory();
    const auto posters_dir = cache.parent_path() / L"posters";
    const auto sources_dir = cache.parent_path() / L"perf-sources";
    std::error_code error;
    for (const auto& folder : {cache, posters_dir, sources_dir}) std::filesystem::create_directories(folder, error);
    std::vector<std::filesystem::path> poster_sources, cover_sources;
    std::wstring art_dir;
    if (const auto length = GetEnvironmentVariableW(L"LAUNCHME_PERF_ART_DIR", value, 4096); length && length < 4096) art_dir = value;
    const auto collect = [&](const std::filesystem::path& folder, std::vector<std::filesystem::path>& out) {
        for (const auto& entry : std::filesystem::directory_iterator(folder, error))
            if (entry.is_regular_file() && (entry.path().extension() == L".img" || entry.path().extension() == L".jpg" || entry.path().extension() == L".png"))
                out.push_back(entry.path());
        std::sort(out.begin(), out.end());
    };
    if (!art_dir.empty()) {
        collect(std::filesystem::path(art_dir) / L"posters", poster_sources); collect(std::filesystem::path(art_dir) / L"covers", cover_sources);
        // Copied once into the run's own folder: the hard links below never point at the caller's files.
        size_t n = 0;
        for (auto* list : {&poster_sources, &cover_sources})
            for (auto& source : *list) {
                const auto copy = sources_dir / (std::to_wstring(n++) + source.extension().wstring());
                if (!std::filesystem::copy_file(source, copy, std::filesystem::copy_options::overwrite_existing, error)) return 3;
                source = copy;
            }
    }
    const auto write = [](const std::filesystem::path& file, const std::vector<uint8_t>& bytes) {
        std::ofstream out(file, std::ios::binary); out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return static_cast<bool>(out);
    };
    if (poster_sources.empty() || cover_sources.empty()) {
        poster_sources.clear(); cover_sources.clear();
        for (uint32_t i = 0; i < 48; ++i) {
            const auto poster = sources_dir / (L"poster" + std::to_wstring(i) + L".jpg"), cover = sources_dir / (L"cover" + std::to_wstring(i) + L".jpg");
            if (!write(poster, jpegFixture(600, 900, i)) || !write(cover, jpegFixture(600, 900, 1000 + i))) return 3;
            poster_sources.push_back(poster); cover_sources.push_back(cover);
        }
    }
    auto library = std::make_unique<LibraryMessage>(); library->account = 1;
    size_t linked = 0, copied = 0;
    const auto place = [&](const std::filesystem::path& source, const std::filesystem::path& target) {
        std::filesystem::remove(target, error);
        if (CreateHardLinkW(target.c_str(), source.c_str(), nullptr)) { ++linked; return true; }
        ++copied; return std::filesystem::copy_file(source, target, error);
    };
    for (uint32_t i = 0; i < game_count; ++i) {
        // Distinct titles of varied length, as a real library (text layouts are not all one entry).
        static constexpr const wchar_t* words[] = {L"Shadow", L"Legends of", L"Star", L"Crimson", L"Iron", L"The Last", L"Ocean", L"Neon", L"Kingdom", L"Echoes"};
        Game game{1000 + i, std::wstring(words[i % 10]) + L' ' + std::wstring(words[(i / 10) % 10]) + L' ' + std::to_wstring(i)};
        game.installed = i % 4 != 0;
        game.last_played = i < 20 ? 100 + i : 0;
        const auto poster = gameArtworkPath(game, ArtworkKind::poster, cache), cover = gameArtworkPath(game, ArtworkKind::cover, cache);
        if (!place(poster_sources[i % poster_sources.size()], poster) || !place(cover_sources[i % cover_sources.size()], cover)) return 4;
        if (!storeAutomaticTextCrop(cover, std::filesystem::path(cover).replace_extension(L".focus"), game.name, {0.5f, 0.5f})) return 5;
        library->games.push_back(std::move(game));
    }
    {
        // Decode cost of one poster as the grid asks for it (single thread, warm, 20 runs each).
        Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.GetAddressOf()));
        std::ifstream input(poster_sources.front(), std::ios::binary);
        const std::vector<uint8_t> bytes(std::istreambuf_iterator<char>{input}, {});
        std::cout << "decode probe (first poster, " << bytes.size() / 1024 << " KB):";
        for (const uint32_t edge : {1024u, 512u, 256u}) {
            const auto began = Clock::now();
            uint32_t w = 0, h = 0;
            for (int i = 0; i < 20; ++i) { const auto image = decodeImage(wic.Get(), bytes, {true, false, false, edge}); w = image.width; h = image.height; }
            std::cout << " edge " << edge << " -> " << w << 'x' << h << ' ' << std::chrono::duration<double, std::milli>(Clock::now() - began).count() / 20 << " ms;";
        }
        std::cout << '\n';
    }
    app.onLibrary(std::move(library));
    app.showCategory(0);
    if (!app.desktopActive() || app.cards_.size() != game_count) return 6;
    app.startArtworkWorkers(false); // The disk lane only: nothing reaches the network.

    MONITORINFOEXW monitor{}; monitor.cbSize = sizeof(monitor);
    DEVMODEW mode{}; mode.dmSize = sizeof(mode);
    if (GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor) &&
        EnumDisplaySettingsW(monitor.szDevice, ENUM_CURRENT_SETTINGS, &mode)) app.refresh_hz_ = static_cast<int>(mode.dmDisplayFrequency);
    RECT bounds{}; GetClientRect(window, &bounds);
    ShowWindow(window, SW_SHOW); UpdateWindow(window);
    const auto layout0 = app.desktopLayout();
    std::cout << "Desktop grid: viewport=" << bounds.right << 'x' << bounds.bottom << "; scale=" << app.desktopScale() << "; display Hz=" << app.refresh_hz_
              << "; games=" << game_count << "; columns=" << layout0.columns() << "; poster=" << layout0.cellWidth() << 'x' << layout0.posterHeight()
              << " DIP; art=" << (art_dir.empty() ? "synthetic JPEG 600x900" : "LAUNCHME_PERF_ART_DIR") << " (" << poster_sources.size() << " posters, "
              << cover_sources.size() << " covers; " << linked << " links, " << copied << " copies)\n";

    // Visible rows' coverage: any image (a cover may stand in) and the poster itself.
    const auto coverage = [&](double& art, double& posters) {
        const auto L = app.desktopLayout();
        const auto [first, end] = L.visibleRange(app.cards_.size(), app.desktop_scroll_, 0);
        size_t total = 0, with_art = 0, with_poster = 0;
        for (size_t i = first; i < end; ++i) {
            const auto cell = L.poster(i, app.desktop_scroll_);
            if (cell.bottom <= L.gridTop() || cell.top >= L.height) continue;
            ++total;
            const bool poster = app.renderer_.posterReadyForTest(i);
            with_poster += poster; with_art += poster || app.renderer_.coverReadyForTest(i);
        }
        art = total ? static_cast<double>(with_art) / total : 1.0;
        posters = total ? static_cast<double>(with_poster) / total : 1.0;
    };
    std::vector<Phase> phases;
    std::ofstream csv;
    if (const auto length = GetEnvironmentVariableW(L"LAUNCHME_PERF_TRACE", value, 4096); length && length < 4096) {
        csv.open(std::wstring(value) + L".desktop.csv");
        if (csv) csv << "phase,time_ms,frame_ms,tick_ms,paint_ms,upload_ms,uploads,art,posters\n" << std::fixed << std::setprecision(3);
    }
    FrameClock frame_clock;
    const auto run_start = Clock::now();
    Clock::time_point last_paint{};
    // Runs frames until `step(elapsed_ms)` returns false; `settle` phases also end when complete.
    // As main.cpp: queued messages first (a WM_PAINT there paints only when Launcher::deferPaint
    // declines), then the frame clock's tick and its paint (Launcher::updateFrame). Frame
    // intervals are measured between tick paints; other paints count as extra.
    const auto run = [&](const char* name, double limit_ms, const std::function<bool(double)>& step, bool settle) {
        Phase phase; phase.name = name;
        const auto decoder_before = app.renderer_.decoderForTest().completed();
        const double decode_before = app.renderer_.decoderForTest().decodeMilliseconds();
        const auto began = Clock::now();
        last_paint = {};
        double tick_ms = 0;
        bool complete = false;
        const auto paint = [&](bool frame) {
            const auto paint_start = Clock::now();
            app.paint();
            const auto now = Clock::now();
            const double paint_ms = std::chrono::duration<double, std::milli>(now - paint_start).count();
            const auto timing = app.renderer_.timing();
            phase.uploads += timing.uploads;
            if (!frame) { ++phase.extra_paints; phase.extra_paint_ms += paint_ms; phase.upload.push_back(timing.upload_ms); return; }
            double art = 0, posters = 0; coverage(art, posters);
            const double interval = last_paint == Clock::time_point{} ? 0 : std::chrono::duration<double, std::milli>(now - last_paint).count();
            last_paint = now;
            if (interval > 0) phase.intervals.push_back(interval);
            phase.tick.push_back(tick_ms); phase.paint.push_back(paint_ms); phase.upload.push_back(timing.upload_ms);
            phase.draw.push_back(timing.draw_ms - timing.upload_ms); phase.present.push_back(timing.present_ms);
            phase.art.push_back(art); phase.posters.push_back(posters);
            const double frame_elapsed = std::chrono::duration<double, std::milli>(now - began).count();
            if (art >= 1.0 && phase.art_ms < 0) phase.art_ms = frame_elapsed;
            if (posters >= 1.0 && phase.complete_ms < 0 && std::abs(app.desktop_scroll_ - app.desktop_scroll_target_) < 0.5f) phase.complete_ms = frame_elapsed;
            if (csv) csv << name << ',' << std::chrono::duration<double, std::milli>(now - run_start).count() << ',' << interval << ',' << tick_ms << ','
                         << paint_ms << ',' << timing.upload_ms << ',' << timing.uploads << ',' << art << ',' << posters << '\n';
            complete = settle && phase.complete_ms >= 0;
        };
        while (!complete) {
            const double elapsed = std::chrono::duration<double, std::milli>(Clock::now() - began).count();
            if (elapsed > limit_ms || !step(elapsed)) break;
            MSG message{};
            for (int batch = 0; batch < 64 && PeekMessageW(&message, window, 0, 0, PM_REMOVE); ++batch) {
                if (message.message == cover_message) app.onCover(std::unique_ptr<CoverMessage>(reinterpret_cast<CoverMessage*>(message.lParam)));
                else if (message.message == library_worker_error_message) delete reinterpret_cast<LibraryWorkerErrorMessage*>(message.lParam);
                else if (message.message == WM_PAINT) { if (app.deferPaint()) ValidateRect(window, nullptr); else paint(false); }
                else { TranslateMessage(&message); DispatchMessageW(&message); }
            }
            frame_clock.update(window, true);
            if (frame_clock.ready()) {
                frame_clock.consume();
                app.setFrameRate(frame_clock.targetHz(), frame_clock.refreshHz());
                const auto tick_start = Clock::now();
                app.tick();
                tick_ms = std::chrono::duration<double, std::milli>(Clock::now() - tick_start).count();
                // Launcher::updateFrame, painting directly (this test window has no launcher WndProc).
                if (app.paint_deferred_) { app.paint_deferred_ = false; InvalidateRect(window, nullptr, FALSE); }
                if (GetUpdateRect(window, nullptr, FALSE)) { app.frame_painting_ = true; paint(true); app.frame_painting_ = false; }
            }
            if (!complete) frame_clock.wait();
        }
        phase.decoded = app.renderer_.decoderForTest().completed() - decoder_before;
        phase.decode_ms = app.renderer_.decoderForTest().decodeMilliseconds() - decode_before;
        phases.push_back(std::move(phase));
    };
    const auto L = app.desktopLayout();
    const float scale = app.desktopScale();
    const int wheel_x = static_cast<int>((L.gridLeft() + L.gridRight()) * 0.5f * scale), wheel_y = static_cast<int>((L.gridTop() + L.height) * 0.5f * scale);

    run("open", 15000, [](double) { return true; }, true);
    {
        int notches = 0;
        run("wheel", seconds * 1000.0, [&](double elapsed) {
            while (notches < static_cast<int>(elapsed / 50.0)) { app.desktopWheel(static_cast<float>(wheel_x), static_cast<float>(wheel_y), -WHEEL_DELTA, false); ++notches; }
            return true;
        }, false);
    }
    const float wheel_end = app.desktop_scroll_target_;
    run("settle", 15000, [](double) { return true; }, true);
    {
        const float from = app.desktop_scroll_target_, to = L.maxScroll(app.cards_.size()) * 0.7f;
        run("drag", 1500, [&](double elapsed) {
            app.desktop_scroll_ = app.desktop_scroll_target_ = from + (to - from) * static_cast<float>(std::min(1.0, elapsed / 1500.0));
            app.desktop_scrolled_at_ = GetTickCount64(); app.desktopRequestArt(); InvalidateRect(window, nullptr, FALSE);
            return true;
        }, false);
    }
    run("drag-settle", 15000, [](double) { return true; }, true);
    {
        // A free-spinning wheel: 60 notches/s for 2 s, faster than any disk lane can follow.
        int notches = 0;
        run("flick", 2000, [&](double elapsed) {
            while (notches < static_cast<int>(elapsed / (1000.0 / 60))) { app.desktopWheel(static_cast<float>(wheel_x), static_cast<float>(wheel_y), -WHEEL_DELTA, false); ++notches; }
            return true;
        }, false);
    }
    run("flick-settle", 15000, [](double) { return true; }, true);
    app.desktop_scroll_ = app.desktop_scroll_target_ = std::max(0.0f, wheel_end - L.viewHeight() * 2);
    app.desktopRequestArt(); InvalidateRect(window, nullptr, FALSE);
    run("return", 15000, [](double) { return true; }, true);

    const double period = 1000.0 / std::max(1, app.refresh_hz_);
    std::cout << std::fixed << std::setprecision(2);
    for (const auto& phase : phases) {
        const auto hitches = std::count_if(phase.intervals.begin(), phase.intervals.end(), [&](double v) { return v > period * 1.5; });
        std::cout << phase.name << ": frames=" << phase.paint.size() << "; interval p50=" << percentile(phase.intervals, 0.5) << " p95=" << percentile(phase.intervals, 0.95)
                  << " p99=" << percentile(phase.intervals, 0.99) << " max=" << largest(phase.intervals) << " ms; hitches=" << hitches
                  << "; tick avg=" << mean(phase.tick) << " max=" << largest(phase.tick) << "; paint avg=" << mean(phase.paint) << " p95=" << percentile(phase.paint, 0.95)
                  << " max=" << largest(phase.paint) << "; draw avg=" << mean(phase.draw) << " EndDraw avg=" << mean(phase.present) << "; extra paints=" << phase.extra_paints << " (" << phase.extra_paint_ms << " ms)"
                  << "; upload max=" << largest(phase.upload) << " ms (" << phase.uploads << " bitmaps); art avg="
                  << mean(phase.art) * 100 << "% min=" << (phase.art.empty() ? 0 : *std::min_element(phase.art.begin(), phase.art.end()) * 100) << "%; posters avg="
                  << mean(phase.posters) * 100 << "%; all art at=" << phase.art_ms << " ms; complete at=" << phase.complete_ms << " ms; decoded=" << phase.decoded
                  << " (" << phase.decode_ms << " ms)\n";
    }
    const auto textures = app.renderer_.textureStats();
    PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
    K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory));
    std::cout << "end: card bitmaps=" << textures.bitmaps << " (" << textures.bytes / 1048576.0 << " MB); art_ RAM=" << app.art_.bytes() / 1048576.0
              << " MB in " << app.art_.size() << " entries; private MB=" << memory.PrivateUsage / 1048576.0 << '\n';
    std::cout << memoryReport("end");
    if (app.settings_.low_memory) {
        app.releaseMemory(); // A game in front of the launcher (memory.cpp).
        Sleep(300);
        std::cout << memoryReport("behind a game");
        app.restoreMemory();
        app.desktopRequestArt(); InvalidateRect(window, nullptr, FALSE);
        run("back", 15000, [](double) { return true; }, true);
        std::cout << "back: complete at=" << phases.back().complete_ms << " ms; " << memoryReport("back in front");
    }
    return 0;
}
