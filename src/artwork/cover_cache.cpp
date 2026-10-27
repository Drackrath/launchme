#include "cover_cache.hpp"
#include "image_compact.hpp"
#include "screenscraper_art.hpp"
#include "steamgriddb_search.hpp"
#include "../net/http.hpp"
#include "../net/json.hpp"
#include "../platform/user_config.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/text_encoding.hpp"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <atomic>

namespace {
bool atomicBytes(const std::filesystem::path& file, const uint8_t* bytes, size_t size);
thread_local bool retryable_artwork_error = false;
std::atomic<ULONGLONG> sgdb_resume_at{0};
HttpResponse request(const std::wstring& url, bool authorized, size_t max_bytes) {
    std::wstring headers = L"Accept: application/json, image/jpeg, image/png\r\n";
    if (authorized) {
        if (GetTickCount64() < sgdb_resume_at.load()) { retryable_artwork_error = true; return {}; }
        const auto key = userSecret(L"STEAMGRIDDB_API_KEY", L"steamgriddb_api_key.txt");
        if (key.empty()) return {};
        headers += L"Authorization: Bearer " + key + L"\r\n";
    }
    auto reply = httpGet(url, headers, max_bytes);
    if (authorized && reply.status == 429) {
        const auto until = GetTickCount64() + static_cast<ULONGLONG>(std::clamp(reply.retry_after_seconds ? reply.retry_after_seconds : 60u, 1u, 3600u)) * 1000;
        auto previous = sgdb_resume_at.load();
        while (previous < until && !sgdb_resume_at.compare_exchange_weak(previous, until)) {}
    }
    if (!reply.status || reply.status == 429 || reply.status >= 500) retryable_artwork_error = true;
    return reply;
}
std::vector<uint8_t> get(const std::wstring& url, bool authorized, size_t max_bytes) { return request(url, authorized, max_bytes).body; }

// Only the first asset of an SGDB list response is needed: data[0].url (or .thumb).
std::wstring firstUrl(const std::vector<uint8_t>& body, std::string_view key = "url") {
    const auto parsed = json::parse(std::string_view(reinterpret_cast<const char*>(body.data()), body.size()));
    const auto* data = parsed ? parsed->get("data") : nullptr;
    if (!data || data->type != json::Value::Type::array || data->elements.empty()) return {};
    const auto* url = data->elements.front().get(key);
    if (!url || url->type != json::Value::Type::string || !url->text.starts_with("https://")) return {};
    return text_encoding::wide(url->text);
}

bool isImage(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < 128) return false;
    return (bytes[0] == 0xff && bytes[1] == 0xd8) ||
           (bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G');
}

// A cached or local image in one read (size known up front, no byte-by-byte growth); empty
// when missing, larger than `limit` or not a JPEG/PNG. Downloads are capped at 10 MiB and
// chosen variants at 20 MiB, so the default never rejects a file this cache wrote.
std::vector<uint8_t> readImage(const std::filesystem::path& file, size_t limit = 20 * 1024 * 1024) {
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input) return {};
    const auto size = static_cast<long long>(input.tellg());
    if (size <= 0 || static_cast<unsigned long long>(size) > limit) return {};
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())) || !isImage(bytes)) return {};
    return bytes;
}

