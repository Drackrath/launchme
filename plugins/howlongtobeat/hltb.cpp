#include "hltb.hpp"
#include "../../src/platform/text_encoding.hpp"
#include <algorithm>
#include <regex>
#include <sstream>

namespace hltb {
using namespace plugins::wire;
namespace {
constexpr size_t limit = 2 * 1024 * 1024;
const std::wstring base = L"https://howlongtobeat.com";
const std::wstring headers = L"Accept: application/json, text/html, */*\r\nReferer: https://howlongtobeat.com/\r\nOrigin: https://howlongtobeat.com\r\n";
std::string body(const HttpResponse& r) { return {r.body.begin(), r.body.end()}; }
uint32_t number(const json::Value& v, const char* key) {
    const auto& n = field(v, key);
    return n.type == json::Value::Type::number && std::isfinite(n.numeric) && n.numeric >= 0 && n.numeric <= UINT32_MAX && std::floor(n.numeric) == n.numeric ? static_cast<uint32_t>(n.numeric) : 0;
}
json::Value numeric(uint32_t n) { json::Value value; value.type = json::Value::Type::number; value.numeric = n; return value; }
bool headerValue(std::string_view value) {
    return !value.empty() && value.size() <= 512 && std::all_of(value.begin(), value.end(), [](unsigned char c) { return c >= 32 && c < 127; });
}
Result parseArray(const json::Value& items) {
    Result result;
    if (items.type != json::Value::Type::array) return result;
    result.valid = true;
    for (const auto& item : items.elements) {
        Game game{number(item, "game_id"), text(item, "game_name"), number(item, "comp_main"), number(item, "comp_plus"), number(item, "comp_100")};
        if (game.id && !game.name.empty() && game.name.size() <= 512 && !text_encoding::wide(game.name).empty()) result.games.push_back(std::move(game));
        if (result.games.size() == 20) break;
    }
    return result;
}
}
Result parseResults(std::string_view bytes) {
    const auto root = bytes.size() <= limit ? json::parse(bytes) : std::nullopt;
    return root ? parseArray(field(*root, "data")) : Result{};
}
Result parseGamePage(std::string_view html) {
    if (html.size() > limit) return {};
    const auto id = html.find("id=\"__NEXT_DATA__\"");
    if (id == std::string_view::npos) return {};
    const auto begin = html.find('>', id), end = html.find("</script>", begin);
    if (begin == std::string_view::npos || end == std::string_view::npos) return {};
    const auto root = json::parse(html.substr(begin + 1, end - begin - 1));
    if (!root) return {};
    return parseArray(field(field(field(field(field(*root, "props"), "pageProps"), "game"), "data"), "game"));
}
json::Value gameJson(const Game& game) {
    auto result = object(); result.properties = {{"game_id", numeric(game.id)}, {"game_name", string(game.name)},
        {"comp_main", numeric(game.main)}, {"comp_plus", numeric(game.extra)}, {"comp_100", numeric(game.complete)}};
    return result;
}
std::string duration(uint32_t seconds, bool pl) {
    if (!seconds) return "—";
    const auto minutes = (static_cast<uint64_t>(seconds) + 30) / 60;
    if (minutes < 60) return std::to_string(std::max<uint64_t>(1, minutes)) + " min";
    return std::to_string(minutes / 60) + (pl ? " godz." : " h") + (minutes % 60 ? " " + std::to_string(minutes % 60) + " min" : "");
}
std::string endpoint(std::string_view script) {
    if (script.find("searchTerms") == std::string_view::npos || script.size() > limit) return {};
    static const std::regex call(R"hltb(fetch\s*\(\s*["'](/api/[A-Za-z0-9_/-]+)["']\s*,\s*\{[^}]{0,1024}method\s*:\s*["']POST["'])hltb");
    std::cmatch match;
    return std::regex_search(script.data(), script.data() + script.size(), match, call) ? match[1].str() : std::string{};
}
bool Client::discover() {
    const auto page = get(base + L"/", headers, limit);
    if (page.status != 200) return false;
    const auto html = body(page);
    static const std::regex scripts(R"hltb(src=["'](/_next/static/chunks/[^"'<>]+\.js)["'])hltb");
    int count = 0;
    for (std::sregex_iterator i(html.begin(), html.end(), scripts), end; i != end && count++ < 24; ++i) {
        const auto script = get(base + text_encoding::wide((*i)[1].str()), headers, limit);
        if (script.status == 200) if (auto path = endpoint(body(script)); !path.empty()) { endpoint_ = std::move(path); return true; }
    }
    return false;
}
Result Client::search(const std::string& name) {
    if (name.empty() || name.size() > 512) return {};
    HttpBudget budget(30000);
    if (endpoint_.empty() && !discover()) return {{}, false, "Search endpoint unavailable"};
    const auto init = get(base + text_encoding::wide(endpoint_) + L"/init", headers, limit);
    const auto auth = init.status == 200 ? json::parse(body(init)) : std::nullopt;
    if (!auth) { endpoint_.clear(); return {{}, false, "Search initialization failed (HTTP " + std::to_string(init.status) + ")"}; }
    std::string key, value;
    for (const auto& [field_name, item] : auth->properties) {
        auto lower = field_name; std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lower.find("key") != std::string::npos) key = item.stringOr();
        else if (lower.find("val") != std::string::npos) value = item.stringOr();
    }
    const auto token = text(*auth, "token");
    if (!headerValue(token) || (!key.empty() && (!headerValue(key) || !headerValue(value)))) return {{}, false, "Invalid search initialization"};
    auto request = json::parse(R"({"searchType":"games","searchTerms":[],"searchPage":1,"size":20,"searchOptions":{"games":{"userId":0,"platform":"","sortCategory":"popular","rangeCategory":"main","rangeTime":{"min":0,"max":0},"gameplay":{"perspective":"","flow":"","genre":"","difficulty":""},"rangeYear":{"max":"","min":""},"modifier":""},"users":{"sortCategory":"postcount"},"lists":{"sortCategory":"follows"},"filter":"","sort":0,"randomizer":0},"useCache":true})");
    std::istringstream words(name); std::string word;
    while (words >> word) request->properties["searchTerms"].elements.push_back(string(word));
    auto h = headers + L"Content-Type: application/json\r\nx-auth-token: " + text_encoding::wide(token) + L"\r\n";
    if (!key.empty()) {
        request->properties[key] = string(value);
        h += L"x-hp-key: " + text_encoding::wide(key) + L"\r\nx-hp-val: " + text_encoding::wide(value) + L"\r\n";
    }
    const auto reply = post(base + text_encoding::wide(endpoint_), h, encode(*request), limit);
    if (reply.status != 200) { endpoint_.clear(); return {{}, false, "Search failed (HTTP " + std::to_string(reply.status) + ")"}; }
    auto result = parseResults(body(reply));
    if (!result.valid) result.error = "Invalid search response";
    return result;
}
Result Client::game(uint32_t id) {
    if (!id) return {};
    HttpBudget budget(15000);
    const auto reply = get(base + L"/game/" + std::to_wstring(id), headers, limit);
    auto result = reply.status == 200 ? parseGamePage(body(reply)) : Result{};
    if (!result.valid || result.games.size() != 1 || result.games[0].id != id) return {{}, false, "Game data unavailable (HTTP " + std::to_string(reply.status) + ")"};
    return result;
}
}
