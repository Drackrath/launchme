#include "game_ratings.hpp"
#include "../net/json.hpp"
#include "../net/url_encoding.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/text_encoding.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <thread>
#include <unordered_set>

namespace ratings {
namespace {
constexpr uint64_t missing_lifetime = 7 * 24 * 60 * 60;
uint64_t nowSeconds() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}
std::optional<uint64_t> number(const json::Value* value, uint64_t maximum = UINT32_MAX) {
    if (!value || value->type != json::Value::Type::number || !std::isfinite(value->numeric) ||
        value->numeric < 0 || value->numeric > static_cast<double>(maximum) || std::floor(value->numeric) != value->numeric) return {};
    return static_cast<uint64_t>(value->numeric);
}
std::optional<double> score(const json::Value* value, double maximum) {
    if (!value || value->type != json::Value::Type::number || !std::isfinite(value->numeric) || value->numeric < 0 || value->numeric > maximum) return {};
    return value->numeric;
}
bool safeSlug(std::string_view value) {
    return !value.empty() && value.size() <= 200 && std::all_of(value.begin(), value.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
    });
}
std::string readSmall(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() <= 0 || input.tellg() > 16384) return {};
    std::string bytes(static_cast<size_t>(input.tellg()), '\0'); input.seekg(0);
    if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) return {};
    return bytes;
}
Source source(const Game& game) { return steamArtwork(game) ? Source::steam : Source::rawg; }
std::string identity(const Game& game) {
    // A renamed user shortcut must not inherit the previous title's rating.
    return source(game) == Source::steam ? gameKey(game) :
        gameKey(game) + ':' + text_encoding::utf8(normalizedTitle(game.name)) + ':' + std::to_string(rawgPlatform(game));
}
std::filesystem::path cacheFile(const std::filesystem::path& cache, const Game& game) {
    return cache / (gameCacheKey(game) + ".rating.json");
}
struct Cached { bool fresh = false; std::optional<Rating> rating; };
Cached load(const std::filesystem::path& cache, const Game& game, uint64_t now) {
    const auto root = json::parse(readSmall(cacheFile(cache, game)));
    if (!root || number(root->get("version")) != 1) return {};
    const auto* id = root->get("identity");
    const auto* manual = root->get("manual");
    const bool identified = manual && manual->type == json::Value::Type::boolean && manual->flag;
    if (!id || id->stringOr() != key_codec::encode(identified ? gameKey(game) : identity(game))) return {};
    const auto checked = number(root->get("checked"), 4102444800ULL);
    if (!checked || *checked > now + 86400) return {};
    const auto* value = root->get("rating");
    if (!value) return {};
    if (value->type == json::Value::Type::null_value) return {identified || now < *checked + missing_lifetime, {}};
    if (value->type != json::Value::Type::object) return {};
    const auto points = score(value->get("score"), source(game) == Source::steam ? 100 : 5);
    const auto count = number(value->get("count")), rawg_id = number(value->get("rawg_id"));
    if (!points || !count || !rawg_id) return {};
    Rating rating; rating.source = source(game); rating.score = *points; rating.count = static_cast<uint32_t>(*count);
    if (const auto critic = number(value->get("metacritic"), 100); critic && *critic) rating.metacritic = static_cast<int>(*critic);
    rating.rawg_id = static_cast<uint32_t>(*rawg_id);
    if (const auto* slug = value->get("slug")) rating.slug = slug->stringOr();
    if (!rating.available() || (rating.source == Source::rawg && (!rating.rawg_id || !safeSlug(rating.slug)))) return {};
    return {true, std::move(rating)};
}
bool save(const std::filesystem::path& cache, const Game& game, const Result& result, uint64_t now, bool manual = false) {
    std::ostringstream text; text.imbue(std::locale::classic());
    text << "{\"version\":1,\"manual\":" << (manual ? "true" : "false") << ",\"identity\":\"" << key_codec::encode(manual ? gameKey(game) : identity(game))
         << "\",\"checked\":" << now << ",\"rating\":";
    if (!result.rating) text << "null";
    else {
        const auto& rating = *result.rating;
        text << "{\"score\":" << std::setprecision(12) << rating.score << ",\"count\":" << rating.count
             << ",\"metacritic\":" << rating.metacritic.value_or(0) << ",\"rawg_id\":" << rating.rawg_id << ",\"slug\":\"" << rating.slug << "\"}";
    }
    text << '}'; return atomic_file::write(cacheFile(cache, game), text.str());
}
std::filesystem::path cooldownFile(const std::filesystem::path& cache, size_t provider) {
    return cache / (provider ? L"rawg.cooldown" : L"steam.cooldown");
}
bool coolingDown(const std::filesystem::path& cache, size_t provider, uint64_t now) {
    const auto bytes = readSmall(cooldownFile(cache, provider));
    uint64_t until = 0;
    const auto parsed = std::from_chars(bytes.data(), bytes.data() + bytes.size(), until);
    return parsed.ec == std::errc{} && parsed.ptr == bytes.data() + bytes.size() && until > now && until <= now + 86400;
}
std::wstring requestUrl(const Game& game, const std::wstring& key) {
    if (source(game) == Source::steam) return L"https://store.steampowered.com/appreviews/" + std::to_wstring(game.app_id) +
        L"?json=1&language=all&purchase_type=steam&review_type=all&num_per_page=0";
    auto result = L"https://api.rawg.io/api/games?key=" + url_encoding::component(key) + L"&search=" + url_encoding::component(game.name) +
        L"&search_precise=true&search_exact=true&page_size=10";
    if (const int platform = rawgPlatform(game)) result += L"&platforms=" + std::to_wstring(platform);
    return result;
}
}