uint32_t gameIdForTitle(uint32_t app_id, const std::wstring& title,
                        const std::filesystem::path& cache) {
    if (title.empty() || title.starts_with(L"AppID ") || placeholderGameName(title)) return 0;
    static std::mutex mutex;
    static std::unordered_map<std::wstring, uint32_t> known;
    // Titles SteamGridDB had no match for: each kind of artwork (cover, poster, hero, logo, …)
    // asked again would repeat the same searches; for half an hour they are not.
    static std::unordered_map<std::wstring, ULONGLONG> missed;
    constexpr ULONGLONG miss_lifetime = 30 * 60 * 1000;
    const auto mapping = cache / (std::to_wstring(app_id) + L".sgdbid");
    // The title is part of the in-memory key: a renamed user-added game must be matched
    // again (its stale mapping file is removed together with its artwork).
    const auto key = mapping.wstring() + L'\n' + title;
    {
        std::lock_guard lock(mutex);
        if (const auto existing = known.find(key); existing != known.end()) return existing->second;
        if (const auto miss = missed.find(key); miss != missed.end()) {
            if (GetTickCount64() - miss->second < miss_lifetime) return 0;
            missed.erase(miss);
        }
    }
    uint32_t result = 0;
    {
        std::ifstream saved(mapping);
        if (saved >> result && result) {
            std::lock_guard lock(mutex);
            known[key] = result;
            return result;
        }
    }
    const auto api_key = userSecret(L"STEAMGRIDDB_API_KEY", L"steamgriddb_api_key.txt");
    const bool failed_before = retryable_artwork_error;
    retryable_artwork_error = false;
    const auto match = resolveGridGame(title, api_key, [](const auto& url, const auto&, size_t limit) { return request(url, true, limit); });
    const bool unreachable = retryable_artwork_error; // A rate limit or no connection: not an answer.
    retryable_artwork_error = failed_before || unreachable;
    result = match.source.id;
    if (!result && !unreachable && !api_key.empty()) {
        std::lock_guard lock(mutex);
        missed[key] = GetTickCount64();
    }
    if (result) {
        {
            std::lock_guard lock(mutex);
            known[key] = result;
        }
        atomic_file::write(mapping, std::to_string(result));
    }
    return result;
}

std::vector<uint8_t> grid(uint32_t id, const wchar_t* source, const wchar_t* dimensions) {
    const auto endpoint = L"https://www.steamgriddb.com/api/v2/grids/" + std::wstring(source) +
                          L"/" + std::to_wstring(id) + L"?dimensions=" + dimensions + L"&limit=1";
    const auto url = firstUrl(get(endpoint, true, 512 * 1024));
    if (url.empty()) return {};
    return get(url, false, 10 * 1024 * 1024);
}

std::vector<uint8_t> asset(uint32_t id, const wchar_t* source, const wchar_t* kind) {
    const auto endpoint = L"https://www.steamgriddb.com/api/v2/" + std::wstring(kind) + L"/" +
                          source + L"/" + std::to_wstring(id) + L"?limit=1";
    const auto response = get(endpoint, true, 512 * 1024);
    auto url = firstUrl(response);
    if (url.empty()) url = firstUrl(response, "thumb");
    if (url.empty()) return {};
    return get(url, false, 10 * 1024 * 1024);
}

std::vector<uint8_t> firstStoreScreenshot(uint32_t app_id) {
    const auto id = std::to_wstring(app_id);
    const auto response = get(L"https://store.steampowered.com/api/appdetails?appids=" + id +
                              L"&filters=screenshots", false, 512 * 1024);
    const auto parsed = json::parse(std::string_view(reinterpret_cast<const char*>(response.data()), response.size()));
    if (!parsed) return {};
    const auto* entry = parsed->get(std::to_string(app_id));
    const auto* success = entry ? entry->get("success") : nullptr;
    if (!success || success->type != json::Value::Type::boolean || !success->flag) return {};
    const auto* data = entry->get("data");
    const auto* screenshots = data ? data->get("screenshots") : nullptr;
    if (!screenshots || screenshots->type != json::Value::Type::array || screenshots->elements.empty()) return {};
    const auto& first = screenshots->elements.front();
    for (const auto* field : {"path_full", "path_thumbnail"}) {
        const auto* value = first.get(field);
        if (!value || !value->text.starts_with("https://")) continue;
        const std::wstring url(value->text.begin(), value->text.end());
        auto image = get(url, false, 10 * 1024 * 1024);
        if (isImage(image)) return image;
    }
    return {};
}

