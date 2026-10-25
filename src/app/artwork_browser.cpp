#include "launcher.hpp"
#include "../artwork/cover_cache.hpp"
#include "../artwork/esde_art.hpp"
#include "../artwork/screenscraper_art.hpp"
#include "../platform/atomic_file.hpp"
#include <algorithm>
#include <cstdlib>
#include <fstream>

namespace {
std::vector<uint8_t> cachedImage(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() < 128 || input.tellg() > 20 * 1024 * 1024) return {};
    std::vector<uint8_t> bytes(static_cast<size_t>(input.tellg()));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())) || !decodableArtwork(bytes)) return {};
    return bytes;
}
void saveThumbnail(const std::filesystem::path& file, const std::vector<uint8_t>& bytes) {
    atomic_file::write(file, bytes);
}
CoverMessage::Kind coverKind(ArtworkKind kind) {
    switch (kind) {
    case ArtworkKind::cover: return CoverMessage::Kind::cover;
    case ArtworkKind::hero: return CoverMessage::Kind::hero;
    case ArtworkKind::poster: return CoverMessage::Kind::poster;
    case ArtworkKind::landscape: return CoverMessage::Kind::landscape;
    case ArtworkKind::disc: return CoverMessage::Kind::disc;
    default: return CoverMessage::Kind::logo;
    }
}
uint32_t fnv(std::wstring_view text) {
    uint32_t hash = 2166136261u;
    for (const auto ch : text) { hash ^= static_cast<uint32_t>(ch); hash *= 16777619u; }
    return hash;
}
// Variant IDs of the other sources stay apart from SteamGridDB's and from disk picks (0x80000000).
constexpr uint32_t screenscraper_ids = 0x40000000u, esde_ids = 0x20000000u;

// ScreenScraper media types and ES-DE media folders per artwork slot, best first.
std::vector<std::wstring_view> screenscraperTypes(ArtworkKind kind) {
    switch (kind) {
    case ArtworkKind::cover: return {L"box-2D", L"box-3D", L"support-2D", L"mixrbv2", L"mixrbv1"};
    case ArtworkKind::poster: return {L"box-2D", L"box-3D"};
    case ArtworkKind::logo: return {L"wheel-hd", L"wheel", L"wheel-carbon", L"wheel-steel"};
    default: return {L"fanart", L"ss", L"sstitle"};
    }
}
std::vector<const wchar_t*> esdeFolders(ArtworkKind kind) {
    switch (kind) {
    case ArtworkKind::cover: return {L"covers", L"box2dfront", L"3dboxes", L"miximages", L"physicalmedia"};
    case ArtworkKind::poster: return {L"covers", L"box2dfront", L"3dboxes"};
    case ArtworkKind::logo: return {L"marquees", L"wheel"};
    default: return {L"fanart", L"screenshots", L"titlescreens", L"miximages"};
    }
}
GridPage providerPage(const Game& game, ArtworkKind kind, ArtworkProvider provider) {
    GridPage page;
    page.provider = provider;
    std::unordered_set<uint32_t> seen;
    if (provider == ArtworkProvider::screenscraper) {
        auto gallery = kind == ArtworkKind::disc ? screenscraperDiscGallery(game, userConfigDirectory()) :
            screenscraperGallery(game, screenscraperTypes(kind), userConfigDirectory());
        page.error = std::move(gallery.error);
        for (const auto& media : gallery.media) {
            if (kind == ArtworkKind::logo && _wcsicmp(media.format.c_str(), L"png") != 0) continue; // Drawn over art: transparency.
            GridAsset asset;
            asset.id = screenscraper_ids | (fnv(media.url) & 0x1fffffffu);
            if (!seen.insert(asset.id).second) continue;
            asset.url = asset.thumbnail = media.url;
            asset.author = L"ScreenScraper · " + media.type + (media.region.empty() ? L"" : L" (" + media.region + L")");
            page.assets.push_back(std::move(asset));
        }
    } else {
        for (const auto& file : esdeMediaFiles(game.platform, game.name, game.rom, esdeFolders(kind))) {
            GridAsset asset;
            asset.id = esde_ids | (fnv(file.native()) & 0x1fffffffu);
            if (!seen.insert(asset.id).second) continue;
            asset.url = asset.thumbnail = file.native();
            asset.author = L"ES-DE · " + file.parent_path().filename().wstring();
            page.assets.push_back(std::move(asset));
        }
        if (page.assets.empty()) page.error = tr(L"ES-DE nie ma grafik tej gry. Zescrapuj ją w ES-DE.");
    }
    return page;
}
}

