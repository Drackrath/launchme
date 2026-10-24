#include "companion_tools.hpp"
#include "companion_events.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace companion::tools {
namespace {
using namespace game_index;
std::string utf8(std::wstring_view text) { return text_encoding::utf8Lenient(text); }
std::wstring wide(std::string_view text) { return text_encoding::wideLenient(text); }
std::wstring textArg(const json::Value& args, const char* name) {
    const auto* value = args.get(name);
    return value && value->type == json::Value::Type::string ? wide(value->text) : std::wstring{};
}
std::vector<std::wstring> listArg(const json::Value& args, const char* name) {
    std::vector<std::wstring> result;
    const auto* value = args.get(name);
    if (!value) return result;
    if (value->type == json::Value::Type::string && !value->text.empty()) result.push_back(wide(value->text));
    if (value->type == json::Value::Type::array)
        for (const auto& item : value->elements) if (item.type == json::Value::Type::string && !item.text.empty() && result.size() < 6) result.push_back(wide(item.text));
    return result;
}
int intArg(const json::Value& args, const char* name, int fallback, int low, int high) {
    const auto* value = args.get(name);
    if (!value) return fallback;
    double number = fallback;
    if (value->type == json::Value::Type::number) number = value->numeric;
    else if (value->type == json::Value::Type::string) number = std::atof(value->text.c_str());
    if (!std::isfinite(number)) return fallback;
    return std::clamp(static_cast<int>(number), low, high);
}
json::Value error(const char* message) {
    auto out = json::object();
    out.properties["error"] = json::string(message);
    return out;
}
std::string join(const std::vector<std::wstring>& list, size_t limit) {
    std::string out;
    for (size_t i = 0; i < list.size() && i < limit; ++i) { if (i) out += ", "; out += utf8(list[i]); }
    return out;
}
std::string hours(uint32_t minutes) {
    if (minutes < 60) return std::to_string(minutes) + " min";
    const auto tenths = (minutes * 10 + 30) / 60;
    return std::to_string(tenths / 10) + (tenths % 10 && tenths < 1000 ? "." + std::to_string(tenths % 10) : "") + " h";
}
// Days from one yyyymmdd date to another (negative: before); 0 when either is not a date.
int daysUntil(int from, int to) {
    const auto date = [](int value) {
        return std::chrono::year_month_day{std::chrono::year{value / 10000}, std::chrono::month{static_cast<unsigned>(value / 100 % 100)},
                                           std::chrono::day{static_cast<unsigned>(value % 100)}};
    };
    const auto a = date(from), b = date(to);
    if (!a.ok() || !b.ok()) return 0;
    return static_cast<int>((std::chrono::sys_days{b} - std::chrono::sys_days{a}).count());
}
// plan_grania: the games in progress by what is left, then short ones that fit.
json::Value planFor(const Index& index, int minutes, const Context& context) {
    auto out = json::object();
    out.properties["time"] = json::string(hours(static_cast<uint32_t>(minutes)));
    auto going = json::array();
    std::vector<std::pair<const Record*, uint32_t>> left;
    for (const auto* record : inProgress(index, context.now, 12))
        left.push_back({record, *record->hltb_main > *record->playtime_minutes ? *record->hltb_main - *record->playtime_minutes : 15});
    std::sort(left.begin(), left.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
    for (size_t i = 0; i < left.size() && i < 5; ++i) {
        auto item = describe(*left[i].first, context);
        item.properties["story_left_about"] = json::string(hours(left[i].second));
        item.properties["could_finish_now"] = json::boolean(left[i].second <= static_cast<uint32_t>(minutes));
        going.elements.push_back(std::move(item));
    }
    out.properties["in_progress"] = std::move(going);
    // Never played (or barely), with a main story that fits; installed ones first, then the best rated.
    std::vector<const Record*> fits;
    for (const auto& key : index.keys()) {
        const auto* record = index.get(key);
        if (!record || !record->owned || !record->hltb_main || *record->hltb_main > static_cast<uint32_t>(minutes) || *record->hltb_main < 20) continue;
        if (record->playtime_minutes && *record->playtime_minutes >= 15) continue;
        fits.push_back(record);
    }
    std::sort(fits.begin(), fits.end(), [](const Record* a, const Record* b) {
        if (a->installed != b->installed) return a->installed;
        return ratingScore(*a) > ratingScore(*b);
    });
    auto short_games = json::array();
    for (size_t i = 0; i < fits.size() && i < 6; ++i) short_games.elements.push_back(describe(*fits[i], context));
    out.properties["short_unplayed"] = std::move(short_games);
    if (left.empty() && fits.empty()) out.properties["note"] = json::string("Nothing with a known HowLongToBeat time fits; suggest from szukaj_w_bibliotece instead.");
    return out;
}
Query queryFrom(const json::Value& args, const Context& context) {
    Query query;
    query.text = textArg(args, "tekst");
    query.genres = listArg(args, "gatunki");
    query.tags = listArg(args, "tagi");
    query.developer = textArg(args, "tworca");
    query.store = textArg(args, "sklep");
    query.year_from = intArg(args, "rok_od", 0, 0, 2100);
    query.year_to = intArg(args, "rok_do", 0, 0, 2100);
    query.status = statusFromKey(utf8(textArg(args, "stan"))).value_or(Status::any);
    query.sort = sortFromKey(utf8(textArg(args, "sortuj"))).value_or(Sort::relevance);
    query.limit = static_cast<size_t>(intArg(args, "limit", 8, 1, 20));
    query.now = context.now;
    query.seed = context.seed;
    return query;
}
}

std::string declarations(bool notes) { return declarations(Features{notes}); }

std::string declarations(const Features& features) {
    const bool notes = features.notes;
    std::string out = R"([
{"name":"szukaj_w_bibliotece","description":"Search the game library of the person you are chatting with (their own local launcher data). Use it for any question about which games they have: oldest/newest, by genre, tag, studio, year, store, installed or never played, shortest (HowLongToBeat), best rated, recently played, random picks. Returns matching games with release date, genres, tags, studio, play time and scores, plus the total count.","parameters":{"type":"object","properties":{
"tekst":{"type":"string","description":"Words of a title, series or studio."},
"gatunki":{"type":"array","items":{"type":"string"},"description":"Genres, e.g. RPG, Strategy, Adventure, Action, Simulation (English or Polish). Each must match."},
"tagi":{"type":"array","items":{"type":"string"},"description":"Steam-style tags, e.g. Roguelike, Open World, Co-op, Horror, Pixel Graphics, Story Rich, Metroidvania."},
"tworca":{"type":"string","description":"Developer or publisher."},
"sklep":{"type":"string","description":"Store or platform: Steam, GOG, Epic, Battle.net, Xbox, a console name."},
"rok_od":{"type":"integer","description":"Released in or after this year."},
"rok_do":{"type":"integer","description":"Released in or before this year."},
"stan":{"type":"string","enum":["any","installed","not_installed","never_played","played","recent","completed"],"description":"recent = played in the last 30 days; completed = every achievement."},
"sortuj":{"type":"string","enum":["relevance","oldest","newest","last_played","playtime","rating","metacritic","shortest","longest","added","random","title"],"description":"oldest/newest by release date; shortest/longest by HowLongToBeat main story; added = newest in the library."},
"limit":{"type":"integer","description":"How many games to return, 1-20 (default 8)."}}}},
{"name":"szczegoly_gry","description":"Everything the launcher knows about one game in the library of the person you are chatting with: release date, studio, publisher, series, genres, tags, description, play time, achievements, HowLongToBeat times, Steam/RAWG/Metacritic scores. Lists close matches when the title is ambiguous.","parameters":{"type":"object","properties":{
"tytul":{"type":"string","description":"The game's title (approximate is fine)."}},"required":["tytul"]}},
{"name":"statystyki","description":"Statistics of the game library of the person you are chatting with: totals (games, installed, played, never played, completed, hours played) and the largest groups by genre, tag, developer, publisher, series, year, decade, store or status. Optional filters like szukaj_w_bibliotece.","parameters":{"type":"object","properties":{
"grupuj":{"type":"string","enum":["genre","tag","developer","publisher","franchise","year","decade","store","status"]},
"gatunki":{"type":"array","items":{"type":"string"}},"tagi":{"type":"array","items":{"type":"string"}},
"stan":{"type":"string","enum":["any","installed","not_installed","never_played","played","recent","completed"]},
"limit":{"type":"integer","description":"How many groups, 1-20 (default 10)."}},"required":["grupuj"]}},
{"name":"podobne_gry","description":"Games most like a given one in the library of the person you are chatting with (shared genres, tags, studio, series).","parameters":{"type":"object","properties":{
"tytul":{"type":"string"},"limit":{"type":"integer","description":"1-10 (default 5)."}},"required":["tytul"]}})";
    if (notes)
        out += R"(,
{"name":"zapamietaj","description":"Remember one short, lasting fact about the person you are chatting with for future conversations (a favourite genre, a game they love or hate, their name, a joke, a goal) along with how it made YOU feel in character.","parameters":{"type":"object","properties":{
"notatka":{"type":"string","description":"The fact about them, one short sentence in their language."},
"emocja":{"type":"string","enum":["smile","angry","sad","excited","shock","thinking","smug","shy","fear","sleepy","playful","cool","love","laugh","cry","confused","determined"],"description":"How that moment made you feel."},
"odczucie":{"type":"string","description":"One short phrase in first person on how you personally felt or reacted to it in character (e.g. flustered, jealous, amused, proud)."}},"required":["notatka"]}})";
    if (features.recall)
        out += R"(,
{"name":"przypomnij","description":"Look through your earlier conversations with the person you are talking to (and your notes after them) for a topic, when they refer to something you talked about before or you want to come back to it. Returns the matching lines with when they were said.","parameters":{"type":"object","properties":{
"temat":{"type":"string","description":"A few key words of the topic, e.g. a game, a person, a plan."}},"required":["temat"]}})";
    if (features.news)
        out += R"(,
{"name":"wiadomosci","description":"What is new in gaming, from the launcher's news tab (no web search): news and updates of the Steam games owned by the person you are chatting with, game premieres of the coming days, and the latest industry headlines. Use it when they ask what's new or for small talk.","parameters":{"type":"object","properties":{}}})";
    if (features.plan)
        out += R"(,
{"name":"plan_grania","description":"Plan a gaming session for the time available to the person you are chatting with: the games they are in the middle of with how much is left (HowLongToBeat main story), and short games from their library that fit into that time.","parameters":{"type":"object","properties":{
"minuty":{"type":"integer","description":"The time they have, in minutes (e.g. 60 for an hour, 600 for a weekend day)."}},"required":["minuty"]}})";
    if (features.web)
        out += R"(,
{"name":"szukaj_w_sieci","description":"Search the web. Use it when the person you are chatting with asks you to check something online (e.g. 'sprawdź w necie', 'w internecie', 'w sieci', 'search the web'), or for a hard factual question the library and the news tab cannot answer: release dates, patches, prices, guides, current facts. Answer only from what it returns; it gives a short answer and a few sources.","parameters":{"type":"object","properties":{
"zapytanie":{"type":"string","description":"What to search for, a concise web query (English often finds more)."}},"required":["zapytanie"]}})";
    if (features.achievements)
        out += R"(,
{"name":"osiagniecia_gry","description":"Inspect the achievements of the person you are chatting with in one Steam game: freshly unlocked achievements (from recent days), their rarest unlocked trophies (global share under 10%), and the easiest missing ones to reach 100%. Use it lightly as a fun observation, never to recite old achievements.","parameters":{"type":"object","properties":{
"tytul":{"type":"string","description":"The game's title in their library."}},"required":["tytul"]}})";
    if (features.friends)
        out += R"(,
{"name":"znajomi_online","description":"Check which Steam friends are currently online or playing a game right now, and whether the person you are chatting with owns/has installed that game (especially online/co-op/multiplayer games they could join).","parameters":{"type":"object","properties":{}}})";
    if (features.collections)
        out += R"(,
{"name":"kolekcje_i_ulubione","description":"List the Steam collections and favorites of the person you are chatting with, or create/update a collection or favorites from games in their library. By default creates and edits a Steam library collection (synced with Steam).","parameters":{"type":"object","properties":{
"akcja":{"type":"string","enum":["list","create","add","remove","favorite","unfavorite"],"description":"list = see collections and favorites; create/add = create or add games to a Steam collection; remove = remove games from a collection; favorite/unfavorite = toggle Ulubione."},
"nazwa":{"type":"string","description":"Name of the collection (required for create, add, remove)."},
"gry":{"type":"array","items":{"type":"string"},"description":"Titles of games from their library to add, remove, or favorite."},
"typ":{"type":"string","enum":["steam","local"],"description":"steam (default) = a Steam library collection; local = Launchme-only collection."}},"required":["akcja"]}})";
    if (features.history)
        out += R"(,
{"name":"historia_grania","description":"See what the person you are chatting with played in the last N days: games actively played, games barely started and put down (under 45 min), and recently completed games.","parameters":{"type":"object","properties":{
"dni":{"type":"integer","description":"How many past days to inspect, 1-90 (default 14)."}}}})";
    if (features.roulette)
        out += R"(,
{"name":"zakrec_ruletka","description":"Spin the visual game roulette wheel in the chat using optional filters (genre, tag, studio, store, max HowLongToBeat minutes, status). Returns the exact game the wheel lands on so you can announce it.","parameters":{"type":"object","properties":{
"gatunki":{"type":"array","items":{"type":"string"},"description":"Optional genres to filter the roulette pool."},
"tagi":{"type":"array","items":{"type":"string"},"description":"Optional Steam-style tags to filter the roulette pool."},
"tworca":{"type":"string","description":"Optional developer or publisher."},
"sklep":{"type":"string","description":"Optional store or platform."},
"max_minut":{"type":"integer","description":"Optional maximum HowLongToBeat main story length in minutes."},
"stan":{"type":"string","enum":["installed","never_played","any"],"description":"Defaults to installed."}}}})";
    out += "]";
    return out;
}