std::vector<uint8_t> localSteamCover(uint32_t app_id, const std::filesystem::path& steam_root) {
    const auto folder = steam_root / L"appcache" / L"librarycache" / std::to_wstring(app_id);
    std::error_code ec;
    std::filesystem::recursive_directory_iterator iterator(folder, ec);
    if (ec) return {};
    std::vector<std::pair<int, std::filesystem::path>> candidates;
    for (const auto& entry : iterator) {
        if (iterator.depth() > 2) { iterator.disable_recursion_pending(); continue; }
        if (!entry.is_regular_file(ec) || ec) continue;
        const auto filename = entry.path().filename().wstring();
        const auto extension = entry.path().extension().wstring();
        if (extension != L".jpg" && extension != L".jpeg" && extension != L".png") continue;
        int priority = 6;
        if (filename == L"library_600x900.jpg") priority = 0;
        else if (filename == L"library_capsule.jpg") priority = 1;
        else if (filename == L"library_header.jpg") priority = 2;
        else if (filename == L"library_hero.jpg") priority = 3;
        else if (filename == L"icon.jpg") priority = 8;
        candidates.emplace_back(priority, entry.path());
    }
    std::sort(candidates.begin(), candidates.end());
    for (const auto& [priority, path] : candidates) {
        (void)priority;
        if (auto image = readImage(path, 8 * 1024 * 1024); !image.empty()) return image;
    }
    return {};
}
} // namespace

bool consumeArtworkRetryableError() {
    const bool value = retryable_artwork_error;
    retryable_artwork_error = false;
    return value;
}
uint32_t artworkRateLimitSeconds() {
    const auto deadline = sgdb_resume_at.load(), now = GetTickCount64();
    return std::max(steamGridDbWaitSeconds(), deadline > now ? static_cast<uint32_t>((deadline - now + 999) / 1000) : 0u);
}

std::filesystem::path gameArtworkDirectory(const Game& game, const std::filesystem::path& cache) {
    if (steamArtwork(game)) return cache;
    return cache.parent_path() / L"stores" / sourceSlug(game.source) / gameCacheKey(game);
}

std::filesystem::path gameCoverPath(const Game& game, const std::filesystem::path& cache) {
    return gameArtworkPath(game, ArtworkKind::cover, cache);
}
std::filesystem::path gameArtworkPath(const Game& game, ArtworkKind kind, const std::filesystem::path& cache) {
    const auto chosen = selectedArtworkId(game, kind, cache);
    if (chosen) return artworkVariantPath(game, kind, chosen, cache);
    if (steamArtwork(game)) {
        const auto folder = kind == ArtworkKind::cover ? cache : cache.parent_path() /
            (kind == ArtworkKind::logo ? L"logos" : kind == ArtworkKind::poster ? L"posters" :
             kind == ArtworkKind::landscape ? L"landscapes" : kind == ArtworkKind::disc ? L"discs" : L"heroes");
        return folder / (std::to_wstring(game.app_id) + (kind == ArtworkKind::hero ? L".v2.hd.img" : L".v2.img"));
    }
    return gameArtworkDirectory(game, cache) / (std::wstring(artworkKindSlug(kind)) + L".v2.img");
}
std::filesystem::path cachedArtworkFile(const Game& game, ArtworkKind kind, const std::filesystem::path& cache, bool rom_first) {
    const auto valid = [](const std::filesystem::path& file) {
        std::ifstream input(file, std::ios::binary);
        std::vector<uint8_t> header(128);
        return input.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size())) && isImage(header);
    };
    if (const auto chosen = selectedArtworkId(game, kind, cache)) {
        if (auto file = artworkVariantPath(game, kind, chosen, cache); valid(file)) return file;
    }
    // The ES-DE/ScreenScraper copies esde_art.cpp keeps (emulation/covers|backgrounds|logos).
    const auto emu = game.kind == GameKind::nonsteam && (kind == ArtworkKind::cover || kind == ArtworkKind::hero || kind == ArtworkKind::logo) ?
        cache.parent_path() / L"emulation" / (kind == ArtworkKind::cover ? L"covers" : kind == ArtworkKind::hero ? L"backgrounds" : L"logos") /
            (std::to_wstring(game.app_id) + L".img") :
        std::filesystem::path{};
    const bool emu_first = !emu.empty() && (kind == ArtworkKind::hero || rom_first);
    if (emu_first && valid(emu)) return emu;
    if (auto file = gameArtworkPath(game, kind, cache); valid(file)) return file;
    if (!emu.empty() && !emu_first && valid(emu)) return emu;
    return {};
}
bool cachedArtworkAvailable(const Game& game, ArtworkKind kind, const std::filesystem::path& cache, bool rom_first) {
    return !cachedArtworkFile(game, kind, cache, rom_first).empty();
}
std::vector<uint8_t> cachedArtwork(const Game& game, ArtworkKind kind, const std::filesystem::path& cache, bool rom_first) {
    const auto file = cachedArtworkFile(game, kind, cache, rom_first);
    return file.empty() ? std::vector<uint8_t>{} : readImage(file);
}

