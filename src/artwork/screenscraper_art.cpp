#include "screenscraper_art.hpp"
#include "../platform/unicode_text.hpp"
#include "image_codec.hpp"
#include "image_compact.hpp"
#include "cover_cache.hpp"
#include "../library/game_ratings.hpp"
#include "../i18n/i18n.hpp"
#include "../library/esde_gamelist.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/video_manifest.hpp"
#include "../platform/user_config.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <span>

ScreenScraperGallery screenscraperGallery(const Game& game, const std::vector<std::wstring_view>& types,
                                          const std::filesystem::path& config_root, const screenscraper::Get& get) {
    namespace ss = screenscraper;
    ScreenScraperGallery result;
    const auto config = ss::load();
    if (!config.ready()) { result.error = tr(L"Dodaj dane ScreenScraper w Ustawieniach → Media i dane."); return result; }
    const auto metadata = config_root / L"metadata";
    const auto rom = ss::describeRom(game.rom, 0);
    const auto systems = ss::systemIds(game.platform, game.rom);
    if (!rom || systems.empty()) { result.error = tr(L"Nie znam pliku ROM ani systemu tej gry."); return result; }
    auto cached = ss::loadCached(game, metadata);
    std::error_code ec;
    const bool connected = std::filesystem::exists(game.rom, ec);
    // An answer for this file, or any answer while its drive is not connected, is used as is;
    // a miss remembered for less than 7 days is not asked again.
    if (cached && !cached->game && (cached->fingerprint == ss::fingerprint(game, *rom) || !connected) &&
        ss::unixMs() - cached->checked < ss::missing_retry_ms) { result.error = tr(L"ScreenScraper nie zna tego pliku ROM."); return result; }
    if (!cached || !cached->game || (connected && cached->fingerprint != ss::fingerprint(game, *rom))) {
        if (!connected) { result.error = tr(L"Nie znam pliku ROM ani systemu tej gry."); return result; }
        if (ss::paused(metadata)) { result.error = tr(L"ScreenScraper jest chwilowo niedostępny. Spróbuję później."); return result; }
        auto file = *rom;
        if (file.size && file.size <= ss::hash_limit) file.md5 = ss::md5File(game.rom);
        ss::Lookup found;
        for (size_t i = 0; i < systems.size() && i < 2; ++i) {
            found = ss::lookup(config, systems[i], file, i18n::language() == i18n::Language::pl ? L"pl" : L"en", get);
            if (found.status != ss::Status::missing) break;
        }
        const auto now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        switch (found.status) {
        case ss::Status::found: cached = ss::Cached{ss::fingerprint(game, file), now, found.game}; ss::saveCached(game, *cached, metadata); break;
        case ss::Status::missing:
            ss::saveCached(game, ss::Cached{ss::fingerprint(game, file), now, {}}, metadata);
            result.error = tr(L"ScreenScraper nie zna tego pliku ROM."); return result;
        case ss::Status::rejected:
            ss::pause(metadata, 60 * 60 * 1000);
            result.error = tr(L"ScreenScraper odrzucił dane dewelopera (devid/devpassword) lub konto. Sprawdź Ustawienia → Biblioteka."); return result;
        case ss::Status::quota:
            ss::pause(metadata, 12ull * 60 * 60 * 1000);
            result.error = tr(L"Dzienny limit zapytań ScreenScraper wyczerpany. Spróbuję jutro."); return result;
        case ss::Status::throttled: ss::pause(metadata, 2 * 60 * 1000); [[fallthrough]];
        default: result.error = tr(L"ScreenScraper jest chwilowo niedostępny. Spróbuję później."); return result;
        }
    }
    for (const auto type : types)
        for (const auto& media : cached->game->media)
            if (media.type == type && !media.url.empty()) result.media.push_back(media);
    return result;
}

std::filesystem::path downloadedVideoPath(const Game& game, const std::filesystem::path& folder) {
    if (folder.empty() || game.rom.empty()) return {};
    std::wstring system;
    for (const auto& candidate : esde::systems(game.platform, game.rom))
        if (screenscraper::systemId(candidate)) { system = candidate; break; }
    const auto rom = screenscraper::describeRom(game.rom, 0); // "Game.ps3" for folder games.
    if (system.empty() || !rom || rom->name.empty()) return {};
    return folder / system / L"videos" / (std::filesystem::path(rom->name).stem().wstring() + L".mp4");
}