bool needsIndex(std::string_view name) { return name != recall && name != news && name != remember && name != web; }

json::Value recallAnswer(const std::vector<Recalled>& lines, const std::vector<Recalled>& summaries) {
    auto out = json::object();
    const auto list = [](const std::vector<Recalled>& items, bool speaker) {
        auto array = json::array();
        for (const auto& item : items) {
            auto entry = json::object();
            entry.properties["when"] = json::string(utf8(item.when));
            if (speaker) entry.properties["who"] = json::string(item.player ? "them" : "you"); // "them": the person she chats with.
            entry.properties["text"] = json::string(utf8(item.text));
            array.elements.push_back(std::move(entry));
        }
        return array;
    };
    if (!lines.empty()) out.properties["conversations"] = list(lines, true);
    if (!summaries.empty()) out.properties["your_notes"] = list(summaries, false);
    if (lines.empty() && summaries.empty()) out.properties["note"] = json::string("Nothing about it in your earlier conversations; say you don't remember, in character.");
    return out;
}

json::Value newsAnswer(const std::vector<Headline>& owned, const std::vector<Headline>& premieres, const std::vector<Headline>& headlines, int today, uint64_t now) {
    const auto ago = [&](uint64_t published) -> std::string {
        if (!published || !now) return {};
        const auto days = now > published ? (now - published) / 86400 : 0;
        return days == 0 ? "today" : days == 1 ? "yesterday" : std::to_string(days) + " days ago";
    };
    const auto list = [&](const std::vector<Headline>& items, size_t limit) {
        auto array = json::array();
        for (size_t i = 0; i < items.size() && i < limit; ++i) {
            const auto& item = items[i];
            auto entry = json::object();
            entry.properties["title"] = json::string(utf8(item.title));
            if (!item.source.empty()) entry.properties["about"] = json::string(utf8(item.source));
            if (item.day) {
                const int days = daysUntil(today, item.day);
                entry.properties["release"] = json::string(days == 0 ? "today" : days == 1 ? "tomorrow" : days > 1 ? "in " + std::to_string(days) + " days" :
                                                           std::to_string(item.day / 10000) + "-" + std::to_string(item.day / 100 % 100) + "-" + std::to_string(item.day % 100));
            }
            if (const auto when = ago(item.published); !when.empty()) entry.properties["published"] = json::string(when);
            array.elements.push_back(std::move(entry));
        }
        return array;
    };
    auto out = json::object();
    if (!owned.empty()) out.properties["your_games"] = list(owned, 6);
    if (!premieres.empty()) out.properties["premieres"] = list(premieres, 8);
    if (!headlines.empty()) out.properties["headlines"] = list(headlines, 6);
    if (owned.empty() && premieres.empty() && headlines.empty())
        out.properties["note"] = json::string("No news yet: the launcher's news tab (Glance) has not loaded anything. Do not make news up.");
    else out.properties["note"] = json::string("Premieres and headlines are not necessarily in the library of the person you are chatting with. Mark game titles with [[Title]] for clickable links, but do not mark whole headlines or claim that person owns these games.");
    return out;
}