std::vector<uint8_t> artworkForGame(const Game& game, ArtworkKind kind,
                                  const std::filesystem::path& cache,
                                  const std::filesystem::path& steam_root) {
    if (!isGame(game) || gameKey(game).empty()) return {};
    if (auto chosen = selectedArtwork(game, kind, cache); !chosen.empty()) return chosen;
    if (kind == ArtworkKind::disc) return discArtwork(game, cache).bytes;
    if (steamArtwork(game)) {
        switch (kind) {
        case ArtworkKind::cover: return coverForSteamApp(game.app_id, game.name, cache, steam_root);
        case ArtworkKind::logo: return logoForSteamApp(game.app_id, game.name, cache);
        case ArtworkKind::hero: return heroForSteamApp(game.app_id, game.name, cache);
        case ArtworkKind::poster: return posterForSteamApp(game.app_id, game.name, cache, steam_root);
        case ArtworkKind::landscape: return landscapeForSteamApp(game.app_id, game.name, cache, steam_root);
        }
    }
    const auto directory = gameArtworkDirectory(game, cache);
    const auto file = directory / (std::wstring(artworkKindSlug(kind)) + L".v2.img");
    if (auto bytes = readImage(file, 10 * 1024 * 1024); !bytes.empty()) return bytes;
    // Store/shortcut IDs are never Steam AppIDs. Only a separately verified artwork
    // mapping may borrow Steam art; this never changes launch/install actions.
    const auto steamGridDb = [&]() -> std::vector<uint8_t> {
        auto source = selectedArtworkSource(game, cache);
        if (!source.id) source = {gameIdForTitle(0, artworkTitle(game), directory), false};
        if (!source.id) return {};
        const auto route = source.steam ? L"steam" : L"game";
        std::vector<uint8_t> found;
        if (kind == ArtworkKind::cover) {
            for (const auto* dimensions : {L"512x512", L"600x900"}) {
                found = grid(source.id, route, dimensions);
                if (isImage(found)) break;
            }
        } else if (kind == ArtworkKind::poster) found = grid(source.id, route, L"600x900");
        else if (kind == ArtworkKind::landscape) {
            for (const auto* dimensions : {L"920x430", L"460x215"}) {
                found = grid(source.id, route, dimensions);
                if (isImage(found)) break;
            }
        } else found = asset(source.id, route, kind == ArtworkKind::logo ? L"logos" : L"heroes");
        return found;
    };
    // An Epic account's games: Epic's own catalog art (its CDN, no key and no rate limit). The tall box
    // art is the poster, and the cover where SteamGridDB has none; the wide art and the logo stand in
    // for SteamGridDB's background, landscape tile and logo.
    const auto& epic_url = kind == ArtworkKind::poster || kind == ArtworkKind::cover ? game.epic_tall_art :
                           kind == ArtworkKind::logo ? game.epic_logo_art : game.epic_wide_art;
    const auto epic = [&]() -> std::vector<uint8_t> {
        if (game.source != GameSource::epic || !epic_url.starts_with("https://")) return {};
        auto bytes = get(text_encoding::wide(epic_url), false, 10 * 1024 * 1024);
        return isImage(bytes) ? bytes : std::vector<uint8_t>{};
    };
    std::vector<uint8_t> image;
    if (kind == ArtworkKind::poster) image = epic();
    if (!isImage(image)) image = steamGridDb();
    if (!isImage(image) && kind != ArtworkKind::poster) image = epic();
    if (!isImage(image)) return {};
    if (!atomicBytes(file, image.data(), image.size())) { retryable_artwork_error = true; return {}; }
    return image;
}

