int LauncherUiTest::companionSemanticMemory(HWND window) {
    namespace semantic = companion::semantic;
    const companion_fixture::Cast cast;
    Launcher app;
    app.hwnd_ = window; app.execute_ = fakeExecute;
    app.settings_.companion_look = "hana";
    app.settings_.companion_guests = false;
    app.settings_.companion_gamification = false;
    app.settings_.companion_weather = false;
    app.settings_.launch_animation = false;
    if (!app.renderer_.initialize(window)) return 8400;
    struct Encoder : semantic::Embedder {
        std::string identity() const override { return "ui-fixture-384"; }
        semantic::Vector encode(std::wstring_view text, bool query) override {
            semantic::Vector result{};
            result[query || text.starts_with(L"Only ") ? 0 : 1] = 1;
            return result;
        }
    };
    auto& memory = app.companionMemory();
    memory.note(L"Only Hana remembers the old lighthouse", 100, "hana");
    memory.note(L"Only Rin knows the confidential codeword", 100, "rin");
    for (int i = 0; i < 50; ++i) memory.note(L"A routine detail number " + std::to_wstring(i), 200 + i, "hana");
    app.companion_.semantic = std::make_shared<semantic::Service>(userConfigDirectory() / L"companion_vectors.dat", [] { return std::make_unique<Encoder>(); });
    app.companionSemantic();
    for (const auto end = GetTickCount64() + 5000; GetTickCount64() < end && app.companion_.semantic->progress().state != semantic::Progress::State::ready; Sleep(5)) {}
    if (app.companion_.semantic->progress().state != semantic::Progress::State::ready) return 8401;
    struct Seen { std::mutex mutex; std::string body; bool tool_result = false; };
    const auto seen = std::make_shared<Seen>();
    app.settings_.companion_provider = AppSettings::CompanionProvider::foundry;
    app.companion_.probe = [] {
        companion::Probe p;
        p.environment = [](const wchar_t* name) -> std::wstring {
            if (std::wstring_view(name) == L"FOUNDRY_HOST") return L"https://fixture.palantirfoundry.test/";
            if (std::wstring_view(name) == L"FOUNDRY_TOKEN") return L"fixture.token.value.0123456789abcdef";
            return {};
        };
        return p;
    };
    app.companion_.post = [seen](const std::wstring&, const std::wstring&, const std::string& body) {
        std::lock_guard lock(seen->mutex); seen->body = body;
        std::string response = R"({"candidates":[{"content":{"role":"model","parts":[{"text":"[smile]I remember."}]},"finishReason":"STOP"}]})";
        if (body.find("functionResponse") != body.npos) {
            const auto start = body.find("functionResponse");
            seen->tool_result = body.find("Only Hana remembers", start) != body.npos;
        } else if (body.find("USE_RECALL_TOOL") != body.npos) {
            response = R"({"candidates":[{"content":{"role":"model","parts":[{"functionCall":{"name":"przypomnij","args":{"temat":"paraphrase"}}}]},"finishReason":"STOP"}]})";
        }
        return HttpResponse{200, {response.begin(), response.end()}};
    };
    app.companion_.clock = [] { SYSTEMTIME t{}; t.wYear = 2026; t.wMonth = 10; t.wDay = 27; t.wHour = 15; return t; };
    const auto wait = [&] {
        for (const auto end = GetTickCount64() + 5000; GetTickCount64() < end; Sleep(5)) {
            MSG m{};
            while (PeekMessageW(&m, window, companion_message, companion_message, PM_REMOVE))
                app.onCompanion(std::unique_ptr<CompanionMessage>(reinterpret_cast<CompanionMessage*>(m.lParam)));
            if (!app.companion_.busy) return true;
        }
        return false;
    };
    app.companion_.turns = {{true, L"Initial context"}, {false, L"Hello"}, {true, L"paraphrase"}};
    app.requestCompanion(CompanionMessage::Purpose::chat);
    if (!wait()) return 8402;
    {
        std::lock_guard lock(seen->mutex);
        if (seen->body.find("Only Hana remembers the old lighthouse") == seen->body.npos ||
            seen->body.find("Only Rin knows") != seen->body.npos || seen->body.find("semantic_memory") != seen->body.npos) return 8403;
    }
    // Real request/tool plumbing with an offline game index and a mocked model transport.
    auto library = std::make_unique<LibraryMessage>(); library->account = 1;
    library->games.push_back(Game{200, L"Fixture game"});
    app.onLibrary(std::move(library)); app.startGameIndex();
    for (const auto end = GetTickCount64() + 5000; GetTickCount64() < end && app.game_index_ && !app.game_index_->loaded(); Sleep(5)) {}
    if (!app.game_index_ || !app.game_index_->loaded()) return 8404;
    app.companion_.turns = {{true, L"Initial context"}, {false, L"Hello"}, {true, L"USE_RECALL_TOOL"}};
    app.requestCompanion(CompanionMessage::Purpose::chat);
    if (!wait()) return 8405;
    { std::lock_guard lock(seen->mutex); if (!seen->tool_result) return 8406; }
    // Disabling semantic memory leaves every source note intact and persists as a setting.
    const auto source = memory.save();
    app.openSettings();
    app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::companion));
    app.buildSettingsRows();
    const auto row = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& r) { return r.id == settings_ui::Id::companion_semantic_memory; });
    if (row == app.settings_rows_.end()) return 8407;
    app.settings_state_.enter(static_cast<int>(app.settings_rows_.size()));
    app.settings_state_.selected[app.settings_state_.category] = static_cast<int>(row - app.settings_rows_.begin());
    app.revealSetting(settings_ui::Id::companion_semantic_memory);
    wchar_t folder[4096]{};
    if (GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096))
        if (!app.renderer_.captureForTest(std::filesystem::path(folder) / L"companion-semantic-settings.png", [&] { app.paint(); })) return 8408;
    app.settingsActivate();
    if (app.settings_.companion_semantic_memory || app.companion_.semantic || memory.save() != source) return 8409;
    app.saveSettings();
    if (loadAppSettings().companion_semantic_memory) return 8410;
    return 0;
}
