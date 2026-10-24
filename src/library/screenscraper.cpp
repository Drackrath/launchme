#include "screenscraper.hpp"
#include "esde_gamelist.hpp"
#include "../net/json.hpp"
#include "../net/url_encoding.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/text_encoding.hpp"
#include "../platform/user_config.hpp"

#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <mutex>
#include <thread>
#include <charconv>
#include <cstdio>
#include <cwctype>
#include <fstream>
#include <utility>

namespace screenscraper {
namespace {
using text_encoding::wideLenient;

// ---- Minimal XML DOM: elements, attributes, text, CDATA, comments, entities. ----
struct Node {
    std::string name;
    std::vector<std::pair<std::string, std::string>> attributes;
    std::string text;
    std::vector<Node> children;
    const Node* child(std::string_view tag) const {
        for (const auto& node : children) if (node.name == tag) return &node;
        return nullptr;
    }
    std::string attribute(std::string_view key) const {
        for (const auto& [attribute_name, value] : attributes) if (attribute_name == key) return value;
        return {};
    }
};

void appendUtf8(std::string& out, uint32_t code) {
    if (!code || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) return;
    if (code < 0x80) out += static_cast<char>(code);
    else if (code < 0x800) { out += static_cast<char>(0xC0 | (code >> 6)); out += static_cast<char>(0x80 | (code & 0x3F)); }
    else if (code < 0x10000) {
        out += static_cast<char>(0xE0 | (code >> 12)); out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (code >> 18)); out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F)); out += static_cast<char>(0x80 | (code & 0x3F));
    }
}

// XML entities plus the HTML ones ScreenScraper leaves inside its texts (ES-DE fixes the same).
std::string decodeEntities(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (size_t at = 0; at < text.size();) {
        if (text[at] != '&') { out += text[at++]; continue; }
        const auto end = text.find(';', at);
        if (end == std::string_view::npos || end - at > 10) { out += text[at++]; continue; }
        const auto name = text.substr(at + 1, end - at - 1);
        if (name == "amp") out += '&';
        else if (name == "lt") out += '<';
        else if (name == "gt") out += '>';
        else if (name == "quot") out += '"';
        else if (name == "apos") out += '\'';
        else if (name == "nbsp") out += ' ';
        else if (name == "copy") appendUtf8(out, 0xA9);
        else if (name.size() > 1 && name[0] == '#') {
            const bool hex = name[1] == 'x' || name[1] == 'X';
            const auto digits = name.substr(hex ? 2 : 1);
            uint32_t code = 0;
            const auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), code, hex ? 16 : 10);
            if (ec != std::errc{} || ptr != digits.data() + digits.size()) { out += text[at++]; continue; }
            appendUtf8(out, code);
        } else { out += text[at++]; continue; }
        at = end + 1;
    }
    return out;
}

class XmlReader {
public:
    explicit XmlReader(std::string_view text) : text_(text) {}
    std::optional<Node> document() {
        skipMisc();
        Node root;
        if (!element(root, 0)) return {};
        return root;
    }
private:
    bool starts(std::string_view prefix) const { return text_.substr(at_, prefix.size()) == prefix; }
    void skipSpace() { while (at_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[at_]))) ++at_; }
    bool skipPast(std::string_view marker) {
        const auto end = text_.find(marker, at_);
        if (end == std::string_view::npos) { at_ = text_.size(); return false; }
        at_ = end + marker.size(); return true;
    }
    void skipMisc() {
        for (;;) {
            skipSpace();
            if (starts("\xEF\xBB\xBF")) at_ += 3;
            else if (starts("<?")) skipPast("?>");
            else if (starts("<!--")) skipPast("-->");
            else if (starts("<!")) skipPast(">");
            else return;
        }
    }
    std::string name() {
        const auto start = at_;
        while (at_ < text_.size() && !std::isspace(static_cast<unsigned char>(text_[at_])) &&
               text_[at_] != '>' && text_[at_] != '/' && text_[at_] != '=') ++at_;
        return std::string(text_.substr(start, at_ - start));
    }
    bool element(Node& node, int depth) {
        if (depth > 32 || at_ >= text_.size() || text_[at_] != '<') return false;
        ++at_;
        node.name = name();
        if (node.name.empty()) return false;
        for (;;) {
            skipSpace();
            if (at_ >= text_.size()) return false;
            if (starts("/>")) { at_ += 2; return true; }
            if (text_[at_] == '>') { ++at_; break; }
            auto key = name();
            skipSpace();
            if (key.empty() || at_ >= text_.size() || text_[at_] != '=') return false;
            ++at_; skipSpace();
            if (at_ >= text_.size() || (text_[at_] != '"' && text_[at_] != '\'')) return false;
            const char quote = text_[at_++];
            const auto end = text_.find(quote, at_);
            if (end == std::string_view::npos) return false;
            node.attributes.emplace_back(std::move(key), decodeEntities(text_.substr(at_, end - at_)));
            at_ = end + 1;
        }
        while (at_ < text_.size()) {
            if (starts("</")) {
                at_ += 2; name(); skipSpace();
                if (at_ >= text_.size() || text_[at_] != '>') return false;
                ++at_; return true;
            }
            if (starts("<!--")) { skipPast("-->"); continue; }
            if (starts("<![CDATA[")) {
                at_ += 9;
                const auto end = text_.find("]]>", at_);
                if (end == std::string_view::npos) return false;
                node.text.append(text_.substr(at_, end - at_)); at_ = end + 3; continue;
            }
            if (text_[at_] == '<') {
                if (node.children.size() >= 4096) return false;
                node.children.emplace_back();
                if (!element(node.children.back(), depth + 1)) return false;
                continue;
            }
            const auto end = std::min(text_.find('<', at_), text_.size());
            node.text += decodeEntities(text_.substr(at_, end - at_));
            at_ = end;
        }
        return false;
    }
    std::string_view text_;
    size_t at_ = 0;
};