std::vector<uint8_t> storeScreenshotForSteamApp(uint32_t app_id) {
    return firstStoreScreenshot(app_id);
}

std::filesystem::path coverCacheDirectory() {
    return userConfigDirectory() / L"covers";
}

std::vector<uint8_t> coverForSteamApp(uint32_t app_id, const std::wstring& title,
                                      const std::filesystem::path& cache,
                                      const std::filesystem::path& steam_root) {
    const auto file = cache / (std::to_wstring(app_id) + L".v2.img");
    if (auto bytes = readImage(file); !bytes.empty()) return bytes;
    const auto id = std::to_wstring(app_id);
    std::vector<uint8_t> image;
    for (const auto* dimensions : {L"512x512", L"600x900"}) {
        image = grid(app_id, L"steam", dimensions);
        if (isImage(image)) break;
    }
    if (!isImage(image)) {
        // SteamGridDB game IDs are separate from Steam AppIDs; new entries may
        // have artwork but no Steam mapping yet. Match the actual manifest name.
        const auto game_id = gameIdForTitle(app_id, title, cache);
        if (game_id) for (const auto* dimensions : {L"512x512", L"600x900"}) {
            image = grid(game_id, L"game", dimensions);
            if (isImage(image)) break;
        }
    }
    if (!isImage(image)) image = localSteamCover(app_id, steam_root);
    if (!isImage(image))
        for (const auto* file_name : {L"library_600x900.jpg", L"header.jpg", L"capsule_616x353.jpg"}) {
            image = get(L"https://shared.fastly.steamstatic.com/store_item_assets/steam/apps/" +
                        id + L"/" + file_name, false, 4 * 1024 * 1024);
            if (isImage(image)) break;
        }
    if (!isImage(image))
        image = get(L"https://cdn.cloudflare.steamstatic.com/steam/apps/" + id + L"/header.jpg",
                    false, 4 * 1024 * 1024);
    if (!isImage(image)) image = heroForSteamApp(app_id, title, cache);
    if (!isImage(image)) return {};
    if (!atomicBytes(file, image.data(), image.size())) { retryable_artwork_error = true; return {}; }
    return image;
}

std::vector<uint8_t> logoForSteamApp(uint32_t app_id, const std::wstring& title,
                                     const std::filesystem::path& cache) {
    const auto logos = cache.parent_path() / L"logos";
    const auto file = logos / (std::to_wstring(app_id) + L".v2.img");
    if (auto bytes = readImage(file); !bytes.empty()) return bytes;
    auto image = asset(app_id, L"steam", L"logos");
    if (!isImage(image)) {
        const auto game_id = gameIdForTitle(app_id, title, cache);
        if (game_id) image = asset(game_id, L"game", L"logos");
    }
    if (!isImage(image)) return {};
    if (!atomicBytes(file, image.data(), image.size())) { retryable_artwork_error = true; return {}; }
    return image;
}

std::vector<uint8_t> heroForSteamApp(uint32_t app_id, const std::wstring& title,
                                     const std::filesystem::path& cache) {
    const auto heroes = cache.parent_path() / L"heroes";
    const auto file = heroes / (std::to_wstring(app_id) + L".v2.hd.img");
    if (auto bytes = readImage(file); !bytes.empty()) return bytes;
    // Full-resolution source, downsampled to 1920px by WIC on the UI side.
    auto image = asset(app_id, L"steam", L"heroes");
    if (!isImage(image)) {
        const auto game_id = gameIdForTitle(app_id, title, cache);
        if (game_id) image = asset(game_id, L"game", L"heroes");
    }
    if (!isImage(image)) image = storeScreenshotForSteamApp(app_id);
    if (!isImage(image)) return {};
    if (!atomicBytes(file, image.data(), image.size())) { retryable_artwork_error = true; return {}; }
    return image;
}