void Launcher::initializeArtworkBrowser() {
    const auto queue = artwork_jobs_;
    const auto alive = alive_;
    const auto window = hwnd_;
    for (int i = 0; i < 2; ++i) work_.start([queue, alive, window] {
        const auto cache = coverCacheDirectory();
        while (alive->load()) {
            ArtworkBrowserJob job;
            {
                std::unique_lock lock(queue->mutex);
                queue->wake.wait(lock, [&] { return !alive->load() || !queue->jobs.empty(); });
                if (!alive->load()) return;
                job = std::move(queue->jobs.front());
                queue->jobs.pop_front();
            }
            if (queue->generation.load() != job.generation) continue;
            auto result = std::make_unique<ArtworkBrowserMessage>();
            result->type = job.type;
            result->generation = job.generation;
            result->asset_id = job.asset.id;
            result->kind = job.kind;
            if (job.type == ArtworkBrowserMessage::Type::search) {
                result->search = searchGridGames(job.query, userSecret(L"STEAMGRIDDB_API_KEY", L"steamgriddb_api_key.txt"));
            } else if (job.type == ArtworkBrowserMessage::Type::local) {
                // An image from disk becomes one more variant of this game, kept like a SteamGridDB pick.
                auto bytes = cachedImage(std::filesystem::path(job.query));
                if (bytes.empty()) result->error = tr(L"Wybierz obraz PNG lub JPEG (do 20 MB).");
                else {
                    uint32_t hash = 2166136261u;
                    for (const auto byte : bytes) { hash ^= byte; hash *= 16777619u; }
                    result->asset_id = 0x80000000u | (hash & 0x7fffffffu); // Above SteamGridDB asset IDs.
                    if (!cacheArtworkVariant(job.game, job.kind, result->asset_id, bytes, cache)) result->error = tr(L"Nie można zapisać grafiki w cache.");
                    else {
                        if (job.kind == ArtworkKind::cover) {
                            const auto file = artworkVariantPath(job.game, job.kind, result->asset_id, cache);
                            auto focus = file; focus.replace_extension(L".focus");
                            result->focus = cachedTextCrop(file, focus, job.game.name);
                        }
                        result->bytes = std::move(bytes);
                    }
                }
            } else if (job.type == ArtworkBrowserMessage::Type::list) {
                result->page = job.provider == ArtworkProvider::steamgriddb ?
                    browseSteamGridDB(job.game, job.kind, job.page, job.source, userSecret(L"STEAMGRIDDB_API_KEY", L"steamgriddb_api_key.txt")) :
                    providerPage(job.game, job.kind, job.provider);
                result->applied_id = selectedArtworkId(job.game, job.kind, cache);
            } else {
                const bool thumbnail = job.type == ArtworkBrowserMessage::Type::thumbnail;
                // ES-DE files are on disk already: no thumbnail copies of them.
                const bool esde = job.provider == ArtworkProvider::esde;
                const auto file = thumbnail && esde ? std::filesystem::path(job.asset.url) : thumbnail ? cache.parent_path() / L"artwork" / L"thumbnails" /
                    artworkKindSlug(job.kind) / (std::to_wstring(job.asset.id) + L".img") :
                    artworkVariantPath(job.game, job.kind, job.asset.id, cache);
                result->bytes = cachedImage(file);
                if (result->bytes.empty()) {
                    result->bytes = job.provider == ArtworkProvider::screenscraper ? screenscraperImage(job.asset.url, thumbnail) :
                                    esde ? cachedImage(std::filesystem::path(job.asset.url)) : downloadGridImage(job.asset, thumbnail);
                    if (job.kind == ArtworkKind::disc && !thumbnail && !result->bytes.empty() && !discImage(result->bytes)) {
                        result->bytes.clear(); result->error = tr(L"Wybrana grafika nie jest skanem płyty.");
                    }
                    if (!result->bytes.empty()) {
                        if (thumbnail) { if (!esde) saveThumbnail(file, result->bytes); } // Never write into ES-DE's folders.
                        else if (!cacheArtworkVariant(job.game, job.kind, job.asset.id, result->bytes, cache)) {
                            result->bytes.clear();
                            result->error = tr(L"Nie można zapisać grafiki w cache.");
                        }
                    }
                }
                if (!thumbnail && result->bytes.empty() && result->error.empty()) result->error = tr(L"Nie można pobrać wybranej grafiki.");
                if (!thumbnail && !result->bytes.empty() && job.kind == ArtworkKind::cover) {
                    auto focus = file;
                    focus.replace_extension(L".focus");
                    result->focus = cachedTextCrop(file, focus, job.game.name);
                }
            }
            if (!alive->load() || queue->generation.load() != job.generation) continue;
            if (PostMessageW(window, artwork_browser_message, 0, reinterpret_cast<LPARAM>(result.get()))) result.release();
        }
    });
}

