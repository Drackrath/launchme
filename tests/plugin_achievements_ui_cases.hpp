// Included after LauncherUiTest: achievements of a game outside Steam from an achievements plugin
// (src/app/plugin_achievements.cpp). Details asks the provider once (a pending question is not
// asked again), the answer fills the card's counts, the achievements screen shows its rows with the
// plugin's source and icons (https into achievements\<game cache key>\, or a local picture), and a
// newer answer replaces an open list. Steam games keep their own. Captures: plugin-achievements*.png.

int LauncherUiTest::pluginAchievements(HWND window) {
    Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute; app.settings_.launch_animation = false;
    if (!app.renderer_.initialize(window)) return 4100;
    plugins::Plugin plugin;
    plugin.manifest = plugins::parseManifest(R"({"apiVersion":1,"architecture":"windows-x64","id":"test.achievements","version":"1.0.0","entry":"fixture.dll",
        "name":"Achievements","iconText":"ACH","contributions":[{"id":"list","kind":"achievements","title":"Achievements","command":"list"}]})");
    plugin.enabled = true; app.plugins_.plugins.push_back(plugin); app.refreshPluginContributions();
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[32768]{};
        if (!GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, static_cast<DWORD>(std::size(folder)))) { app.paint(); return true; }
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    Game steam{620, L"Portal 2"};
    Game witcher; witcher.source = GameSource::gog; witcher.kind = GameKind::store; witcher.source_id = "1207658924"; witcher.name = L"The Witcher 3"; witcher.installed = true;
    if (app.pluginAchievementsProvider(steam) || !app.pluginAchievementsProvider(witcher)) return 4101;
    app.openDetails(witcher); app.details_loading_ = false;
    app.plugin_jobs_->jobs.clear();
    app.runtime_started_ = true; app.requestPluginAchievements(witcher); app.requestPluginAchievements(witcher); app.runtime_started_ = false;
    if (app.plugin_jobs_->jobs.size() != 1) return 4102; // Pending: not asked twice.
    auto job = app.plugin_jobs_->jobs.front(); app.plugin_jobs_->jobs.clear();
    const auto& params = job.value;
    if (job.kind != plugins::Job::Kind::achievements || job.plugin != "test.achievements" || plugins::wire::text(params, "command") != "list" ||
        plugins::wire::text(plugins::wire::field(params, "game"), "sourceId") != "1207658924" ||
        plugins::wire::field(params, "accounts").type != json::Value::Type::object) return 4103;
    // The plugin's list: one earned with an https icon, one locked with a picture on this computer, one hidden.
    const auto icon_file = userConfigDirectory() / L"plugin-achievement-icon.png";
    { const auto png = pngFixture(200, 64, 64); std::ofstream out(icon_file, std::ios::binary); out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size())); }
    const auto answer = [&](plugins::Job asked, const std::string& items) {
        auto message = std::make_unique<PluginsMessage>(); message->job = std::move(asked); message->job.epoch = app.plugin_epoch_;
        message->response = *json::parse(R"({"achievements":{"source":"GOG","items":[)" + items + "]}}");
        app.onPlugins(std::move(message));
    };
    const std::string three = R"({"id":"KILL","name":"Monster slayer","description":"Kill 100 monsters","icon":"https://images.gog.com/kill.png","achieved":true,"unlockedAt":1709634030,"percent":41.5},
        {"id":"CARD","name":"Gwent master","description":"Win a Gwent tournament","icon":)" + plugins::wire::quote(text_encoding::utf8(icon_file.wstring())) + R"(,"achieved":false,"percent":8.2},
        {"id":"END","name":"The end","description":"Spoiler","achieved":false,"hidden":true,"percent":2})";
    answer(job, three);
    if (app.details_.achievements_total != 3 || app.details_.achievements_unlocked != 1 || app.achievementsDetailAction() < 0) return 4104;
    const auto* known = app.pluginAchievements(witcher);
    if (!known || known->source != L"GOG" || known->pending || known->list.items.size() != 3) return 4105;
    app.detail_action_ = app.achievementsDetailAction();
    if (!capture(L"plugin-achievements-details.png")) return 4106;
    // Its screen: the cached list at once, the source after the title, icons asked for into the game's folder.
    app.plugin_jobs_->jobs.clear();
    app.openAchievements();
    auto& state = app.achievements_;
    if (!app.achievementsOpen() || state.items.size() != 3 || state.source != L"GOG" || state.key != gameKey(witcher) || state.app_id != 0) return 4107;
    if (state.items[0].name != L"Monster slayer" || !state.items[0].achieved || state.items[2].name != L"Ukryte osiągnięcie" || !state.items[2].description.empty()) return 4108;
    {
        std::lock_guard lock(app.achievement_jobs_->mutex);
        const auto& jobs = app.achievement_jobs_->jobs;
        if (jobs.size() != 2 || !jobs[0].icon || jobs[0].folder != text_encoding::wideLenient(gameCacheKey(witcher)) || jobs[1].url != icon_file.wstring()) return 4109;
    }
    for (size_t i = 0; i < 2; ++i) {
        auto icon = std::make_unique<AchievementsMessage>(); icon->generation = app.achievement_jobs_->generation.load(); icon->icon = true;
        icon->index = i; icon->url = state.icons[i]; icon->bytes = pngFixture(static_cast<uint8_t>(90 + i * 80), 64, 64);
        app.onAchievements(std::move(icon));
    }
    if (!capture(L"plugin-achievements.png")) return 4110;
    // Enter/A on an achievement: its card (date and rarity at once); Left/Right its neighbours;
    // Back returns to the list on the row it showed. A click on a row opens it, beside it closes.
    {
        state.selected = 0; app.launch();
        if (!state.card || state.selected != 0 || !app.achievementsOpen()) return 4120;
        if (state.items[0].earned.empty() || state.items[0].percent != 41.5f || state.items[0].share.empty()) return 4121;
        if (!capture(L"plugin-achievements-card.png")) return 4122;
        app.navigate(1); app.navigate(1); app.navigate(1); // Clamped at the last one.
        if (!state.card || state.selected != 2) return 4123;
        if (!capture(L"plugin-achievements-card-hidden.png")) return 4124; // Locked and hidden: no description.
        app.navigate(-1);
        app.back();
        if (state.card || !app.achievementsOpen() || state.selected != 1) return 4125;
        const auto view = app.renderer_.viewport();
        const achievements_ui::Layout layout{view.width, view.height};
        const auto row = layout.row(2, state.scroll);
        app.achievementsClick((row.left + row.right) * 0.5f, (row.top + row.bottom) * 0.5f);
        if (!state.card || state.selected != 2) return 4126;
        app.achievementsClick(4, 4); // Beside the card.
        if (state.card || !app.achievementsOpen()) return 4127;
        app.openAchievementCard(0);
        const auto back = layout.cardBack();
        app.achievementsClick((back.left + back.right) * 0.5f, (back.top + back.bottom) * 0.5f);
        if (state.card || !app.achievementsOpen()) return 4128;
        state.selected = 0;
    }
    // A fresh answer while it is open replaces the list (a second one earned since).
    app.runtime_started_ = true; app.requestPluginAchievements(witcher, true); app.runtime_started_ = false;
    if (app.plugin_jobs_->jobs.size() != 1) return 4111;
    job = app.plugin_jobs_->jobs.front(); app.plugin_jobs_->jobs.clear();
    std::string updated = three;
    updated.replace(updated.find(R"("achieved":false,"percent":8.2)"), 30, R"("achieved":true,"unlockedAt":1709700000,"percent":8.2)");
    answer(job, updated);
    if (state.unlocked != 2 || state.items[0].name != L"Gwent master" || app.details_.achievements_unlocked != 2) return 4112;
    // A passing failure keeps the last list; a game it has none for gets no card.
    app.runtime_started_ = true; app.requestPluginAchievements(witcher, true); app.runtime_started_ = false;
    job = app.plugin_jobs_->jobs.front(); app.plugin_jobs_->jobs.clear();
    {
        auto failed = std::make_unique<PluginsMessage>(); failed->job = job; failed->job.epoch = app.plugin_epoch_;
        failed->response = *json::parse(R"({"message":"GOG did not answer."})"); app.onPlugins(std::move(failed));
    }
    if (!app.pluginAchievements(witcher) || app.pluginAchievements(witcher)->list.total != 3 || state.items.size() != 3) return 4113;
    app.closeAchievements();
    Game other; other.source = GameSource::epic; other.kind = GameKind::store; other.source_id = "App"; other.name = L"No achievements";
    app.openDetails(other); app.details_loading_ = false; app.plugin_jobs_->jobs.clear();
    app.runtime_started_ = true; app.requestPluginAchievements(other); app.runtime_started_ = false;
    job = app.plugin_jobs_->jobs.front(); app.plugin_jobs_->jobs.clear();
    {
        auto message = std::make_unique<PluginsMessage>(); message->job = job; message->job.epoch = app.plugin_epoch_;
        message->response = *json::parse(R"({"achievements":null})"); app.onPlugins(std::move(message));
    }
    if (!app.pluginAchievements(other) || app.pluginAchievements(other)->list.total || app.details_.achievements_total || app.achievementsDetailAction() != -2) return 4114;
    // The desktop pane asks for its selected game once per selection; moving on drops the previous
    // unanswered question (asked again when it comes back), one asked in Szczegóły stays queued.
    {
        Game first; first.source = GameSource::gog; first.kind = GameKind::store; first.source_id = "11"; first.name = L"First";
        Game second = first; second.source_id = "12"; second.name = L"Second";
        Game third = first; third.source_id = "13"; third.name = L"Third";
        app.plugin_jobs_->jobs.clear(); app.plugin_achievements_focus_.clear();
        app.runtime_started_ = true;
        app.requestFocusedAchievements(first); app.requestFocusedAchievements(first);
        if (app.plugin_jobs_->jobs.size() != 1 || !app.plugin_jobs_->jobs.front().background) return 4115;
        app.requestFocusedAchievements(second);
        if (app.plugin_jobs_->jobs.size() != 1 || gameKey(app.plugin_jobs_->jobs.front().game) != gameKey(second) ||
            app.plugin_achievements_[gameKey(first)].pending) return 4116;
        app.requestPluginAchievements(second); // Szczegóły of the same game: its question stays and is no longer the pane's.
        if (app.plugin_jobs_->jobs.size() != 1 || app.plugin_jobs_->jobs.front().background) return 4117;
        app.requestFocusedAchievements(third);
        if (app.plugin_jobs_->jobs.size() != 2 || gameKey(app.plugin_jobs_->jobs.back().game) != gameKey(third)) return 4118;
        app.requestFocusedAchievements(first); // Back to the first: asked again.
        if (app.plugin_jobs_->jobs.size() != 2 || gameKey(app.plugin_jobs_->jobs.back().game) != gameKey(first)) return 4119;
        app.runtime_started_ = false; app.plugin_jobs_->jobs.clear();
    }
    std::error_code ignored; std::filesystem::remove(icon_file, ignored);
    return 0;
}
