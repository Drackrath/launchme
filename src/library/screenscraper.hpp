#pragma once

#include "screenscraper_config.hpp"
#include "../model/game.hpp"
#include "../model/game_details.hpp"
#include "../net/http.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// ScreenScraper API v2 client for emulated shortcuts, identifying a game by its ROM file the way
// ES-DE does: jeuInfos.php with the system, the ROM's file name and, for files up to
// hash_limit, its MD5 and size. Credentials come from screenscraper_config only; they are
// stripped from every cached URL and added back when a media file is downloaded.
namespace screenscraper {
using Get = std::function<HttpResponse(const std::wstring& url, const std::wstring& headers, size_t max)>;

struct Media {
    std::wstring type;   // box-2D, fanart, ss, wheel, wheel-hd, ...
    std::wstring region; // wor, eu, us, jp, ss, ...
    std::wstring format; // png, jpg
    std::wstring url;    // Without devid/devpassword/ssid/sspassword.
};
struct GameInfo {
    uint32_t id = 0;
    std::wstring name;
    GameDetails details;
    std::wstring players;
    int note = -1; // Out of 20; -1 = none.
    std::vector<Media> media;
    std::vector<std::wstring> names; // Regional aliases of this exact game ID.
};
struct Quota {
    uint32_t requests_today = 0, max_per_day = 0, ko_today = 0, max_ko_per_day = 0, max_per_minute = 0;
    bool exhausted() const {
        return (max_per_day && requests_today >= max_per_day) || (max_ko_per_day && ko_today >= max_ko_per_day);
    }
};
// found/missing are answers about this ROM; the others stop the pass without caching anything.
enum class Status { found, missing, invalid, rejected, quota, throttled, closed };
struct Lookup {
    Status status = Status::invalid;
    std::optional<GameInfo> game;     // The first (best) game.
    std::vector<GameInfo> games;      // jeuRecherche.php: every game found, best first.
    Quota quota;
    std::wstring error;
};

// Every ScreenScraper call of the process goes through here: one at a time (accounts get one
// thread unless they contribute), jeuInfos lookups at least 1.2 s apart.
HttpResponse request(const std::wstring& url, const std::wstring& headers, size_t max, const Get& get, bool lookup);
HttpResponse gated(const std::function<HttpResponse()>& call, bool lookup, bool account_refresh = false);
Quota observedQuota();
void observeQuota(const Quota& quota);
// metadata\screenscraper.cooldown: after a refusal, quota or throttling nothing is asked before
// this Unix ms, whichever part of the launcher would ask.
uint64_t pausedUntil(const std::filesystem::path& metadata);
bool paused(const std::filesystem::path& metadata);
void pause(const std::filesystem::path& metadata, uint64_t milliseconds);
uint64_t unixMs();

// ScreenScraper "systemeid" of an ES-DE system folder (ES-DE's own mapping); 0 = unknown.
uint32_t systemId(std::wstring_view esde_system);
// The platform's ES-DE systems, then the ROM's folder (esde::systems), mapped and de-duplicated.
std::vector<uint32_t> systemIds(const std::wstring& platform, const std::filesystem::path& rom);

struct RomFile {
    std::wstring name;     // romnom: file name with extension, or the game folder ("Game.ps3").
    uint64_t size = 0;
    std::wstring md5;      // Lower-case hex; empty above the hash limit, for folders, or on error.
};
inline constexpr uint64_t hash_limit = 256ull * 1024 * 1024;
std::optional<RomFile> describeRom(const std::filesystem::path& rom, uint64_t limit = hash_limit);
std::wstring md5File(const std::filesystem::path& file);

std::wstring gameInfoUrl(const Config& config, uint32_t system, const RomFile& rom);
std::wstring stripCredentials(std::wstring_view url);
// The cached media URL with the current credentials, or empty when it is no HTTPS URL of the
// configured API host or of *.screenscraper.fr (credentials never go anywhere else).
std::wstring mediaUrl(const Config& config, std::wstring_view url);

Lookup parse(uint32_t http_status, std::string_view body, bool xml, std::wstring_view language);
Lookup lookup(const Config& config, uint32_t system, const RomFile& rom, std::wstring_view language,
              const Get& get = httpGetPrivate);
// jeuRecherche.php: games named like query on the system (0 = any), up to 30, best first.
Lookup search(const Config& config, const std::wstring& query, uint32_t system, std::wstring_view language,
               const Get& get = httpGetPrivate);
Lookup lookupGame(const Config& config, uint32_t id, uint32_t system, std::wstring_view language,
                  const Get& get = httpGetPrivate);

// ssuserInfos.php: are the credentials accepted, and how much may this account ask today.
struct Account {
    Status status = Status::invalid; // found = accepted.
    std::wstring user;
    uint32_t level = 0, threads = 0;
    Quota quota;
    std::wstring error;
};
Account parseAccount(uint32_t http_status, std::string_view body);
Account account(const Config& config, const Get& get = httpGetPrivate);

enum class ArtSlot { cover, background, logo };
// Best media for a slot: cover box-2D; background fanart, then screenshot; logo wheel-hd/wheel
// (PNG only). Regions eu, wor, us, jp, ss, then any.
const Media* pickMedia(const GameInfo& game, ArtSlot slot);
// status (optional): the HTTP status, so callers can pause after 429/430/431.
std::vector<uint8_t> downloadMedia(const Config& config, const Media& media, const Get& get = httpGetPrivate, uint32_t* status = nullptr);
// After an HTTP status: pauses every caller for throttling (429, 2 min) or the daily quota (430/431, 12 h).
void pauseAfter(const std::filesystem::path& metadata, uint32_t status);

// metadata\<game-key>.screenscraper.json: the identification of one ROM (or its absence).
struct Cached {
    std::wstring fingerprint; // ROM path and size: another file is another lookup.
    uint64_t checked = 0;     // Unix ms.
    std::optional<GameInfo> game; // Empty: ScreenScraper does not know this ROM.
    bool manual = false;          // Picked in "Zidentyfikuj grę": wins over ES-DE's match too.
};
std::wstring fingerprint(const Game& game, const RomFile& rom);
std::optional<Cached> loadCached(const Game& game, const std::filesystem::path& metadata);
bool saveCached(const Game& game, const Cached& cached, const std::filesystem::path& metadata);
std::string serialize(const Cached& cached);
std::optional<Cached> deserialize(std::string_view text);
inline constexpr uint64_t missing_retry_ms = 7ull * 24 * 60 * 60 * 1000;
}