std::wstring trimmed(std::wstring text, size_t limit) {
    const auto first = text.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return {};
    text = text.substr(first, text.find_last_not_of(L" \t\r\n") - first + 1);
    if (text.size() > limit) text.resize(limit);
    return text;
}
std::wstring wideText(std::string_view text, size_t limit = 200) { return trimmed(wideLenient(text), limit); }

uint32_t number(std::string_view text) {
    uint32_t value = 0;
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
}

// "1993-05-12" stays as is, "1993" too; anything else is dropped.
std::wstring releaseDate(std::wstring_view date) {
    const auto digits = [&](size_t from, size_t to) {
        return date.size() >= to && std::all_of(date.begin() + from, date.begin() + to, [](wchar_t ch) { return ch >= L'0' && ch <= L'9'; });
    };
    if (!digits(0, 4)) return {};
    if (date.size() >= 10 && date[4] == L'-' && date[7] == L'-' && digits(5, 7) && digits(8, 10)) return std::wstring(date.substr(0, 10));
    return std::wstring(date.substr(0, 4));
}

// Ordered preference lists for per-region / per-language values.
std::vector<std::string> regionOrder() { return {"eu", "wor", "us", "ss", "jp"}; }
std::vector<std::string> languageOrder(std::wstring_view language) {
    std::vector<std::string> order;
    if (!language.empty()) order.push_back(text_encoding::utf8(language));
    for (const char* fallback : {"en", "wor"}) if (std::find(order.begin(), order.end(), fallback) == order.end()) order.emplace_back(fallback);
    return order;
}

struct Localized { std::string key, text; };
std::string choose(const std::vector<Localized>& values, const std::vector<std::string>& order) {
    for (const auto& wanted : order)
        for (const auto& value : values) if (value.key == wanted && !value.text.empty()) return value.text;
    for (const auto& value : values) if (!value.text.empty()) return value.text;
    return {};
}

std::wstring joinGenres(const std::vector<Localized>& genres, const std::vector<std::string>& order) {
    std::string language;
    for (const auto& wanted : order) {
        if (std::any_of(genres.begin(), genres.end(), [&](const Localized& genre) { return genre.key == wanted; })) { language = wanted; break; }
    }
    std::wstring result;
    for (const auto& genre : genres) {
        if (genre.key != language || genre.text.empty()) continue;
        const auto name = wideText(genre.text, 60);
        if (result.find(name) != std::wstring::npos) continue;
        if (!result.empty()) result += L", ";
        result += name;
        if (result.size() > 160) break;
    }
    return result;
}

bool notGame(std::wstring_view name) {
    std::wstring upper(name.substr(0, 12));
    for (auto& ch : upper) ch = static_cast<wchar_t>(std::towupper(ch));
    return upper == L"ZZZ(NOTGAME)";
}

struct Raw {
    uint32_t id = 0;
    std::vector<Localized> names, synopsis, dates, genres;
    std::string developer, publisher, players, note;
    std::vector<Media> media;
};

std::optional<GameInfo> finish(Raw raw, std::wstring_view language) {
    GameInfo game;
    game.id = raw.id;
    game.name = wideText(choose(raw.names, regionOrder()));
    if (game.name.empty() || notGame(game.name)) return {};
    for (const auto& localized : raw.names) {
        auto name = wideText(localized.text);
        if (!name.empty() && game.names.size() < 32 && std::find(game.names.begin(), game.names.end(), name) == game.names.end()) game.names.push_back(std::move(name));
    }
    const auto languages = languageOrder(language);
    game.details.description = wideText(choose(raw.synopsis, languages), 1800);
    game.details.release_date = releaseDate(wideText(choose(raw.dates, regionOrder()), 32));
    game.details.developer = wideText(raw.developer);
    game.details.publisher = wideText(raw.publisher);
    game.details.genres = joinGenres(raw.genres, languages);
    game.players = wideText(raw.players, 16);
    if (!raw.note.empty()) {
        const auto value = static_cast<int>(number(raw.note));
        if (value > 0 && value <= 20) game.note = value;
    }
    game.details.players = game.players;
    if (game.note > 0) game.details.score = std::to_wstring(game.note) + L"/20";
    game.media = std::move(raw.media);
    return game;
}

Quota quotaFrom(const std::function<std::string(std::string_view)>& field) {
    Quota quota;
    quota.requests_today = number(field("requeststoday"));
    quota.max_per_day = number(field("maxrequestsperday"));
    quota.ko_today = number(field("requestskotoday"));
    quota.max_ko_per_day = number(field("maxrequestskoperday"));
    quota.max_per_minute = number(field("maxrequestspermin"));
    if (!quota.max_per_minute) quota.max_per_minute = number(field("maxrequestsperdmin")); // Spelling in the API docs.
    return quota;
}