std::filesystem::path gameVideoFile(const Game& game, const std::filesystem::path& config_root,
                                    const std::filesystem::path& video_folder, const VideoDownload& download) {
    if (game.kind != GameKind::nonsteam || !game.app_id) return {};
    if (auto local = esdeVideoFile(game.platform, game.name, game.rom); !local.empty()) return local;
    const auto target = downloadedVideoPath(game, video_folder);
    if (target.empty()) return {};
    std::error_code ec;
    if (std::filesystem::is_regular_file(target, ec) && std::filesystem::file_size(target, ec) > 1024) return target;
    const auto identified = screenscraper::loadCached(game, config_root / L"metadata");
    if (!identified || !identified->game || screenscraper::paused(config_root / L"metadata")) return {};
    // A download that failed is not tried again for 3 days, however often the game is focused.
    auto failed = target; failed += L".none";
    {
        std::ifstream marker(failed);
        uint64_t since = 0;
        if (marker >> since && screenscraper::unixMs() - since < 3ull * 24 * 60 * 60 * 1000) return {};
    }
    const auto config = screenscraper::load();
    if (!config.ready()) return {};
    // One video download at a time: browsing past other games must not queue theirs behind it.
    static std::atomic_bool downloading{false};
    if (downloading.exchange(true)) return {};
    struct Release { std::atomic_bool& flag; ~Release() { flag.store(false); } } release{downloading};
    bool attempted = false;
    for (const auto type : {L"video-normalized", L"video"}) {
        for (const auto& media : identified->game->media) {
            if (media.type != type || media.url.empty()) continue;
            const auto address = screenscraper::mediaUrl(config, media.url);
            if (address.empty()) continue;
            attempted = true;
            const auto response = screenscraper::gated([&] { return download(address, target); }, false);
            if (response.status == 429 || response.status == 430 || response.status == 431) {
                screenscraper::pauseAfter(config_root / L"metadata", response.status);
                return {};
            }
            auto part = target; part += L".part";
            if (response.status != 200 && std::filesystem::exists(part, ec)) return {}; // Interrupted: resumed next time.
            if (response.status != 200) continue;
            // An MP4 starts with a box whose type is "ftyp"; "NOMEDIA" and errors do not.
            char head[8]{};
            { std::ifstream input(target, std::ios::binary); input.read(head, 8); }
            if (std::filesystem::file_size(target, ec) > 1024 && std::memcmp(head + 4, "ftyp", 4) == 0) {
                video_manifest::add(video_folder, target);
                return target;
            }
            std::filesystem::remove(target, ec);
        }
    }
    if (attempted) atomic_file::write(failed, std::to_string(screenscraper::unixMs()));
    return {};
}

HttpResponse downloadVideo(const std::wstring& url, const std::filesystem::path& file) {
    return httpDownloadFile(url, L"Accept: video/mp4\r\n", file, 96 * 1024 * 1024, 5 * 60 * 1000, true);
}

std::vector<uint8_t> screenscraperImage(std::wstring_view url, bool thumbnail, const screenscraper::Get& get) {
    if (screenscraper::paused(userConfigDirectory() / L"metadata")) return {};
    const auto config = screenscraper::load();
    screenscraper::Media media;
    media.url = std::wstring(url);
    if (thumbnail) media.url += (media.url.find(L'?') == std::wstring::npos ? L"?" : L"&") + std::wstring(L"maxwidth=400");
    uint32_t status = 0;
    auto bytes = screenscraper::downloadMedia(config, media, get, &status);
    if (bytes.empty()) screenscraper::pauseAfter(userConfigDirectory() / L"metadata", status);
    return bytes;
}