json::Value achievementsAnswer(std::wstring_view title, const AchievementSummary& summary, uint64_t now) {
    auto out = json::object();
    out.properties["game"] = json::string(utf8(title));
    out.properties["unlocked"] = json::number(summary.unlocked);
    out.properties["total"] = json::number(summary.total);
    if (summary.total) out.properties["percent"] = json::number(summary.unlocked * 100 / summary.total);
    if (summary.items.empty()) {
        out.properties["note"] = json::string(summary.total ? "Detailed achievement names are not cached locally for this game yet; only counts are known."
                                                            : "No Steam achievements cached locally for this game.");
        return out;
    }
    const auto pct = [](float value) {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%.1f%%", value);
        return std::string(buf);
    };
    const auto ago = [&](uint64_t unlocked_at) -> std::string {
        if (!unlocked_at || !now || unlocked_at > now + 86400) return {};
        const uint64_t days = now > unlocked_at ? (now - unlocked_at) / 86400 : 0;
        return days == 0 ? "today" : days == 1 ? "yesterday" : std::to_string(days) + " days ago";
    };
    // Freshly unlocked (last 30 days), newest first: ignore old unlocks unless rare.
    std::vector<const AchievementItem*> fresh, rare, locked;
    for (const auto& item : summary.items) {
        if (item.achieved) {
            if (now && item.unlocked_at && now >= item.unlocked_at && (now - item.unlocked_at) <= 30 * 86400) fresh.push_back(&item);
            if (item.global_percent >= 0.0f && item.global_percent <= 10.0f) rare.push_back(&item);
        } else if (!item.hidden) locked.push_back(&item);
    }
    std::sort(fresh.begin(), fresh.end(), [](const auto* a, const auto* b) { return a->unlocked_at > b->unlocked_at; });
    std::sort(rare.begin(), rare.end(), [](const auto* a, const auto* b) { return a->global_percent < b->global_percent; });
    std::sort(locked.begin(), locked.end(), [](const auto* a, const auto* b) { return a->global_percent > b->global_percent; });
    if (!fresh.empty()) {
        auto arr = json::array();
        for (size_t i = 0; i < fresh.size() && i < 5; ++i) {
            auto entry = json::object();
            entry.properties["name"] = json::string(utf8(fresh[i]->name));
            if (!fresh[i]->description.empty()) entry.properties["description"] = json::string(utf8(fresh[i]->description));
            if (const auto when = ago(fresh[i]->unlocked_at); !when.empty()) entry.properties["unlocked"] = json::string(when);
            if (fresh[i]->global_percent >= 0) entry.properties["players_with_it"] = json::string(pct(fresh[i]->global_percent));
            arr.elements.push_back(std::move(entry));
        }
        out.properties["recent_unlocks"] = std::move(arr);
    }
    if (!rare.empty()) {
        auto arr = json::array();
        for (size_t i = 0; i < rare.size() && i < 3; ++i) {
            auto entry = json::object();
            entry.properties["name"] = json::string(utf8(rare[i]->name));
            if (!rare[i]->description.empty()) entry.properties["description"] = json::string(utf8(rare[i]->description));
            entry.properties["players_with_it"] = json::string(pct(rare[i]->global_percent));
            if (const auto when = ago(rare[i]->unlocked_at); !when.empty()) entry.properties["unlocked"] = json::string(when);
            arr.elements.push_back(std::move(entry));
        }
        out.properties["rarest_unlocked"] = std::move(arr);
    }
    if (!locked.empty() && summary.unlocked < summary.total) {
        out.properties["remaining_to_100_percent"] = json::number(summary.total - summary.unlocked);
        auto arr = json::array();
        for (size_t i = 0; i < locked.size() && i < 4; ++i) {
            auto entry = json::object();
            entry.properties["name"] = json::string(utf8(locked[i]->name));
            if (!locked[i]->description.empty()) entry.properties["description"] = json::string(utf8(locked[i]->description));
            if (locked[i]->global_percent >= 0) entry.properties["players_with_it"] = json::string(pct(locked[i]->global_percent));
            arr.elements.push_back(std::move(entry));
        }
        out.properties["easiest_missing"] = std::move(arr);
    }
    out.properties["note"] = json::string("Keep achievement remarks light: highlight rare trophies (under 10%) or brand-new unlocks from recent_unlocks, or when only a few remain to 100%; do not list old achievements.");
    return out;
}

