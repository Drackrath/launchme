// Szczegóły → Osiągnięcia: the signed-in account's achievements of one game over the details
// screen. For Steam games a worker reads the Steam client's own files first (appcache/stats: the
// full schema and the account's unlocks; else the cached game page), then the public shares of
// players, or — only when the client has no schema — the Web API list with the user's key
// (Ustawienia → Biblioteka). Other games get theirs from an achievements plugin
// (plugin_achievements.cpp). Icons of the rows on screen are downloaded once into
// achievements\<AppID or game cache key>\; a plugin may also point at a picture on this computer.
#include "launcher.hpp"
#include "../library/steam.hpp"
#include "../library/steam_stats.hpp"
#include "../platform/atomic_file.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>

namespace {
std::wstring localDate(uint64_t unix_time) {
    ULARGE_INTEGER ticks{};
    ticks.QuadPart = (unix_time + 11644473600ULL) * 10000000ULL;
    const FILETIME utc{ticks.LowPart, ticks.HighPart};
    SYSTEMTIME universal{}, local{};
    wchar_t text[64]{};
    if (!FileTimeToSystemTime(&utc, &universal) || !SystemTimeToTzSpecificLocalTime(nullptr, &universal, &local) ||
        !GetDateFormatEx(i18n::localeName(), DATE_SHORTDATE, &local, nullptr, text, static_cast<int>(std::size(text)), nullptr)) return {};
    return text;
}
std::wstring percent(float value) {
    wchar_t text[16]{};
    swprintf_s(text, value < 10 ? L"%.1f" : L"%.0f", value);
    std::wstring result = text;
    if (i18n::language() != i18n::Language::en) std::replace(result.begin(), result.end(), L'.', L',');
    return result + tr(L"% graczy");
}
bool looksLikeImage(const std::vector<uint8_t>& bytes) {
    return bytes.size() >= 64 && ((bytes[0] == 0xff && bytes[1] == 0xd8) || (bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G'));
}
std::vector<uint8_t> readImage(const std::filesystem::path& file) {
    std::vector<uint8_t> bytes;
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (input && input.tellg() > 0 && input.tellg() < 1024 * 1024) {
        bytes.resize(static_cast<size_t>(input.tellg()));
        input.seekg(0);
        if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) bytes.clear();
    }
    return looksLikeImage(bytes) ? bytes : std::vector<uint8_t>{};
}
// A cache name for any https icon: Steam's own file name, else a hash of the URL.
std::wstring iconFile(const std::wstring& url) {
    if (auto steam = achievementIconFile(url); !steam.empty()) return steam;
    uint64_t hash = 1469598103934665603ULL;
    for (const wchar_t c : url) { hash ^= static_cast<uint16_t>(c); hash *= 1099511628211ULL; }
    wchar_t name[32]{};
    swprintf_s(name, L"%016llx.img", static_cast<unsigned long long>(hash));
    return name;
}
}

void Launcher::initializeAchievements() {
    const auto queue = achievement_jobs_;
    const auto alive = alive_;
    const auto hwnd = hwnd_;
    work_.start([queue, alive, hwnd] {
        const auto steam = findSteamRoot();
        const auto icons = userConfigDirectory() / L"achievements";
        const auto post = [&](std::unique_ptr<AchievementsMessage> message) {
            if (!alive->load() || queue->generation.load() != message->generation) return;
            if (PostMessageW(hwnd, achievements_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
        };
        while (alive->load()) {
            AchievementsJob job;
            {
                std::unique_lock lock(queue->mutex);
                queue->wake.wait(lock, [&] { return !alive->load() || !queue->jobs.empty(); });
                if (!alive->load()) return;
                job = std::move(queue->jobs.front());
                queue->jobs.pop_front();
            }
            if (queue->generation.load() != job.generation) continue;
            auto message = std::make_unique<AchievementsMessage>();
            message->generation = job.generation;
            if (!job.icon) {
                const bool english = i18n::english();
                // The client's schema and the account's unlock bits: complete and exact.
                message->list = readLocalAchievements(steam, job.account, job.app_id, english);
                if (message->list.items.empty()) message->list = readCachedAchievements(steam, job.account, job.app_id);
                if (!message->list.total) { // Counts only (achievement_progress.json).
                    const auto stats = readSteamStats(steam, job.account, job.app_id);
                    message->list.total = stats.achievements_total;
                    message->list.unlocked = stats.achievements_unlocked;
                }
                const auto key = userSecret(L"STEAM_WEB_API_KEY", L"steam_web_api_key.txt");
                const bool full = !key.empty() && !message->list.complete;
                // Shares of players are public; ask for them when the local list lacks some.
                const bool shares = message->list.complete &&
                    std::any_of(message->list.items.begin(), message->list.items.end(), [](const SteamAchievement& item) { return item.global_percent < 0; });
                auto local = shares ? message->list : SteamAchievements{};
                message->final = !full && !shares;
                post(std::move(message));
                if (!full && !shares) continue;
                HttpBudget budget(20000);
                auto next = std::make_unique<AchievementsMessage>();
                next->generation = job.generation;
                if (full) next->list = fetchSteamAchievements(key, job.account, job.app_id, english); // Empty: the local list stays.
                else if (const auto global = fetchGlobalPercentages(job.app_id); !global.empty()) {
                    applyGlobalPercentages(local, global);
                    next->list = std::move(local);
                }
                post(std::move(next));
                continue;
            }
            message->icon = true;
            message->index = job.index;
            message->url = job.url;
            if (!job.url.starts_with(L"https://")) { // A plugin's picture on this computer (an emulator's trophy icon).
                message->bytes = readImage(job.url);
                if (!message->bytes.empty()) post(std::move(message));
                continue;
            }
            const auto name = iconFile(job.url);
            if (name.empty() || job.folder.empty()) continue;
            const auto file = icons / job.folder / name;
            message->bytes = readImage(file);
            if (message->bytes.empty()) {
                HttpBudget budget(10000);
                auto reply = httpGet(job.url, L"Accept: image/jpeg, image/png\r\n", 1024 * 1024);
                message->bytes = reply.status == 200 && looksLikeImage(reply.body) ? std::move(reply.body) : std::vector<uint8_t>{};
                if (!message->bytes.empty()) atomic_file::write(file, std::span<const uint8_t>(message->bytes));
            }
            if (!message->bytes.empty()) post(std::move(message));
        }
    });
}

int Launcher::achievementsDetailAction() const {
    return screen_ == ScreenMode::details && !detail_games_.empty() && (steamArtwork(detail_games_[0]) || pluginAchievementsProvider(detail_games_[0])) &&
           details_.achievements_total ? lastDetailAction() + 1 : -2;
}

void Launcher::openAchievements() {
    if (screen_ != ScreenMode::details || detail_games_.empty()) return;
    const auto& game = detail_games_[0];
    const bool steam = steamArtwork(game);
    if (!steam && !pluginAchievementsProvider(game)) return;
    achievements_ = {};
    achievements_.open = true;
    achievements_.loading = true;
    achievements_.app_id = steam ? game.app_id : 0;
    achievements_.key = gameKey(game);
    achievements_.folder = steam ? std::to_wstring(game.app_id) : text_encoding::wideLenient(gameCacheKey(game));
    achievements_.title = game.name;
    achievements_.unlocked = details_.achievements_unlocked;
    achievements_.total = details_.achievements_total;
    achievement_icons_requested_.clear();
    renderer_.resetArtworkThumbnails(0);
    if (!steam) {
        // The plugin's last answer at once; a fresh one replaces it when it comes.
        {
            std::lock_guard lock(achievement_jobs_->mutex);
            achievement_jobs_->generation.fetch_add(1);
            achievement_jobs_->jobs.clear();
        }
        if (const auto* known = pluginAchievements(game)) {
            achievements_.source = known->source;
            if (known->list.total) showAchievementList(known->list, !known->pending);
            else { achievements_.loading = known->pending; achievements_.message = known->message; }
        }
        requestPluginAchievements(game);
        if (const auto* known = pluginAchievements(game); known && !known->pending) achievements_.loading = false;
        audio_.drawer();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    {
        std::lock_guard lock(achievement_jobs_->mutex);
        const auto generation = achievement_jobs_->generation.fetch_add(1) + 1;
        achievement_jobs_->jobs.clear();
        achievement_jobs_->jobs.push_back({generation, false, game.app_id, account_});
    }
    achievement_jobs_->wake.notify_one();
    audio_.drawer();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::closeAchievements() {
    achievements_.open = false;
    {
        std::lock_guard lock(achievement_jobs_->mutex);
        achievement_jobs_->generation.fetch_add(1);
        achievement_jobs_->jobs.clear();
    }
    renderer_.resetArtworkThumbnails(0);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::onAchievements(std::unique_ptr<AchievementsMessage> message) {
    if (!achievementsOpen() || message->generation != achievement_jobs_->generation.load()) return;
    auto& state = achievements_;
    if (message->icon) {
        // Asked for by an earlier list of the same game (cached, then full or re-sorted): its row
        // may now hold another achievement.
        if (message->index >= state.icons.size() || state.icons[message->index] != message->url) return;
        renderer_.setArtworkThumbnail(message->index, std::move(message->bytes), false);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    recordAchievements(state.key, state.title, message->list); // The profile's history learns its unlocks.
    showAchievementList(message->list, message->final);
}

void Launcher::showAchievementList(const SteamAchievements& list, bool final) {
    auto& state = achievements_;
    state.loading = !final;
    if (list.total) { state.total = list.total; state.unlocked = list.unlocked; }
    // The first (cached) answer fills the list; a later full list replaces it.
    if (!list.items.empty() && (state.items.empty() || list.complete)) {
        state.items.clear();
        state.icons.clear();
        state.complete = list.complete;
        for (const auto& item : list.items) {
            const bool secret = item.hidden && !item.achieved;
            state.items.push_back({secret || item.name.empty() ? std::wstring(tr(L"Ukryte osiągnięcie")) : item.name,
                                   secret ? std::wstring{} : item.description,
                                   item.achieved ? (item.unlocked_at ? tr(L"Zdobyte ") + localDate(item.unlocked_at) : std::wstring(tr(L"Zdobyte"))) :
                                   item.global_percent >= 0 ? percent(item.global_percent) : std::wstring{},
                                   item.achieved,
                                   item.achieved && item.unlocked_at ? localDate(item.unlocked_at) : std::wstring{},
                                   item.global_percent,
                                   item.global_percent >= 0 ? percent(item.global_percent) : std::wstring{}});
            state.icons.push_back(item.icon);
        }
        state.selected = std::min(state.selected, static_cast<int>(state.items.size()) - 1);
        if (state.selected < 0) state.card = false;
        if (!state.card) state.scroll = 0; // A fresher list under an open card keeps its place.
        achievement_icons_requested_.assign(state.items.size(), 0);
        renderer_.resetArtworkThumbnails(state.items.size());
        requestAchievementIcons();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::requestAchievementIcons() {
    if (!achievementsOpen() || achievements_.items.empty()) return;
    const auto view = renderer_.viewport();
    const achievements_ui::Layout layout{view.width, view.height};
    const auto [first, end] = layout.visible(achievements_.items.size(), achievements_.scroll);
    bool queued = false;
    {
        std::lock_guard lock(achievement_jobs_->mutex);
        const auto generation = achievement_jobs_->generation.load();
        for (size_t i = first; i < end && i < achievement_icons_requested_.size(); ++i) {
            if (achievement_icons_requested_[i] || achievements_.icons[i].empty()) continue;
            achievement_icons_requested_[i] = 1;
            achievement_jobs_->jobs.push_back({generation, true, achievements_.app_id, account_, i, achievements_.icons[i], achievements_.folder});
            queued = true;
        }
    }
    if (queued) achievement_jobs_->wake.notify_one();
}

void Launcher::openAchievementCard(int index) {
    if (!achievementsOpen() || index < 0 || index >= static_cast<int>(achievements_.items.size())) return;
    achievements_.selected = index;
    achievements_.card = true;
    audio_.drawer();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::closeAchievementCard() {
    if (!achievements_.card) return;
    achievements_.card = false;
    const auto view = renderer_.viewport();
    const achievements_ui::Layout layout{view.width, view.height};
    // The row it showed (moved on from inside the card) in view again.
    achievements_.scroll = layout.ensureVisible(achievements_.selected, achievements_.rows(), achievements_.scroll);
    requestAchievementIcons();
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// Up past the first achievement: Back (selected -1), which Enter/A presses. On a card: the
// previous or next achievement's card.
void Launcher::achievementsMove(int amount) {
    if (!achievementsOpen() || !amount || (achievements_.items.empty() && amount > 0)) return;
    const int next = std::clamp(achievements_.selected + amount, achievements_.card ? 0 : -1, static_cast<int>(achievements_.items.size()) - 1);
    if (next == achievements_.selected) { navigationBoundary(); return; }
    achievements_.selected = next;
    const auto view = renderer_.viewport();
    const achievements_ui::Layout layout{view.width, view.height};
    // The last item also brings the "remaining" row into view.
    achievements_.scroll = layout.ensureVisible(next + 1 == static_cast<int>(achievements_.items.size()) ? static_cast<int>(achievements_.rows()) - 1 : next,
                                                achievements_.rows(), achievements_.scroll);
    requestAchievementIcons();
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::achievementsClick(float x, float y) {
    const auto view = renderer_.viewport();
    const achievements_ui::Layout layout{view.width, view.height};
    if (achievements_.card) { // Its Back, or anywhere beside it, returns to the list.
        if (layout.cardBack().contains(x, y) || !layout.card().contains(x, y)) closeAchievementCard();
        return;
    }
    if (layout.back().contains(x, y)) { closeAchievements(); return; }
    const int row = layout.hit(x, y, achievements_.items.size(), achievements_.scroll);
    if (row >= 0) openAchievementCard(row);
}