std::vector<uint8_t> screenscraperArt(const Game& game, EmuArtKind kind, const std::filesystem::path& config_root,
                                      const screenscraper::Get& get) {
    if (game.kind != GameKind::nonsteam || !game.app_id) return {};
    const auto cached = screenscraper::loadCached(game, config_root / L"metadata");
    if (!cached || !cached->game || screenscraper::paused(config_root / L"metadata")) return {};
    const auto config = screenscraper::load();
    if (!config.ready()) return {};
    const auto slot = kind == EmuArtKind::cover ? screenscraper::ArtSlot::cover :
                      kind == EmuArtKind::background ? screenscraper::ArtSlot::background : screenscraper::ArtSlot::logo;
    const auto* media = screenscraper::pickMedia(*cached->game, slot);
    if (!media) return {};
    uint32_t status = 0;
    auto bytes = screenscraper::downloadMedia(config, *media, get, &status);
    if (bytes.empty()) { screenscraper::pauseAfter(config_root / L"metadata", status); return {}; }
    writeCachedImage(esdeArtCachePath(game.app_id, kind, config_root / L"emulation"), bytes);
    return bytes;
}

uint32_t discSystemId(const Game& game) {
    if (!isGame(game)) return 0;
    if (game.kind != GameKind::nonsteam || game.platform == L"PC" || game.platform.empty()) return 138; // Microsoft Windows, API systemesListe.
    const auto systems = screenscraper::systemIds(game.platform, game.rom);
    for (const auto system : systems)
        for (const auto optical : {20u, 22u, 23u, 29u, 32u, 33u, 57u, 58u, 59u, 60u, 61u, 70u, 72u, 114u, 129u, 130u, 133u, 171u, 13u, 16u, 18u})
            if (system == optical) return system;
    return 0;
}

ScreenScraperGallery screenscraperDiscGallery(const Game& game, const std::filesystem::path& config_root, const screenscraper::Get& get) {
    namespace ss = screenscraper;
    ScreenScraperGallery result;
    const auto system = discSystemId(game);
    if (!system) { result.error = tr(L"Ta platforma nie korzysta z płyt."); return result; }
    const auto config = ss::load();
    if (!config.ready()) { result.error = tr(L"Dodaj dane ScreenScraper w Ustawieniach → Media i dane."); return result; }
    if (ss::paused(config_root / L"metadata")) { result.retryable = true; result.error = tr(L"ScreenScraper jest chwilowo niedostępny. Spróbuję później."); return result; }
    uint32_t last_http = 0;
    bool requested = false;
    const auto observed = [&](const std::wstring& url, const std::wstring& headers, size_t max) {
        requested = true; auto response = get(url, headers, max); last_http = response.status; return response;
    };
    if (game.kind == GameKind::nonsteam && !game.rom.empty() && system != 138) {
        result = screenscraperGallery(game, {L"support-2D"}, config_root, observed);
        result.retryable = (requested && !last_http) || last_http >= 500 || last_http == 429 || last_http == 430 || last_http == 431;
        if (!result.media.empty()) result.retryable = false;
    } else {
        // Separate from ROM identification: no guessed title match can overwrite an identified ROM.
        const auto folder = config_root / L"metadata" / L"discs";
        const auto fingerprint = std::to_wstring(system) + L":" + ratings::normalizedTitle(game.name);
        auto cached = ss::loadCached(game, folder);
        if (cached && cached->fingerprint != fingerprint) cached.reset();
        if (cached && !cached->game && ss::unixMs() - cached->checked < ss::missing_retry_ms) return result;
        if (!cached || !cached->game) {
            ss::Lookup found;
            uint32_t matched_system = system;
            // Older Windows CD editions may belong to the Windows 95/98 catalog (137).
            for (const auto platform : {system, system == 138 ? 137u : system}) {
                matched_system = platform;
                found = ss::search(config, game.name, platform, L"en", observed);
                if (found.status != ss::Status::missing && (found.status != ss::Status::found || !found.games.empty())) break;
                if (system != 138) break;
            }
            if (found.status != ss::Status::found && found.status != ss::Status::missing) {
                result.retryable = true;
                if (found.status == ss::Status::rejected) ss::pause(config_root / L"metadata", 60 * 60 * 1000);
                ss::pauseAfter(config_root / L"metadata", last_http);
                result.error = tr(L"Nie można pobrać grafik płyt ze ScreenScraper."); return result;
            }
            const auto wanted = ratings::normalizedTitle(game.name);
            const ss::GameInfo* match = nullptr;
            for (const auto& candidate : found.games) {
                const bool same = ratings::normalizedTitle(candidate.name) == wanted ||
                    std::any_of(candidate.names.begin(), candidate.names.end(), [&](const auto& name) { return ratings::normalizedTitle(name) == wanted; });
                if (!same) continue;
                if (match && match->id != candidate.id) { result.error = tr(L"ScreenScraper znalazł kilka wydań tej gry."); return result; }
                match = &candidate;
            }
            std::optional<ss::GameInfo> game_info;
            if (match) {
                auto full = ss::lookupGame(config, match->id, matched_system, L"en", observed);
                if (full.status != ss::Status::found || !full.game) {
                    result.retryable = true; result.error = tr(L"Nie można pobrać grafik płyt ze ScreenScraper."); return result;
                }
                game_info = std::move(full.game);
            }
            cached = ss::Cached{fingerprint, ss::unixMs(), std::move(game_info)};
            ss::saveCached(game, *cached, folder);
        }
        if (cached->game) for (const auto& media : cached->game->media) if (media.type == L"support-2D") result.media.push_back(media);
    }
    std::erase_if(result.media, [](const auto& media) { return media.url.empty() || !unicode_text::equalIgnoreCase(media.format, L"png"); });
    const auto rank = [](const auto& media) { return media.region == L"eu" ? 0 : media.region == L"wor" ? 1 : media.region == L"us" ? 2 : media.region == L"jp" ? 3 : 4; };
    std::stable_sort(result.media.begin(), result.media.end(), [&](const auto& a, const auto& b) { return rank(a) < rank(b); });
    return result;
}