json::Value friendsAnswer(const std::vector<FriendOnline>& list, const Index& index) {
    auto out = json::object();
    auto playing = json::array();
    auto online = json::array();
    auto joinable = json::array();
    const auto isOnlineTag = [](std::wstring_view label) {
        const auto f = fold(label);
        return f == L"coop" || f == L"onlinecoop" || f == L"multiplayer" || f == L"pvp" || f == L"mmo" || f == L"lancoop" || f == L"wieloosobowe" || f == L"kooperacja";
    };
    for (const auto& person : list) {
        if (!person.game.empty()) {
            auto entry = json::object();
            entry.properties["friend"] = json::string(utf8(person.name));
            entry.properties["game"] = json::string(utf8(person.game));
            const Record* owned = nullptr;
            if (person.app_id) owned = index.get("steam:" + std::to_string(person.app_id));
            if (!owned) {
                const auto wanted = fold(person.game);
                for (const auto* hit : index.lookup(person.game, 2))
                    if (fold(hit->title) == wanted) { owned = hit; break; }
            }
            const bool has_game = owned && owned->owned;
            bool multi = false;
            if (has_game) {
                for (const auto& t : owned->tags) if (isOnlineTag(t)) { multi = true; break; }
                for (const auto& g : owned->genres) if (isOnlineTag(g)) { multi = true; break; }
            }
            entry.properties["you_own_it"] = json::boolean(has_game);
            if (has_game) {
                entry.properties["installed"] = json::boolean(owned->installed);
                entry.properties["online_or_coop"] = json::boolean(multi);
                entry.properties["library_title"] = json::string(utf8(owned->title));
                if (joinable.elements.size() < 6) joinable.elements.push_back(json::string(utf8(owned->title)));
            }
            if (playing.elements.size() < 10) playing.elements.push_back(std::move(entry));
        } else if (person.state == L"online" || person.state == L"away") {
            if (online.elements.size() < 8) online.elements.push_back(json::string(utf8(person.name)));
        }
    }
    out.properties["playing_now"] = std::move(playing);
    if (!joinable.elements.empty()) out.properties["joinable_from_your_library"] = std::move(joinable);
    if (!online.elements.empty()) out.properties["online_idle"] = std::move(online);
    if (list.empty()) out.properties["note"] = json::string("No Steam friends are online right now (or Steam friends list is unavailable).");
    return out;
}