std::vector<uint8_t> posterForSteamApp(uint32_t app_id, const std::wstring& title,
                                       const std::filesystem::path& cache,
                                       const std::filesystem::path& steam_root) {
    const auto file = cache.parent_path() / L"posters" / (std::to_wstring(app_id) + L".v2.img");
    if (auto bytes = readImage(file); !bytes.empty()) return bytes;
    auto image = grid(app_id, L"steam", L"600x900");
    if (!isImage(image)) {
        const auto game_id = gameIdForTitle(app_id, title, cache);
        if (game_id) image = grid(game_id, L"game", L"600x900");
    }
    if (!isImage(image) && !steam_root.empty()) {
        // Steam keeps the official vertical capsule of most owned games locally.
        std::error_code ec;
        std::filesystem::recursive_directory_iterator folder(steam_root / L"appcache" / L"librarycache" / std::to_wstring(app_id), ec);
        for (; !ec && folder != std::filesystem::recursive_directory_iterator{}; folder.increment(ec)) {
            if (folder.depth() > 2) { folder.disable_recursion_pending(); continue; }
            if (folder->path().filename() != L"library_600x900.jpg") continue;
            image = readImage(folder->path(), 8 * 1024 * 1024);
            if (!image.empty()) break;
        }
    }
    if (!isImage(image))
        image = get(L"https://shared.fastly.steamstatic.com/store_item_assets/steam/apps/" + std::to_wstring(app_id) +
                    L"/library_600x900.jpg", false, 4 * 1024 * 1024);
    if (!isImage(image)) return {};
    if (!atomicBytes(file, image.data(), image.size())) { retryable_artwork_error = true; return {}; }
    return image;
}

std::vector<uint8_t> landscapeForSteamApp(uint32_t app_id, const std::wstring& title,
                                          const std::filesystem::path& cache,
                                          const std::filesystem::path& steam_root) {
    const auto file = cache.parent_path() / L"landscapes" / (std::to_wstring(app_id) + L".v2.img");
    if (auto bytes = readImage(file); !bytes.empty()) return bytes;
    std::vector<uint8_t> image;
    for (const auto* dimensions : {L"920x430", L"460x215"}) {
        image = grid(app_id, L"steam", dimensions);
        if (isImage(image)) break;
    }
    if (!isImage(image)) {
        const auto game_id = gameIdForTitle(app_id, title, cache);
        if (game_id) for (const auto* dimensions : {L"920x430", L"460x215"}) {
            image = grid(game_id, L"game", dimensions);
            if (isImage(image)) break;
        }
    }
    if (!isImage(image) && !steam_root.empty()) {
        // Steam keeps the store header capsule of owned games in its library cache.
        std::error_code ec;
        std::filesystem::recursive_directory_iterator folder(steam_root / L"appcache" / L"librarycache" / std::to_wstring(app_id), ec);
        for (; !ec && folder != std::filesystem::recursive_directory_iterator{}; folder.increment(ec)) {
            if (folder.depth() > 2) { folder.disable_recursion_pending(); continue; }
            const auto name = folder->path().filename().wstring();
            if (name != L"header.jpg" && name != L"library_header.jpg") continue;
            image = readImage(folder->path(), 8 * 1024 * 1024);
            if (!image.empty()) break;
        }
    }
    if (!isImage(image))
        for (const auto* file_name : {L"header.jpg", L"capsule_616x353.jpg"}) {
            image = get(L"https://shared.fastly.steamstatic.com/store_item_assets/steam/apps/" + std::to_wstring(app_id) +
                        L"/" + file_name, false, 4 * 1024 * 1024);
            if (isImage(image)) break;
        }
    if (!isImage(image)) return {};
    if (!atomicBytes(file, image.data(), image.size())) { retryable_artwork_error = true; return {}; }
    return image;
}

const wchar_t* artworkKindSlug(ArtworkKind kind) {
    return kind == ArtworkKind::cover ? L"cover" : kind == ArtworkKind::hero ? L"hero" :
           kind == ArtworkKind::poster ? L"poster" : kind == ArtworkKind::landscape ? L"landscape" : kind == ArtworkKind::disc ? L"disc" : L"logo";
}

