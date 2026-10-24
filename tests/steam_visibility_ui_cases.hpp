#include "../src/library/steam_sync_state.hpp"
// Local pending Steam visibility must survive public scans, progress-only updates and account switches.
int LauncherUiTest::steamVisibility(HWND window) {
    Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute;
    if (!app.renderer_.initialize(window)) return 4000;
    Game game{61000, L"Steam visibility fixture"}, other{61001, L"Other Steam fixture"}, owned{61002, L"Owned Steam fixture"};
    Game secret{61004, L"Private in Steam"}; secret.visibility = GameVisibility::private_game;
    owned.installed = false;
    const auto snapshot = [&](uint64_t account, bool refresh = true) {
        auto scan = std::make_unique<LibraryMessage>(); scan->account = account; scan->refresh = refresh;
        scan->games = {game, other, owned}; scan->private_games = {secret}; scan->excluded = {secret.app_id};
        scan->collections = {{L"Visibility collection", {game.app_id, other.app_id, owned.app_id, 61003}, "user-collections.visibility"}};
        scan->cached_names = {{61003, L"Uninstalled collection fixture"}};
        return scan;
    };
    const auto count = [](const std::vector<Game>& list, uint32_t id) {
        return std::count_if(list.begin(), list.end(), [&](const Game& item) { return item.source == GameSource::steam && item.app_id == id; });
    };
    const auto visible = [&](const Game& value) { return count(app.public_library_, value.app_id) != 0; };
    const auto shown = [&](const std::vector<Game>& list, uint32_t id) -> const Game* {
        const auto found = std::find_if(list.begin(), list.end(), [&](const Game& item) { return item.source == GameSource::steam && item.app_id == id; });
        return found == list.end() ? nullptr : &*found;
    };
    const auto action = [&] {
        app.detail_action_ = 1; app.enterDown(false); app.enterUp();
        const int row = context_menu::row(context_menu::Item::hide, app.menuRows());
        if (!app.menu_open_ || row < 0) return false;
        app.menu_selected_ = row; app.enterDown(false); app.enterUp(); return true;
    };
    app.onLibrary(snapshot(11, false));
    app.openSearch(); app.search_query_ = game.name; app.updateSearch();
    if (app.cards_.size() != 1) return 4001;
    app.openDetails(app.cards_[0].game);
    if (!action() || app.screen_ != ScreenMode::search || !app.cards_.empty() || !app.search_results_.empty() || visible(game)) return 4002;
    const std::string key = "steam-user:11/steam:61000";
    if (!app.library_state_.hidden.contains(key) || app.library_state_.hidden.contains("steam:61000") ||
        app.installed_library_[0].visibility != GameVisibility::public_game || app.collections_source_[0].app_ids[0] != game.app_id) return 4003;
    const auto saved = loadLibraryState(userConfigDirectory() / L"library-state.ini");
    if (!saved || !saved->hidden.contains(key)) return 4004;
    app.library_state_ = *saved;
    app.buildShelves(); app.buildShelves();
    if (count(app.private_category_.games, game.app_id) != 1) return 4005;
    for (const auto& category : app.categories_) if (count(category.games, game.app_id)) return 4006;
    app.collections_source_.clear(); app.buildShelves(); // The fallback Updated/Unplayed shelves must respect hiding too.
    for (const auto& category : app.categories_) if (count(category.games, game.app_id)) return 4007;
    app.onLibrary(snapshot(11));
    if (visible(game) || count(app.private_category_.games, game.app_id) != 1) return 4008;
    app.togglePrivateCategory();
    if (!visible(game) || !shown(app.public_library_, game.app_id) || shown(app.public_library_, game.app_id)->visibility != GameVisibility::hidden) return 4009;
    app.openPcLibrary(GameSource::steam); app.openDetails(*shown(app.public_library_, game.app_id));
    if (!app.hiddenInLauncher(app.cards_[0].game)) return 4010;
    // Poll results remain public in the raw source but hidden in every UI copy.
    auto progress = snapshot(11); progress->installation_update = true;
    progress->games[0].installation = {InstallPhase::downloading, 10, 100};
    app.onLibrary(std::move(progress));
    progress = snapshot(11); progress->installation_update = true;
    progress->games[0].installation = {InstallPhase::downloading, 20, 100};
    app.onLibrary(std::move(progress));
    const auto* hidden = shown(app.private_category_.games, game.app_id);
    if (!hidden || hidden->visibility != GameVisibility::hidden || hidden->installation.done != 20 ||
        app.cards_[0].game.visibility != GameVisibility::hidden || app.cards_[0].game.installation.done != 20 ||
        app.installed_library_[0].visibility != GameVisibility::public_game) return 4011;
    // The independent details poll must preserve the local choice as well.
    auto details = std::make_unique<StoreMessage>(); details->account = 11; details->generation = app.store_jobs_->generation.load();
    details->details = true; details->installed = app.installed_library_[0]; app.onStore(std::move(details));
    if (app.cards_[0].game.visibility != GameVisibility::hidden || !app.hiddenInLauncher(app.cards_[0].game)) return 4012;
    app.togglePrivateCategory();
    std::vector<downloads_ui::Row> downloads;
    if (visible(game) || count(app.collectDownloads(downloads), game.app_id)) return 4013;
    app.togglePrivateCategory();
    downloads.clear();
    if (!count(app.collectDownloads(downloads), game.app_id)) return 4014;
    app.openPcLibrary(GameSource::steam); app.openDetails(*shown(app.public_library_, game.app_id));
    if (!action() || app.library_state_.hidden.contains(key) || app.cards_[0].game.visibility != GameVisibility::public_game ||
        count(app.private_category_.games, game.app_id)) return 4015;
    app.back(); app.togglePrivateCategory();
    if (!visible(game)) return 4016;
    // Client-owned uninstalled games and collection-only entries use the same overlay.
    app.openDetails(owned); if (!action() || visible(owned)) return 4017;
    const auto* entry = shown(app.uninstalled_library_, 61003);
    if (!entry) return 4018;
    const auto uninstalled = *entry;
    app.openPcLibrary(GameSource::steam); app.openDetails(uninstalled);
    if (!action() || visible(uninstalled)) return 4018;
    app.settings_.show_uninstalled = false; app.buildShelves(); app.togglePrivateCategory();
    if (visible(owned) || visible(uninstalled)) return 4019;
    app.settings_.show_uninstalled = true; app.buildShelves();
    if (!visible(owned) || !visible(uninstalled)) return 4020;
    app.togglePrivateCategory();
    // The local toggle never overrides Steam's own private/hidden status.
    app.openPcLibrary(GameSource::steam); app.openDetails(secret);
    if (app.menuRows().hide) return 4021;
    app.back(); app.openDetails(game); if (!action()) return 4022;
    auto native_hidden = snapshot(11);
    native_hidden->games.erase(native_hidden->games.begin());
    auto private_copy = game; private_copy.visibility = GameVisibility::private_game;
    native_hidden->private_games.push_back(private_copy); native_hidden->excluded.insert(game.app_id);
    app.onLibrary(std::move(native_hidden));
    app.togglePrivateCategory(); app.openPcLibrary(GameSource::steam); app.openDetails(*shown(app.public_library_, game.app_id));
    if (app.menuRows().hide || app.cards_[0].game.visibility != GameVisibility::private_game) return 4023;
    app.onLibrary(snapshot(11)); // Steam reveals it again, Launchme's hide remains.
    app.back();
    if (app.private_shelf_) app.togglePrivateCategory();
    if (visible(game) || !app.library_state_.hidden.contains(key)) return 4024;
    app.onLibrary(snapshot(12, false));
    if (!visible(game) || !visible(owned) || !visible(uninstalled) || app.locallyHidden(game)) return 4025;
    app.onLibrary(snapshot(11, false));
    if (visible(game) || visible(owned) || visible(uninstalled)) return 4026;
    // A merged Steam/GOG tile hides and shows its copies together, without losing collection membership.
    app.library_state_.hidden.erase(key);
    app.library_state_.steam_hidden_pending.erase(key);
    Game gog; gog.kind = GameKind::store; gog.source = GameSource::gog; gog.source_id = "steam-visibility-copy"; gog.name = game.name;
    app.settings_.enabled_pc_sources.insert("gog"); app.store_library_ = {gog}; app.buildShelves(); app.openLibrary();
    const auto group = std::find_if(app.cards_.begin(), app.cards_.end(), [&](const auto& card) { return card.game.app_id == game.app_id; });
    if (group == app.cards_.end() || group->game.version_group.empty()) return 4027;
    app.openDetails(group->game);
    if (!action() || !app.library_state_.hidden.contains(key) || !app.library_state_.hidden.contains(gameKey(gog))) return 4028;
    app.togglePrivateCategory(); app.openLibrary();
    const auto hidden_group = std::find_if(app.cards_.begin(), app.cards_.end(), [&](const auto& card) { return card.game.app_id == game.app_id; });
    if (hidden_group == app.cards_.end() || hidden_group->game.version_group.empty()) return 4029;
    app.openDetails(hidden_group->game);
    if (!action() || app.library_state_.hidden.contains(key) || app.library_state_.hidden.contains(gameKey(gog))) return 4030;
    app.back(); if (app.private_shelf_) app.togglePrivateCategory();
    // Acknowledged native state replaces Steam-only overlays; future edits in Steam are visible.
    steam_sync::Snapshot native; native.account = 11; native.collections = app.collections_source_;
    native.hidden = {owned.app_id, uninstalled.app_id}; native.private_apps = {secret.app_id};
    const auto receive = [&](const steam_sync::Snapshot& snapshot, std::vector<steam_sync::Change> applied = {}) {
        auto result = std::make_unique<SteamSyncMessage>(); result->result.account = snapshot.account;
        result->result.snapshot = snapshot; result->result.applied = std::move(applied); app.onSteamSync(std::move(result));
    };
    receive(native, steam_sync::pending(app.library_state_, 11));
    if (!steam_sync::pending(app.library_state_, 11).empty() || !app.hiddenInSteam(owned) || visible(owned)) return 4031;
    native.collections.push_back({L"Ulubione", {game.app_id}, "user-collections.favorite"});
    native.collections[0].name = L"Renamed in Steam";
    receive(native);
    if (!app.isFavorite(game) || app.collections_source_[0].name != L"Renamed in Steam") return 4032;
    native.hidden.insert(game.app_id); receive(native);
    if (visible(game) || !app.hiddenInSteam(game)) return 4033;
    app.togglePrivateCategory(); app.openPcLibrary(GameSource::steam); app.openDetails(*shown(app.public_library_, game.app_id));
    if (!app.menuRows().hide || !app.hiddenInLauncher(app.cards_[0].game) || !action()) return 4034;
    if (!app.library_state_.steam_hidden_pending.contains(key) || app.library_state_.steam_hidden_pending.at(key)) return 4035;
    receive(native, {{steam_sync::Kind::hidden, {}, game.app_id, true}}); // Delayed ack of an earlier edit.
    if (!app.library_state_.steam_hidden_pending.contains(key)) return 4036;
    native.hidden.erase(game.app_id); receive(native, {{steam_sync::Kind::hidden, {}, game.app_id, false}});
    if (app.library_state_.steam_hidden_pending.contains(key) || app.hiddenInSteam(game) || !visible(game)) return 4037;
    app.toggleFavorite(game);
    auto failure = std::make_unique<SteamSyncMessage>(); failure->result.account = 11; failure->result.error = "unavailable";
    app.onSteamSync(std::move(failure));
    if (app.isFavorite(game) || steam_sync::pending(app.library_state_, 11).empty()) return 4038;
    native.collections.back().app_ids.clear(); receive(native, steam_sync::pending(app.library_state_, 11));
    if (app.isFavorite(game) || !steam_sync::pending(app.library_state_, 11).empty()) return 4039;
    native.collections.back().app_ids = {game.app_id}; receive(native);
    if (!app.isFavorite(game)) return 4040;
    auto foreign = native; foreign.account = 12; foreign.collections.clear(); receive(foreign);
    if (!app.isFavorite(game) || app.steam_sync_snapshot_->account != 11) return 4041;
    return 0;
}
