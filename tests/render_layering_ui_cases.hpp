// Pixel comparisons at intermediate animation states: hidden geometry must not
// change the foreground, while the exposed part must still be rendered.
int renderLayering(HWND window) {
    Renderer renderer;
    if (!renderer.initialize(window)) return 2670;
    renderer.steelbook_for_test = false;
    Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.GetAddressOf())))) return 2671;
    auto folder = userConfigDirectory() / L"layering";
    wchar_t capture_dir[4096]{};
    if (const auto n = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", capture_dir, 4096); n && n < 4096) folder = capture_dir;
    std::filesystem::create_directories(folder);
    std::vector<GameCard> cards(2);
    ViewContext context;
    size_t selected = 0;
    const auto capture = [&](const std::wstring& name) {
        const auto file = folder / (name + L".png");
        if (!renderer.captureForTest(file, [&] { renderer.paint(cards, selected, false, 0, 1, context); })) return DecodedImage{};
        std::ifstream input(file, std::ios::binary);
        return decodeImage(wic.Get(), std::vector<uint8_t>(std::istreambuf_iterator<char>(input), {}), {});
    };
    const auto delta = [&](const auto& a, const auto& b, settings_ui::Rect area) {
        if (!a.valid() || !b.valid() || a.width != b.width || a.height != b.height) return -1;
        const float scale = renderer.viewport().scale;
        int largest = 0;
        for (int y = int(area.top * scale); y < int(area.bottom * scale); ++y)
            for (int x = int(area.left * scale); x < int(area.right * scale); ++x)
                for (int c = 0; c < 3; ++c) {
                    const size_t p = (size_t(y) * a.width + x) * 4 + c;
                    largest = std::max(largest, std::abs(int(a.pixels[p]) - int(b.pixels[p])));
                }
        return largest;
    };
    for (const auto shape : {AppSettings::TileShape::squircle, AppSettings::TileShape::rounded, AppSettings::TileShape::square}) {
        renderer.setTileShape(shape);
        const auto suffix = L"-" + std::to_wstring(static_cast<int>(shape));
        context = {}; context.screen = ScreenMode::library; context.library_mode = library_view::View::coverflow;
        cards.assign(2, {}); renderer.resetCovers(cards.size(), false); selected = 0;
        cards[0].x = 100; cards[0].y = 260; cards[0].size = 420; cards[0].aspect = coverflow_layout::aspect;
        cards[1].x = 410; cards[1].y = 300; cards[1].size = 310; cards[1].aspect = coverflow_layout::aspect;
        for (auto& card : cards) { card.game.installed = true; card.opacity = 0.4f; }
        // An arriving case with no artwork, then a case whose poster has just arrived.
        for (const bool artwork : {false, true}) {
            if (artwork) renderer.setPoster(0, shareImage(pngFixture(85, 80, 120)));
            const auto name = L"coverflow-occlusion" + suffix + (artwork ? L"-art" : L"-loading");
            cards[0].disc = 0;
            const auto hidden = capture(name + L"-reference");
            cards[0].disc = 1;
            const auto exposed = capture(name);
            if (delta(hidden, exposed, {180, 390, 360, 550}) != 0 || // Inside its own fading case.
                delta(hidden, exposed, {425, 445, 445, 495}) != 0 || // Inside a neighbouring case.
                delta(hidden, exposed, {390, 460, 400, 490}) < 10) return 2672;
        }
        for (const bool posters : {false, true}) {
            context = {}; context.screen = ScreenMode::shelves;
            context.xmb = true; context.xmb_posters = posters; context.xmb_top = 2; context.xmb_row = 1;
            cards.assign(4, {}); renderer.resetCovers(cards.size(), false); selected = 3;
            for (auto& card : cards) { card.game.installed = true; card.opacity = 1; }
            cards[0].game.kind = GameKind::category;
            cards[0].x = 150; cards[0].y = 130; cards[0].size = 150; cards[0].opacity = 0.6f;
            cards[1].game.kind = GameKind::platform;
            cards[1].x = 322; cards[1].y = 149; cards[1].size = 112;
            cards[2].x = 181; cards[2].y = 110; cards[2].size = posters ? 132.0f : 88.0f;
            cards[3].x = 163; cards[3].y = 190; cards[3].size = posters ? 186.0f : 124.0f;
            cards[2].aspect = cards[3].aspect = posters ? xmb_layout::poster_aspect : 1;
            cards[2].game.name = cards[3].game.name = L"Scrolling game label";
            const auto name = L"xmb-occlusion" + suffix + (posters ? L"-posters" : L"-squares");
            cards[2].opacity = cards[3].opacity = 0;
            const auto reference = capture(name + L"-reference");
            cards[2].opacity = cards[3].opacity = 1;
            const auto scrolling = capture(name);
            // Both selected and unselected items pass under the category; their labels
            // also pass under neighbouring top-row tiles, rather than being drawn over them.
            if (delta(reference, scrolling, {195, 155, 250, 260}) != 0 ||
                delta(reference, scrolling, {350, 180, 400, 240}) != 0 ||
                delta(reference, scrolling, {210, 300, 240, 310}) < 10) return 2673;
        }
    }
    return 0;
}
