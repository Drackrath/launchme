// GOG and Epic: the formats of their achievement APIs (as SuccessStory reads them).
#include "achievements.hpp"
#include "../../src/net/url_encoding.hpp"
#include "../../src/platform/text_encoding.hpp"

#include <map>

namespace achievements {
using namespace plugins::wire;
namespace {
double percentOf(const json::Value* value) {
    if (!value) return -1;
    double share = -1;
    if (value->type == json::Value::Type::number) share = value->numeric;
    else if (value->type == json::Value::Type::string) { try { share = std::stod(value->text); } catch (...) { share = -1; } }
    return share >= 0 && share <= 100 ? share : -1;
}
std::string identifier(const json::Value* value) {
    if (!value) return {};
    return value->type == json::Value::Type::string || value->type == json::Value::Type::number ? value->text : std::string{};
}
const json::Value& path(const json::Value& root, std::initializer_list<const char*> keys) {
    const json::Value* at = &root;
    for (const auto* key : keys) at = &field(*at, key);
    return *at;
}
std::string graphql(const std::string& query, const std::string& variables) {
    return "{\"query\":" + quote(query) + ",\"variables\":" + variables + "}";
}
}

std::wstring gogUrl(const std::string& product) {
    return L"https://gameplay.gog.com/clients/" + url_encoding::component(product) + L"/users/{account}/achievements";
}

std::optional<List> parseGog(std::string_view body) {
    const auto root = json::parse(body, 8 * 1024 * 1024);
    if (!root || root->type != json::Value::Type::object) return {};
    const auto& items = field(*root, "items");
    if (items.type != json::Value::Type::array) return {};
    List list; list.source = "GOG";
    for (const auto& entry : items.elements) {
        Item item;
        item.id = identifier(entry.get("achievement_key"));
        if (item.id.empty()) item.id = identifier(entry.get("achievement_id"));
        if (item.id.empty()) continue;
        item.name = text(entry, "name"); item.description = text(entry, "description");
        const auto date = text(entry, "date_unlocked");
        item.achieved = !date.empty();
        item.unlocked_at = item.achieved ? parseIsoTime(date) : 0;
        item.icon = text(entry, item.achieved ? "image_url_unlocked" : "image_url_locked");
        if (item.icon.empty()) item.icon = text(entry, "image_url_unlocked");
        item.percent = percentOf(entry.get("rarity"));
        const auto& visible = field(entry, "visible");
        item.hidden = visible.type == json::Value::Type::boolean && !visible.flag;
        list.items.push_back(std::move(item));
    }
    return list;
}

std::string epicSchemaQuery(const std::string& sandbox, const std::string& locale) {
    return graphql("query Achievement($sandboxId: String!, $locale: String!) { Achievement { productAchievementsRecordBySandbox(sandboxId: $sandboxId, locale: $locale) "
                   "{ productId sandboxId totalAchievements achievements { achievement { name hidden rarity { percent } unlockedDisplayName lockedDisplayName "
                   "unlockedDescription lockedDescription unlockedIconLink lockedIconLink XP } } } } }",
                   "{\"sandboxId\":" + quote(sandbox) + ",\"locale\":" + quote(locale) + "}");
}

std::string epicPlayerQuery(const std::string& product) {
    return graphql("query PlayerProfileAchievementsByProductId($epicAccountId: String!, $productId: String!) { PlayerProfile { playerProfile(epicAccountId: $epicAccountId) "
                   "{ productAchievements(productId: $productId) { __typename ... on PlayerProductAchievementsResponseSuccess { data { totalUnlocked "
                   "playerAchievements { playerAchievement { achievementName unlocked unlockDate } } } } } } } }",
                   "{\"epicAccountId\":\"{account}\",\"productId\":" + quote(product) + "}");
}

std::optional<EpicSchema> parseEpicSchema(std::string_view body) {
    const auto root = json::parse(body, 8 * 1024 * 1024);
    if (!root) return {};
    const auto& record = path(*root, {"data", "Achievement", "productAchievementsRecordBySandbox"});
    const auto& items = field(record, "achievements");
    if (record.type != json::Value::Type::object || items.type != json::Value::Type::array) return {};
    EpicSchema out;
    out.product = text(record, "productId");
    out.list.source = "Epic";
    for (const auto& entry : items.elements) {
        const auto& a = field(entry, "achievement");
        Item item;
        item.id = text(a, "name");
        if (item.id.empty()) continue;
        item.hidden = field(a, "hidden").type == json::Value::Type::boolean && field(a, "hidden").flag;
        // Locked until the player's answer: the locked texts and icon (a hidden one keeps its secret).
        item.name = text(a, "lockedDisplayName"); item.description = text(a, "lockedDescription");
        if (item.name.empty()) item.name = text(a, "unlockedDisplayName");
        if (item.description.empty() && !item.hidden) item.description = text(a, "unlockedDescription");
        item.icon = text(a, "lockedIconLink");
        if (item.icon.empty()) item.icon = text(a, "unlockedIconLink");
        const auto& share = path(a, {"rarity", "percent"});
        item.percent = percentOf(share.type == json::Value::Type::null_value ? nullptr : &share);
        out.unlocked.push_back({text(a, "unlockedDisplayName"), text(a, "unlockedDescription"), text(a, "unlockedIconLink")});
        out.list.items.push_back(std::move(item));
    }
    return out;
}

bool applyEpicPlayer(EpicSchema& schema, std::string_view body) {
    auto& list = schema.list;
    const auto root = json::parse(body, 8 * 1024 * 1024);
    if (!root) return false;
    const auto& answer = path(*root, {"data", "PlayerProfile", "playerProfile", "productAchievements"});
    if (answer.type != json::Value::Type::object) return false;
    const auto& unlocks = path(answer, {"data", "playerAchievements"});
    if (unlocks.type != json::Value::Type::array) return true; // No progress yet (or a private profile): all locked.
    std::map<std::string, uint64_t> earned;
    for (const auto& entry : unlocks.elements) {
        const auto& a = field(entry, "playerAchievement");
        if (field(a, "unlocked").type == json::Value::Type::boolean && field(a, "unlocked").flag)
            earned[text(a, "achievementName")] = parseIsoTime(text(a, "unlockDate"));
    }
    for (size_t i = 0; i < list.items.size(); ++i) {
        auto& item = list.items[i];
        const auto found = earned.find(item.id);
        if (found == earned.end()) continue;
        item.achieved = true; item.unlocked_at = found->second;
        if (i < schema.unlocked.size()) { // Earned: the unlocked name, text and picture.
            const auto& [name, description, icon] = schema.unlocked[i];
            if (!name.empty()) item.name = name;
            if (!description.empty()) item.description = description;
            if (!icon.empty()) item.icon = icon;
        }
    }
    return true;
}
}