Media mediaFrom(std::string type, std::string region, std::string format, std::string_view url) {
    return {wideText(type, 40), wideText(region, 16), wideText(format, 8), stripCredentials(wideText(url, 2048))};
}

std::optional<GameInfo> gameFromXml(const Node& game, std::wstring_view language) {
    Raw raw;
    raw.id = number(game.attribute("id"));
    const auto localized = [&](const char* group, const char* item, const char* key, std::vector<Localized>& out) {
        if (const auto* parent = game.child(group))
            for (const auto& node : parent->children) if (node.name == item) out.push_back({node.attribute(key), node.text});
    };
    localized("noms", "nom", "region", raw.names);
    localized("synopsis", "synopsis", "langue", raw.synopsis);
    localized("dates", "date", "region", raw.dates);
    localized("genres", "genre", "langue", raw.genres);
    if (raw.names.empty()) if (const auto* name = game.child("nom")) raw.names.push_back({"ss", name->text});
    for (const auto* tag : {"developpeur", "editeur", "joueurs", "note"}) {
        const auto* node = game.child(tag);
        const auto text = node ? node->text : std::string{};
        std::string_view name(tag);
        (name == "developpeur" ? raw.developer : name == "editeur" ? raw.publisher : name == "joueurs" ? raw.players : raw.note) = text;
    }
    if (const auto* medias = game.child("medias"))
        for (const auto& node : medias->children)
            if (node.name == "media") raw.media.push_back(mediaFrom(node.attribute("type"), node.attribute("region"), node.attribute("format"), node.text));
    return finish(std::move(raw), language);
}

// jeuInfos.php: Data/jeu; jeuRecherche.php: Data/jeux/jeu, best match first.
Lookup parseXml(std::string_view body, std::wstring_view language) {
    Lookup result;
    auto document = XmlReader(body).document();
    if (!document || document->name != "Data") { result.status = Status::invalid; return result; }
    const auto& data = *document;
    if (const auto* user = data.child("ssuser"))
        result.quota = quotaFrom([&](std::string_view tag) { const auto* node = user->child(tag); return node ? node->text : std::string{}; });
    const auto add = [&](const Node& parent) {
        for (const auto& node : parent.children)
            if (node.name == "jeu") if (auto game = gameFromXml(node, language)) result.games.push_back(std::move(*game));
    };
    add(data);
    if (const auto* list = data.child("jeux")) add(*list);
    if (!result.games.empty()) result.game = result.games.front();
    result.status = result.game ? Status::found : Status::missing;
    return result;
}
std::string jsonText(const json::Value* value) {
    if (!value) return {};
    if (value->type == json::Value::Type::string) return value->text;
    if (value->type == json::Value::Type::number) {
        const auto rounded = static_cast<long long>(value->numeric);
        return std::to_string(rounded);
    }
    if (value->type == json::Value::Type::object) return jsonText(value->get("text"));
    return {};
}

std::optional<GameInfo> gameFromJson(const json::Value& game, std::wstring_view language) {
    if (game.type != json::Value::Type::object) return {};
    Raw raw;
    raw.id = number(jsonText(game.get("id")));
    const auto localized = [](const json::Value* list, const char* key, std::vector<Localized>& out) {
        if (!list || list->type != json::Value::Type::array) return;
        for (const auto& item : list->elements) out.push_back({jsonText(item.get(key)), jsonText(item.get("text"))});
    };
    localized(game.get("noms"), "region", raw.names);
    localized(game.get("synopsis"), "langue", raw.synopsis);
    localized(game.get("dates"), "region", raw.dates);
    if (raw.names.empty()) raw.names.push_back({"ss", jsonText(game.get("nom"))});
    if (const auto* genres = game.get("genres"); genres && genres->type == json::Value::Type::array)
        for (const auto& genre : genres->elements) localized(genre.get("noms"), "langue", raw.genres);
    raw.developer = jsonText(game.get("developpeur"));
    raw.publisher = jsonText(game.get("editeur"));
    raw.players = jsonText(game.get("joueurs"));
    raw.note = jsonText(game.get("note"));
    if (const auto* medias = game.get("medias"); medias && medias->type == json::Value::Type::array)
        for (const auto& item : medias->elements)
            raw.media.push_back(mediaFrom(jsonText(item.get("type")), jsonText(item.get("region")), jsonText(item.get("format")), jsonText(item.get("url"))));
    return finish(std::move(raw), language);
}

Lookup parseJson(std::string_view body, std::wstring_view language) {
    Lookup result;
    const auto root = json::parse(body);
    const auto* response = root ? root->get("response") : nullptr;
    if (!response || response->type != json::Value::Type::object) { result.status = Status::invalid; return result; }
    if (const auto* user = response->get("ssuser"))
        result.quota = quotaFrom([&](std::string_view key) { return jsonText(user->get(key)); });
    if (const auto* game = response->get("jeu")) if (auto found = gameFromJson(*game, language)) result.games.push_back(std::move(*found));
    if (const auto* list = response->get("jeux"); list && list->type == json::Value::Type::array)
        for (const auto& item : list->elements) if (auto found = gameFromJson(item, language)) result.games.push_back(std::move(*found));
    if (!result.games.empty()) result.game = result.games.front();
    result.status = result.game ? Status::found : Status::missing;
    return result;
}
std::wstring lower(std::wstring text) {
    for (auto& ch : text) ch = static_cast<wchar_t>(std::towlower(ch));
    return text;
}