std::wstring normalizedTitle(std::wstring_view name) {
    std::wstring lower(name);
    if (!name.empty()) LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, name.data(), static_cast<int>(name.size()),
                                   lower.data(), static_cast<int>(lower.size()), nullptr, nullptr, 0);
    std::wstring result;
    for (const wchar_t c : lower) {
        WORD type = 0; GetStringTypeW(CT_CTYPE1, &c, 1, &type);
        if (type & (C1_ALPHA | C1_DIGIT)) result += c;
    }
    return result;
}
bool saveIdentified(const std::filesystem::path& cache, const Game& game, const Rating& rating) {
    if (steamArtwork(game) || gameKey(game).empty() || rating.source != Source::rawg || !rating.rawg_id || !safeSlug(rating.slug)) return false;
    return save(cache, game, {true, rating.available() ? std::optional<Rating>{rating} : std::nullopt}, nowSeconds(), true);
}
int rawgPlatform(const Game& game) {
    if (game.kind != GameKind::nonsteam || game.platform.empty()) return 4; // PC stores and user-added PC games.
    static const std::pair<std::wstring_view, int> platforms[] = {
        {L"PS5",187}, {L"PS4",18}, {L"PS3",16}, {L"PS2",15}, {L"PS1",27}, {L"PSX",27}, {L"PSP",17}, {L"PS Vita",19},
        {L"Xbox",6}, {L"Xbox 360",14}, {L"Xbox One",1}, {L"Nintendo Switch",7}, {L"Switch",7}, {L"Wii",11}, {L"Wii U",10},
        {L"GameCube",105}, {L"Nintendo 64",83}, {L"N64",83}, {L"Nintendo 3DS",8}, {L"3DS",8}, {L"Nintendo DS",9}, {L"NDS",9},
        {L"Game Boy Advance",24}, {L"GBA",24}, {L"Game Boy",26}, {L"Game Boy Color",43}, {L"NES",49}, {L"SNES",79},
        {L"Dreamcast",106}, {L"Saturn",107}, {L"Mega Drive",167}, {L"Genesis",167}, {L"Amiga",166}
    };
    const auto name = normalizedTitle(game.platform);
    for (const auto& [label, id] : platforms) if (name == normalizedTitle(label)) return id;
    return 0;
}
Result parseSteam(std::string_view body) {
    const auto root = json::parse(body);
    if (!root || number(root->get("success")) != 1) return {};
    const auto* summary = root->get("query_summary");
    if (!summary) return {};
    const auto positive = number(summary->get("total_positive")), negative = number(summary->get("total_negative")), total = number(summary->get("total_reviews"));
    if (!positive || !negative || !total || *positive + *negative != *total) return {};
    if (!*total) return {true, {}};
    Rating rating; rating.score = 100.0 * static_cast<double>(*positive) / static_cast<double>(*total); rating.count = static_cast<uint32_t>(*total);
    return {true, std::move(rating)};
}
Result parseRawg(const Game& game, std::string_view body) {
    const auto root = json::parse(body);
    const auto* results = root ? root->get("results") : nullptr;
    if (!results || results->type != json::Value::Type::array) return {};
    const auto title = normalizedTitle(game.name);
    if (title.empty()) return {true, {}};
    const json::Value* match = nullptr;
    for (const auto& item : results->elements) {
        const auto* name = item.get("name");
        if (!name || normalizedTitle(text_encoding::wide(name->stringOr())) != title) continue;
        if (const int platform = rawgPlatform(game)) {
            const auto* list = item.get("platforms");
            if (!list || list->type != json::Value::Type::array || !std::any_of(list->elements.begin(), list->elements.end(), [&](const auto& entry) {
                const auto* p = entry.get("platform"); return p && number(p->get("id")) == static_cast<uint64_t>(platform);
            })) continue;
        }
        if (match) return {true, {}}; // Ambiguous title: never pick the first result blindly.
        match = &item;
    }
    if (!match) return {true, {}};
    const auto id = number(match->get("id")), count = number(match->get("ratings_count"));
    const auto points = score(match->get("rating"), 5);
    const auto* slug = match->get("slug");
    if (!id || !*id || !slug || !safeSlug(slug->stringOr())) return {};
    Rating rating; rating.source = Source::rawg; rating.rawg_id = static_cast<uint32_t>(*id); rating.slug = slug->stringOr();
    if (count && *count) {
        if (!points) return {};
        rating.score = *points; rating.count = static_cast<uint32_t>(*count);
    }
    if (const auto critic = number(match->get("metacritic"), 100); critic && *critic) rating.metacritic = static_cast<int>(*critic);
    return rating.available() ? Result{true, std::move(rating)} : Result{true, {}};
}