std::vector<const Record*> inProgress(const Index& index, uint64_t now, size_t limit) {
    std::vector<const Record*> out;
    for (const auto& key : index.keys()) {
        const auto* record = index.get(key);
        if (!record || !record->owned || !record->hltb_main || *record->hltb_main < 60 || !record->playtime_minutes) continue;
        const auto played = *record->playtime_minutes, story = *record->hltb_main;
        if (played <= 30 || static_cast<uint64_t>(played) * 100 >= static_cast<uint64_t>(story) * 115) continue;
        if (!record->last_played || !now || record->last_played + 10 * 86400 < now) continue;
        if (record->achievements_total && record->achievements_unlocked >= record->achievements_total) continue;
        out.push_back(record);
    }
    std::sort(out.begin(), out.end(), [](const Record* a, const Record* b) { return a->last_played > b->last_played; });
    if (out.size() > limit) out.resize(limit);
    return out;
}

std::wstring playingText(const Index& index, uint64_t now, bool english) {
    const auto games = inProgress(index, now, 1);
    if (games.empty()) return {};
    const auto& game = *games.front();
    const auto played = *game.playtime_minutes, story = *game.hltb_main;
    const auto share = std::min<uint32_t>(99, played * 100 / story);
    const std::wstring way = english ? (share < 35 ? L"near the start" : share < 70 ? L"about halfway" : L"near the end")
                                     : (share < 35 ? L"na początku" : share < 70 ? L"mniej więcej w połowie" : L"pod koniec");
    return english ? L"\n‹He is|She is|They are› in the middle of [[" + game.title + L"]], " + way + L" of its main story (" + wide(hours(played)) + L" played of about " +
                         wide(hours(story)) + L", HowLongToBeat). Now and then you may ask how it's going or cheer ‹him|her|them› on — not in every reply."
                   : L"\n‹On jest|Ona jest› w trakcie [[" + game.title + L"]], " + way + L" głównej fabuły (" + wide(hours(played)) + L" z około " + wide(hours(story)) +
                         L" wg HowLongToBeat). Czasem możesz zapytać, jak ‹mu|jej› idzie, albo ‹go|ją› dopingować — nie w każdej odpowiedzi.";
}

