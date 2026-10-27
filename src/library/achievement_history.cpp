#include "achievement_history.hpp"
#include "../net/json.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <set>
#include <span>

namespace achievement_history {
namespace {
constexpr size_t max_file = 32 * 1024 * 1024;
std::wstring clip(std::wstring text, size_t limit) { if (text.size() > limit) text.resize(limit); return text; }
std::string field(const json::Value& value, const char* key) { const auto* found = value.get(key); return found ? found->stringOr() : std::string{}; }
uint64_t number(const json::Value& value, const char* key) {
    const auto* found = value.get(key);
    return found && found->type == json::Value::Type::number && found->numeric >= 0 && found->numeric < 1.8e19 ? static_cast<uint64_t>(found->numeric) : 0;
}
void order(History& history) {
    std::stable_sort(history.unlocks.begin(), history.unlocks.end(), [](const Unlock& a, const Unlock& b) { return a.at > b.at; });
    if (history.unlocks.size() > max_unlocks) history.unlocks.resize(max_unlocks);
}
std::pair<int, int> monthOf(uint64_t unix_seconds) {
    const time_t time = static_cast<time_t>(unix_seconds);
    tm local{};
    if (localtime_s(&local, &time)) return {1970, 1};
    return {local.tm_year + 1900, local.tm_mon + 1};
}
} // namespace

size_t merge(History& history, const std::string& game, const std::wstring& name, const std::vector<Unlock>& unlocked, Progress progress) {
    if (game.empty() || game.size() > 200) return 0;
    if (!name.empty()) history.names[game] = clip(name, 200);
    if (progress.total) history.progress[game] = {std::min(progress.unlocked, progress.total), progress.total};
    std::set<std::string> known;
    for (const auto& item : history.unlocks) if (item.game == game) known.insert(item.id);
    size_t added = 0;
    for (auto item : unlocked) {
        if (!item.at || item.id.empty() || item.id.size() > 200 || !known.insert(item.id).second) {
            // Known: a fresher name, icon or rarity replaces the old one, its time stays.
            if (!item.id.empty()) for (auto& old : history.unlocks)
                if (old.game == game && old.id == item.id) {
                    if (!item.name.empty()) old.name = clip(item.name, 300);
                    if (!item.description.empty()) old.description = clip(item.description, 600);
                    if (!item.icon.empty()) old.icon = clip(item.icon, 1000);
                    if (item.rarity >= 0) old.rarity = item.rarity;
                }
            continue;
        }
        item.game = game;
        item.name = clip(item.name, 300); item.description = clip(item.description, 600); item.icon = clip(item.icon, 1000);
        history.unlocks.push_back(std::move(item));
        ++added;
    }
    order(history);
    return added;
}

std::string format(const History& history) {
    auto root = json::object();
    root.properties["version"] = json::number(1);
    auto games = json::object();
    for (const auto& [key, name] : history.names) {
        auto entry = json::object();
        entry.properties["name"] = json::string(text_encoding::utf8Lenient(name));
        if (const auto found = history.progress.find(key); found != history.progress.end()) {
            entry.properties["unlocked"] = json::number(found->second.unlocked);
            entry.properties["total"] = json::number(found->second.total);
        }
        games.properties[key] = std::move(entry);
    }
    for (const auto& [key, value] : history.progress) {
        if (history.names.contains(key)) continue;
        auto entry = json::object();
        entry.properties["unlocked"] = json::number(value.unlocked);
        entry.properties["total"] = json::number(value.total);
        games.properties[key] = std::move(entry);
    }
    root.properties["games"] = std::move(games);
    auto list = json::array();
    for (const auto& item : history.unlocks) {
        auto entry = json::object();
        entry.properties["g"] = json::string(item.game);
        entry.properties["id"] = json::string(item.id);
        entry.properties["n"] = json::string(text_encoding::utf8Lenient(item.name));
        if (!item.description.empty()) entry.properties["d"] = json::string(text_encoding::utf8Lenient(item.description));
        if (!item.icon.empty()) entry.properties["i"] = json::string(text_encoding::utf8Lenient(item.icon));
        entry.properties["t"] = json::number(static_cast<double>(item.at));
        if (item.rarity >= 0) entry.properties["r"] = json::number(item.rarity);
        list.elements.push_back(std::move(entry));
    }
    root.properties["unlocks"] = std::move(list);
    auto scanned = json::object();
    for (const auto& [key, stamp] : history.scanned) scanned.properties[key] = json::string(std::to_string(stamp));
    root.properties["scanned"] = std::move(scanned);
    return json::serialize(root);
}

History parse(std::string_view text) {
    History history;
    const auto root = json::parse(text, max_file);
    if (!root || root->type != json::Value::Type::object) return history;
    if (const auto* version = root->get("version"); !version || version->intOr() != 1) return history;
    if (const auto* games = root->get("games"); games && games->type == json::Value::Type::object)
        for (const auto& [key, entry] : games->properties) {
            if (key.empty() || key.size() > 200) continue;
            if (const auto name = text_encoding::wideLenient(field(entry, "name")); !name.empty()) history.names[key] = clip(name, 200);
            const auto total = static_cast<uint32_t>(std::min<uint64_t>(number(entry, "total"), 100000));
            if (total) history.progress[key] = {static_cast<uint32_t>(std::min<uint64_t>(number(entry, "unlocked"), total)), total};
        }
    if (const auto* list = root->get("unlocks"); list && list->type == json::Value::Type::array) {
        std::set<std::pair<std::string, std::string>> seen;
        for (const auto& entry : list->elements) {
            Unlock item;
            item.game = field(entry, "g"); item.id = field(entry, "id");
            item.at = number(entry, "t");
            if (item.game.empty() || item.id.empty() || !item.at || !seen.insert({item.game, item.id}).second) continue;
            item.name = clip(text_encoding::wideLenient(field(entry, "n")), 300);
            item.description = clip(text_encoding::wideLenient(field(entry, "d")), 600);
            item.icon = clip(text_encoding::wideLenient(field(entry, "i")), 1000);
            if (const auto* rarity = entry.get("r"); rarity && rarity->type == json::Value::Type::number && rarity->numeric >= 0 && rarity->numeric <= 100)
                item.rarity = static_cast<float>(rarity->numeric);
            history.unlocks.push_back(std::move(item));
            if (history.unlocks.size() >= max_unlocks) break;
        }
    }
    if (const auto* scanned = root->get("scanned"); scanned && scanned->type == json::Value::Type::object)
        for (const auto& [key, stamp] : scanned->properties) {
            uint64_t value = 0;
            const auto digits = stamp.stringOr();
            if (digits.empty() || digits.size() > 20 || !std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; })) continue;
            for (const char c : digits) value = value * 10 + static_cast<uint64_t>(c - '0');
            history.scanned[key] = value;
        }
    order(history);
    return history;
}

