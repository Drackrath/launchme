#include "epic_account.hpp"
#include "../net/json.hpp"
#include "../net/url_encoding.hpp"
#include "../platform/protected_file.hpp"
#include "../platform/text_encoding.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <thread>
#include <wincrypt.h>

namespace epic {
namespace {
using text_encoding::wide;
using text_encoding::utf8;
constexpr wchar_t oauth[] = L"https://account-public-service-prod03.ol.epicgames.com/account/api/oauth/";
std::string field(const json::Value& v, const char* key) { const auto p = v.get(key); return p ? p->stringOr() : std::string{}; }
std::string body(const HttpResponse& r) { return {r.body.begin(), r.body.end()}; }
bool printable(std::string_view s) { return !s.empty() && s.size() <= 8192 && std::all_of(s.begin(), s.end(), [](unsigned char c) { return c > 32 && c < 127; }); }
// The catalog item's own art (keyImages): its tall box art, wide art and logo, the first of each
// kind in Epic's order of preference. A game without any keeps "-" (looked at, none there).
void keyImages(const json::Value& product, Game& game) {
    std::map<std::string, std::string> urls;
    if (const auto images = product.get("keyImages"); images && images->type == json::Value::Type::array)
        for (const auto& image : images->elements) {
            const auto type = field(image, "type"), url = field(image, "url");
            if (!type.empty() && url.starts_with("https://") && url.size() <= 2048 && printable(url)) urls.emplace(type, url);
        }
    const auto first = [&](std::initializer_list<const char*> types) {
        for (const auto* type : types) if (const auto found = urls.find(type); found != urls.end()) return found->second;
        return std::string{};
    };
    game.store_tall_art = first({"DieselGameBoxTall", "OfferImageTall", "DieselStoreFrontTall", "Thumbnail"});
    game.store_wide_art = first({"DieselGameBox", "OfferImageWide", "DieselStoreFrontWide"});
    game.store_logo_art = first({"DieselGameBoxLogo"});
    if (game.store_tall_art.empty()) game.store_tall_art = "-";
}
// Form bodies (token grants) need narrow text; URLs use url_encoding::component.
std::string encode(std::string_view s) {
    constexpr char hex[] = "0123456789ABCDEF"; std::string out;
    for (unsigned char c : s) {
        if ((c < 128 && isalnum(c)) || c == '-' || c == '_' || c == '.' || c == '~') out += c;
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}
std::string quote(std::string_view s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        if (c >= 32) out += c;
    }
    return out + '"';
}
Http transport(Http http) {
    if (http) return http;
    return [](const std::wstring& url, const std::wstring& headers, const std::string& data, bool post) {
        return post ? httpPostPrivate(url, headers, data, 8 * 1024 * 1024) : httpGetPrivate(url, headers, 8 * 1024 * 1024);
    };
}
Result failure(const HttpResponse& r) {
    if (r.status == 200) return {};
    if (r.status == 400 || r.status == 401 || r.status == 403) return {Failure::expired};
    if (r.status == 429) return {Failure::rate_limit, r.retry_after_seconds ? r.retry_after_seconds : 60};
    return {r.status == 0 || r.status >= 500 ? Failure::offline : Failure::invalid};
}
class Api final : public Backend {
    Http http_;
    Result token(const Config& cfg, const std::string& grant, Tokens& tokens, const Cancel& cancel) {
        if (!cfg.valid()) return {Failure::configuration};
        if (cancel()) return {Failure::cancelled};
        auto credentials = cfg.client + ':' + cfg.secret;
        DWORD size = 0;
        CryptBinaryToStringA(reinterpret_cast<const BYTE*>(credentials.data()), static_cast<DWORD>(credentials.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &size);
        std::string basic(size, '\0');
        if (!CryptBinaryToStringA(reinterpret_cast<const BYTE*>(credentials.data()), static_cast<DWORD>(credentials.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, basic.data(), &size)) return {Failure::configuration};
        basic.resize(size);
        if (!basic.empty() && basic.back() == '\0') basic.pop_back();
        // Never retry an ambiguous one-use code or refresh grant, nor follow redirects.
        auto response = http_(std::wstring(oauth) + L"token", L"Content-Type: application/x-www-form-urlencoded\r\nAuthorization: Basic " + wide(basic) + L"\r\n", grant + "&token_type=eg1", true);
        SecureZeroMemory(credentials.data(), credentials.size()); SecureZeroMemory(basic.data(), basic.size());
        if (auto result = failure(response); !result) return result;
        auto text = body(response); auto next = parseTokens(text); SecureZeroMemory(text.data(), text.size());
        SecureZeroMemory(response.body.data(), response.body.size());
        if (!next) return {Failure::invalid};
        if (!tokens.account.empty() && next->account != tokens.account) return {Failure::expired};
        tokens = std::move(*next); // Preserve rotations even if cancellation raced the HTTP response.
        return {};
    }
    Result refresh(const Config& cfg, Tokens& t, const Cancel& cancel) { return token(cfg, "grant_type=refresh_token&refresh_token=" + encode(t.refresh), t, cancel); }
    Result get(const std::wstring& url, const Config& cfg, Tokens& t, const Cancel& cancel, HttpResponse& out) {
        bool refreshed = false;
        for (int attempt = 0; attempt < 3; ++attempt) {
            if (cancel()) return {Failure::cancelled};
            out = http_(url, L"Authorization: Bearer " + wide(t.access) + L"\r\nAccept: application/json\r\n", {}, false);
            if (out.status == 401 && !refreshed) {
                auto r = refresh(cfg, t, cancel); if (!r) return r;
                refreshed = true; --attempt; continue;
            }
            if ((out.status && out.status < 500) || attempt == 2) return failure(out);
            for (int i = 0; i < (1 << attempt) * 10 && !cancel(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return {Failure::offline};
    }
public:
    explicit Api(Http http) : http_(transport(std::move(http))) {}
    Result exchange(const Config& cfg, const std::string& code, Tokens& t, const Cancel& cancel) override {
        return printable(code) ? token(cfg, "grant_type=authorization_code&code=" + encode(code), t, cancel) : Result{Failure::invalid};
    }
    Result verify(const Config& cfg, Tokens& t, const Cancel& cancel) override {
        // Legendary requires at least ten minutes on the short-lived access token.
        if (t.expires <= nowSeconds() + 900) { auto r = refresh(cfg, t, cancel); if (!r) return r; }
        HttpResponse response; auto r = get(std::wstring(oauth) + L"verify", cfg, t, cancel, response); if (!r) return r;
        const auto root = json::parse(body(response));
        return root && field(*root, "account_id") == t.account && field(*root, "client_id") == cfg.client ? Result{} : Result{Failure::expired};
    }
    Result call(const Config& cfg, const AccountRequest& call, Tokens& t, const Cancel& cancel, HttpResponse& out) override {
        if (!call.url.starts_with(L"https://")) return {Failure::invalid};
        if (t.expires <= nowSeconds() + 60) { auto r = refresh(cfg, t, cancel); if (!r) return r; }
        HttpBudget budget(20000);
        const auto headers = [&] {
            return L"Authorization: Bearer " + wide(t.access) + L"\r\nAccept: application/json\r\n" + (call.post ? L"Content-Type: application/json\r\n" : L"");
        };
        out = http_(call.url, headers(), call.body, call.post);
        if (out.status == 401 && !cancel()) {
            auto r = refresh(cfg, t, cancel); if (!r) return r;
            out = http_(call.url, headers(), call.body, call.post);
        }
        return cancel() ? Result{Failure::cancelled} : failure(out);
    }
    Result library(const Config& cfg, Session& session, const Cancel& cancel) override {
        HttpBudget budget(600000);
        struct Item { std::string app, space, catalog; };
        std::map<std::string, Item> items;
        std::set<std::string> cursors;
        std::string cursor;
        // Library service is the ownership source, including titles delivered by partner stores.
        for (size_t page = 0; ; ++page) {
            if (page >= 1000 || !cursors.insert(cursor).second) return {Failure::invalid};
            HttpResponse response;
            auto r = get(L"https://library-service.live.use1a.on.epicgames.com/library/api/public/items?includeMetadata=true" +
                (cursor.empty() ? L"" : L"&cursor=" + url_encoding::component(std::string_view(cursor))), cfg, session.tokens, cancel, response);
            if (!r) return r;
            const auto root = json::parse(body(response)); const auto records = root ? root->get("records") : nullptr;
            const auto meta = root ? root->get("responseMetadata") : nullptr;
            if (!records || records->type != json::Value::Type::array || !meta || meta->type != json::Value::Type::object) return {Failure::invalid};
            for (const auto& value : records->elements) {
                Item item{field(value, "appName"), field(value, "namespace"), field(value, "catalogItemId")};
                if (item.space == "ue" || item.app.starts_with("UE_") || item.app.empty() || item.app == "1" || field(value, "sandboxType") == "PRIVATE") continue;
                if (!identifier(item.app) || !identifier(item.space) || !identifier(item.catalog)) return {Failure::invalid};
                if (const auto old = items.find(item.app); old != items.end() && (old->second.space != item.space || old->second.catalog != item.catalog)) return {Failure::invalid};
                items[item.app] = std::move(item);
                if (items.size() > 10000) return {Failure::invalid};
            }
            const auto next = meta->get("nextCursor");
            if (next && next->type != json::Value::Type::null_value && next->type != json::Value::Type::string) return {Failure::invalid};
            cursor = next ? next->stringOr() : std::string{};
            if (cursor.empty()) break;
            if (cursor.size() > 8192) return {Failure::invalid};
        }
        std::map<std::string, Game> previous;
        for (const auto& game : session.games) previous.emplace(game.source_id, game);
        std::vector<Game> games;
        for (const auto& [id, item] : items) {
            if (cancel()) return {Failure::cancelled};
            // Known games are kept as saved; one saved before Epic's art was read (0.2.4 and older) is asked once more.
            if (const auto old = previous.find(id); old != previous.end() && old->second.epic_catalog == item.catalog && old->second.epic_namespace == item.space &&
                !old->second.store_tall_art.empty()) {
                games.push_back(old->second); continue;
            }
            HttpResponse response;
            auto r = get(L"https://catalog-public-service-prod06.ol.epicgames.com/catalog/api/shared/namespace/" + url_encoding::component(std::string_view(item.space)) +
                L"/bulk/items?id=" + url_encoding::component(std::string_view(item.catalog)) + L"&includeDLCDetails=true&includeMainGameDetails=true&country=US&locale=en", cfg, session.tokens, cancel, response);
            if (!r) return r;
            const auto root = json::parse(body(response)); const auto product = root ? root->get(item.catalog) : nullptr;
            if (!product || product->type != json::Value::Type::object || field(*product, "id") != item.catalog) return {Failure::invalid};
            const auto categories = product->get("categories");
            const auto main = product->get("mainGameItem");
            bool skip = main && main->type == json::Value::Type::object;
            if (!categories || categories->type != json::Value::Type::array) return {Failure::invalid};
            for (const auto& category : categories->elements) {
                const auto path = field(category, "path");
                skip |= path == "addons" || path.starts_with("addons/") || path == "mods" || path.starts_with("plugins") || path.starts_with("assets");
            }
            if (skip) continue;
            Game game; game.source = GameSource::epic; game.kind = GameKind::store; game.installed = false;
            game.source_id = id; game.epic_namespace = item.space; game.epic_catalog = item.catalog;
            game.name = text_encoding::wideLenient(field(*product, "title"));
            if (game.name.empty() || game.name.size() > 2048) return {Failure::invalid};
            keyImages(*product, game);
            if (const auto old = previous.find(id); old != previous.end()) { game.visibility = old->second.visibility; game.store_playtime_minutes = old->second.store_playtime_minutes; }
            if (const auto attrs = product->get("customAttributes")) {
                for (const auto key : {"ThirdPartyManagedApp", "ThirdPartyManagedProvider", "partnerLinkType"})
                    if (const auto attr = attrs->get(key); attr && !field(*attr, "value").empty()) game.epic_partner = field(*attr, "value");
            }
            games.push_back(std::move(game));
        }
        if (cancel()) return {Failure::cancelled};
        // Play time Epic counted for each title (its launcher's own "Czas gry"), best effort: a
        // refused or broken answer leaves the library as it is, without play time.
        if (identifier(session.tokens.account)) {
            HttpResponse response;
            if (get(L"https://library-service.live.use1a.on.epicgames.com/library/api/public/playtime/account/" +
                    url_encoding::component(std::string_view(session.tokens.account)) + L"/all", cfg, session.tokens, cancel, response)) {
                std::map<std::string, uint32_t> minutes;
                if (const auto root = json::parse(body(response)); root && root->type == json::Value::Type::array)
                    for (const auto& entry : root->elements) {
                        const auto* total = entry.get("totalTime");
                        const auto id = field(entry, "artifactId");
                        if (total && total->type == json::Value::Type::number && total->numeric >= 0 && total->numeric < 4e11 && identifier(id))
                            minutes[id] = static_cast<uint32_t>(std::min<double>(UINT32_MAX, total->numeric / 60));
                    }
                for (auto& game : games)
                    if (const auto found = minutes.find(game.source_id); found != minutes.end()) game.store_playtime_minutes = found->second;
            }
            if (cancel()) return {Failure::cancelled};
        }
        session.games = std::move(games); session.synced_at = nowSeconds(); return {};
    }
};
}
uint64_t nowSeconds() { return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()); }
bool identifier(std::string_view s) {
    return !s.empty() && s.size() <= 96 && s != "." && s != ".." && s.front() != '-' &&
        std::all_of(s.begin(), s.end(), [](unsigned char c) { return (c < 128 && isalnum(c)) || c == '-' || c == '_' || c == '.'; });
}
bool Config::valid() const { return identifier(client) && printable(secret); }
Config prepareConfig(const std::filesystem::path& file, const Http& custom) {
    const auto parse = [](std::string_view text) {
        Config cfg; const auto root = json::parse(text); const auto v = root ? root->get("egl_config") : nullptr;
        if (v) { cfg.client = field(*v, "client_id"); cfg.secret = field(*v, "client_secret"); }
        return cfg;
    };
    if (const auto text = protected_file::read(file, 1024 * 1024)) { const auto cfg = parse(*text); if (cfg.valid()) return cfg; }
    const auto response = transport(custom)(L"https://api.legendary.gl/v1/version.json", {}, {}, false);
    const auto cfg = parse(body(response));
    if (response.status != 200 || !cfg.valid()) return {};
    const auto text = "{\"egl_config\":{\"client_id\":" + quote(cfg.client) + ",\"client_secret\":" + quote(cfg.secret) + "}}";
    return protected_file::write(file, text, L"Launchme Epic client") ? cfg : Config{};
}
std::wstring authorizationUrl(const Config& cfg) {
    if (!cfg.valid()) return {};
    return L"https://www.epicgames.com/id/login?redirectUrl=" +
           url_encoding::component(std::string_view("https://www.epicgames.com/id/api/redirect?clientId=" + cfg.client + "&responseType=code"));
}
std::optional<std::string> authorizationCode(std::wstring_view input) {
    if (input.empty() || input.size() > 8192) return {};
    auto text = utf8(input);
    const auto first = text.find_first_not_of(" \r\n\t"), last = text.find_last_not_of(" \r\n\t");
    if (first == text.npos) return {};
    text = text.substr(first, last - first + 1);
    if (text.starts_with('{')) { const auto root = json::parse(text); if (!root) return {}; text = field(*root, "authorizationCode"); }
    else if (text.size() > 2 && text.front() == '"' && text.back() == '"') text = text.substr(1, text.size() - 2);
    // Epic's redirect displays JSON, not a callback containing a code in the URL.
    if (text.size() < 16 || text.size() > 2048 || !std::all_of(text.begin(), text.end(), [](unsigned char c) { return (c < 128 && isalnum(c)) || c == '-' || c == '_'; })) return {};
    return text;
}
std::optional<Tokens> parseTokens(std::string_view text) {
    const auto root = json::parse(text); if (!root) return {};
    Tokens t{field(*root, "access_token"), field(*root, "refresh_token"), field(*root, "account_id"), field(*root, "displayName")};
    const auto expires = root->get("expires_in");
    if (!printable(t.access) || !printable(t.refresh) || !identifier(t.account) || t.name.size() > 2048 || !expires || expires->intOr() <= 0 || expires->intOr() > 365 * 86400) return {};
    t.expires = nowSeconds() + expires->intOr(); return t;
}
std::string sessionJson(const Tokens& t, bool include_refresh) {
    const time_t time = static_cast<time_t>(t.expires); tm utc{}; gmtime_s(&utc, &time); char date[64]{}; strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return "{\"access_token\":" + quote(t.access) + ",\"refresh_token\":" + quote(include_refresh ? t.refresh : "") + ",\"account_id\":" + quote(t.account) +
        ",\"displayName\":" + quote(t.name) + ",\"expires_at\":" + quote(date) + ",\"expires_in\":" + std::to_string(t.expires > nowSeconds() ? t.expires - nowSeconds() : 1) + "}";
}
std::unique_ptr<Backend> makeBackend(Http http) { return std::make_unique<Api>(std::move(http)); }
}