json::Value describe(const Record& record, const Context& context, bool full) {
    auto out = json::object();
    auto& p = out.properties;
    p["title"] = json::string(utf8(record.title));
    if (record.release.known()) p["released"] = json::string(utf8(record.release.iso()));
    p["store"] = json::string(utf8(record.platform.empty() ? record.store : record.platform));
    if (!record.genres.empty()) p["genres"] = json::string(join(record.genres, 4));
    if (!record.tags.empty()) p["tags"] = json::string(join(record.tags, full ? 20 : 5));
    if (!record.developers.empty()) p["developer"] = json::string(join(record.developers, 2));
    p["installed"] = json::boolean(record.installed);
    if (record.playtime_minutes && *record.playtime_minutes) p["played"] = json::string(hours(*record.playtime_minutes));
    else if (!record.played()) p["played"] = json::string("never");
    if (record.last_played) {
        const auto days = context.now > record.last_played ? (context.now - record.last_played) / 86400 : 0;
        p["last_played_days_ago"] = json::number(static_cast<double>(days));
    }
    if (record.achievements_total)
        p["achievements"] = json::string(std::to_string(record.achievements_unlocked) + "/" + std::to_string(record.achievements_total));
    if (record.hltb_main) p["hltb_main_story"] = json::string(hours(*record.hltb_main));
    if (record.review_percent)
        p["steam_reviews"] = json::string(std::to_string(*record.review_percent) + "% positive" +
                                          (record.review_count ? " of " + std::to_string(*record.review_count) : std::string{}));
    else if (record.rawg_rating) { char buffer[16]; snprintf(buffer, sizeof(buffer), "%.1f/5", *record.rawg_rating); p["rawg_rating"] = json::string(buffer); }
    if (record.metacritic) p["metacritic"] = json::number(*record.metacritic);
    if (full) {
        if (!record.publishers.empty()) p["publisher"] = json::string(join(record.publishers, 3));
        if (!record.franchise.empty()) p["series"] = json::string(utf8(record.franchise));
        if (!record.description.empty()) p["description"] = json::string(utf8(record.description));
        if (record.hltb_extra) p["hltb_main_and_extras"] = json::string(hours(*record.hltb_extra));
        if (record.hltb_complete) p["hltb_completionist"] = json::string(hours(*record.hltb_complete));
        if (record.review_count) p["review_count"] = json::number(*record.review_count);
        if (!record.aliases.empty()) p["also_known_as"] = json::string(join(record.aliases, 3));
    }
    return out;
}