namespace {
std::filesystem::path selectionFolder(const Game& game, const std::filesystem::path& cache) {
    return cache.parent_path() / L"artwork" / L"selections" / gameCacheKey(game);
}
bool atomicBytes(const std::filesystem::path& file, const uint8_t* bytes, size_t size) {
    // Artwork is kept at the size the launcher draws (image_compact.hpp); other files as they are.
    return writeCachedImage(file, std::span(bytes, size));
}
}

std::filesystem::path artworkVariantPath(const Game& game, ArtworkKind kind, uint32_t asset_id,
                                         const std::filesystem::path& cache) {
    return selectionFolder(game, cache) / artworkKindSlug(kind) / (std::to_wstring(asset_id) + L".img");
}

uint32_t selectedArtworkId(const Game& game, ArtworkKind kind, const std::filesystem::path& cache) {
    if (!isGame(game) || gameKey(game).empty()) return 0;
    std::ifstream input(selectionFolder(game, cache) / (std::wstring(artworkKindSlug(kind)) + L".selected"));
    unsigned version = 0;
    uint32_t id = 0;
    if (!(input >> version >> id) || version != 1 || !id) return 0;
    std::error_code ec;
    const auto size = std::filesystem::file_size(artworkVariantPath(game, kind, id, cache), ec);
    return !ec && size >= 128 && size <= 20 * 1024 * 1024 ? id : 0;
}

GridSource selectedArtworkSource(const Game& game, const std::filesystem::path& cache) {
    if (!isGame(game) || gameKey(game).empty()) return {};
    std::ifstream input(selectionFolder(game, cache) / L"source.selected");
    unsigned version = 0, steam = 0; uint32_t id = 0;
    if (!(input >> version >> steam >> id) || version != 1 || steam > 1 || !id) return {};
    return {id, steam == 1};
}
bool selectArtworkSource(const Game& game, GridSource source, const std::filesystem::path& cache) {
    return isGame(game) && !gameKey(game).empty() && source.id &&
        atomic_file::write(selectionFolder(game, cache) / L"source.selected",
                           "1 " + std::to_string(source.steam ? 1 : 0) + ' ' + std::to_string(source.id) + '\n');
}

std::vector<uint8_t> selectedArtwork(const Game& game, ArtworkKind kind, const std::filesystem::path& cache) {
    const auto id = selectedArtworkId(game, kind, cache);
    return id ? readImage(artworkVariantPath(game, kind, id, cache)) : std::vector<uint8_t>{};
}

bool cacheArtworkVariant(const Game& game, ArtworkKind kind, uint32_t asset_id,
                         const std::vector<uint8_t>& bytes, const std::filesystem::path& cache) {
    return isGame(game) && !gameKey(game).empty() && asset_id && bytes.size() <= 20 * 1024 * 1024 && isImage(bytes) &&
           atomicBytes(artworkVariantPath(game, kind, asset_id, cache), bytes.data(), bytes.size());
}

bool removeGameArtwork(const Game& game, const std::filesystem::path& cache, bool selections) {
    // Steam artwork shares one cache folder; only per-game folders can be removed.
    if (!isGame(game) || gameKey(game).empty() || steamArtwork(game)) return false;
    std::error_code error;
    std::filesystem::remove_all(gameArtworkDirectory(game, cache), error);
    bool removed = !error;
    if (selections) {
        std::filesystem::remove_all(selectionFolder(game, cache), error);
        removed = removed && !error;
    }
    return removed;
}

bool selectArtworkVariant(const Game& game, ArtworkKind kind, uint32_t asset_id,
                          const std::filesystem::path& cache) {
    if (!isGame(game) || gameKey(game).empty() || !asset_id) return false;
    std::ifstream image(artworkVariantPath(game, kind, asset_id, cache), std::ios::binary);
    uint8_t signature[128]{};
    if (!image.read(reinterpret_cast<char*>(signature), sizeof(signature)) ||
        !isImage(std::vector<uint8_t>(std::begin(signature), std::end(signature)))) return false;
    const auto value = "1 " + std::to_string(asset_id) + '\n';
    return atomicBytes(selectionFolder(game, cache) / (std::wstring(artworkKindSlug(kind)) + L".selected"),
                       reinterpret_cast<const uint8_t*>(value.data()), value.size());
}