std::wstring host(std::wstring_view url) {
    if (!url.starts_with(L"https://")) return {};
    url.remove_prefix(8);
    return lower(std::wstring(url.substr(0, url.find_first_of(L"/?#:"))));
}

void appendJson(std::string& out, std::wstring_view text) {
    out += '"';
    for (const unsigned char ch : text_encoding::utf8Lenient(text)) {
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (ch < 0x20) { char buffer[8]; snprintf(buffer, sizeof buffer, "\\u%04x", ch); out += buffer; }
            else out += static_cast<char>(ch);
        }
    }
    out += '"';
}

std::wstring field(const json::Value& value, const char* key, size_t limit = 2048) {
    const auto* item = value.get(key);
    auto text = item ? text_encoding::wideLenient(item->stringOr()) : std::wstring{};
    if (text.size() > limit) text.resize(limit);
    return text;
}

std::filesystem::path cacheFile(const Game& game, const std::filesystem::path& metadata) {
    return metadata / (gameCacheKey(game) + ".screenscraper.json");
}

bool image(const std::vector<uint8_t>& bytes) {
    return bytes.size() > 128 && ((bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G') ||
                                  (bytes[0] == 0xff && bytes[1] == 0xd8));
}
} // namespace

namespace {
std::mutex quota_mutex;
Quota observed_quota;
uint64_t quota_observed_at = 0;
}
Quota observedQuota() { std::lock_guard lock(quota_mutex); return observed_quota; }
void observeQuota(const Quota& quota) {
    if (!quota.max_per_day && !quota.max_per_minute && !quota.max_ko_per_day) return;
    std::lock_guard lock(quota_mutex);
    observed_quota = quota; quota_observed_at = unixMs();
}

