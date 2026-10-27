#include "ea_account.hpp"
#include "../i18n/i18n.hpp"
#include "../library/store_adapter.hpp"
#include "../net/json.hpp"
#include "../platform/text_encoding.hpp"

#ifdef _WIN32
#include <Windows.h>
#endif
#include <algorithm>
#include <chrono>
#include <cwctype>
#include <map>
#include <set>

namespace ea {
namespace {
constexpr const wchar_t* graphql_url = L"https://service-aggregation-layer.juno.ea.com/graphql";
constexpr size_t page_limit = 100, max_pages = 100, max_games = 10000;

std::string_view trim(std::string_view text) {
    while (!text.empty() && static_cast<unsigned char>(text.front()) <= ' ') text.remove_prefix(1);
    while (!text.empty() && static_cast<unsigned char>(text.back()) <= ' ') text.remove_suffix(1);
    return text;
}
bool tokenText(std::string_view token) {
    return token.size() >= 16 && token.size() <= 8192 && std::all_of(token.begin(), token.end(), [](char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
               ch == '.' || ch == '-' || ch == '_' || ch == '=' || ch == '+' || ch == '/';
    });
}
std::string field(const json::Value* value, std::string_view key) {
    const auto* item = value ? value->get(key) : nullptr;
    if (!item) return {};
    if (item->type == json::Value::Type::string) return item->text;
    if (item->type == json::Value::Type::number) return item->text; // Raw digits: IDs above 2^53 stay exact.
    return {};
}
std::string jsonString(std::string_view text) {
    std::string result = "\"";
    for (const char ch : text) {
        if (ch == '"' || ch == '\\') { result += '\\'; result += ch; }
        else if (static_cast<unsigned char>(ch) < 0x20) { char code[8]; sprintf_s(code, "\\u%04x", static_cast<unsigned char>(ch)); result += code; }
        else result += ch;
    }
    return result + '"';
}
std::string request(std::string_view query, std::string_view variables = "{}") {
    return "{\"query\":" + jsonString(query) + ",\"variables\":" + std::string(variables) + "}";
}
std::wstring title(std::string_view text) {
    auto name = text_encoding::wideLenient(text);
    std::erase_if(name, [](wchar_t ch) { return ch == L'®' || ch == L'™' || ch == L'©' || ch < 0x20; });
    while (!name.empty() && iswspace(name.back())) name.pop_back();
    while (!name.empty() && iswspace(name.front())) name.erase(name.begin());
    return name.size() <= 512 ? name : std::wstring{};
}

// One GraphQL call. EA reports an expired/invalid token as 401/403 or an UNAUTHENTICATED error.
std::optional<json::Value> call(const Post& post, const std::string& token, const std::string& body, Failure& failure) {
    const auto bearer = text_encoding::wide(token);
    const auto reply = post(graphql_url, L"Content-Type: application/json\r\nAccept: application/json\r\nAuthorization: Bearer " + bearer +
        L"\r\nAuthToken: " + bearer + L"\r\nX-AuthToken: " + bearer + L"\r\n", body);
    if (reply.status == 401 || reply.status == 403) { failure = Failure::expired; return {}; }
    if (reply.status != 200) { failure = reply.status ? Failure::response : Failure::network; return {}; }
    auto root = json::parse(std::string_view(reinterpret_cast<const char*>(reply.body.data()), reply.body.size()));
    if (!root) { failure = Failure::response; return {}; }
    if (const auto* errors = root->get("errors"); errors && errors->type == json::Value::Type::array && !errors->elements.empty()) {
        for (const auto& error : errors->elements) {
            const auto* extensions = error.get("extensions");
            const auto code = field(extensions, "code");
            if (code == "UNAUTHENTICATED" || code == "UNAUTHORIZED" || code == "FORBIDDEN") { failure = Failure::expired; return {}; }
        }
        if (!root->get("data") || root->get("data")->type != json::Value::Type::object) { failure = Failure::response; return {}; }
    }
    const auto* data = root->get("data");
    if (!data || data->type != json::Value::Type::object) { failure = Failure::response; return {}; }
    return *data;
}
}

namespace {
// The page's JSON object, also when the browser's own text came along with it ("Pretty-print").
std::optional<json::Value> pastedJson(std::string_view pasted) {
    const auto open = pasted.find('{'), close = pasted.rfind('}');
    if (open == std::string_view::npos || close == std::string_view::npos || close < open) return {};
    auto root = json::parse(pasted.substr(open, close - open + 1));
    if (!root || root->type != json::Value::Type::object) return {};
    return root;
}
int hexDigit(char ch) {
    return ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
}
// `name=value` in an address's query or fragment (access_token=…&token_type=…), decoded.
std::string addressValue(std::string_view pasted, std::string_view name) {
    for (size_t at = pasted.find(name); at != std::string_view::npos; at = pasted.find(name, at + 1)) {
        const size_t end = at + name.size();
        if ((at && pasted[at - 1] != '#' && pasted[at - 1] != '?' && pasted[at - 1] != '&') ||
            end >= pasted.size() || pasted[end] != '=') continue; // Part of another name.
        auto raw = pasted.substr(end + 1);
        raw = raw.substr(0, raw.find_first_of("&#\"'"));
        std::string value;
        for (size_t i = 0; i < raw.size(); ++i) {
            if (raw[i] == '%' && i + 2 < raw.size() && hexDigit(raw[i + 1]) >= 0 && hexDigit(raw[i + 2]) >= 0) {
                value += static_cast<char>(hexDigit(raw[i + 1]) * 16 + hexDigit(raw[i + 2])); i += 2;
            } else value += raw[i];
        }
        return value;
    }
    return {};
}
}

std::string accessToken(std::string_view pasted) {
    pasted = trim(pasted);
    if (const auto root = pastedJson(pasted)) {
        const auto token = field(&*root, "access_token");
        return tokenText(token) ? token : std::string{};
    }
    if (const auto token = addressValue(pasted, "access_token"); !token.empty()) return tokenText(token) ? token : std::string{};
    return tokenText(pasted) ? std::string(pasted) : std::string{};
}

bool signedOut(std::string_view pasted) {
    pasted = trim(pasted);
    // {"error_code":"login_required","error":"login_required","error_number":"102100"}, or the same
    // fields in an address (#error_code=login_required&…).
    if (const auto root = pastedJson(pasted)) return field(&*root, "error") == "login_required" || field(&*root, "error_code") == "login_required";
    return addressValue(pasted, "error") == "login_required" || addressValue(pasted, "error_code") == "login_required";
}

Import importLibrary(std::string_view pasted, const Post& post) {
    Import result;
    const auto token = accessToken(pasted);
    if (token.empty()) { result.failure = signedOut(pasted) ? Failure::signed_out : Failure::input; return result; }
    auto& failure = result.failure;
    // Identity: the account whose library this is (persona and name are not stored).
    const auto identity = call(post, token, request("query{me{player{pd psd displayName}}}"), failure);
    if (!identity) return result;
    const auto* player = identity->get("me") ? identity->get("me")->get("player") : nullptr;
    auto account = field(player, "pd");
    if (account.empty() || account.size() > 64 || !std::all_of(account.begin(), account.end(), [](char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '-' || ch == '_'; })) {
        failure = Failure::response; return result;
    }
    // Owned base games (EA storefront, PC), paged by the service's cursor.
    std::vector<std::string> offers;
    std::set<std::string> seen_offers;
    std::string next;
    for (size_t page = 0; page < max_pages; ++page) {
        const auto variables = "{\"limit\":" + std::to_string(page_limit) + ",\"next\":" + (next.empty() ? std::string("null") : jsonString(next)) + "}";
        const auto data = call(post, token, request(
            "query getEntitlements($limit: Int, $next: String) { me { ownedGameProducts(locale: \"DEFAULT\" entitlementEnabled: true "
            "storefronts: [EA] type: [DIGITAL_FULL_GAME, PACKAGED_FULL_GAME] platforms: [PC] paging: {limit: $limit, next: $next}) "
            "{ next items { originOfferId product { baseItem { gameType } } } } } }", variables), failure);
        if (!data) return result;
        const auto* owned = data->get("me") ? data->get("me")->get("ownedGameProducts") : nullptr;
        const auto* items = owned ? owned->get("items") : nullptr;
        if (!items || items->type != json::Value::Type::array) { failure = Failure::response; return result; }
        for (const auto& item : items->elements) {
            const auto* product = item.get("product");
            const auto* base = product ? product->get("baseItem") : nullptr;
            const auto offer = field(&item, "originOfferId");
            if (field(base, "gameType") == "BASE_GAME" && !offer.empty() && offer.size() <= 96 && seen_offers.insert(offer).second) offers.push_back(offer);
        }
        const auto following = field(owned, "next");
        if (following.empty() || following == next || offers.size() >= max_games) break;
        next = following;
    }
    // Offer -> content ID (the ID the EA app launches and installerdata.xml carries) and title.
    std::map<std::string, Game> games;
    for (size_t begin = 0; begin < offers.size(); begin += page_limit) {
        std::string list = "[";
        for (size_t i = begin; i < std::min(offers.size(), begin + page_limit); ++i) list += (i > begin ? "," : "") + jsonString(offers[i]);
        const auto data = call(post, token, request(
            "query getOffers($offerIds: [String!]!) { legacyOffers(offerIds: $offerIds, locale: \"DEFAULT\") { offerId: id contentId } "
            "gameProducts(offerIds: $offerIds, locale: \"DEFAULT\") { items { id originOfferId baseItem { title keyArt { aspect16x9Image { path } } "
            "packArt { aspect9x16Image { path } largestImage { path } } primaryLogo { largestImage { path } } } } } }",
            "{\"offerIds\":" + list + "]}"), failure);
        if (!data) return result;
        const auto* legacy = data->get("legacyOffers");
        const auto* products = data->get("gameProducts") ? data->get("gameProducts")->get("items") : nullptr;
        if (!legacy || legacy->type != json::Value::Type::array) { failure = Failure::response; return result; }
        for (const auto& offer : legacy->elements) {
            const auto offer_id = field(&offer, "offerId"), content = field(&offer, "contentId");
            if (offer_id.empty() || !validStoreGameId(GameSource::ea, content)) continue;
            const json::Value* product = nullptr;
            if (products && products->type == json::Value::Type::array) for (const auto& candidate : products->elements) {
                const auto id = field(&candidate, "id"), origin = field(&candidate, "originOfferId");
                if (origin == offer_id || id == content || id == offer_id) { product = &candidate; break; }
            }
            // Offers with identification data but no product are not games of the library.
            const auto name = product ? title(field(product->get("baseItem"), "title")) : std::wstring{};
            if (name.empty()) continue;
            Game game;
            game.source = GameSource::ea; game.kind = GameKind::store; game.installed = false;
            game.source_id = content; game.name = name; game.account_id = account;
            // EA's own art (no key needed to fetch it): the tall pack art, the 16:9 key art, the logo.
            const auto* base = product->get("baseItem");
            const auto image = [&](const char* group, const char* size) {
                const auto* holder = base && base->get(group) ? base->get(group)->get(size) : nullptr;
                auto path = field(holder, "path");
                return path.starts_with("https://") && path.size() <= 1024 && path.find_first_of("\"\r\n") == std::string::npos ? path : std::string{};
            };
            game.store_tall_art = image("packArt", "aspect9x16Image");
            if (game.store_tall_art.empty()) game.store_tall_art = image("packArt", "largestImage");
            game.store_wide_art = image("keyArt", "aspect16x9Image");
            game.store_logo_art = image("primaryLogo", "largestImage");
            games.try_emplace(gameKey(game), std::move(game));
        }
    }
    result.snapshot.account = account;
    result.snapshot.synced_at = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    for (auto& [key, game] : games) { (void)key; result.snapshot.games.push_back(std::move(game)); }
    return result;
}

std::wstring failureText(Failure failure) {
    switch (failure) {
    case Failure::input: return tr(L"To nie jest odpowiedź EA z tokenem. Skopiuj całą stronę z access_token po zalogowaniu.");
    case Failure::signed_out: return tr(L"EA: najpierw zaloguj się na ea.com, potem odśwież kartę z tokenem i skopiuj ją ponownie.");
    case Failure::expired: return tr(L"Token EA wygasł. Odśwież kartę z tokenem i wklej nową odpowiedź.");
    case Failure::network: return tr(L"Nie można połączyć się z EA. Sprawdź połączenie z siecią.");
    case Failure::response: return tr(L"EA zwróciło nieoczekiwaną odpowiedź.");
    default: return {};
    }
}
}
