int LauncherUiTest::companionSocialDynamics(HWND window) {
    const companion_fixture::Cast cast;
    const auto hana = companion_fixture::hana;
    Launcher app;
    app.hwnd_ = window; app.execute_ = fakeExecute;
    app.settings_.companion_look = "hana";
    app.settings_.companion_gamification = false;
    app.settings_.companion_guests = false;
    app.settings_.companion_semantic_memory = false;
    app.settings_.companion_weather = false;
    app.settings_.launch_animation = false;
    app.settings_.screen_transitions = false;
    if (!app.renderer_.initialize(window)) return 8500;
    app.screen_ = ScreenMode::companion;
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
    struct Fixture { std::mutex mutex; std::string reply, body; int calls = 0; bool fail = false; };
    const auto fixture = std::make_shared<Fixture>();
    app.companion_.post = [fixture](const std::wstring&, const std::wstring&, const std::string& body) {
        std::lock_guard lock(fixture->mutex); fixture->body = body; ++fixture->calls;
        if (fixture->fail) throw std::runtime_error("summary fixture transport failure");
        auto root = json::object(), candidate = json::object(), content = json::object(), part = json::object();
        part.properties["text"] = json::string(fixture->reply);
        auto parts = json::array(); parts.elements.push_back(std::move(part)); content.properties["parts"] = std::move(parts);
        content.properties["role"] = json::string("model"); candidate.properties["content"] = std::move(content);
        auto list = json::array(); list.elements.push_back(std::move(candidate)); root.properties["candidates"] = std::move(list);
        const auto result = json::serialize(root); return HttpResponse{200, {result.begin(), result.end()}};
    };
    app.companionMemory().note(L"An old memory to preserve", 100, "hana");
    app.companionMemory().note(L"Rin private memory", 100, "rin");
    app.companionSocialPrepare(hana);
    auto& self = app.companionMindState().of("hana");
    self.social.reflection_attempt = app.companionMindContext(hana).now;
    const auto wait = [&] {
        for (const auto end = GetTickCount64() + 5000; GetTickCount64() < end; Sleep(5)) {
            MSG m{};
            while (PeekMessageW(&m, window, companion_message, companion_message, PM_REMOVE))
                app.onCompanion(std::unique_ptr<CompanionMessage>(reinterpret_cast<CompanionMessage*>(m.lParam)));
            if (!app.companion_.busy && app.companion_.summary_pending.empty()) return true;
        }
        return false;
    };
    const auto send = [&](const std::wstring& input, const std::string& appraisal, const std::string& reply = "[smile]Okay.") {
        { std::lock_guard lock(fixture->mutex); fixture->reply = "<ocena>" + appraisal + "</ocena>" + reply; }
        app.companion_.turns = {{true, L"Initial context"}, {false, L"Hello"}, {true, input}};
        app.requestCompanion(CompanionMessage::Purpose::chat);
        return wait();
    };
    const auto before = app.companionAffinity().relationOf("hana");
    if (!send(L"Thank you for being here", R"({"social":{"intent":"listen","signal":"supportive","confidence":0.9,"quote":"Thank you for being here"}})")) return 8501;
    const auto after = app.companionAffinity().relationOf("hana");
    if (after.trust <= before.trust || companion::affinity::pointsOf(app.companionAffinity().points, "hana") != 0 || self.social.events.empty()) return 8502;
    const auto stage = app.companionStage(hana);
    app.settings_.companion_gamification = true;
    if (app.companionStage(hana) != stage) return 8503;
    app.settings_.companion_gamification = false;
    const auto snapshot = self.policy.baseline_reward;
    if (!send(L"I am busy", R"({"social":{"intent":"defer","signal":"busy","confidence":0.9,"quote":"I am busy","topic":{"name":"work","state":"deferred","hours":24,"quote":"I am busy"}}})")) return 8504;
    if (app.companionAffinity().relationOf("hana").trust != after.trust || self.policy.baseline_reward != snapshot || self.social.topics.empty()) return 8505;
    if (!send(L"We agree to stay friends", R"({"social":{"intent":"answer","agreement":{"role":"friend","mutual":true,"quote":"agree to stay friends"}}})")) return 8506;
    if (app.companionAffinity().relationOf("hana").role != companion::RelationRole::friend_role) return 8507;
    if (!send(L"Hello again", R"({"social":{"own_feeling":"affection","perceived_player":"perhaps interested"}})", "[relacja:romans][smile]Hello.")) return 8508;
    if (app.companionAffinity().relationOf("hana").role != companion::RelationRole::friend_role) return 8509;
    if (!send(L"I work at Atlas", R"({"wiem":[{"t":"Works at Atlas","z":"powiedzial","p":0.9,"fact":"employment.company"}]})")) return 8510;
    if (!send(L"Now I work at Borealis", R"({"wiem":[{"t":"Works at Borealis","z":"powiedzial","p":0.9,"fact":"employment.company"}]})")) return 8511;
    const auto fact = std::find_if(app.companionMemory().notes.begin(), app.companionMemory().notes.end(), [](const auto& n) { return n.fact_key == "employment.company"; });
    if (fact == app.companionMemory().notes.end() || fact->previous.size() != 1 || fact->previous.front().text != L"Works at Atlas") return 8512;
    if (app.companionMemory().notesOf("rin").size() != 1) return 8513;

    // Grounded reflection uses offered ids from distinct turns; no extra per-turn model call.
    std::vector<std::string> ids; std::string previous_turn;
    for (const auto& event : self.social.evidence(app.companionMindContext(hana).now)) {
        if (ids.empty() || event.turn != previous_turn) { ids.push_back(event.id); previous_turn = event.turn; }
        if (ids.size() == 2) break;
    }
    if (ids.size() != 2) return 8514;
    self.social.reflection_attempt = 0;
    { std::lock_guard lock(fixture->mutex); fixture->reply = "<summary>Talked about work.</summary><reflection>Work has been changing lately.</reflection><evidence>" + ids[0] + "|" + ids[1] + "</evidence>"; }
    app.requestCompanionSummary(true);
    if (!wait() || app.companionCognition().reflections.empty() || app.companionCognition().reflections.back().evidence_ids.size() != 2) return 8515;
    { std::lock_guard lock(fixture->mutex); if (fixture->calls != 7) return 8516; }
    // The same source state, including historical facts and the journal, survives restart serialization.
    const auto mind = companion::mind::State::load(app.companionMindState().save());
    const auto memory = companion::Memory::load(app.companionMemory().save());
    if (!mind || !memory || mind->find("hana")->social.events.size() != self.social.events.size() || memory->notes.size() != app.companionMemory().notes.size()) return 8517;
    app.companionMind(true);
    app.companion_.chat.mind_age = 10;
    const auto nodes = app.companionMindNodes();
    const auto event = std::find_if(nodes.begin(), nodes.end(), [](const auto& n) { return n.category == tr(L"Zdarzenie w relacji"); });
    if (event == nodes.end()) return 8518;
    app.companion_.chat.mind_selected = static_cast<int>(event - nodes.begin());
    wchar_t folder[4096]{};
    if (GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096))
        if (!app.renderer_.captureForTest(std::filesystem::path(folder) / L"companion-social-journal.png", [&] { app.paint(); })) return 8519;

    // A failed diary releases its slot and leaves the conversation available for a later retry.
    app.companion_.turns = {{true, L"Context"}, {true, L"First message"}, {false, L"Reply"}, {true, L"Second message"}};
    app.companion_.summarized = 0;
    { std::lock_guard lock(fixture->mutex); fixture->fail = true; }
    app.requestCompanionSummary();
    if (!wait() || app.companion_.summarized != 0) return 8520;
    { std::lock_guard lock(fixture->mutex); fixture->fail = false; fixture->reply = "<summary>A retried diary.</summary>"; }
    app.requestCompanionSummary();
    if (!wait() || app.companion_.summarized != app.companion_.turns.size()) return 8521;
    { std::lock_guard lock(fixture->mutex); if (fixture->calls != 9) return 8522; }

    // Forgetting invalidates replies and independent background results from the old context.
    const auto old_epoch = app.companion_.memory_epoch;
    const auto old_generation = app.companion_.generation;
    app.companion_.busy = app.companion_.chat.waiting = true;
    app.companionForget();
    if (app.companion_.busy || app.companion_.chat.waiting) return 8523;
    const auto lines = app.companion_.chat.log.size();
    for (const auto purpose : {CompanionMessage::Purpose::chat, CompanionMessage::Purpose::summary,
                               CompanionMessage::Purpose::reading, CompanionMessage::Purpose::life}) {
        auto stale = std::make_unique<CompanionMessage>();
        stale->purpose = purpose; stale->generation = old_generation; stale->memory_epoch = old_epoch;
        stale->reply = companion::parseText(L"<summary>Forgotten summary</summary>Forgotten reply");
        stale->notes = {L"Forgotten fact"}; stale->summary_look = stale->reply_look = "hana";
        stale->daily_readings.push_back({100, 20261027, "hana", L"Old topic", L"Old reading"});
        stale->life_to = 100;
        app.onCompanion(std::move(stale));
    }
    if (!app.companionMemory().notes.empty() || !app.companionMemory().summaries.empty() ||
        !app.companionCognition().readings.empty() || app.companionMindState().find("hana") ||
        app.companion_.chat.log.size() != lines) return 8524;
    if (!send(L"New conversation", R"({"wiem":[{"t":"New fact","z":"powiedzial","p":0.9}]})") ||
        app.companionMemory().notes.empty()) return 8525;
    // Two owners starting in the same second retain their independent latest snapshots.
    app.companion_.persist = true;
    app.companion_.chat.queue.clear();
    app.companion_.chat_started = app.companionNow();
    for (const auto look : {hana, companion_fixture::rin}) {
        app.companion_.chat_look = look;
        app.companion_.chat.log = {{true, look == hana ? L"Hana's retained chat" : L"Rin's retained chat"}};
        app.companionSaveChat();
    }
    {
        // Both character identities must have independent scheduled write watermarks.
        const auto io = app.companion_.chat_io;
        std::lock_guard lock(io->mutex);
        if (io->newest.size() != 2) return 8526;
    }
    bool both_saved = false;
    for (const auto end = GetTickCount64() + 5000; GetTickCount64() < end; Sleep(5)) {
        std::lock_guard lock(app.companion_.chat_io->mutex);
        const auto first = companion::chats::read(companion::chats::file(userConfigDirectory(), hana, app.companion_.chat_started));
        const auto second = companion::chats::read(companion::chats::file(userConfigDirectory(), companion_fixture::rin, app.companion_.chat_started));
        if (first && second && first->lines.back().text == L"Hana's retained chat" && second->lines.back().text == L"Rin's retained chat") { both_saved = true; break; }
    }
    if (!both_saved) return 8527;
    return 0;
}