bool discImage(const std::vector<uint8_t>& bytes) {
    if (bytes.empty() || bytes.size() > 20 * 1024 * 1024) return false;
    const auto image = image_codec::decode(bytes, [](const image_codec::Info& info) -> std::optional<image_codec::Size> {
        const uint32_t w = info.width, h = info.height;
        if (w < 64 || h < 64 || w > 8192 || h > 8192 || static_cast<double>(w) / h < 0.92 || static_cast<double>(w) / h > 1.08) return {};
        return image_codec::Size{w, h};
    });
    if (!image) return false;
    const uint32_t w = image->width, h = image->height;
    const auto alpha = [&](uint32_t x, uint32_t y) { return static_cast<int>(image->pixels[(size_t(y) * w + x) * 4 + 3]); };
    for (const auto [x, y] : {std::pair{0u, 0u}, {w - 1, 0u}, {0u, h - 1}, {w - 1, h - 1}, {w / 2, h / 2}})
        if (const int a = alpha(x, y); a < 0 || a > 32) return false;
    for (const auto [x, y] : {std::pair{w / 4, h / 2}, {w * 3 / 4, h / 2}, {w / 2, h / 4}, {w / 2, h * 3 / 4}})
        if (alpha(x, y) < 180) return false;
    return true;
}

DiscArtwork discArtwork(const Game& game, const std::filesystem::path& cache, const screenscraper::Get& get) {
    if (auto bytes = cachedArtwork(game, ArtworkKind::disc, cache); !bytes.empty()) return {std::move(bytes)};
    // Never resolve the selected-variant path after HTTP: a manual pick may have changed it
    // while this request was in flight. Background downloads own only the automatic cache file.
    const auto automatic = steamArtwork(game) ? cache.parent_path() / L"discs" / (std::to_wstring(game.app_id) + L".v2.img") :
        gameArtworkDirectory(game, cache) / L"disc.v2.img";
    const auto gallery = screenscraperDiscGallery(game, cache.parent_path(), get);
    if (gallery.retryable) return {{}, true};
    const auto config = screenscraper::load();
    size_t attempts = 0;
    for (const auto& media : gallery.media) {
        if (++attempts > 3) break;
        uint32_t status = 0;
        auto bytes = screenscraper::downloadMedia(config, media, get, &status);
        screenscraper::pauseAfter(cache.parent_path() / L"metadata", status);
        if (!status || status == 429 || status == 430 || status == 431 || status >= 500) return {{}, true};
        if (!discImage(bytes)) continue;
        if (!writeCachedImage(automatic, bytes)) return {{}, true};
        return {std::move(bytes)};
    }
    return {};
}
