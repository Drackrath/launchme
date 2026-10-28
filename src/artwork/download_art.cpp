#include "download_art.hpp"
#include "../platform/hashing.hpp"
#include "image_compact.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/text_encoding.hpp"
#include "../library/emulation_platforms.hpp"
#include "../library/rom_layout.hpp"
#ifdef _WIN32
#include <bcrypt.h>
#endif
#include <algorithm>
#include <cwctype>
#include <fstream>

namespace download_art {
namespace {
std::wstring lower(std::wstring s) { for (auto& c : s) c = static_cast<wchar_t>(towlower(c)); return s; }
std::wstring trimmed(std::wstring s) {
    const auto first = s.find_first_not_of(L" \t\r\n");
    return first == s.npos ? L"" : s.substr(first, s.find_last_not_of(L" \t\r\n") - first + 1);
}
std::string framed(std::string s) { return std::to_string(s.size()) + ':' + s; }
std::filesystem::path cacheFile(const Target& t, const std::filesystem::path& root) {
    const auto key = t.key();
    const auto name = hashing::bytesHex(hashing::Algorithm::sha256, std::span(reinterpret_cast<const uint8_t*>(key.data()), key.size()));
    if (name.empty()) return {};
    return root / L"download-thumbnails" / (name + ".img");
}
}
bool pcPlatform(std::wstring_view platform) {
    const auto p = lower(trimmed(std::wstring(platform)));
    return p == L"pc" || p == L"windows" || p == L"pc (windows)" || p == L"linux" || p == L"macos" || p == L"steam";
}
std::wstring normalizePlatform(std::wstring platform) {
    platform = trimmed(std::move(platform));
    const auto name = lower(platform);
    for (const auto& known : emulation_platforms) {
        if (lower(std::wstring(known.name)) == name || known.slug == name) return std::wstring(known.name);
        for (const auto alias : known.collections) if (!alias.empty() && alias == name) return std::wstring(known.name);
    }
    return platform;
}
std::wstring cleanName(std::wstring name) {
    name = trimmed(std::move(name));
    // A download named after its file: the extension (2-4 letters/digits) is no part of the title.
    const auto extension = std::filesystem::path(name).extension().wstring();
    if (extension.size() >= 3 && extension.size() <= 5 && std::all_of(extension.begin() + 1, extension.end(), [](wchar_t c) { return std::iswalnum(c) != 0; }) &&
        std::any_of(extension.begin() + 1, extension.end(), [](wchar_t c) { return std::iswalpha(c) != 0; }))
        name.resize(name.size() - extension.size());
    // Release/file annotations, not a game's subtitle. Keep ordinary bracketed words.
    for (;;) {
        name = trimmed(std::move(name));
        if (name.empty() || name.back() != L']') break;
        const auto open = name.rfind(L'['); if (open == name.npos) break;
        const auto tag = lower(name.substr(open + 1, name.size() - open - 2));
        const bool id = tag.size() == 16 && tag.find_first_not_of(L"0123456789abcdef") == tag.npos;
        const bool version = tag.size() > 1 && tag[0] == L'v' && tag.find_first_not_of(L"0123456789", 1) == tag.npos;
        if (!id && !version && tag != L"base" && tag != L"update" && tag != L"dlc") break;
        name.resize(open);
    }
    return trimmed(std::move(name));
}
std::string Target::key() const {
    return "download-art:" + std::string(pc ? "sgdb:" : "ss:") + std::to_string(steam_app_id) + ':' +
        framed(text_encoding::utf8(lower(normalizePlatform(platform)))) + framed(title_id) + framed(text_encoding::utf8(lower(cleanName(name))));
}
Target forGame(const Game& game) {
    Target t; t.name = game.name; t.platform = game.platform;
    const bool pc_store = game.kind != GameKind::nonsteam && game.source != GameSource::custom && game.source != GameSource::drmfree;
    t.pc = game.rom.empty() && (pc_store || game.platform.empty() || pcPlatform(game.platform));
    if (t.pc) { t.platform = L"PC"; if (steamArtwork(game)) t.steam_app_id = game.app_id; }
    return t;
}

std::vector<uint8_t> fetch(const Target& target, const std::filesystem::path& cache,
                          const std::filesystem::path& metadata, const std::wstring& sgdb_key,
                          const screenscraper::Config& ss, const std::wstring& language, const ArtworkHttp& get) {
    const auto file = cacheFile(target, cache);
    if (file.empty()) return {};
    {
        std::ifstream input(file, std::ios::binary | std::ios::ate);
        const auto size = input ? static_cast<int64_t>(input.tellg()) : -1;
        if (size > 0 && size <= 20 * 1024 * 1024) {
            std::vector<uint8_t> bytes(static_cast<size_t>(size)); input.seekg(0);
            if (input.read(reinterpret_cast<char*>(bytes.data()), size) && decodableArtwork(bytes)) return bytes;
        }
    }
    const auto name = cleanName(target.name);
    if (name.empty()) return {};
    std::vector<uint8_t> bytes;
    if (target.pc) {
        if (sgdb_key.empty()) return {};
        const auto cooldown = metadata / L"download-steamgriddb";
        if (screenscraper::paused(cooldown)) return {};
        const ArtworkHttp guarded = [&](const auto& url, const auto& headers, size_t max) {
            auto response = get(url, headers, max);
            if (url.starts_with(L"https://www.steamgriddb.com/api/")) {
                if (response.status == 401 || response.status == 403) screenscraper::pause(cooldown, 60 * 60 * 1000);
                else if (response.status == 429) screenscraper::pause(cooldown, 2 * 60 * 1000);
            }
            return response;
        };
        Game game; game.name = name; game.app_id = target.steam_app_id;
        if (!game.app_id) { game.source = GameSource::custom; game.source_id = target.key(); }
        auto page = browseSteamGridDB(game, ArtworkKind::cover, 0, {}, sgdb_key, guarded);
        std::stable_sort(page.assets.begin(), page.assets.end(), [](const auto& a, const auto& b) {
            return a.width == a.height && b.width != b.height;
        });
        for (size_t i = 0; i < page.assets.size() && i < 2; ++i) {
            bytes = downloadGridImage(page.assets[i], false, get);
            if (!bytes.empty()) break;
        }
    } else {
        if (!ss.ready() || target.platform.empty() || screenscraper::paused(metadata)) return {};
        auto systems = screenscraper::systemIds(normalizePlatform(target.platform), {});
        if (const auto exact = screenscraper::systemId(target.platform)) systems = {exact};
        if (const auto folder = rom_layout::system(text_encoding::utf8(target.platform)); !folder.empty())
            if (const auto exact = screenscraper::systemId(text_encoding::wide(folder))) systems = {exact};
        if (systems.size() != 1) return {}; // Never guess the console or search every platform.
        auto found = screenscraper::search(ss, name, systems[0], language, get);
        // Some catalogs remove apostrophes while jeuRecherche requires them (Kirbys/Yoshis).
        // Retry only a missing result, never a refused or throttled request.
        const auto first_space = name.find(L' ');
        if (found.status == screenscraper::Status::missing && first_space != name.npos && first_space >= 3 &&
            (name[first_space - 1] == L's' || name[first_space - 1] == L'S') && name.find_first_of(L"'’‘") == name.npos) {
            auto possessive = name; possessive.insert(first_space - 1, 1, L'\'');
            found = screenscraper::search(ss, possessive, systems[0], language, get);
        }
        if (found.status != screenscraper::Status::found) {
            if (found.status == screenscraper::Status::quota) screenscraper::pause(metadata, 12ull * 60 * 60 * 1000);
            else if (found.status == screenscraper::Status::throttled) screenscraper::pause(metadata, 2 * 60 * 1000);
            else if (found.status == screenscraper::Status::rejected) screenscraper::pause(metadata, 60 * 60 * 1000);
            return {};
        }
        std::vector<GridGame> candidates;
        const auto unquoted = [](std::wstring s) { std::erase_if(s, [](wchar_t c) { return c == L'\'' || c == L'’' || c == L'‘'; }); return s; };
        const auto wanted = unquoted(name);
        const auto add = [&](const screenscraper::GameInfo& game) {
            auto best = unquoted(game.name);
            for (const auto& alias : game.names) {
                const auto candidate = unquoted(alias);
                if (gridTitleScore(wanted, candidate) > gridTitleScore(wanted, best)) best = candidate;
            }
            candidates.push_back({{game.id, false}, best});
        };
        for (const auto& game : found.games) add(game);
        if (candidates.empty() && found.game) add(*found.game);
        const auto match = matchGridGame(wanted, candidates);
        if (!match.id) return {}; // A sequel or ambiguous name must not borrow a cover.
        const screenscraper::GameInfo* game = nullptr;
        for (const auto& candidate : found.games) if (candidate.id == match.id) game = &candidate;
        if (!game && found.game && found.game->id == match.id) game = &*found.game;
        if (!game) return {};
        if (const auto* media = screenscraper::pickMedia(*game, screenscraper::ArtSlot::cover)) {
            uint32_t status = 0;
            bytes = screenscraper::downloadMedia(ss, *media, get, &status);
            screenscraper::pauseAfter(metadata, status);
        }
    }
    if (!decodableArtwork(bytes)) return {};
    if (auto compact = compactImage(bytes, ImageRole::cover); !compact.empty()) bytes = std::move(compact);
    atomic_file::write(file, bytes);
    return bytes;
}
}