std::wstring compactLabel(const Rating& rating) {
    if (!rating.available()) return {};
    if (!rating.count) return rating.metacritic ? std::to_wstring(*rating.metacritic) + L"/100" : L"";
    if (rating.source == Source::steam) return std::to_wstring(std::lround(rating.score)) + L"%";
    std::wostringstream text; text.imbue(std::locale::classic());
    text << std::fixed << std::setprecision(1) << rating.score << L"/5";
    return text.str();
}
std::wstring label(const Rating& rating) {
    if (!rating.available()) return {};
    std::wostringstream text; text.imbue(std::locale::classic());
    text << (rating.source == Source::steam ? L"Steam" : L"RAWG");
    if (rating.count) {
        if (rating.source == Source::steam) text << L"  " << std::lround(rating.score) << L"%";
        else text << L"  " << std::fixed << std::setprecision(1) << rating.score << L"/5";
        text << L"  (" << rating.count << L")";
    }
    if (rating.metacritic) text << L"  ·  Metacritic " << *rating.metacritic << L"/100";
    return text.str();
}
std::wstring url(const Rating& rating, uint32_t steam_id) {
    if (rating.source == Source::steam) return steam_id ? L"https://store.steampowered.com/app/" + std::to_wstring(steam_id) + L"/#app_reviews_hash" : L"";
    return safeSlug(rating.slug) ? L"https://rawg.io/games/" + text_encoding::wide(rating.slug) : L"";
}

void collect(std::vector<Game> games, const std::filesystem::path& cache, const std::wstring& rawg_key,
             const std::atomic_bool& alive, const Publish& publish, const Backend& backend, Limits limits) {
    const auto now = backend.now ? backend.now : std::function<uint64_t()>{nowSeconds};
    const auto wait = [&](std::chrono::milliseconds duration) {
        if (backend.wait) return backend.wait(duration) && alive.load();
        const auto until = std::chrono::steady_clock::now() + duration;
        while (alive.load() && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        return alive.load();
    };
    // Installed/recent games first. The next startup advances past cached results.
    std::stable_sort(games.begin(), games.end(), [](const Game& a, const Game& b) {
        return a.installed != b.installed ? a.installed : a.last_played > b.last_played;
    });
    std::unordered_set<std::string> seen;
    std::vector<Game> missing;
    Map ready;
    const auto flush = [&] { if (!ready.empty() && alive.load()) { publish(std::move(ready)); ready.clear(); } };
    for (const auto& game : games) {
        if (!alive.load()) return;
        const auto key = gameKey(game);
        if (!isGame(game) || game.visibility != GameVisibility::public_game || key.empty() || !seen.insert(key).second) continue;
        if (source(game) == Source::rawg && (game.name.empty() || placeholderGameName(game.name))) continue;
        const auto cached = load(cache, game, now());
        if (cached.rating) ready.emplace(key, *cached.rating);
        if (ready.size() >= 128) flush();
        if (!cached.fresh) missing.push_back(game);
    }
    flush(); // Cached scores reach UI before the first HTTP request.
    std::array<size_t, 2> used{};
    std::array<bool, 2> blocked{coolingDown(cache, 0, now()), rawg_key.empty() || coolingDown(cache, 1, now())};
    bool requested = false;
    for (const auto& game : missing) {
        const size_t provider = source(game) == Source::rawg ? 1 : 0;
        if (!alive.load()) return;
        if (blocked[provider] || used[provider] >= limits.per_source) continue;
        if (requested && !wait(limits.spacing)) return;
        requested = true; ++used[provider];
        HttpResponse response;
        { HttpBudget budget(6000);
          const auto address = requestUrl(game, rawg_key);
          response = backend.get ? backend.get(address, provider != 0) : provider ?
              httpGetPrivate(address, L"Accept: application/json\r\n", 256 * 1024) : httpGet(address, L"Accept: application/json\r\n", 256 * 1024); }
        if (!alive.load()) return;
        const std::string_view bytes(reinterpret_cast<const char*>(response.body.data()), response.body.size());
        const auto result = response.status == 200 ? (provider ? parseRawg(game, bytes) : parseSteam(bytes)) : Result{};
        if (!result.valid) {
            // Don't turn 401/429/offline/malformed JSON into a permanent "no rating".
            blocked[provider] = true;
            const uint64_t delay = std::clamp<uint64_t>(response.retry_after_seconds, 300, 86400);
            atomic_file::write(cooldownFile(cache, provider), std::to_string(now() + delay));
            continue;
        }
        // An explicit identification can complete while this request is in flight.
        // Its saved result wins over the startup pass's older guess.
        const auto current = load(cache, game, now());
        if (current.fresh) { if (current.rating) ready.emplace(gameKey(game), *current.rating); }
        else {
            save(cache, game, result, now());
            if (result.rating) ready.emplace(gameKey(game), *result.rating);
        }
        if (ready.size() >= 5) flush();
    }
    flush();
}
}
