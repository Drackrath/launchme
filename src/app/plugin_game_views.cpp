#include "launcher.hpp"
#include <algorithm>

using namespace plugins::wire;
namespace {
json::Value secondsValue(std::optional<uint64_t> seconds) {
    json::Value result;
    if (seconds) { result.type = json::Value::Type::number; result.numeric = static_cast<double>(std::min<uint64_t>(*seconds, 9007199254740991ULL)); }
    return result;
}
}
std::optional<Game> Launcher::pluginActiveGame() const {
    // The overlay always describes the game underneath it, never the selected library card.
    if (overlay_target_) {
        auto game = trackedGame(overlay_target_->key);
        if (isGame(game) && !gameKey(game).empty() && game.visibility == GameVisibility::public_game) return game;
        for (const auto& candidate : public_library_)
            if (gameKey(candidate) == overlay_target_->key && candidate.visibility == GameVisibility::public_game) return candidate;
    }
    std::optional<Game> result; uint64_t newest = 0;
    for (const auto& [key, session] : game_sessions_) {
        if (!session.running() || session.frozen()) continue;
        auto game = trackedGame(key);
        if (!isGame(game) || gameKey(game).empty() || game.visibility != GameVisibility::public_game) continue;
        if (!result || session.startedAt() > newest) { newest = session.startedAt(); result = std::move(game); }
    }
    return result;
}
std::string Launcher::pluginWidgetScope() const {
    const auto game = pluginActiveGame();
    if (!game) return {};
    const auto session = game_sessions_.find(gameKey(*game));
    return recent_games::historyKey(*game, account_, gog_state_.account, epic_state_.account) + ":session:" +
        std::to_string(session == game_sessions_.end() ? 0 : session->second.startedAt());
}
std::optional<plugins::Action> Launcher::pluginDetailProvider() const {
    if (plugins_busy_ || screen_ != ScreenMode::details || detail_games_.empty()) return {};
    const auto providers = plugins_.contributions("details", &detail_games_[0]);
    return providers.empty() ? std::nullopt : std::optional<plugins::Action>(providers.front());
}
int Launcher::pluginDetailAction() const {
    return pluginDetailProvider() ? std::max({lastDetailAction(), achievementsDetailAction(), ratingDetailAction()}) + 1 : -2;
}
void Launcher::openPluginDetail() {
    if (const auto action = pluginDetailProvider()) pluginAction(*action, detail_games_[0]);
}
std::string Launcher::pluginComparisonKey(const std::string& plugin, const Game& game) { return plugin + '/' + gameCacheKey(game); }
const Launcher::PluginComparison* Launcher::pluginComparison(const std::string& plugin, const Game& game) const {
    const auto found = plugin_comparisons_.find(pluginComparisonKey(plugin, game));
    if (found == plugin_comparisons_.end() || found->second.name != game.name ||
        found->second.locale != (i18n::language() == i18n::Language::pl ? "pl" : "en")) return nullptr;
    return &found->second;
}
std::optional<uint64_t> Launcher::pluginPlaytimeSeconds(const Game& game) const {
    const auto key = recent_games::historyKey(game, account_, gog_state_.account, epic_state_.account);
    if (key.empty()) return {};
    if (!steamArtwork(game)) {
        const auto saved = library_state_.playtime_ms.find(key);
        return saved == library_state_.playtime_ms.end() ? 0 : saved->second / 1000;
    }
    const auto session = game_sessions_.find(gameKey(game));
    const auto token = session == game_sessions_.end() ? std::nullopt : std::optional<uint64_t>(session->second.startedAt());
    if (const auto sample = plugin_playtimes_.find(key); sample != plugin_playtimes_.end() && sample->second.seconds && sample->second.session_started == token) {
        const auto& value = sample->second;
        const auto elapsed = session == game_sessions_.end() ? 0 : session->second.playedMs();
        return *value.seconds + (elapsed >= value.session_ms ? (elapsed - value.session_ms) / 1000 : 0);
    }
    if (screen_ == ScreenMode::details && !detail_games_.empty() && gameKey(detail_games_[0]) == gameKey(game) && details_.playtime_minutes)
        return static_cast<uint64_t>(*details_.playtime_minutes) * 60;
    return {};
}
json::Value Launcher::pluginGameParams(const Game& game) const {
    auto value = object(); value.properties["game"] = *json::parse(plugins::gameJson(game));
    value.properties["playtimeSeconds"] = secondsValue(pluginPlaytimeSeconds(game));
    return value;
}
void Launcher::ensurePluginPlaytime(const Game& game) {
    if (!runtime_started_ || !steamArtwork(game)) return;
    const auto key = recent_games::historyKey(game, account_, gog_state_.account, epic_state_.account);
    const auto session = game_sessions_.find(gameKey(game));
    const auto token = session == game_sessions_.end() ? std::nullopt : std::optional<uint64_t>(session->second.startedAt());
    auto& cache = plugin_playtimes_[key]; const auto now = GetTickCount64();
    if (cache.session_started == token && (cache.pending || (cache.checked_at && (now - cache.checked_at < 30000 || (token && cache.seconds))))) return;
    cache = {}; cache.session_started = token; cache.pending = true;
    plugins::Job job; job.kind = plugins::Job::Kind::playtime; job.key = key; job.game = game; job.account = account_; job.session_started = token;
    submitPlugin(std::move(job));
}
void Launcher::requestGameComparisons(const Game& game, bool force) {
    if (!runtime_started_ || plugins_busy_) return;
    const auto providers = plugins_.contributions("details", &game);
    if (providers.empty()) return;
    ensurePluginPlaytime(game);
    const auto now = GetTickCount64(); const std::string locale = i18n::language() == i18n::Language::pl ? "pl" : "en";
    for (const auto& action : providers) {
        const auto key = pluginComparisonKey(action.plugin, game);
        auto& cache = plugin_comparisons_[key];
        const bool same = cache.name == game.name && cache.locale == locale;
        if (!force && same && (cache.pending || (cache.checked_at && now - cache.checked_at < 60000))) continue;
        cache.name = game.name; cache.locale = locale; cache.pending = true; cache.generation = ++plugin_details_generation_;
        if (!same || cache.value.game_key != gameKey(game)) cache.value = {gameKey(game), 0, 0, 0, tr(L"Wczytywanie…")};
        plugins::Job job; job.kind = plugins::Job::Kind::details; job.plugin = action.plugin; job.key = action.item.id; job.game = game;
        job.generation = cache.generation; job.value = pluginGameParams(game); job.value.properties["command"] = string(action.item.command);
        job.value.properties["refresh"] = plugins::wire::boolean(force); submitPlugin(std::move(job));
    }
    // Only small reference snapshots, not a second full game library.
    if (plugin_comparisons_.size() > 128) {
        auto oldest = plugin_comparisons_.end();
        for (auto it = plugin_comparisons_.begin(); it != plugin_comparisons_.end(); ++it)
            if (!it->second.pending && (oldest == plugin_comparisons_.end() || it->second.checked_at < oldest->second.checked_at)) oldest = it;
        if (oldest != plugin_comparisons_.end()) plugin_comparisons_.erase(oldest);
    }
}
void Launcher::pollPluginGameViews() {
    if (!runtime_started_ || plugins_busy_) return;
    if (screen_ == ScreenMode::details && !detail_games_.empty()) requestGameComparisons(detail_games_[0]);
    if (friends_open_) {
        const auto scope = pluginWidgetScope();
        if (scope != plugin_widget_scope_ || (!scope.empty() && GetTickCount64() >= next_plugin_widget_refresh_)) refreshPluginWidgets(true);
    }
}
bool Launcher::receivePluginGameView(const plugins::Result& message) {
    using Kind = plugins::Job::Kind;
    const auto& job = message.job;
    if (job.kind == Kind::playtime) {
        if (job.account != account_) return true;
        const auto found = plugin_playtimes_.find(job.key);
        if (found == plugin_playtimes_.end() || found->second.session_started != job.session_started) return true;
        auto& cache = found->second; cache.pending = false; cache.checked_at = GetTickCount64();
        const auto session = game_sessions_.find(gameKey(job.game));
        const auto token = session == game_sessions_.end() ? std::nullopt : std::optional<uint64_t>(session->second.startedAt());
        if (token != job.session_started) { cache.seconds.reset(); return true; }
        cache.seconds = job.playtime_seconds;
        cache.session_ms = session == game_sessions_.end() ? 0 : session->second.playedMs();
        return true;
    }
    const bool active_widget = job.kind == Kind::widget && job.active_game;
    if (active_widget && (job.generation != plugin_widget_generation_ || pluginWidgetScope() != plugin_widget_scope_)) return true;
    if (job.kind != Kind::details && job.kind != Kind::command && !active_widget) return false;
    const auto* owner = plugins_.find(job.plugin);
    if (!owner || !owner->enabled) return job.kind != Kind::command;
    const auto key = pluginComparisonKey(job.plugin, job.game);
    const auto found = plugin_comparisons_.find(key);
    if (job.kind == Kind::details && (found == plugin_comparisons_.end() || found->second.generation != job.generation)) return true;
    const auto parsed = plugins::parseTimeComparison(message.response, gameKey(job.game));
    if (parsed && parsed->main_seconds) { // How long the game takes, for the game index.
        game_index::Patch times;
        const auto minutes = [](uint32_t seconds) { return seconds >= 60 ? std::optional<uint32_t>((seconds + 30) / 60) : std::nullopt; };
        times.hltb_main = minutes(parsed->main_seconds); times.hltb_extra = minutes(parsed->extra_seconds); times.hltb_complete = minutes(parsed->complete_seconds);
        gameIndexMerge(parsed->game_key, game_index::Source::hltb, std::move(times));
    }
    if (parsed || job.kind == Kind::details) {
        auto& cache = plugin_comparisons_[key];
        cache.value = parsed.value_or(plugins::TimeComparison{gameKey(job.game), 0, 0, 0, tr(L"Dane chwilowo niedostępne")});
        cache.name = job.game.name; cache.locale = job.locale;
        // Cache-only widget reads cannot postpone the next provider refresh forever,
        // or acknowledge a newer details request that is still in flight.
        if (!active_widget) { cache.checked_at = GetTickCount64(); cache.pending = false; }
        if (job.kind == Kind::command) cache.generation = ++plugin_details_generation_; // A chosen match beats an old passive result.
    }
    if (active_widget) {
        for (size_t i = 0; i < plugin_widgets_.size(); ++i)
            if (plugin_widgets_[i].plugin == job.plugin && plugin_widgets_[i].item.id == job.key)
                plugin_widget_text_[i] = text_encoding::wideLenient(text(message.response, "text")).substr(0, 256);
    }
    return job.kind != Kind::command;
}
std::wstring Launcher::pluginWidgetText(size_t index) const {
    if (index >= plugin_widgets_.size()) return {};
    const auto& action = plugin_widgets_[index];
    if (action.item.active_game) {
        const auto game = pluginActiveGame();
        if (!game) return tr(L"Brak aktywnej gry");
        if (const auto* data = pluginComparison(action.plugin, *game))
            return data->value.main_seconds ? plugins::comparisonLabel(data->value, pluginPlaytimeSeconds(*game)) : data->value.status;
        return tr(L"Wczytywanie…");
    }
    return index < plugin_widget_text_.size() ? plugin_widget_text_[index] : std::wstring{};
}
