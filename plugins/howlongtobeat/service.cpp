#include "service.hpp"
#include <algorithm>

namespace hltb {
using namespace plugins::wire;
namespace {
json::Value number(uint64_t n) { json::Value out; out.type = json::Value::Type::number; out.numeric = static_cast<double>(n); return out; }
std::optional<uint64_t> playtime(const json::Value& params) {
    const auto& v = field(params, "playtimeSeconds");
    if (v.type != json::Value::Type::number || !std::isfinite(v.numeric) || v.numeric < 0 || v.numeric > 9007199254740991.0 || std::floor(v.numeric) != v.numeric) return {};
    return static_cast<uint64_t>(v.numeric);
}
std::wstring normalized(const std::string& name) {
    std::wstring out; bool space = false;
    for (const auto ch : foldGameTitle(text_encoding::wide(name))) {
        if (ch == L'™' || ch == L'®' || ch == L'©') continue;
        if (iswspace(ch)) { space = !out.empty(); continue; }
        if (space) out += L' ';
        space = false; out += ch;
    }
    return out;
}
std::string userTime(std::optional<uint64_t> value, bool pl) {
    if (!value) return "—";
    if (*value < 60) return "0 min";
    return duration(static_cast<uint32_t>(std::min<uint64_t>(*value, UINT32_MAX)), pl);
}
std::string percent(std::optional<uint64_t> played, uint32_t reference) {
    return played && reference ? std::to_string(static_cast<uint64_t>(std::min(1e12, std::round(static_cast<double>(*played) * 100 / reference)))) + "%" : "—";
}
}
uint64_t Service::now() { return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()); }
std::filesystem::path Service::file(const Context& game) const { return data_ / (key_codec::encode(game.key) + ".json"); }
Service::Cache Service::load(const Context& game) const {
    Cache out;
    std::ifstream in(file(game), std::ios::binary | std::ios::ate);
    if (!in || in.tellg() < 1 || in.tellg() > 128 * 1024) return out;
    std::string bytes(static_cast<size_t>(in.tellg()), '\0'); in.seekg(0);
    if (!in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) return out;
    const auto cache = json::parse(bytes);
    if (!cache || text(*cache, "name") != game.name) return out;
    const auto result = parseResults(bytes);
    if (!result.valid) return out;
    out.matches = result.games;
    const auto id = text(*cache, "selected"), time = text(*cache, "fetched");
    const auto selected = std::from_chars(id.data(), id.data() + id.size(), out.selected);
    const auto fetched = std::from_chars(time.data(), time.data() + time.size(), out.fetched);
    if (selected.ec != std::errc{} || selected.ptr != id.data() + id.size()) out.selected = 0;
    if (fetched.ec != std::errc{} || fetched.ptr != time.data() + time.size()) out.fetched = 0;
    return out;
}
bool Service::save(const Context& game, const Cache& cache) const {
    auto value = object(); value.properties["name"] = string(game.name);
    value.properties["selected"] = string(std::to_string(cache.selected)); value.properties["fetched"] = string(std::to_string(cache.fetched));
    auto& list = value.properties["data"]; list.type = json::Value::Type::array;
    for (const auto& entry : cache.matches) list.elements.push_back(gameJson(entry));
    return atomic_file::write(file(game), encode(value));
}
const Game* Service::selected(const Cache& cache) {
    for (const auto& game : cache.matches) if (game.id == cache.selected) return &game;
    return nullptr;
}
bool Service::autoMatch(Cache& cache, const std::string& name) {
    if (selected(cache)) return false; // A manual choice is never replaced.
    const auto title = normalized(name); uint32_t found = 0;
    for (const auto& game : cache.matches) if (normalized(game.name) == title) {
        if (found) return false; // Multiple equal names require an explicit choice.
        found = game.id;
    }
    if (!found) return false;
    cache.selected = found; return true;
}
std::string Service::update(const Context& game, Cache& cache, bool force, bool pl) {
    const auto time = now();
    const bool expired = !cache.fetched || cache.fetched > time || time - cache.fetched > 7 * 86400;
    if (!force && !expired) return {};
    if (!force && retry_after_.contains(game.key) && retry_after_.at(game.key) > time)
        return pl ? "Dane chwilowo niedostępne" : "Data temporarily unavailable";
    const auto result = cache.selected ? client.game(cache.selected) : client.search(game.name);
    if (!result.valid) {
        if (retry_after_.size() > 128) retry_after_.clear();
        retry_after_[game.key] = time + 60;
        return pl ? "Dane chwilowo niedostępne" : "Data temporarily unavailable";
    }
    retry_after_.erase(game.key);
    cache.matches = result.games; cache.fetched = time;
    if (!selected(cache)) cache.selected = 0;
    return save(game, cache) ? std::string{} : pl ? "Nie można zapisać danych" : "Could not save data";
}
json::Value Service::comparison(const Context& game, const Cache& cache, bool pl, std::string status) {
    auto result = object(); result.properties["gameKey"] = string(game.key);
    if (const auto* match = selected(cache)) {
        result.properties["mainSeconds"] = number(match->main);
        result.properties["extraSeconds"] = number(match->extra);
        result.properties["completionSeconds"] = number(match->complete);
        if (!match->main && status.empty()) status = pl ? "Brak czasu głównej fabuły" : "No main story time";
    } else if (status.empty()) status = cache.matches.empty() ? (pl ? "Nie znaleziono gry" : "No games found") : (pl ? "Wybierz dopasowanie" : "Choose a match");
    result.properties["status"] = string(std::move(status)); return result;
}
json::Value Service::page(const Context& game, const Cache& cache, bool pl, const std::string& status) {
    auto view = object(); view.properties["title"] = string("HowLongToBeat"); view.properties["status"] = string(status);
    auto section = object(); section.properties["title"] = string(game.name);
    section.properties["empty"] = string(pl ? "Nie znaleziono gry" : "No games found");
    json::Value items; items.type = json::Value::Type::array;
    const auto add = [&](std::string title, std::string subtitle, std::string command = {}, std::string uri = {}) {
        auto row = object(); row.properties["title"] = string(std::move(title)); row.properties["subtitle"] = string(std::move(subtitle));
        if (!command.empty()) row.properties["command"] = string(std::move(command));
        if (!uri.empty()) row.properties["uri"] = string(std::move(uri));
        items.elements.push_back(std::move(row));
    };
    if (const auto* match = selected(cache)) {
        add(pl ? "Twój czas gry" : "Your playtime", userTime(game.played, pl));
        add(pl ? "Główna fabuła" : "Main story", duration(match->main, pl));
        add(pl ? "Czas względem głównej fabuły" : "Time vs main story", percent(game.played, match->main));
        add(pl ? "Fabuła i dodatki" : "Main + extras", duration(match->extra, pl));
        add(pl ? "Ukończenie 100%" : "Completionist", duration(match->complete, pl));
        add(pl ? "Otwórz HowLongToBeat" : "Open HowLongToBeat", "", {}, "https://howlongtobeat.com/game/" + std::to_string(match->id));
        add(pl ? "Zmień dopasowanie" : "Change match", match->name, "search");
    } else {
        for (const auto& candidate : cache.matches)
            add(candidate.name, (pl ? "Fabuła: " : "Story: ") + duration(candidate.main, pl) + " · 100%: " + duration(candidate.complete, pl), "pick." + std::to_string(candidate.id));
        if (cache.matches.empty()) add(pl ? "Otwórz HowLongToBeat" : "Open HowLongToBeat", "", {}, "https://howlongtobeat.com/");
    }
    section.properties["items"] = std::move(items);
    auto& sections = view.properties["sections"]; sections.type = json::Value::Type::array; sections.elements.push_back(std::move(section));
    return view;
}
json::Value Service::dispatch(const json::Value& input) {
    const auto method = text(input, "method"); const auto& params = field(input, "params");
    auto response = object(); const bool pl = text(input, "locale") == "pl";
    if (method == "activate") { data_ = text_encoding::wide(text(params, "dataDirectory")); return response; }
    if (method == "deactivate") { data_.clear(); page_game_ = {}; retry_after_.clear(); return response; }
    if (method != "command" && method != "details" && method != "widget") return response;
    const auto& dto = field(params, "game"); Context game{text(dto, "key"), text(dto, "name"), playtime(params)};
    const bool valid = !game.key.empty() && game.key.size() <= 1024 && !game.name.empty() && game.name.size() <= 512;
    if (method == "command") {
        if (valid) page_game_ = game;
        else game = page_game_;
    } // Background details/widgets never change the game of an open match-selection page.
    if (game.key.empty()) {
        response.properties["text"] = string(pl ? "Brak aktywnej gry" : "No active game");
        if (method == "command") response.properties["message"] = response.properties["text"];
        return response;
    }
    if (!valid && method != "command") return response;
    auto cache = load(game); const auto command = text(params, "command"); std::string status;
    if (method == "widget" && !cache.fetched) {
        const bool failed = retry_after_.contains(game.key) && retry_after_.at(game.key) > now();
        status = failed ? (pl ? "Dane chwilowo niedostępne" : "Data temporarily unavailable") : (pl ? "Ładowanie danych" : "Loading data");
    }
    if (method == "command" && command == "search") { cache.selected = 0; cache.fetched = 0; }
    if (method == "command" && command.starts_with("pick.")) {
        const auto id = command.substr(5); uint32_t choice = 0;
        const auto parsed = std::from_chars(id.data(), id.data() + id.size(), choice);
        if (parsed.ec == std::errc{} && parsed.ptr == id.data() + id.size() && std::any_of(cache.matches.begin(), cache.matches.end(), [&](const auto& item) { return item.id == choice; })) {
            cache.selected = choice;
            if (!save(game, cache)) status = pl ? "Nie można zapisać dopasowania" : "Could not save the match";
        }
    } else if (method != "widget") status = update(game, cache, field(params, "refresh").flag || (method == "command" && command == "search"), pl);
    if (method != "command" && autoMatch(cache, game.name)) save(game, cache);
    response.properties["comparison"] = comparison(game, cache, pl, status);
    if (method == "command") response.properties["drawer"] = page(game, cache, pl, status);
    if (method == "widget") {
        const auto* match = selected(cache);
        response.properties["text"] = string(game.name + "\n" + (match ? userTime(game.played, pl) + " / " + duration(match->main, pl) + " · " + percent(game.played, match->main) : text(response.properties["comparison"], "status")));
    }
    return response;
}
}