HttpResponse gated(const std::function<HttpResponse()>& call, bool lookup, bool account_refresh) {
    static std::timed_mutex gate;
    static std::chrono::steady_clock::time_point last_request{};
    std::unique_lock lock(gate, std::defer_lock);
    while (!lock.try_lock_for(std::chrono::milliseconds(100))) if (httpCancelled()) return {};
    const auto metadata = userConfigDirectory() / L"metadata";
    if (!account_refresh && paused(metadata)) return {429};
    Quota quota;
    {
        std::lock_guard quota_lock(quota_mutex);
        // A server-provided quota is a snapshot, not a permanent account lock.
        if (unixMs() - quota_observed_at > 12ull * 60 * 60 * 1000) observed_quota = {};
        quota = observed_quota;
    }
    if (!account_refresh && quota.exhausted()) { pause(metadata, 12ull * 60 * 60 * 1000); return {430}; }
    const uint64_t spacing = std::max<uint64_t>(lookup ? 1200 : 0, quota.max_per_minute ? (60000ull + quota.max_per_minute - 1) / quota.max_per_minute : 0);
    const auto ready = last_request + std::chrono::milliseconds(spacing);
    while (std::chrono::steady_clock::now() < ready) {
        if (httpCancelled()) return {};
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (httpCancelled()) return {};
    last_request = std::chrono::steady_clock::now();
    const auto result = call();
    {
        std::lock_guard quota_lock(quota_mutex);
        if (result.status && !account_refresh && observed_quota.max_per_day) ++observed_quota.requests_today;
    }
    if (result.status == 429) pause(metadata, uint64_t{std::max(120u, result.retry_after_seconds)} * 1000);
    else if (result.status == 430 || result.status == 431) pauseAfter(metadata, result.status);
    return result;
}

HttpResponse request(const std::wstring& url, const std::wstring& headers, size_t max, const Get& get, bool lookup) {
    return gated([&] { return get(url, headers, max); }, lookup, url.find(L"/ssuserInfos.php?") != std::wstring::npos);
}

uint64_t unixMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

uint64_t pausedUntil(const std::filesystem::path& metadata) {
    std::ifstream input(metadata / L"screenscraper.cooldown");
    uint64_t until = 0;
    return input >> until ? until : 0;
}

bool paused(const std::filesystem::path& metadata) { return pausedUntil(metadata) > unixMs(); }

void pauseAfter(const std::filesystem::path& metadata, uint32_t status) {
    if (status == 429) pause(metadata, 2 * 60 * 1000);
    else if (status == 430 || status == 431) pause(metadata, 12ull * 60 * 60 * 1000);
}

void pause(const std::filesystem::path& metadata, uint64_t milliseconds) {
    atomic_file::write(metadata / L"screenscraper.cooldown", std::to_string(std::max(pausedUntil(metadata), unixMs() + milliseconds)));
}

uint32_t systemId(std::wstring_view esde_system) {
    // ES-DE system folders -> ScreenScraper system IDs (ES-DE's screenscraper_platformid_map).
    static constexpr std::pair<std::wstring_view, uint32_t> map[] = {
        {L"3do", 29}, {L"amiga", 64}, {L"amiga600", 64}, {L"amiga1200", 64}, {L"amigacd32", 130}, {L"amstradcpc", 65},
        {L"arcade", 75}, {L"mame", 75}, {L"fbneo", 75}, {L"atari2600", 26}, {L"atari5200", 40}, {L"atari7800", 41},
        {L"atari800", 43}, {L"atarixe", 43}, {L"atarijaguar", 27}, {L"atarijaguarcd", 171}, {L"atarilynx", 28}, {L"atarist", 42},
        {L"atomiswave", 53}, {L"c64", 66}, {L"cdimono1", 133}, {L"cdtv", 129}, {L"colecovision", 48}, {L"dos", 135},
        {L"dreamcast", 23}, {L"easyrpg", 231}, {L"famicom", 3}, {L"fds", 106}, {L"gameandwatch", 52}, {L"gamegear", 21},
        {L"gb", 9}, {L"gba", 12}, {L"gbc", 10}, {L"gc", 13}, {L"genesis", 1}, {L"megadrive", 1}, {L"mastersystem", 2},
        {L"megacd", 20}, {L"segacd", 20}, {L"sega32x", 19}, {L"intellivision", 115}, {L"msx", 113}, {L"msx1", 113}, {L"msx2", 116},
        {L"n3ds", 17}, {L"n64", 14}, {L"naomi", 56}, {L"nds", 15}, {L"neogeo", 142}, {L"neogeocd", 70}, {L"nes", 3},
        {L"ngp", 25}, {L"ngpc", 82}, {L"odyssey2", 104}, {L"videopac", 104}, {L"openbor", 214}, {L"pcengine", 31}, {L"tg16", 31},
        {L"pcenginecd", 114}, {L"tg-cd", 114}, {L"pcfx", 72}, {L"pico8", 234}, {L"plus4", 99}, {L"pokemini", 211},
        {L"ps2", 58}, {L"ps3", 59}, {L"ps4", 60}, {L"psp", 61}, {L"psvita", 62}, {L"psx", 57}, {L"saturn", 22},
        {L"scummvm", 123}, {L"sfc", 4}, {L"snes", 4}, {L"sg-1000", 109}, {L"solarus", 223}, {L"supergrafx", 105},
        {L"switch", 225}, {L"tic80", 222}, {L"vectrex", 102}, {L"vic20", 73}, {L"virtualboy", 11}, {L"wii", 16},
        {L"wiiu", 18}, {L"wonderswan", 45}, {L"wonderswancolor", 46}, {L"x68000", 79}, {L"xbox", 32}, {L"xbox360", 33},
        {L"zx81", 77}, {L"zxspectrum", 76}};
    const auto wanted = lower(std::wstring(esde_system));
    for (const auto& [name, id] : map) if (name == wanted) return id;
    return 0;
}

std::vector<uint32_t> systemIds(const std::wstring& platform, const std::filesystem::path& rom) {
    std::vector<uint32_t> result;
    for (const auto& system : esde::systems(platform, rom))
        if (const auto id = systemId(system); id && std::find(result.begin(), result.end(), id) == result.end()) result.push_back(id);
    return result;
}

std::wstring md5File(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    if (!input) return {};
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    struct Cleanup {
        BCRYPT_ALG_HANDLE& a; BCRYPT_HASH_HANDLE& h;
        ~Cleanup() { if (h) BCryptDestroyHash(h); if (a) BCryptCloseAlgorithmProvider(a, 0); }
    } cleanup{algorithm, hash};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_MD5_ALGORITHM, nullptr, 0) < 0 ||
        BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0) return {};
    std::vector<UCHAR> buffer(1 << 20);
    while (input) {
        input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        if (input.gcount() && BCryptHashData(hash, buffer.data(), static_cast<ULONG>(input.gcount()), 0) < 0) return {};
    }
    if (!input.eof()) return {};
    std::array<UCHAR, 16> digest{};
    if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) return {};
    constexpr wchar_t hex[] = L"0123456789abcdef";
    std::wstring result;
    for (const auto byte : digest) { result += hex[byte >> 4]; result += hex[byte & 15]; }
    return result;
}

