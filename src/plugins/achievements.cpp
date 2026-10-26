#include "achievements.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <set>
#include <stdexcept>

namespace plugins {
using namespace wire;
namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
std::wstring label(const json::Value& item, const char* key, size_t limit) {
    const auto* value = item.get(key);
    if (!value || value->type == json::Value::Type::null_value) return {};
    require(value->type == json::Value::Type::string && value->text.size() <= limit && value->text.find('\0') == std::string::npos, "Invalid achievement text");
    return text_encoding::wideLenient(value->text);
}
std::wstring lowered(std::wstring_view text) {
    std::wstring out(text);
    for (auto& c : out) if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
    return out;
}
// The URL's host, lower case; empty when it is not https://host/…
std::wstring host(std::wstring_view url) {
    if (!url.starts_with(L"https://")) return {};
    auto rest = url.substr(8);
    const auto end = rest.find_first_of(L"/?#");
    auto name = rest.substr(0, end);
    if (name.empty() || name.find_first_of(L"@:\\ ") != std::wstring_view::npos) return {};
    return lowered(name);
}
bool under(std::wstring_view name, std::wstring_view domain) {
    return name == domain || (name.size() > domain.size() && name.ends_with(domain) && name[name.size() - domain.size() - 1] == L'.');
}
}

bool storeHostAllowed(std::string_view account, std::wstring_view url) {
    const auto name = host(url);
    if (name.empty() || url.size() > 4096) return false;
    if (account == "gog") return name == L"gameplay.gog.com" || name == L"api.gog.com" || name == L"embed.gog.com" || name == L"www.gog.com" || name == L"menu.gog.com";
    if (account == "epic") return under(name, L"epicgames.com") && name != L"epicgames.com";
    return false;
}

AchievementsAnswer parseAchievements(const json::Value& response) {
    AchievementsAnswer out;
    require(response.type == json::Value::Type::object, "Invalid achievements response");
    if (const auto* requests = response.get("storeRequests")) {
        require(requests->type == json::Value::Type::array && !requests->elements.empty() && requests->elements.size() <= max_store_requests, "Invalid store requests");
        std::set<std::string> ids;
        for (const auto& item : requests->elements) {
            StoreRequest request;
            request.id = text(item, "id"); request.account = text(item, "account");
            const auto method = text(item, "method");
            request.request.url = text_encoding::wide(text(item, "url"));
            request.request.body = text(item, "body");
            request.request.post = method == "POST";
            require(!request.id.empty() && request.id.size() <= 64 && ids.insert(request.id).second, "Invalid store request ID");
            require((method == "GET" || method == "POST") && (request.request.post || request.request.body.empty()), "Invalid store request method");
            require(request.request.body.size() <= 64 * 1024, "Store request body too large");
            require(storeHostAllowed(request.account, request.request.url), "Store request host not allowed");
            out.requests.push_back(std::move(request));
        }
        out.kind = AchievementsAnswer::Kind::requests;
        if (const auto* state = response.get("state")) out.state = *state;
        return out;
    }
    const auto* list = response.get("achievements");
    if (!list) {
        out.message = text_encoding::wideLenient(text(response, "message")).substr(0, 512);
        out.kind = out.message.empty() ? AchievementsAnswer::Kind::none : AchievementsAnswer::Kind::message;
        return out;
    }
    if (list->type == json::Value::Type::null_value) { out.kind = AchievementsAnswer::Kind::none; return out; }
    require(list->type == json::Value::Type::object, "Invalid achievements");
    out.source = label(*list, "source", 64);
    const auto& items = field(*list, "items");
    require(items.type == json::Value::Type::array && items.elements.size() <= max_achievements, "Invalid achievement items");
    std::set<std::string> ids;
    for (const auto& item : items.elements) {
        SteamAchievement a;
        a.id = text(item, "id");
        require(!a.id.empty() && a.id.size() <= 128 && ids.insert(a.id).second, "Invalid achievement ID");
        a.name = label(item, "name", 1024); a.description = label(item, "description", 4096);
        a.icon = label(item, "icon", 4096);
        // An https image, or a picture the plugin found on this computer (an emulator's trophy icons).
        if (!a.icon.empty() && !a.icon.starts_with(L"https://") &&
            !(std::filesystem::path(a.icon).is_absolute() && a.icon.find_first_of(std::wstring(L"\0\r\n", 3)) == std::wstring::npos)) a.icon.clear();
        a.achieved = field(item, "achieved").type == json::Value::Type::boolean && field(item, "achieved").flag;
        a.hidden = field(item, "hidden").type == json::Value::Type::boolean && field(item, "hidden").flag;
        if (const auto* at = item.get("unlockedAt"); at && at->type == json::Value::Type::number && std::isfinite(at->numeric) && at->numeric > 0 && at->numeric < 4102444800.0)
            a.unlocked_at = static_cast<uint64_t>(at->numeric);
        if (!a.achieved) a.unlocked_at = 0;
        if (const auto* share = item.get("percent"); share && share->type == json::Value::Type::number && std::isfinite(share->numeric) && share->numeric >= 0 && share->numeric <= 100)
            a.global_percent = static_cast<float>(share->numeric);
        out.list.items.push_back(std::move(a));
    }
    out.list.total = static_cast<uint32_t>(out.list.items.size());
    out.list.unlocked = static_cast<uint32_t>(std::count_if(out.list.items.begin(), out.list.items.end(), [](const SteamAchievement& a) { return a.achieved; }));
    out.list.complete = true;
    sortAchievements(out.list.items);
    out.kind = out.list.total ? AchievementsAnswer::Kind::list : AchievementsAnswer::Kind::none;
    return out;
}

json::Value storeResponses(const std::vector<std::pair<std::string, std::optional<HttpResponse>>>& answers) {
    json::Value out; out.type = json::Value::Type::array;
    for (const auto& [id, answer] : answers) {
        auto entry = object();
        entry.properties["id"] = string(id);
        json::Value status; status.type = json::Value::Type::number; status.numeric = answer ? answer->status : 0;
        entry.properties["status"] = status;
        // Text only (JSON or HTML); a binary body, or one that would not fit the plugin request
        // (LAUNCHME_PLUGIN_MAX_JSON for all of them), is dropped and marked truncated.
        std::string body = answer ? std::string(answer->body.begin(), answer->body.end()) : std::string{};
        const bool dropped = body.size() > 400 * 1024 || body.find('\0') != std::string::npos || (answer && answer->truncated);
        if (dropped) body.clear();
        entry.properties["body"] = string(std::move(body));
        entry.properties["truncated"] = boolean(dropped);
        out.elements.push_back(std::move(entry));
    }
    return out;
}
}