History load(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() <= 0 || input.tellg() > static_cast<std::streamoff>(max_file)) return {};
    std::string text(static_cast<size_t>(input.tellg()), '\0');
    input.seekg(0);
    if (!input.read(text.data(), static_cast<std::streamsize>(text.size()))) return {};
    return parse(text);
}

bool save(const History& history, const std::filesystem::path& file) {
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    const auto text = format(history);
    return atomic_file::write(file, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
}

std::array<Month, 12> monthly(const History& history, int year, int month) {
    std::array<Month, 12> result{};
    for (int i = 0; i < 12; ++i) {
        int y = year, m = month - (11 - i);
        while (m < 1) { m += 12; --y; }
        result[static_cast<size_t>(i)] = {y, m, 0};
    }
    for (const auto& item : history.unlocks) {
        const auto [y, m] = monthOf(item.at);
        for (auto& slot : result) if (slot.year == y && slot.month == m) { ++slot.count; break; }
    }
    return result;
}

uint32_t since(const History& history, uint64_t unix_seconds) {
    return static_cast<uint32_t>(std::count_if(history.unlocks.begin(), history.unlocks.end(), [&](const Unlock& item) { return item.at >= unix_seconds; }));
}

std::vector<Completion> completion(const std::map<std::string, Progress>& progress) {
    std::vector<Completion> result;
    for (const auto& [key, value] : progress)
        if (value.total && value.unlocked) result.push_back({key, std::min(value.unlocked, value.total), value.total});
    std::stable_sort(result.begin(), result.end(), [](const Completion& a, const Completion& b) {
        const float sa = a.share(), sb = b.share();
        if (sa != sb) return sa > sb;
        return a.unlocked > b.unlocked;
    });
    return result;
}
} // namespace achievement_history