std::optional<RomFile> describeRom(const std::filesystem::path& rom, uint64_t limit) {
    if (rom.empty()) return {};
    // A folder game started through a file inside it (RPCS3: "Game.ps3\PS3_GAME\USRDIR\EBOOT.BIN").
    for (auto folder = rom.parent_path(); folder.has_relative_path(); folder = folder.parent_path())
        if (_wcsicmp(folder.extension().c_str(), L".ps3") == 0) return RomFile{folder.filename().wstring(), 0, {}};
    std::error_code ec;
    // A multi-disc playlist names its discs; ScreenScraper knows the first disc, not the list.
    if (_wcsicmp(rom.extension().c_str(), L".m3u") == 0 && std::filesystem::is_regular_file(rom, ec)) {
        std::ifstream list(rom);
        std::string line;
        while (std::getline(list, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
            const auto first = line.find_first_not_of(" \t\xEF\xBB\xBF");
            if (first == std::string::npos || line[first] == '#') continue;
            auto disc = std::filesystem::path(text_encoding::wideLegacy(line.substr(first)));
            if (disc.is_relative()) disc = rom.parent_path() / disc;
            if (_wcsicmp(disc.extension().c_str(), L".m3u") != 0 && std::filesystem::is_regular_file(disc, ec)) return describeRom(disc, limit);
            break;
        }
    }
    RomFile file{rom.filename().wstring(), 0, {}};
    if (file.name.empty()) return {};
    if (std::filesystem::is_directory(rom, ec)) return file;
    const auto size = std::filesystem::file_size(rom, ec);
    if (!ec) {
        file.size = size;
        if (size && size <= limit) file.md5 = md5File(rom);
    }
    return file;
}

std::wstring gameInfoUrl(const Config& config, uint32_t system, const RomFile& rom) {
    auto url = config.base_url + L"jeuInfos.php?devid=" + url_encoding::component(config.dev_id) +
        L"&devpassword=" + url_encoding::component(config.dev_password) +
        L"&softname=" + url_encoding::component(config.softname) + L"&output=" + url_encoding::component(config.output);
    if (!config.user.empty() && !config.password.empty())
        url += L"&ssid=" + url_encoding::component(config.user) + L"&sspassword=" + url_encoding::component(config.password);
    if (system) url += L"&systemeid=" + std::to_wstring(system);
    url += L"&romnom=" + url_encoding::component(rom.name);
    // As ES-DE: the size goes with the hash; a name-only lookup lets ScreenScraper match the file name.
    if (!rom.md5.empty()) url += L"&md5=" + rom.md5 + L"&romtaille=" + std::to_wstring(rom.size);
    return url;
}

std::wstring stripCredentials(std::wstring_view url) {
    const auto query = url.find(L'?');
    if (query == std::wstring_view::npos) return std::wstring(url);
    std::wstring result(url.substr(0, query));
    const auto fragment = url.find(L'#', query);
    const auto parameters = url.substr(query + 1, fragment == std::wstring_view::npos ? std::wstring_view::npos : fragment - query - 1);
    bool first = true;
    for (size_t at = 0; at <= parameters.size();) {
        const auto end = std::min(parameters.find(L'&', at), parameters.size());
        const auto part = parameters.substr(at, end - at);
        const auto key = lower(std::wstring(part.substr(0, part.find(L'='))));
        at = end + 1;
        if (part.empty() || key == L"devid" || key == L"devpassword" || key == L"ssid" || key == L"sspassword" || key == L"softname") continue;
        result += first ? L'?' : L'&'; first = false;
        result += part;
    }
    return result;
}

std::wstring mediaUrl(const Config& config, std::wstring_view url) {
    std::wstring address(url);
    if (address.starts_with(L"http://")) address.insert(4, 1, L's');
    for (size_t at = 0; (at = address.find(L' ', at)) != std::wstring::npos;) address.replace(at, 1, L"%20");
    const auto target = host(address);
    if (target.empty() || address.find_first_of(L"\r\n\t\"<>\\") != std::wstring::npos) return {};
    const bool trusted = target == host(config.base_url) || target == L"screenscraper.fr" || target.ends_with(L".screenscraper.fr");
    if (!trusted) return {};
    address = stripCredentials(address);
    address += address.find(L'?') == std::wstring::npos ? L'?' : L'&';
    address += L"devid=" + url_encoding::component(config.dev_id) + L"&devpassword=" + url_encoding::component(config.dev_password) +
               L"&softname=" + url_encoding::component(config.softname);
    if (!config.user.empty() && !config.password.empty())
        address += L"&ssid=" + url_encoding::component(config.user) + L"&sspassword=" + url_encoding::component(config.password);
    return address;
}

Lookup parse(uint32_t http_status, std::string_view body, bool xml, std::wstring_view language) {
    Lookup result;
    const auto text = std::string(body.substr(0, 400));
    const auto says = [&](std::string_view marker) { return text.find(marker) != std::string::npos; };
    switch (http_status) {
    case 200: break;
    case 404: result.status = Status::missing; return result;
    case 400: result.status = Status::missing; result.error = L"HTTP 400"; return result; // Malformed for this ROM only.
    case 403: result.status = Status::rejected; return result;
    case 429: result.status = Status::throttled; return result;
    case 430: case 431: result.status = Status::quota; return result;
    case 401: case 423: case 426: result.status = Status::closed; result.error = L"HTTP " + std::to_wstring(http_status); return result;
    default: result.status = Status::invalid; result.error = L"HTTP " + std::to_wstring(http_status); return result;
    }
    const auto first = body.find_first_not_of(" \t\r\n\xEF\xBB\xBF");
    const char lead = first == std::string_view::npos ? '\0' : body[first];
    if (lead != '<' && lead != '{') {
        // Plain-text answers with status 200 ("Erreur : Rom/Iso/Dossier non trouvée !").
        if (says("Erreur de login") || says("identifiants d")) result.status = Status::rejected;
        else if (says("non trouv")) result.status = Status::missing;
        else if (says("quota") || says("Quota")) result.status = Status::quota;
        else { result.status = Status::invalid; result.error = wideText(text, 160); }
        return result;
    }
    (void)xml; // The body's own format decides; output= only asks for one.
    result = lead == '<' ? parseXml(body, language) : parseJson(body, language);
    if (result.status == Status::invalid) result.error = wideText(text, 160);
    return result;
}

Lookup search(const Config& config, const std::wstring& query, uint32_t system, std::wstring_view language, const Get& get) {
    if (!config.ready()) return {Status::rejected};
    auto url = config.base_url + L"jeuRecherche.php?devid=" + url_encoding::component(config.dev_id) +
        L"&devpassword=" + url_encoding::component(config.dev_password) +
        L"&softname=" + url_encoding::component(config.softname) + L"&output=" + url_encoding::component(config.output);
    if (!config.user.empty() && !config.password.empty())
        url += L"&ssid=" + url_encoding::component(config.user) + L"&sspassword=" + url_encoding::component(config.password);
    if (system) url += L"&systemeid=" + std::to_wstring(system);
    url += L"&recherche=" + url_encoding::component(query);
    const auto response = request(url, L"Accept: */*\r\n", 8 * 1024 * 1024, get, true);
    auto result = parse(response.status, std::string_view(reinterpret_cast<const char*>(response.body.data()), response.body.size()),
                        lower(config.output) != L"json", language);
    observeQuota(result.quota);
    return result;
}

Lookup lookupGame(const Config& config, uint32_t id, uint32_t system, std::wstring_view language, const Get& get) {
    if (!config.ready() || !id || !system) return {Status::rejected};
    auto url = gameInfoUrl(config, system, {});
    const auto rom = url.find(L"&romnom=");
    if (rom != url.npos) url.resize(rom);
    url += L"&gameid=" + std::to_wstring(id);
    const auto response = request(url, L"Accept: */*\r\n", 8 * 1024 * 1024, get, true);
    auto result = parse(response.status, std::string_view(reinterpret_cast<const char*>(response.body.data()), response.body.size()), false, language);
    observeQuota(result.quota);
    if (result.game && result.game->id != id) return {Status::invalid};
    return result;
}

Lookup lookup(const Config& config, uint32_t system, const RomFile& rom, std::wstring_view language, const Get& get) {
    if (!config.ready()) return {Status::rejected, {}, {}, {}};
    const auto response = request(gameInfoUrl(config, system, rom), L"Accept: */*\r\n", 4 * 1024 * 1024, get, true);
    auto result = parse(response.status, std::string_view(reinterpret_cast<const char*>(response.body.data()), response.body.size()),
                        lower(config.output) != L"json", language);
    observeQuota(result.quota);
    return result;
}

Account parseAccount(uint32_t http_status, std::string_view body) {
    Account result;
    const auto status = parse(http_status, body, true, L"en"); // Errors and plain-text answers alike.
    if (status.status != Status::missing && status.status != Status::invalid && status.status != Status::found) {
        result.status = status.status; result.error = status.error; return result;
    }
    const auto first = body.find_first_not_of(" \t\r\n\xEF\xBB\xBF");
    const char lead = first == std::string_view::npos ? '\0' : body[first];
    std::function<std::string(std::string_view)> field;
    std::optional<Node> document;
    std::optional<json::Value> root;
    const Node* user = nullptr;
    const json::Value* json_user = nullptr;
    if (lead == '<') {
        document = XmlReader(body).document();
        user = document && document->name == "Data" ? document->child("ssuser") : nullptr;
        if (user) field = [&](std::string_view tag) { const auto* node = user->child(tag); return node ? node->text : std::string{}; };
    } else if (lead == '{') {
        root = json::parse(body);
        const auto* response = root ? root->get("response") : nullptr;
        json_user = response ? response->get("ssuser") : nullptr;
        if (json_user) field = [&](std::string_view key) { return jsonText(json_user->get(key)); };
    }
    if (!field) { result.status = Status::invalid; result.error = wideText(body.substr(0, 160), 160); return result; }
    result.status = Status::found;
    result.user = wideText(field("id"), 60);
    result.level = number(field("niveau"));
    result.threads = number(field("maxthreads"));
    result.quota = quotaFrom(field);
    return result;
}

Account account(const Config& config, const Get& get) {
    if (!config.ready()) return {Status::rejected};
    auto url = config.base_url + L"ssuserInfos.php?devid=" + url_encoding::component(config.dev_id) +
        L"&devpassword=" + url_encoding::component(config.dev_password) +
        L"&softname=" + url_encoding::component(config.softname) + L"&output=" + url_encoding::component(config.output);
    if (!config.user.empty() && !config.password.empty())
        url += L"&ssid=" + url_encoding::component(config.user) + L"&sspassword=" + url_encoding::component(config.password);
    const auto response = request(url, L"Accept: */*\r\n", 256 * 1024, get, true);
    auto result = parseAccount(response.status, std::string_view(reinterpret_cast<const char*>(response.body.data()), response.body.size()));
    if (result.status == Status::found) observeQuota(result.quota);
    return result;
}

const Media* pickMedia(const GameInfo& game, ArtSlot slot) {
    const std::vector<std::wstring_view> types = slot == ArtSlot::cover ? std::vector<std::wstring_view>{L"box-2D"} :
        slot == ArtSlot::background ? std::vector<std::wstring_view>{L"fanart", L"ss"} : std::vector<std::wstring_view>{L"wheel-hd", L"wheel"};
    for (const auto type : types) {
        for (const auto region : {L"eu", L"wor", L"us", L"jp", L"ss", L""}) {
            for (const auto& media : game.media) {
                if (media.type != type || media.url.empty()) continue;
                if (*region && media.region != region) continue;
                if (slot == ArtSlot::logo && lower(media.format) != L"png") continue;
                return &media;
            }
        }
    }
    return nullptr;
}

std::vector<uint8_t> downloadMedia(const Config& config, const Media& media, const Get& get, uint32_t* status) {
    if (!config.ready()) return {};
    const auto address = mediaUrl(config, media.url);
    if (address.empty()) return {};
    auto response = request(address, L"Accept: image/png, image/jpeg\r\n", 20 * 1024 * 1024, get, false);
    if (status) *status = response.status;
    // ScreenScraper answers "NOMEDIA" (and similar short texts) with status 200.
    return response.status == 200 && image(response.body) ? std::move(response.body) : std::vector<uint8_t>{};
}

std::wstring fingerprint(const Game& game, const RomFile& rom) {
    return lower(game.rom.wstring()) + L'|' + std::to_wstring(rom.size);
}

std::string serialize(const Cached& cached) {
    std::string out = "{\"launchme_cache\":1,\"fingerprint\":";
    appendJson(out, cached.fingerprint);
    out += ",\"checked\":" + std::to_string(cached.checked);
    if (cached.manual) out += ",\"manual\":true";
    if (cached.game) {
        const auto& game = *cached.game;
        out += ",\"game\":{\"id\":" + std::to_string(game.id) + ",\"name\":";
        appendJson(out, game.name);
        out += ",\"names\":[";
        for (size_t i = 0; i < game.names.size(); ++i) { if (i) out += ','; appendJson(out, game.names[i]); }
        out += ']';
        const std::pair<const char*, const std::wstring*> texts[] = {
            {"description", &game.details.description}, {"release_date", &game.details.release_date},
            {"developer", &game.details.developer}, {"publisher", &game.details.publisher},
            {"genres", &game.details.genres}, {"players", &game.players}};
        for (const auto& [key, value] : texts) { out += ",\""; out += key; out += "\":"; appendJson(out, *value); }
        out += ",\"note\":" + std::to_string(game.note) + ",\"media\":[";
        bool first = true;
        for (const auto& media : game.media) {
            if (!first) out += ',';
            first = false;
            out += "{\"type\":"; appendJson(out, media.type);
            out += ",\"region\":"; appendJson(out, media.region);
            out += ",\"format\":"; appendJson(out, media.format);
            out += ",\"url\":"; appendJson(out, stripCredentials(media.url));
            out += '}';
        }
        out += "]}";
    }
    out += '}';
    return out;
}

std::optional<Cached> deserialize(std::string_view text) {
    const auto root = json::parse(text);
    if (!root || !root->get("launchme_cache") || root->get("launchme_cache")->intOr() != 1) return {};
    Cached cached;
    cached.fingerprint = field(*root, "fingerprint");
    if (const auto* checked = root->get("checked"); checked && checked->type == json::Value::Type::number && checked->numeric >= 0)
        cached.checked = static_cast<uint64_t>(checked->numeric);
    if (const auto* manual = root->get("manual")) cached.manual = manual->flag;
    if (const auto* game = root->get("game"); game && game->type == json::Value::Type::object) {
        GameInfo info;
        if (const auto* id = game->get("id"); id && id->type == json::Value::Type::number && id->numeric > 0 && id->numeric <= UINT32_MAX)
            info.id = static_cast<uint32_t>(id->numeric);
        info.name = field(*game, "name", 200);
        if (const auto* names = game->get("names"); names && names->type == json::Value::Type::array)
            for (const auto& name : names->elements) {
                auto alias = text_encoding::wideLenient(name.stringOr());
                if (!alias.empty() && alias.size() <= 200 && info.names.size() < 32) info.names.push_back(std::move(alias));
            }
        if (info.name.empty()) return {};
        info.details.description = field(*game, "description", 1800);
        info.details.release_date = field(*game, "release_date", 32);
        info.details.developer = field(*game, "developer", 200);
        info.details.publisher = field(*game, "publisher", 200);
        info.details.genres = field(*game, "genres", 200);
        info.players = field(*game, "players", 16);
        if (const auto* note = game->get("note")) info.note = std::clamp(note->intOr(-1), -1, 20);
        info.details.players = info.players;
        if (info.note > 0) info.details.score = std::to_wstring(info.note) + L"/20";
        if (const auto* media = game->get("media"); media && media->type == json::Value::Type::array)
            for (const auto& item : media->elements)
                info.media.push_back({field(item, "type", 40), field(item, "region", 16), field(item, "format", 8), stripCredentials(field(item, "url"))});
        cached.game = std::move(info);
    }
    return cached;
}

std::optional<Cached> loadCached(const Game& game, const std::filesystem::path& metadata) {
    if (gameKey(game).empty()) return {};
    std::ifstream input(cacheFile(game, metadata), std::ios::binary | std::ios::ate);
    if (!input || input.tellg() <= 0 || input.tellg() > 2 * 1024 * 1024) return {};
    std::string bytes(static_cast<size_t>(input.tellg()), '\0');
    input.seekg(0);
    if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) return {};
    return deserialize(bytes);
}

bool saveCached(const Game& game, const Cached& cached, const std::filesystem::path& metadata) {
    if (gameKey(game).empty()) return false;
    return atomic_file::write(cacheFile(game, metadata), serialize(cached));
}
}