json::Value run(std::string_view name, const json::Value& args, const Index& index, const Context& context) {
    if (name == recall) {
        const auto topic = textArg(args, "temat");
        if (topic.empty()) return error("temat is required");
        return context.recall ? context.recall(topic) : error("Your memory of earlier conversations is off.");
    }
    if (name == news) return context.news ? context.news() : error("The news tab is off.");
    if (name == web) {
        const auto query = textArg(args, "zapytanie");
        if (query.empty()) return error("zapytanie is required");
        return context.web ? context.web(query) : error("Web search is off.");
    }
    if (needsIndex(name) && !index.size()) return error("The game index is still loading; answer from the summary in your instructions.");
    if (name == plan) return planFor(index, intArg(args, "minuty", 120, 10, 24 * 60), context);
    if (name == search) {
        const auto query = queryFrom(args, context);
        const auto found = index.search(query);
        auto out = json::object();
        out.properties["total_matches"] = json::number(static_cast<double>(found.total));
        auto games = json::array();
        for (const auto* record : found.games) games.elements.push_back(describe(*record, context));
        out.properties["games"] = std::move(games);
        const bool by_date = query.sort == Sort::release_asc || query.sort == Sort::release_desc || query.year_from || query.year_to;
        if (by_date && found.undated)
            out.properties["note"] = json::string(std::to_string(found.undated) + " matching games have no known release date and are not ranked by it.");
        if ((query.sort == Sort::shortest || query.sort == Sort::longest) && found.untimed)
            out.properties["note"] = json::string(std::to_string(found.untimed) + " matching games have no HowLongToBeat time and are listed last.");
        if (!found.total) out.properties["note"] = json::string("No game in the library matches; try fewer filters or other words.");
        return out;
    }
    if (name == details) {
        const auto title = textArg(args, "tytul");
        if (title.empty()) return error("tytul is required");
        const auto hits = index.lookup(title, 5);
        if (hits.empty()) return error("No such game in the library.");
        auto out = json::object();
        out.properties["game"] = describe(*hits[0], context, true);
        if (hits.size() > 1) {
            auto others = json::array();
            for (size_t i = 1; i < hits.size(); ++i) others.elements.push_back(json::string(utf8(hits[i]->title)));
            out.properties["other_matches"] = std::move(others);
        }
        return out;
    }
    if (name == statistics) {
        const auto group = groupFromKey(utf8(textArg(args, "grupuj"))).value_or(Group::genre);
        auto query = queryFrom(args, context);
        const bool filtered = !query.genres.empty() || !query.tags.empty() || query.status != Status::any;
        const auto stats = index.stats(group, static_cast<size_t>(intArg(args, "limit", 10, 1, 20)), filtered ? &query : nullptr);
        auto out = json::object();
        auto& p = out.properties;
        p["games"] = json::number(static_cast<double>(stats.total));
        p["installed"] = json::number(static_cast<double>(stats.installed));
        p["played"] = json::number(static_cast<double>(stats.played));
        p["never_played"] = json::number(static_cast<double>(stats.never_played));
        p["all_achievements"] = json::number(static_cast<double>(stats.completed));
        p["hours_played"] = json::number(std::round(stats.minutes / 6.0) / 10);
        p["with_release_date"] = json::number(static_cast<double>(stats.dated));
        auto groups = json::array();
        for (const auto& bucket : stats.buckets) {
            auto item = json::object();
            item.properties["name"] = json::string(utf8(bucket.name));
            item.properties["games"] = json::number(static_cast<double>(bucket.games));
            item.properties["played"] = json::number(static_cast<double>(bucket.played));
            if (bucket.minutes) item.properties["hours"] = json::number(std::round(bucket.minutes / 6.0) / 10);
            groups.elements.push_back(std::move(item));
        }
        p["groups"] = std::move(groups);
        return out;
    }
    if (name == similar) {
        const auto title = textArg(args, "tytul");
        const auto hits = index.lookup(title, 1);
        if (hits.empty()) return error("No such game in the library.");
        auto out = json::object();
        out.properties["like"] = json::string(utf8(hits[0]->title));
        auto games = json::array();
        for (const auto& [record, score] : index.similar(hits[0]->key, static_cast<size_t>(intArg(args, "limit", 5, 1, 10)))) {
            auto item = describe(*record, context);
            item.properties["similarity"] = json::number(std::round(score * 100) / 100);
            games.elements.push_back(std::move(item));
        }
        if (games.elements.empty()) out.properties["note"] = json::string("Not enough genre/tag data to compare.");
        out.properties["games"] = std::move(games);
        return out;
    }
    if (name == remember) {
        const auto note = textArg(args, "notatka");
        if (note.empty()) return error("notatka is required");
        const auto emotion = utf8(textArg(args, "emocja"));
        const auto feeling = textArg(args, "odczucie");
        const bool saved = context.remember_affective ? context.remember_affective(note, emotion, feeling)
                                                      : (context.remember && context.remember(note));
        auto out = json::object();
        out.properties["saved"] = json::boolean(saved);
        return out;
    }
    if (name == achievements) {
        const auto title = textArg(args, "tytul");
        if (title.empty()) return error("tytul is required");
        const auto hits = index.lookup(title, 3);
        if (hits.empty()) return error("No such game in the library.");
        const auto* record = hits.front();
        uint32_t app_id = 0;
        if (record->key.starts_with("steam:"))
            std::from_chars(record->key.data() + 6, record->key.data() + record->key.size(), app_id);
        AchievementSummary summary{record->achievements_unlocked, record->achievements_total, {}};
        if (context.achievements) {
            auto detailed = context.achievements(app_id, record->key);
            if (detailed.total || !detailed.items.empty()) summary = std::move(detailed);
        }
        return achievementsAnswer(record->title, summary, context.now);
    }
    if (name == friends) {
        if (!context.friends) return error("Steam friends status is unavailable.");
        return friendsAnswer(context.friends(), index);
    }
    if (name == collections) {
        const auto action_str = utf8(textArg(args, "akcja"));
        const std::string act = action_str.empty() ? "list" : action_str;
        if (act == "list") {
            if (!context.collections) return error("Collections are unavailable.");
            const auto all = context.collections();
            auto out = json::object();
            auto arr = json::array();
            for (const auto& col : all) {
                auto item = json::object();
                item.properties["name"] = json::string(utf8(col.name));
                item.properties["type"] = json::string(col.favorites ? "favorites" : col.steam ? "steam" : "local");
                item.properties["games_count"] = json::number(static_cast<double>(col.games.size()));
                auto sample = json::array();
                for (size_t i = 0; i < col.games.size() && i < 15; ++i) sample.elements.push_back(json::string(utf8(col.games[i])));
                item.properties["games"] = std::move(sample);
                arr.elements.push_back(std::move(item));
            }
            out.properties["collections"] = std::move(arr);
            out.properties["note"] = json::string("When creating or adding to a collection, Steam collections (type='steam') are used by default and sync to the Steam library of the person you are chatting with.");
            return out;
        }
        const auto col_name = textArg(args, "nazwa");
        const bool is_fav = act == "favorite" || act == "unfavorite";
        if (!is_fav && col_name.empty()) return error("nazwa is required for create, add or remove");
        std::vector<std::wstring> raw_titles;
        if (const auto* val = args.get("gry")) {
            if (val->type == json::Value::Type::string && !val->text.empty()) raw_titles.push_back(wide(val->text));
            else if (val->type == json::Value::Type::array)
                for (const auto& el : val->elements)
                    if (el.type == json::Value::Type::string && !el.text.empty() && raw_titles.size() < 20) raw_titles.push_back(wide(el.text));
        }
        if (raw_titles.empty() && is_fav) return error("gry is required for favorite/unfavorite");
        CollectionMutation mutation;
        mutation.action = act == "remove" ? CollectionMutation::Action::remove :
                          act == "favorite" ? CollectionMutation::Action::favorite :
                          act == "unfavorite" ? CollectionMutation::Action::unfavorite : CollectionMutation::Action::create_or_add;
        mutation.collection = col_name;
        const auto typ = utf8(textArg(args, "typ"));
        mutation.steam = typ != "local"; // Defaults to Steam library collection!
        auto matched = json::array(), unmatched = json::array();
        for (const auto& t : raw_titles) {
            const auto hits = index.lookup(t, 3);
            const Record* picked = nullptr;
            const auto wanted = fold(t);
            for (const auto* h : hits) {
                if (fold(h->title) == wanted) { picked = h; break; }
                for (const auto& alias : h->aliases) if (fold(alias) == wanted) { picked = h; break; }
            }
            if (!picked && hits.size() == 1) picked = hits.front();
            if (picked) {
                if (std::find(mutation.keys.begin(), mutation.keys.end(), picked->key) == mutation.keys.end()) {
                    mutation.keys.push_back(picked->key);
                    mutation.titles.push_back(picked->title);
                    matched.elements.push_back(json::string(utf8(picked->title)));
                }
            } else unmatched.elements.push_back(json::string(utf8(t)));
        }
        const bool ok = context.mutate_collection && context.mutate_collection(mutation);
        auto out = json::object();
        out.properties["ok"] = json::boolean(ok);
        out.properties["action"] = json::string(act);
        if (!col_name.empty()) out.properties["collection"] = json::string(utf8(col_name));
        out.properties["type"] = json::string(is_fav ? "favorites" : mutation.steam ? "steam" : "local");
        out.properties["games_applied"] = std::move(matched);
        if (!unmatched.elements.empty()) out.properties["not_in_library"] = std::move(unmatched);
        return out;
    }
    if (name == history) {
        const int days = intArg(args, "dni", 14, 1, 90);
        const uint64_t cutoff = context.now > static_cast<uint64_t>(days) * 86400 ? context.now - static_cast<uint64_t>(days) * 86400 : 0;
        std::vector<const Record*> recent;
        for (const auto& key : index.keys()) {
            const auto* record = index.get(key);
            if (!record || !record->owned || !record->last_played || record->last_played < cutoff) continue;
            recent.push_back(record);
        }
        std::sort(recent.begin(), recent.end(), [](const Record* a, const Record* b) { return a->last_played > b->last_played; });
        auto active = json::array(), dropped = json::array(), completed = json::array();
        for (const auto* record : recent) {
            const uint32_t mins = record->playtime_minutes.value_or(0);
            const bool done = record->achievements_total && record->achievements_unlocked >= record->achievements_total;
            auto item = describe(*record, context);
            if (done && completed.elements.size() < 6) completed.elements.push_back(std::move(item));
            else if (mins > 0 && mins < 45 && dropped.elements.size() < 8) dropped.elements.push_back(std::move(item));
            else if (active.elements.size() < 10) active.elements.push_back(std::move(item));
        }
        auto out = json::object();
        out.properties["days_window"] = json::number(days);
        out.properties["total_played_in_window"] = json::number(static_cast<double>(recent.size()));
        out.properties["active_games"] = std::move(active);
        if (!dropped.elements.empty()) out.properties["barely_started_under_45m"] = std::move(dropped);
        if (!completed.elements.empty()) out.properties["completed"] = std::move(completed);
        if (recent.empty()) out.properties["note"] = json::string("No games in the library have a recorded session in this time window.");
        return out;
    }
    if (name == roulette) {
        auto query = queryFrom(args, context);
        if (args.get("stan") == nullptr) query.status = Status::installed;
        query.limit = 100;
        query.sort = Sort::random;
        auto found = index.search(query);
        bool fallback_uninstalled = false;
        if (found.games.empty() && query.status == Status::installed) {
            query.status = Status::any;
            found = index.search(query);
            fallback_uninstalled = !found.games.empty();
        }
        const int max_min = intArg(args, "max_minut", 0, 0, 10000);
        if (max_min > 0) {
            std::erase_if(found.games, [&](const Record* r) { return !r->hltb_main || *r->hltb_main > static_cast<uint32_t>(max_min); });
        }
        if (found.games.empty()) return error("No games in the library match these roulette filters; try broader filters.");
        std::vector<const Record*> pool;
        for (const auto* r : found.games) {
            if (!r->played()) pool.push_back(r); // Never-played games are twice as likely, just like the button roulette.
            pool.push_back(r);
        }
        const auto indices = companion::spinReel(pool.size(), context.seed ? context.seed : 1u, 28);
        if (indices.empty()) return error("Could not spin the roulette.");
        std::vector<std::wstring> reel;
        reel.reserve(indices.size());
        for (const size_t idx : indices) reel.push_back(pool[idx]->title);
        const Record* picked = pool[indices.back()];
        if (context.roulette) context.roulette(reel, picked->key);
        auto out = json::object();
        out.properties["picked"] = describe(*picked, context, true);
        out.properties["pool_size"] = json::number(static_cast<double>(found.games.size()));
        if (fallback_uninstalled) out.properties["note"] = json::string("No installed game matched, so the wheel spun over uninstalled games too. Announce the picked game with [[Title]]!");
        else out.properties["note"] = json::string("The roulette wheel is spinning in the chat and will stop on this exact game. Announce the result in your style using [[Title]]!");
        return out;
    }
    return error("Unknown tool.");
}
}