void Launcher::openArtworkBrowser(const Game& game) {
    if (!isGame(game) || gameKey(game).empty() || pending_launch_) return;
    desktopLeaveLibrary(); rememberPlace();
    artwork_return_ = screen_;
    artwork_return_selected_ = selected_;
    artwork_return_scroll_ = grid_scroll_;
    // Open on the artwork displayed by the current home layout. The desktop window's grid shows
    // posters (the square cover only stands in for a missing one), whichever page it was opened from.
    const auto opened_from = desktop_mode_ || library_view::posterArt(libraryPresentation()) ? ArtworkKind::poster :
        libraryLandscape() ? ArtworkKind::landscape : flixActive() && selected_ < flix_card_rows_.size() ?
        (flix_card_rows_[selected_] == 0 ? ArtworkKind::landscape : ArtworkKind::poster) :
        metroActive() && selected_ < metro_tiles_.size() ?
        (metro_layout::landscape(metro_tiles_[selected_].role) ? ArtworkKind::landscape : ArtworkKind::poster) :
        (xmbActive() && settings_.xmb_posters) || shelfPosters() ? ArtworkKind::poster : ArtworkKind::cover;
    artwork_state_ = {};
    artwork_state_.game = game;
    artwork_state_.source = artwork_state_.override_source = selectedArtworkSource(game, coverCacheDirectory());
    artwork_state_.query = game.name;
    artwork_state_.category = artwork_ui::category(opened_from);
    dismissMenu();
    menu_progress_ = 0;
    closeFriends();
    closeCategoryMenu();
    suspendDetails();
    screen_ = ScreenMode::artwork;
    rebuildCards(false);
    loadArtworkPage();
}

void Launcher::closeArtworkBrowser() {
    {
        std::lock_guard lock(artwork_jobs_->mutex);
        artwork_jobs_->generation.fetch_add(1);
        artwork_jobs_->jobs.clear();
    }
    artwork_state_.assets.clear();
    artwork_state_.applying = false;
    renderer_.resetArtworkThumbnails(0);
    screen_ = artwork_return_;
    grid_scroll_ = artwork_return_scroll_;
    rebuildCards(false);
    if (screen_ == ScreenMode::details) { requestDetails(); return; }
    if (!cards_.empty()) navigate(static_cast<int>(std::min(artwork_return_selected_, cards_.size() - 1)) - static_cast<int>(selected_), false);
}

void Launcher::closeEditorsFor(const std::function<bool(const Game&)>& gone) {
    if (artworkOpen() && gone(artwork_state_.game)) closeArtworkBrowser();
    if (cropOpen() && !crop_games_.empty() && gone(crop_games_[0])) closeCropEditor();
}

