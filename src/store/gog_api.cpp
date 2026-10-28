#include "gog_account.hpp"
#include "../net/json.hpp"
#include "../net/url_encoding.hpp"
#include "../model/library_time.hpp"
#include "../platform/text_encoding.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <set>
#include <thread>

namespace gog {
namespace {
using text_encoding::wide;
using text_encoding::utf8;
bool printable(std::string_view value, size_t maximum = 8192) {
    return !value.empty() && value.size() <= maximum && std::all_of(value.begin(), value.end(), [](unsigned char c) { return c > 32 && c < 127; });
}
bool id(std::string_view value) {
    return !value.empty() && value.size() <= 96 && value[0] != '0' &&
        std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; });
}
std::string field(const json::Value& value, const char* name) {
    const auto item = value.get(name); return item ? item->stringOr() : std::string{};
}
std::string decimal(const json::Value* value) {
    if (!value) return {};
    if (value->type == json::Value::Type::string) return id(value->text) ? value->text : std::string{};
    // Preserve the JSON lexeme rather than round an ID through double.
    return value->type == json::Value::Type::number && id(value->text) ? value->text : std::string{};
}
std::optional<std::map<std::string, std::string>> query(std::string_view input) {
    std::map<std::string, std::string> result;
    const auto decode = [](std::string_view part) -> std::optional<std::string> {
        std::string out;
        const auto digit = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
        for (size_t i = 0; i < part.size(); ++i) {
            char c = part[i];
            if (c == '%') {
                if (i + 2 >= part.size() || digit(part[i + 1]) < 0 || digit(part[i + 2]) < 0) return {};
                c = static_cast<char>(digit(part[i + 1]) * 16 + digit(part[i + 2])); i += 2;
            } else if (c == '+') c = ' ';
            if (static_cast<unsigned char>(c) <= 32 || static_cast<unsigned char>(c) >= 127) return {};
            out += c;
        }
        return out;
    };
    while (!input.empty()) {
        const auto end = input.find('&'); const auto part = input.substr(0, end); const auto equals = part.find('=');
        if (equals == std::string_view::npos) return {};
        const auto key = decode(part.substr(0, equals)), value = decode(part.substr(equals + 1));
        if (!key || !value || key->empty() || !result.emplace(*key, *value).second) return {};
        if (end == std::string_view::npos) break;
        input.remove_prefix(end + 1);
    }
    return result;
}
std::optional<json::Value> parsed(const HttpResponse& response) {
    return json::parse({reinterpret_cast<const char*>(response.body.data()), response.body.size()});
}
Result failure(const HttpResponse& response) {
    if (response.status == 200) return {};
    if (response.status == 400 || response.status == 401 || response.status == 403) return {Failure::expired};
    if (response.status == 429) return {Failure::rate_limit, std::max(1u, response.retry_after_seconds ? response.retry_after_seconds : 60u)};
    return {response.status == 0 || response.status >= 500 ? Failure::offline : Failure::invalid};
}
class Api final : public Backend {
    Http get_;
    HttpResponse request(const std::wstring& url, const std::wstring& headers, const Cancel& cancel) {
        for (int attempt = 0; attempt < 3 && !cancel(); ++attempt) {
            auto response = get_(url, headers, 2 * 1024 * 1024);
            if (response.status && response.status < 500) return response;
            if (attempt == 2) return response;
            for (int tick = 0; tick < (1 << attempt) * 10 && !cancel(); ++tick) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return {};
    }
    Result token(const Config& config, const std::wstring& grant, Tokens& tokens, const Cancel& cancel) {
        HttpBudget budget(30000);
        if (!config.valid()) return {Failure::configuration};
        // GOG's documented client flow uses GET. Redirects are disabled even
        // here, because the query contains credentials; no URL is ever logged.
        if (cancel()) return {Failure::cancelled};
        // A grant consumes a code or rotates a token. An ambiguous timeout must
        // not cause us to blindly submit those credentials a second time.
        auto response = get_(L"https://auth.gog.com/token?client_id=" + url_encoding::component(config.client_id) +
            L"&client_secret=" + url_encoding::component(config.client_secret) + grant, L"Accept: application/json\r\n", 2 * 1024 * 1024);
        if (cancel()) return {Failure::cancelled};
        if (auto result = failure(response); !result) return result;
        const auto root = parsed(response);
        if (!root) return {Failure::invalid};
        Tokens next{field(*root, "access_token"), field(*root, "refresh_token"), decimal(root->get("user_id")), config.client_id};
        const auto expires = root->get("expires_in");
        if (!printable(next.access) || !printable(next.refresh) || !id(next.user) ||
            !expires || expires->type != json::Value::Type::number || !std::isfinite(expires->numeric) ||
            expires->numeric <= 0 || expires->numeric > 365 * 86400 ||
            (field(*root, "token_type") != "bearer" && field(*root, "token_type") != "Bearer")) return {Failure::invalid};
        if (!tokens.user.empty() && tokens.user != next.user) return {Failure::expired};
        next.expires = nowSeconds() + static_cast<uint64_t>(expires->numeric);
        tokens = std::move(next); return {};
    }
    Result authorized(const std::wstring& url, Tokens& tokens, const Config& config, const Cancel& cancel, HttpResponse& response, const std::wstring& extra = {}) {
        response = request(url, extra + L"Accept: application/json\r\nAuthorization: Bearer " + wide(tokens.access) + L"\r\n", cancel);
        if (cancel()) return {Failure::cancelled};
        if (response.status == 401) {
            const auto result = refresh(config, tokens, cancel); if (!result) return result;
            response = request(url, extra + L"Accept: application/json\r\nAuthorization: Bearer " + wide(tokens.access) + L"\r\n", cancel);
        }
        return cancel() ? Result{Failure::cancelled} : failure(response);
    }
public:
    explicit Api(Http get) : get_(std::move(get)) {}
    Result exchange(const Config& config, const std::string& code, Tokens& tokens, const Cancel& cancel) override {
        if (!printable(code, 2048)) return {Failure::invalid};
        return token(config, L"&grant_type=authorization_code&redirect_uri=" + url_encoding::component(config.redirect_uri) + L"&code=" + url_encoding::component(code), tokens, cancel);
    }
    Result refresh(const Config& config, Tokens& tokens, const Cancel& cancel) override {
        if (!printable(tokens.refresh) || tokens.client != config.client_id) return {Failure::configuration};
        return token(config, L"&grant_type=refresh_token&refresh_token=" + url_encoding::component(tokens.refresh), tokens, cancel);
    }
    Result verify(Tokens& tokens, const Config& config, const Cancel& cancel) override {
        if (tokens.expires <= nowSeconds() + 60) { const auto result = refresh(config, tokens, cancel); if (!result) return result; }
        HttpResponse response;
        const auto result = authorized(L"https://users.gog.com/users/" + wide(tokens.user), tokens, config, cancel, response);
        if (!result) return result;
        const auto root = parsed(response);
        return root && decimal(root->get("id")) == tokens.user ? Result{} : Result{Failure::expired};
    }
    Result call(const AccountRequest& call, Tokens& tokens, const Config& config, const Cancel& cancel, HttpResponse& response) override {
        if (call.post || !call.url.starts_with(L"https://")) return {Failure::invalid};
        if (tokens.expires <= nowSeconds() + 60) { const auto result = refresh(config, tokens, cancel); if (!result) return result; }
        HttpBudget budget(20000);
        return authorized(call.url, tokens, config, cancel, response);
    }
    Result installers(const std::string& product, Tokens& tokens, const Config& config, const Cancel& cancel, std::vector<InstallerVariant>& output) override {
        if (!id(product)) return {Failure::invalid};
        HttpResponse response;
        const auto result = authorized(L"https://api.gog.com/products/" + wide(product) + L"?expand=downloads", tokens, config, cancel, response);
        if (response.status == 404 && !cancel()) { output.clear(); return {}; }
        if (!result) return result;
        auto choices = parseInstallers({reinterpret_cast<const char*>(response.body.data()), response.body.size()}, product);
        if (!choices) return {Failure::invalid};
        output = std::move(*choices); return {};
    }
    Result resolveInstaller(const InstallerVariant& variant, Tokens& tokens, const Config& config, const Cancel& cancel, std::vector<InstallerPart>& output) override {
        HttpBudget budget(120000);
        std::vector<InstallerPart> parts;
        for (const auto& link : variant.downlinks) {
            if (!link.starts_with(L"https://api.gog.com/products/") || link.find(L"/downlink/installer/") == link.npos) return {Failure::invalid};
            HttpResponse response;
            const auto result = authorized(link, tokens, config, cancel, response);
            if (!result) return result;
            const auto root = parsed(response);
            if (!root) return {Failure::invalid};
            auto url = wide(field(*root, "downlink"));
            const auto checksum_url = wide(field(*root, "checksum"));
            if (!installerDownloadUrl(url) || !installerDownloadUrl(checksum_url)) return {Failure::invalid};
            const auto checksum = request(checksum_url, L"Accept: application/xml\r\n", cancel);
            if (cancel()) return {Failure::cancelled};
            if (const auto status = failure(checksum); !status) return status;
            auto part = parseInstallerChecksum({reinterpret_cast<const char*>(checksum.body.data()), checksum.body.size()}, std::move(url));
            if (!part) return {Failure::invalid};
            parts.push_back(std::move(*part));
        }
        if (parts.empty()) return {Failure::invalid};
        output = std::move(parts); return {};
    }
    Result library(Session& session, const Config& config, const Cancel& cancel) override {
        HttpBudget budget(600000);
        std::map<std::string, Game> old;
        for (const auto& game : session.games) old.emplace(game.source_id, game);
        std::vector<std::string> ids;
        std::map<std::string, std::string> certificates;
        std::map<std::string, uint64_t> acquired;
        std::set<std::string> seen, pages;
        std::string page;
        for (size_t index = 0;; ++index) {
            if (index >= 1000 || !pages.insert(page).second) return {Failure::invalid};
            HttpResponse response;
            const auto result = authorized(L"https://galaxy-library.gog.com/users/" + wide(session.tokens.user) + L"/releases" +
                (page.empty() ? L"" : L"?page_token=" + url_encoding::component(page)), session.tokens, config, cancel, response);
            if (!result) return result;
            const auto root = parsed(response); const auto list = root ? root->get("items") : nullptr;
            if (!list || list->type != json::Value::Type::array) return {Failure::invalid};
            for (const auto& item : list->elements) {
                if (field(item, "platform_id") != "gog") continue;
                const auto owned = item.get("owned");
                if (!owned || owned->type != json::Value::Type::boolean) return {Failure::invalid};
                if (!owned->flag) continue;
                const auto product = decimal(item.get("external_id"));
                if (product.empty()) return {Failure::invalid};
                if (seen.insert(product).second) ids.push_back(product);
                // owned_since is account ownership; date_created is only Galaxy's record creation.
                if (const auto value = item.get("owned_since"); value &&
                    (value->type == json::Value::Type::number || value->type == json::Value::Type::string)) {
                    const auto date = library_time::seconds(value->text);
                    if (date && (!acquired.contains(product) || date < acquired[product])) acquired[product] = date;
                }
                const auto certificate = field(item, "certificate");
                if (printable(certificate)) certificates[product] = certificate;
                if (ids.size() > 10000) return {Failure::invalid};
            }
            const auto next = root->get("next_page_token");
            if (next && next->type != json::Value::Type::null_value && next->type != json::Value::Type::string) return {Failure::invalid};
            page = next ? next->stringOr() : std::string{};
            if (page.empty()) break;
            if (!printable(page, 4096) || list->elements.empty()) return {Failure::invalid};
        }
        std::vector<Game> games;
        for (const auto& product : ids) {
            if (cancel()) return {Failure::cancelled};
            if (const auto found = old.find(product); found != old.end()) { games.push_back(found->second); continue; }
            HttpResponse response;
            auto result = authorized(L"https://api.gog.com/products/" + wide(product), session.tokens, config, cancel, response);
            if (response.status == 404 && !cancel()) {
                // Delisted/legacy owned releases can lack a Products API record.
                // GamesDB accepts the certificate from this account's library;
                // never persist it or infer ownership from a public catalog.
                const auto cert = certificates.find(product);
                result = authorized(L"https://gamesdb.gog.com/platforms/gog/external_releases/" + wide(product),
                    session.tokens, config, cancel, response, cert == certificates.end() ? L"" : L"X-GOG-Library-Cert: " + wide(cert->second) + L"\r\n");
                if (!result) return result;
                const auto root = parsed(response);
                if (!root || decimal(root->get("external_id")) != product || field(*root, "platform_id") != "gog") return {Failure::invalid};
                const auto type = field(*root, "type");
                if (type == "dlc" || type == "pack" || type == "spam") continue;
                if (type != "game" && type != "mod") return {Failure::invalid};
                const auto details = root->get("game");
                const auto visible = details ? details->get("visible_in_library") : nullptr;
                if (!visible || visible->type != json::Value::Type::boolean) return {Failure::invalid};
                if (!visible->flag) continue;
                const auto title = root->get("title"), fallback = details->get("title");
                auto name = title ? field(*title, "*") : std::string{};
                if (name.empty() && fallback) name = field(*fallback, "*");
                Game game; game.source = GameSource::gog; game.kind = GameKind::store; game.source_id = product; game.installed = false;
                game.name = wide(name);
                if (game.name.empty() || game.name.size() > 512 || game.name.find_first_of(L"\r\n\0", 0, 3) != std::wstring::npos) return {Failure::invalid};
                games.push_back(std::move(game));
                continue;
            }
            if (!result) return result;
            const auto root = parsed(response);
            if (!root || decimal(root->get("id")) != product) return {Failure::invalid};
            const auto type = field(*root, "game_type");
            if (type == "dlc" || type == "pack") continue;
            if (type != "game") return {Failure::invalid};
            Game game; game.source = GameSource::gog; game.kind = GameKind::store; game.source_id = product; game.installed = false;
            game.name = wide(field(*root, "title"));
            if (game.name.empty() || game.name.size() > 512 || game.name.find_first_of(L"\r\n\0", 0, 3) != std::wstring::npos) return {Failure::invalid};
            const auto secret = root->get("is_secret");
            if (!secret || secret->type != json::Value::Type::boolean) return {Failure::invalid};
            game.visibility = secret->flag ? GameVisibility::private_game : GameVisibility::public_game;
            games.push_back(std::move(game));
        }
        if (cancel()) return {Failure::cancelled};
        // Also enrich cached metadata; a known title must not keep its old import timestamp.
        for (auto& game : games) if (const auto found = acquired.find(game.source_id); found != acquired.end()) game.added_at = found->second;
        session.games = std::move(games); session.synced_at = nowSeconds(); return {};
    }
};
}
uint64_t nowSeconds() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}
bool Config::valid() const {
    return printable(client_id, 128) && printable(client_secret, 1024) && printable(redirect_uri, 2048) &&
        redirect_uri.starts_with("https://") && redirect_uri.find_first_of("#@\\") == std::string::npos;
}
std::wstring authorizationUrl(const Config& config, const std::string& state) {
    if (!config.valid() || !printable(state, 128)) return {};
    return L"https://auth.gog.com/auth?client_id=" + url_encoding::component(config.client_id) + L"&redirect_uri=" + url_encoding::component(config.redirect_uri) +
        L"&response_type=code&layout=client2&state=" + url_encoding::component(state);
}
std::optional<std::string> callbackCode(std::wstring_view input, const Config& config, const std::string& state) {
    if (!config.valid() || state.empty() || input.size() > 8192) return {};
    const auto text = utf8(input); const auto split = text.find('?'), expected_split = config.redirect_uri.find('?');
    if (split == std::string::npos || text.find('#') != std::string::npos || text.substr(0, split) != config.redirect_uri.substr(0, expected_split)) return {};
    const auto values = query(std::string_view(text).substr(split + 1));
    if (!values || (values->contains("state") ? values->at("state") != state : !config.legacy_manual_callback) || !values->contains("code") ||
        !printable(values->at("code"), 2048) || values->contains("error")) return {};
    if (expected_split != std::string::npos) {
        const auto expected = query(std::string_view(config.redirect_uri).substr(expected_split + 1));
        if (!expected) return {};
        for (const auto& [key, value] : *expected) if (!values->contains(key) || values->at(key) != value) return {};
    }
    return values->at("code");
}
std::unique_ptr<Backend> makeBackend(Http get) {
    if (!get) get = httpGetPrivate;
    return std::make_unique<Api>(std::move(get));
}
}
