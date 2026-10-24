#pragma once
#include "http.hpp"
#include <functional>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace glance {
inline constexpr int section_count = 5;
inline constexpr size_t item_limit = 12;
inline constexpr size_t upcoming_limit = 48;
inline constexpr size_t recent_release_limit = 12;
inline constexpr size_t calendar_limit = upcoming_limit + recent_release_limit;
inline constexpr size_t owned_limit = 12;
enum class Section { steam, upcoming, news, features, owned };
enum class ReleasePrecision { unknown, day, month, quarter, year };
struct ReleaseDate {
    ReleasePrecision precision = ReleasePrecision::unknown;
    int year = 0, month = 0, day = 0;
    std::wstring raw;
    bool available = false;
    bool recent = false;
    int group() const { return recent ? 3 : precision == ReleasePrecision::day ? 0 : precision == ReleasePrecision::unknown ? 2 : 1; }
};
ReleaseDate parseReleaseDate(std::wstring date);
struct OwnedGame {
    uint32_t app_id = 0;
    std::wstring name;
    uint64_t last_played = 0;
    bool installed = false;
    bool operator==(const OwnedGame&) const = default;
};
struct Item {
    std::wstring title, url, image, source, date;
    std::string id;
    uint64_t published = 0;
    uint32_t app_id = 0;
    std::vector<std::wstring> fallback_images;
    ReleaseDate release;
};
struct Feed {
    std::vector<Item> items;
    bool unavailable = false, stale = false;
    size_t games_total = 0, games_selected = 0, games_checked = 0;
    bool dates_partial = false;
};
using Get = std::function<HttpResponse(const std::wstring&, const std::wstring&, size_t)>;
bool articleUrl(std::wstring_view url);
bool imageUrl(std::wstring_view url);
std::optional<std::vector<Item>> parseSteam(std::string_view bytes, Section section);
std::optional<std::vector<Item>> parseRss(std::string_view bytes);
std::optional<std::vector<Item>> parseNews(std::string_view bytes, const OwnedGame& game);
std::wstring steamEventImage(std::string_view bytes, const Item& item, bool english);
std::vector<OwnedGame> selectOwned(std::vector<OwnedGame> games);
// Per-app public cache, aggregated only from the current library snapshot. No Steam user ID/key.
Feed loadOwned(std::vector<OwnedGame> games, bool english, const std::filesystem::path& cache, bool refresh, const Get& get = httpGet);
// Worker-only I/O. Valid responses are cached for 30 minutes; errors retain the last good feed.
Feed load(Section section, bool english, const std::filesystem::path& cache, bool refresh, const Get& get = httpGet);
Feed loadUpcoming(bool english, const std::filesystem::path& cache, bool refresh, const Get& get = httpGet);
struct UpcomingPage { std::vector<Item> items; int start = 0, total = 0; };
std::optional<UpcomingPage> parseUpcomingSearch(std::string_view bytes);
// Next 30 days (48 games) and last 7 calendar days including today (12 games).
// One entry per available day before filling a second; undated/approximate entries do not invent dates.
std::vector<Item> selectUpcoming(std::vector<Item> items, int today_yyyymmdd);
struct ImageSession { std::unordered_map<std::wstring,std::string> metadata; };
std::vector<uint8_t> thumbnail(const Item& item, const std::filesystem::path& slot, const Get& get = httpGet,
                               bool english = false, ImageSession* session = nullptr);
}