void Launcher::loadArtworkPage() {
    auto& state = artwork_state_;
    state.search_focus = state.results_mode = false;
    state.assets.clear();
    state.requested.clear();
    state.selected = 0;
    state.scroll = 0;
    state.loading = true;
    state.more = state.applying = false;
    state.status = tr(L"Wczytywanie…");
    renderer_.resetArtworkThumbnails(0);
    {
        std::lock_guard lock(artwork_jobs_->mutex);
        const auto generation = artwork_jobs_->generation.fetch_add(1) + 1;
        artwork_jobs_->jobs.clear();
        ArtworkBrowserJob job{ArtworkBrowserMessage::Type::list, generation, state.game, state.kind(), state.page, state.source};
        job.provider = state.kind() == ArtworkKind::disc ? ArtworkProvider::screenscraper : state.emulated() ? state.provider : ArtworkProvider::steamgriddb;
        if (state.kind() == ArtworkKind::disc && !state.query.empty()) job.game.name = state.query;
        artwork_jobs_->jobs.push_back(std::move(job));
    }
    artwork_jobs_->wake.notify_one();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::editArtworkQuery() {
    auto& state = artwork_state_;
    if (state.applying) return;
    state.search_focus = true; state.gallery = false; state.loading = false;
    state.results_mode = true; state.games.clear(); state.assets.clear(); state.status.clear();
    { std::lock_guard lock(artwork_jobs_->mutex); ++artwork_jobs_->generation; artwork_jobs_->jobs.clear(); }
    renderer_.resetArtworkThumbnails(0);
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::leaveArtworkSearch() {
    auto& state = artwork_state_;
    state.search_focus = state.query_selected = false;
    // An edit that was never searched left an empty results page: show the gallery again.
    if (state.results_mode && state.games.empty() && !state.loading) { state.source = state.override_source; loadArtworkPage(); }
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::clearArtworkQuery() {
    if (!artworkOpen() || artwork_state_.applying) return;
    auto& state = artwork_state_;
    state.query.clear(); state.query_selected = false;
    editArtworkQuery();
    audio_.context();
}
void Launcher::searchArtwork() {
    if (artwork_state_.applying) return;
    if (artwork_state_.kind() == ArtworkKind::disc) { loadArtworkPage(); return; }
    editArtworkQuery();
    auto& state = artwork_state_; state.loading = true; state.status = tr(L"Szukanie gry…"); state.selected = 0; state.scroll = 0;
    ArtworkBrowserJob job; job.type = ArtworkBrowserMessage::Type::search; job.query = state.query;
    { std::lock_guard lock(artwork_jobs_->mutex); job.generation = artwork_jobs_->generation; artwork_jobs_->jobs.push_back(std::move(job)); }
    artwork_jobs_->wake.notify_one();
}

void Launcher::requestArtworkThumbnails() {
    if (!artworkOpen() || artwork_state_.assets.empty()) return;
    const auto view = renderer_.viewport();
    auto& state = artwork_state_;
    if (desktopSheet()) {
        // The desktop dialog's gallery scrolls on its own: the page's thumbnails, the selected first.
        std::lock_guard lock(artwork_jobs_->mutex);
        for (size_t i = 0; i < state.assets.size(); ++i) {
            const size_t index = i == 0 ? std::min(state.selected, state.assets.size() - 1) : (i <= state.selected ? i - 1 : i);
            const auto& asset = state.assets[index];
            if (!state.requested.insert(asset.id).second) continue;
            ArtworkBrowserJob job{ArtworkBrowserMessage::Type::thumbnail, artwork_jobs_->generation.load(), state.game,
                                  state.kind(), state.page, state.source, asset};
            job.provider = state.provider;
            artwork_jobs_->jobs.push_back(std::move(job));
        }
        artwork_jobs_->wake.notify_all();
        return;
    }
    const artwork_ui::Layout layout{view.width, view.height, state.kind()};
    state.scroll = layout.ensureVisible(state.selected, state.assets.size(), state.scroll);
    std::lock_guard lock(artwork_jobs_->mutex);
    const auto request = [&](size_t index) {
        const auto& asset = state.assets[index];
        if (!state.requested.insert(asset.id).second) return;
        ArtworkBrowserJob job{ArtworkBrowserMessage::Type::thumbnail, artwork_jobs_->generation.load(), state.game,
                              state.kind(), state.page, state.source, asset};
        job.provider = state.provider;
        if (index == state.selected) artwork_jobs_->jobs.push_front(std::move(job));
        else artwork_jobs_->jobs.push_back(std::move(job));
    };
    request(state.selected);
    for (size_t i = 0; i < state.assets.size(); ++i) {
        const auto r = layout.cell(i, state.scroll);
        if (r.bottom >= layout.top && r.top < layout.bottom()) request(i);
    }
    artwork_jobs_->wake.notify_all();
}

void Launcher::onArtworkBrowser(std::unique_ptr<ArtworkBrowserMessage> message) {
    if (!artworkOpen() || message->generation != artwork_jobs_->generation.load()) return;
    auto& state = artwork_state_;
    if (message->type == ArtworkBrowserMessage::Type::search) {
        state.loading = false; state.games = std::move(message->search.games); state.status = std::move(message->search.error);
        // Equal titles: the release year known for this game (RAWG, DRM-free folder) comes first.
        int year = 0;
        if (const auto found = rawg_metadata_.find(gameKey(state.game)); found != rawg_metadata_.end() && found->second.details.release_date.size() >= 4)
            year = _wtoi(found->second.details.release_date.substr(0, 4).c_str());
        if (!year && state.game.source == GameSource::drmfree)
            for (const auto& entry : drm_catalog_.games) if (entry.id == state.game.source_id) year = entry.year;
        if (year) {
            const auto title = state.query;
            std::stable_sort(state.games.begin(), state.games.end(), [&](const GridGame& a, const GridGame& b) {
                const int left = gridTitleScore(title, a.name), right = gridTitleScore(title, b.name);
                if (left != right) return left > right;
                const auto distance = [&](const GridGame& game) { return game.year ? std::abs(game.year - year) : 10000; };
                return distance(a) < distance(b);
            });
        }
        state.search_focus = state.games.empty(); state.gallery = !state.games.empty(); state.selected = 0;
        InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    if (message->type == ArtworkBrowserMessage::Type::list) {
        state.loading = false;
        state.assets = std::move(message->page.assets);
        state.provider = message->page.provider;
        if (message->page.provider == ArtworkProvider::steamgriddb) state.source = message->page.source;
        state.more = message->page.more;
        state.applied_id = message->applied_id;
        state.status = message->page.error;
        if (state.status.empty() && state.assets.empty()) state.status = tr(L"Brak grafik.");
        renderer_.resetArtworkThumbnails(state.assets.size());
        requestArtworkThumbnails();
    } else if (message->type == ArtworkBrowserMessage::Type::thumbnail) {
        for (size_t i = 0; i < state.assets.size(); ++i)
            if (state.assets[i].id == message->asset_id)
                renderer_.setArtworkThumbnail(i, std::move(message->bytes), state.kind() == ArtworkKind::logo);
    } else {
        const bool local = message->type == ArtworkBrowserMessage::Type::local;
        if (!state.applying || (!local && (state.assets.empty() || state.assets[state.selected].id != message->asset_id))) return;
        state.applying = false;
        state.status = message->error;
        const auto kind = local ? message->kind : state.kind();
        if (state.status.empty() && (message->bytes.empty() ||
            !selectArtworkVariant(state.game, kind, message->asset_id, coverCacheDirectory())))
            state.status = tr(L"Nie można zastosować grafiki.");
        if (state.status.empty()) {
            state.applied_id = message->asset_id;
            if (local) state.status = tr(L"Zastosowano grafikę z dysku.");
            const auto key = gameKey(state.game);
            invalidateArtwork(state.game, coverKind(kind));
            auto& cached = art_.at(key);
            switch (kind) {
            case ArtworkKind::cover: cached.cover = shareImage(std::move(message->bytes)); cached.focus = message->focus; break;
            case ArtworkKind::logo: cached.logo = shareImage(std::move(message->bytes)); break;
            case ArtworkKind::poster: cached.poster = shareImage(std::move(message->bytes)); desktop_poster_missing_.erase(key); break;
            case ArtworkKind::landscape: cached.landscape = shareImage(std::move(message->bytes)); break;
            case ArtworkKind::disc: cached.disc = shareImage(std::move(message->bytes)); break;
            default: cached.hero = shareImage(std::move(message->bytes)); cached.hero_tone = HeroTone::full; break;
            }
            art_.resize(key);
            trimArt();
            audio_.context();
        }
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::openLocalArtwork() {
    auto& state = artwork_state_;
    if (!artworkOpen() || state.applying) return;
    state.local_open = true; state.local_focus = true; state.gallery = state.search_focus = false;
    state.local_kind = state.local_row = state.category; // The type in view is the likely one.
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::cycleArtworkProvider() {
    auto& state = artwork_state_;
    if (!artworkOpen() || !state.providerChoice() || state.applying) return;
    const auto at = std::find(artwork_ui::providers.begin(), artwork_ui::providers.end(), state.provider) - artwork_ui::providers.begin();
    state.provider = artwork_ui::providers[(at + 1) % artwork_ui::providers.size()];
    state.page = 0;
    state.source = state.override_source;
    loadArtworkPage();
    audio_.context();
}

// "Popraw kadr": the crop editor of the same game; Back there returns to "Zmień grafikę".
void Launcher::openArtworkCrop() {
    auto& state = artwork_state_;
    if (!artworkOpen() || state.applying) return;
    state.local_focus = state.provider_focus = state.back_focus = false;
    state.crop_focus = true; state.gallery = state.search_focus = false;
    audio_.context();
    openCropEditor(state.game);
}

void Launcher::closeLocalArtwork() {
    artwork_state_.local_open = false;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::pickLocalArtwork() {
    auto& state = artwork_state_;
    if (!artworkOpen() || !state.local_open || state.applying) return;
    if (!pick_image_) {
        file_browser::Request request;
        request.title = tr(L"Wybierz grafikę");
        request.extensions = {L".png", L".jpg", L".jpeg"};
        request.images = true;
        request.native = [this] { return pickArtworkImage(hwnd_); };
        openFileBrowser(std::move(request), [this](const std::filesystem::path& picked) { applyLocalArtwork(picked); });
        return;
    }
    // Tests: an injected picker answers at once.
    const auto picked = pick_image_(hwnd_);
    mouse_transition_until_ = GetTickCount64() + GetDoubleClickTime();
    if (picked.empty()) { InvalidateRect(hwnd_, nullptr, FALSE); return; }
    applyLocalArtwork(picked);
}

void Launcher::applyLocalArtwork(const std::filesystem::path& picked) {
    auto& state = artwork_state_;
    if (!artworkOpen() || !state.local_open || state.applying) return;
    const int kind = std::clamp(state.local_kind, 0, static_cast<int>(artwork_ui::kinds.size()) - 1);
    state.local_open = false;
    // The chosen type's gallery behind it; the file is read and applied on a worker.
    if (state.category != kind || state.results_mode) { state.category = kind; state.page = 0; state.source = state.override_source; loadArtworkPage(); }
    state.applying = true;
    state.status = tr(L"Wczytywanie grafiki…");
    ArtworkBrowserJob job;
    job.type = ArtworkBrowserMessage::Type::local; job.game = state.game; job.kind = artwork_ui::kinds[kind]; job.query = picked.native();
    {
        std::lock_guard lock(artwork_jobs_->mutex);
        job.generation = artwork_jobs_->generation.load();
        artwork_jobs_->jobs.push_front(std::move(job));
    }
    artwork_jobs_->wake.notify_one();
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::artworkMove(int horizontal, int vertical) {
    if (!artworkOpen() || artwork_state_.applying) return;
    auto& state = artwork_state_;
    const auto view = renderer_.viewport();
    const artwork_ui::Layout layout{view.width, view.height, state.kind()};
    if (state.local_open) {
        // Panel: the types, then "Wskaż plik…".
        if (vertical) {
            const int next = std::clamp(state.local_row + vertical, 0, artwork_ui::State::local_pick_row);
            if (next == state.local_row) navigationBoundary(); else audio_.navigate();
            state.local_row = next;
        }
        InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    // Right from any rail entry reaches the search field even while the gallery is loading/empty.
    // Keep the selected rail entry so Left returns there; Down enters the results or gallery.
    if (horizontal > 0 && !state.gallery && !state.search_focus) {
        state.search_focus = state.query_selected = true;
        audio_.navigate(); InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    // The rail's buttons under the types, top to bottom: "Z dysku…", "Źródło: …" (emulated games),
    // "Popraw kadr", Back. Up from the first returns to the last type.
    if (state.railButton() && !state.gallery && !state.search_focus) {
        enum Entry { local, provider, crop, back };
        std::vector<Entry> entries{local};
        if (state.providerChoice()) entries.push_back(provider);
        entries.push_back(crop); entries.push_back(back);
        const Entry at = state.local_focus ? local : state.provider_focus ? provider : state.crop_focus ? crop : back;
        const int position = static_cast<int>(std::find(entries.begin(), entries.end(), at) - entries.begin());
        const auto focusEntry = [&](int index) {
            state.local_focus = state.provider_focus = state.crop_focus = state.back_focus = false;
            if (index < 0) return; // The last type.
            const auto entry = entries[static_cast<size_t>(index)];
            (entry == local ? state.local_focus : entry == provider ? state.provider_focus : entry == crop ? state.crop_focus : state.back_focus) = true;
        };
        if (vertical) {
            const int next = position + (vertical > 0 ? 1 : -1);
            if (next >= static_cast<int>(entries.size())) { navigationBoundary(); return; }
            focusEntry(next);
        } else { navigationBoundary(); return; }
        audio_.navigate();
        InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    if (state.search_focus) {
        if (horizontal < 0) { leaveArtworkSearch(); audio_.navigate(); }
        else if (vertical > 0) {
            if ((state.results_mode && !state.games.empty()) || (!state.results_mode && !state.assets.empty())) {
                state.search_focus = state.query_selected = false; state.gallery = true;
            }
            else leaveArtworkSearch();
            audio_.navigate();
        } else if (vertical < 0) navigationBoundary();
        InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    if (state.results_mode && state.gallery) {
        if (horizontal < 0) state.gallery = false;
        else if (vertical < 0 && state.selected == 0) { state.search_focus = true; state.gallery = false; }
        else if (!state.games.empty()) {
            const auto before = state.selected;
            state.selected = static_cast<size_t>(std::clamp(static_cast<int>(state.selected) + vertical, 0, static_cast<int>(state.games.size()) - 1));
            if (vertical && before == state.selected) navigationBoundary();
            const auto row = layout.gameRow(state.selected, state.scroll);
            if (row.bottom > layout.bottom()) state.scroll += row.bottom - layout.bottom();
            if (row.top < layout.top) state.scroll = std::max(0.0f, state.scroll - layout.top + row.top);
        }
        InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    if (!state.gallery) {
        if (vertical) {
            const int next = std::clamp(state.category + vertical, 0, static_cast<int>(artwork_ui::kinds.size()) - 1);
            if (vertical < 0 && state.category == 0) { state.search_focus = true; state.query_selected = true; }
            else if (next != state.category) { state.category = next; state.page = 0; state.source = state.override_source; loadArtworkPage(); }
            else if (vertical > 0) state.local_focus = true; // Below the last type: "Z dysku…".
            else navigationBoundary();
        }
    } else if (horizontal < 0 && state.selected % layout.columns() == 0) state.gallery = false;
    else if (vertical < 0 && static_cast<int>(state.selected) < layout.columns()) {
        state.gallery = false; state.search_focus = true; state.query_selected = true; // Top row → the search field.
    } else if (!state.assets.empty()) {
        const int next = std::clamp(static_cast<int>(state.selected) + horizontal + vertical * layout.columns(),
                                    0, static_cast<int>(state.assets.size()) - 1);
        if (next == static_cast<int>(state.selected)) navigationBoundary();
        state.selected = static_cast<size_t>(next);
        requestArtworkThumbnails();
    }
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::artworkAccept() {
    if (editingArtworkSearch()) { if (!artwork_state_.loading) searchArtwork(); return; }
    if (!artworkOpen() || artwork_state_.applying) return;
    auto& state = artwork_state_;
    if (state.local_open) {
        // A type is a choice, like a radio button; then focus moves to "Wskaż plik…".
        if (state.local_row == artwork_ui::State::local_pick_row) pickLocalArtwork();
        else { state.local_kind = state.local_row; state.local_row = artwork_ui::State::local_pick_row; audio_.context(); InvalidateRect(hwnd_, nullptr, FALSE); }
        return;
    }
    if (state.local_focus && !state.gallery && !state.search_focus) { openLocalArtwork(); return; }
    if (state.provider_focus && !state.gallery && !state.search_focus) { cycleArtworkProvider(); return; }
    if (state.crop_focus && !state.gallery && !state.search_focus) { openArtworkCrop(); return; }
    if (state.back_focus && !state.gallery && !state.search_focus) { closeArtworkBrowser(); return; }
    if (artwork_state_.loading) return;
    if (state.results_mode) {
        if (!state.games.empty()) {
            if (!state.gallery) { state.gallery = true; return; }
            state.source = state.override_source = state.games[state.selected].source; state.page = 0;
            state.provider = ArtworkProvider::steamgriddb; // A SteamGridDB search result shows its gallery.
            loadArtworkPage(); state.gallery = true;
        }
        return;
    }
    if (!state.gallery) {
        state.gallery = !state.assets.empty();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (state.assets.empty() || state.assets[state.selected].id == state.applied_id) return;
    state.applying = true;
    state.status = tr(L"Pobieranie grafiki…");
    {
        std::lock_guard lock(artwork_jobs_->mutex);
        ArtworkBrowserJob job{ArtworkBrowserMessage::Type::apply, artwork_jobs_->generation.load(),
            state.game, state.kind(), state.page, state.source, state.assets[state.selected]};
        job.provider = state.provider;
        artwork_jobs_->jobs.push_front(std::move(job));
    }
    artwork_jobs_->wake.notify_one();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::artworkPage(int amount) {
    if (artwork_state_.results_mode || artwork_state_.search_focus) return;
    if (!artworkOpen() || artwork_state_.loading || artwork_state_.applying || !amount) return;
    if (amount < 0 && artwork_state_.page == 0 || amount > 0 && !artwork_state_.more) return;
    artwork_state_.page += amount > 0 ? 1 : -1;
    loadArtworkPage();
}

void Launcher::artworkWheel(float mouse_x, int amount) {
    const auto view = renderer_.viewport();
    if (!artworkOpen() || artwork_state_.applying) return;
    artwork_state_.gallery = mouse_x / view.scale > settings_ui::Layout{view.width, view.height}.railRight() &&
        (artwork_state_.results_mode ? !artwork_state_.games.empty() : !artwork_state_.assets.empty());
    if (artwork_state_.gallery) artwork_state_.search_focus = false;
    artworkMove(0, amount);
}

void Launcher::artworkClick(float x, float y, bool double_click) {
    const auto view = renderer_.viewport();
    auto& state = artwork_state_;
    const artwork_ui::Layout layout{view.width, view.height, state.kind(), state.providerChoice()};
    if (state.local_open) {
        const int row = layout.hitLocal(x, y);
        if (row == artwork_ui::State::local_pick_row) { state.local_row = row; pickLocalArtwork(); return; }
        if (row >= 0) { state.local_kind = state.local_row = row; audio_.context(); }
        else if (!layout.localPanel().contains(x, y)) closeLocalArtwork();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (layout.base().backButton().contains(x, y)) { closeArtworkBrowser(); return; }
    if (state.applying) return;
    if (layout.localButton().contains(x, y)) { openLocalArtwork(); return; }
    if (state.providerChoice() && layout.providerButton().contains(x, y)) {
        state.local_focus = state.crop_focus = state.back_focus = false; state.provider_focus = true; state.gallery = state.search_focus = false;
        cycleArtworkProvider(); return;
    }
    if (layout.cropButton().contains(x, y)) { openArtworkCrop(); return; }
    state.local_focus = state.provider_focus = state.crop_focus = state.back_focus = false;
    if (!state.query.empty() && layout.clearButton().contains(x, y)) { clearArtworkQuery(); return; }
    if (layout.searchBox().contains(x, y)) { state.search_focus = true; state.gallery = false; state.query_selected = true; InvalidateRect(hwnd_, nullptr, FALSE); return; }
    if (layout.searchButton().contains(x, y)) { searchArtwork(); return; }
    for (int i = 0; i < static_cast<int>(artwork_ui::kinds.size()); ++i) if (layout.base().rail(i).contains(x, y)) {
        state.gallery = false;
        if (i != state.category || state.results_mode) { state.category = i; state.page = 0; state.source = state.override_source; loadArtworkPage(); }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (layout.pageButton(false).contains(x, y)) { artworkPage(-1); return; }
    if (layout.pageButton(true).contains(x, y)) { artworkPage(1); return; }
    if (state.results_mode) {
        for (size_t i = 0; i < state.games.size(); ++i) if (y >= layout.top && y < layout.bottom() && layout.gameRow(i, state.scroll).contains(x, y)) {
            state.selected = i; state.search_focus = false; state.gallery = true; artworkAccept(); return;
        }
        return;
    }
    const int index = layout.hit(x, y, state.assets.size(), state.scroll);
    if (index >= 0) {
        state.selected = static_cast<size_t>(index);
        state.gallery = true;
        if (double_click) artworkAccept();
        requestArtworkThumbnails();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}
