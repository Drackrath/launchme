int LauncherUiTest::pluginLibrarySupplements(HWND window) {
    Launcher app; app.hwnd_ = window; app.alive_->store(false); // No metadata/network workers in this merge test.
    if (!app.renderer_.initialize(window)) return 1100;
    app.account_ = 1; app.scanning_ = false; app.settings_.show_uninstalled = true;
    plugins::Plugin plugin; plugin.enabled = true; plugin.manifest.id = "test.supplement";
    plugins::Contribution contribution; contribution.id = contribution.command = contribution.kind = "library"; contribution.source = "steam";
    plugin.manifest.contributions.push_back(contribution); app.plugins_.plugins.push_back(plugin);
    app.installed_library_ = {Game{620, L"Portal 2"}};
    app.collections_source_ = {{L"Collection", {400}, "collection"}};
    app.excluded_ids_ = {570};
    Game collected{400, L"Portal"}; collected.installed = false;
    Game hinted{1234567, L"Clive Barker's Hellraiser: Revival"}; hinted.installed = false;
    app.uninstalled_library_ = {collected, hinted};
    const auto receive = [&](const char* response, uint64_t account, uint32_t epoch, uint32_t generation) {
        auto message = std::make_unique<PluginsMessage>();
        message->job.kind = plugins::Job::Kind::library; message->job.plugin = plugin.manifest.id;
        message->job.account = account; message->job.epoch = epoch; message->job.generation = generation;
        message->response = *json::parse(response); app.onPlugins(std::move(message));
    };
    const auto count = [&](uint32_t id) { return std::count_if(app.public_library_.begin(), app.public_library_.end(), [&](const Game& g) { return g.app_id == id; }); };
    const auto hints = R"({"steamAppIds":[620,400,570,1234567]})";
    receive(hints, 1, 0, 0);
    if (count(620) != 1 || count(400) != 1 || count(1234567) != 1 || count(570) || app.uninstalled_library_.size() != 2) return 1101;
    if (app.uninstalled_library_[1].name != hinted.name || app.uninstalled_library_[1].installed || !app.installed_library_[0].installed) return 1102;
    app.settings_.show_uninstalled = false; app.refreshRecentGames();
    if (count(1234567) || count(400) || count(620) != 1) return 1103;
    app.settings_.show_uninstalled = true; app.refreshRecentGames();
    receive(R"({"steamAppIds":[]})", 2, 0, 0);
    receive(R"({"steamAppIds":[]})", 1, 10, 0);
    receive(R"({"steamAppIds":[]})", 1, 0, 10);
    receive(R"({"message":"incomplete scan"})", 1, 0, 0);
    if (count(1234567) != 1) return 1104;
    LibraryMessage installation; installation.account = 1; installation.excluded = {570};
    Game installed = hinted; installed.installed = true; installed.name = L"Authoritative manifest name";
    installation.games = {app.installed_library_[0], installed};
    app.updateSteamInstallations(installation);
    if (count(1234567) != 1 || app.uninstalled_library_.size() != 1 || app.installed_library_[1].name != installed.name) return 1105;
    receive(R"({"steamAppIds":[]})", 1, 0, 0);
    if (count(1234567) != 1 || count(400) != 1) return 1106; // Hints cannot remove installed or collection games.
    receive(hints, 1, 0, 0);
    installation.games.resize(1); app.updateSteamInstallations(installation);
    if (count(1234567) != 1 || app.uninstalled_library_.size() != 2 || app.uninstalled_library_[1].installed) return 1107;
    app.plugins_.plugins[0].enabled = false; app.refreshPluginContributions();
    if (count(1234567) || count(400) != 1 || count(620) != 1 || !app.plugin_library_ids_.empty()) return 1108;
    receive(hints, 1, 0, app.plugin_library_generation_);
    if (count(1234567)) return 1109; // A late result cannot revive a disabled plugin.
    app.plugins_.plugins[0].enabled = true; app.refreshPluginContributions();
    receive(hints, 1, 0, app.plugin_library_generation_);
    if (count(1234567) != 1) return 1110;
    app.plugins_.plugins.clear(); app.refreshPluginContributions();
    if (count(1234567) || count(400) != 1) return 1111;
    // Account switches discard hints and invalidate outstanding account-scoped results.
    app.plugins_.plugins.push_back(plugin); app.plugin_library_ids_[plugin.manifest.id] = {1234567};
    auto other = std::make_unique<LibraryMessage>(); other->account = 2;
    app.onLibrary(std::move(other));
    if (!app.plugin_library_ids_.empty() || count(1234567)) return 1112;
    return 0;
}
