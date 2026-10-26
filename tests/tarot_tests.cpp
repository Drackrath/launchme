// Tarot's model (src/library/tarot.hpp): the deal, the wish the chosen cards make, the library and
// store picks, the Steam search URL and the review summary parsed from Steam's search rows.
#include "../src/library/tarot.hpp"
#include "../src/i18n/i18n.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <set>
#include <windows.h>

namespace {
using tarot::Facet;

int indexOf(const tarot::Reading& reading, int value) {
    const auto* spread = reading.current();
    if (!spread) return -1;
    const auto cards = tarot::question(spread->facet).cards;
    for (size_t i = 0; i < spread->cards.size(); ++i)
        if (cards[static_cast<size_t>(spread->cards[i])].value == value) return static_cast<int>(i);
    return -1;
}

std::string escapeJson(std::string_view text) {
    std::string result;
    for (const char c : text) { if (c == '"' || c == '\\') result += '\\'; result += c; }
    return result;
}
std::string row(uint32_t id, const std::string& title, const std::string& date, const std::string& tags, int percent, const std::string& reviews) {
    return "<a href=\"https://store.steampowered.com/app/" + std::to_string(id) + "/x/\" data-ds-appid=\"" + std::to_string(id) +
           "\" data-ds-tagids=\"[" + tags + "]\" class=\"search_result_row ds_collapse_flag\"><div class=\"col search_capsule\">"
           "<img src=\"https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/" + std::to_string(id) + "/capsule_sm_120.jpg\"></div>"
           "<span class=\"title\">" + title + "</span><div class=\"col search_released responsive_secondrow\">" + date + "</div>"
           "<div class=\"col search_reviewscore responsive_secondrow\"><span class=\"search_review_summary positive\" data-tooltip-html=\"Very Positive&lt;br&gt;" +
           std::to_string(percent) + "% of the " + reviews + " user reviews for this game are positive.\"></span></div></a>";
}
std::string page(const std::string& html, int total, int start = 0) {
    return "{\"success\":1,\"start\":" + std::to_string(start) + ",\"total_count\":" + std::to_string(total) + ",\"results_html\":\"" + escapeJson(html) + "\"}";
}

game_index::Record record(std::string key, std::wstring title, std::vector<std::wstring> genres, int year, bool played, bool installed,
                          std::optional<uint32_t> hltb = {}, std::optional<int> percent = {}, std::optional<uint32_t> reviews = {}) {
    game_index::Record result;
    result.key = std::move(key); result.title = std::move(title); result.genres = std::move(genres);
    result.release.year = year; result.last_played = played ? 1700000000 : 0; result.installed = installed;
    result.hltb_main = hltb; result.review_percent = percent; result.review_count = reviews;
    return result;
}
}

int main() {
    i18n::setLanguage(i18n::Language::pl);
    // (1) A reading starts with the intent: buy on Steam or play what is owned.
    const auto start = tarot::begin(7);
    if (start.spreads.size() != 1 || !start.current() || start.current()->facet != Facet::intent || start.current()->cards.size() != 2 ||
        start.complete()) return 1;

    // (2) Every deal: 5–8 cards in all, the intent first, then distinct questions its deck asks,
    // genre and era always (and played for the library); dealt cards valid and distinct.
    std::set<size_t> totals;
    for (uint32_t seed = 1; seed < 400; ++seed) {
        for (const int value : {0, 1}) {
            auto reading = tarot::begin(seed);
            const int card = indexOf(reading, value);
            if (card < 0 || !tarot::choose(reading, static_cast<size_t>(card))) return 2;
            const bool library = value == 1;
            if (reading.pool() != (library ? tarot::Pool::library : tarot::Pool::store)) return 3;
            if (reading.total < 5 || reading.total > 8 || reading.spreads.size() != reading.total) return 4;
            totals.insert(reading.total);
            std::set<Facet> facets;
            for (size_t i = 1; i < reading.spreads.size(); ++i) {
                const auto& spread = reading.spreads[i];
                const auto& question = tarot::question(spread.facet);
                if (spread.facet == Facet::intent || !facets.insert(spread.facet).second || !(library ? question.library : question.store)) return 5;
                if (spread.cards.size() != std::min(question.shown, question.cards.size())) return 6;
                std::set<int> cards(spread.cards.begin(), spread.cards.end());
                if (cards.size() != spread.cards.size() || *cards.begin() < 0 || static_cast<size_t>(*cards.rbegin()) >= question.cards.size()) return 7;
            }
            if (!facets.contains(Facet::genre) || !facets.contains(Facet::era) || facets.contains(Facet::played) != library) return 8;
            if (!library && (facets.contains(Facet::length) || facets.contains(Facet::ready))) return 9;
            if (library && facets.contains(Facet::price)) return 10;
            // Answering everything completes it.
            while (!reading.complete()) if (!tarot::choose(reading, 0)) return 11;
            if (reading.current() || tarot::choose(reading, 0)) return 12;
        }
    }
    if (totals != std::set<size_t>{5, 6, 7, 8}) return 13;

    // (3) Undo takes answers back; undoing the intent drops the deal, and the same answer deals the same again.
    {
        auto reading = tarot::begin(99);
        tarot::choose(reading, static_cast<size_t>(indexOf(reading, 1)));
        const auto dealt = reading.spreads;
        tarot::choose(reading, 1);
        if (!tarot::undo(reading) || reading.answers.size() != 1) return 14;
        if (!tarot::undo(reading) || reading.spreads.size() != 1 || reading.total || tarot::undo(reading)) return 15;
        tarot::choose(reading, static_cast<size_t>(indexOf(reading, 1)));
        if (reading.spreads.size() != dealt.size()) return 16;
        for (size_t i = 0; i < dealt.size(); ++i) if (reading.spreads[i].facet != dealt[i].facet || reading.spreads[i].cards != dealt[i].cards) return 17;
    }

    // (4) The wish: the Warrior asks for action (Steam tag 19), the Golden Age for 2010–2017.
    tarot::Reading chosen;
    const auto genre_cards = tarot::question(Facet::genre).cards;
    const auto warrior = std::find_if(genre_cards.begin(), genre_cards.end(), [](const auto& card) { return std::wstring(card.name) == L"Wojownik"; });
    const auto magician = std::find_if(genre_cards.begin(), genre_cards.end(), [](const auto& card) { return std::wstring(card.name) == L"Mag"; });
    if (warrior == genre_cards.end() || magician == genre_cards.end()) return 18;
    chosen.spreads = {{Facet::intent, {0, 1}}, {Facet::genre, {static_cast<int>(magician - genre_cards.begin()), static_cast<int>(warrior - genre_cards.begin())}},
                      {Facet::era, {0, 1, 2, 3}}, {Facet::played, {0, 1, 2}}, {Facet::fame, {0, 1, 2}}};
    chosen.answers = {1, static_cast<int>(magician - genre_cards.begin()), 1, 0, 1}; // Library, the Magician, Golden Age, played, hidden gem.
    chosen.total = chosen.spreads.size();
    if (!chosen.complete()) return 19;
    const auto wish = tarot::wish(chosen);
    if (wish.pool != tarot::Pool::library || wish.era != 2 || wish.played != 1 || wish.fame != 2 || wish.labels.size() != 1 ||
        wish.genre_tag != 122 || wish.labels[0].weight != 4 || std::find(wish.labels[0].folded.begin(), wish.labels[0].folded.end(), L"rpg") == wish.labels[0].folded.end())
        return 20;
    if (tarot::eraYears(2) != std::pair{2010, 2017} || tarot::eraYears(0) != std::pair{0, 0} || tarot::steamTagId(L"Action") != 19 || tarot::steamTagId(L"No such tag") != 0) return 21;

    // (5) Library picks: the played, well reviewed RPG of the golden age first; a game no longer
    // owned never; at most three.
    std::vector<game_index::Record> records{
        record("steam:1", L"Kroniki Mgły", {L"RPG"}, 2015, true, true, 40 * 60, 95, 4000),
        record("steam:2", L"Strzelanina", {L"Action"}, 2005, false, true, 8 * 60, 70, 90000),
        record("steam:3", L"Łamigłówka", {L"Puzzle"}, 2020, true, false),
        record("gog:4", L"Stara Taktyka", {L"Strategy"}, 2012, true, true, {}, 88, 15000),
        record("steam:5", L"Niewidoczna", {L"RPG"}, 2014, true, true, {}, 99, 3000),
        record("steam:6", L"Bez Danych", {}, 0, false, false),
    };
    records[4].owned = false;
    std::vector<const game_index::Record*> pointers;
    for (const auto& item : records) pointers.push_back(&item);
    for (uint32_t seed = 1; seed < 60; ++seed) {
        const auto picks = tarot::pickLibrary(pointers, wish, seed, false);
        if (picks.size() != 3 || picks[0].key != "steam:1") return 22;
        std::set<std::string> keys;
        for (const auto& pick : picks) if (pick.key == "steam:5" || !keys.insert(pick.key).second || pick.title.empty()) return 23;
    }
    const auto first = tarot::pickLibrary(pointers, wish, 3, false)[0];
    if (first.detail.find(L"2015") == std::wstring::npos || first.detail.find(L"RPG") == std::wstring::npos) return 24;

    // (6) The store: its URL carries the genre tag and the price filter.
    tarot::Wish store;
    store.pool = tarot::Pool::store; store.genre_tag = 19; store.price = 1;
    const auto url = tarot::storeUrl(store, 100);
    if (url.find(L"tags=19") == std::wstring::npos || url.find(L"maxprice=free") == std::wstring::npos || url.find(L"start=100") == std::wstring::npos ||
        url.find(L"sort_by=Reviews_DESC") == std::wstring::npos || url.find(L"category1=998") == std::wstring::npos) return 25;
    store.price = 2;
    if (tarot::storeUrl(store, 0).find(L"specials=1") == std::wstring::npos) return 26;

    // (7) Steam's search rows: their tag IDs and review summary.
    const auto html = row(9001, "Gwiezdna Saga", "12 Mar, 2019", "122,4182,3942", 94, "1,234") +
                      row(9002, "Old Shooter", "1 Nov, 2004", "19,1774", 97, "250,000") +
                      row(100, "Owned Already", "5 May, 2018", "122", 99, "8,000") +
                      row(9003, "Space Tactics", "2 Feb, 2021", "9,1755,3942", 91, "9,100") +
                      row(9004, "Cosy Farm", "3 Mar, 2022", "597,1654", 85, "600");
    const auto parsed = glance::parseUpcomingSearch(page(html, 5));
    if (!parsed || parsed->items.size() != 5) return 27;
    const auto& saga = parsed->items[0];
    if (saga.app_id != 9001 || saga.review_percent != 94 || saga.review_count != 1234u || saga.tag_ids != std::vector<uint32_t>{122, 4182, 3942} ||
        saga.release.year != 2019) return 28;
    if (parsed->items[1].review_count != 250000u) return 29;

    // (8) Store picks: owned games never, the era and the sci-fi setting weigh in.
    tarot::Wish future;
    future.pool = tarot::Pool::store; future.era = 3; future.fame = 2;
    tarot::Labels setting;
    setting.facet = Facet::setting; setting.weight = 2; setting.steam_ids = {3942};
    future.labels.push_back(setting);
    for (uint32_t seed = 1; seed < 60; ++seed) {
        const auto picks = tarot::pickStore(parsed->items, future, {100}, seed, false);
        if (picks.size() != 3) return 30;
        for (const auto& pick : picks) if (pick.app_id == 100 || pick.key != "steam:" + std::to_string(pick.app_id)) return 31;
        if (picks[0].app_id != 9001 && picks[0].app_id != 9003) return 32;
        if (std::find_if(picks.begin(), picks.begin() + 2, [](const auto& pick) { return pick.app_id == 9002; }) != picks.begin() + 2) return 33;
    }

    // (9) The search: two pages; none readable is nullopt.
    int requests = 0;
    const glance::Get get = [&](const std::wstring& address, const std::wstring&, size_t) {
        ++requests;
        if (address.find(L"tags=19") == std::wstring::npos) return HttpResponse{};
        const auto text = page(html, 150, address.find(L"start=100") != std::wstring::npos ? 100 : 0);
        return HttpResponse{200, {text.begin(), text.end()}};
    };
    tarot::Wish action;
    action.genre_tag = 19;
    const auto found = tarot::searchStore(action, get);
    if (!found || found->size() != 10 || requests != 2) return 34;
    action.genre_tag = 9;
    if (tarot::searchStore(action, get)) return 35;

    // (10) The cache: pages are reused for 30 minutes; an older copy stands in when Steam is down.
    const auto folder = std::filesystem::temp_directory_path() / (L"launchme-tarot-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::remove_all(folder);
    action.genre_tag = 19;
    requests = 0;
    if (!tarot::searchStore(action, get, folder) || requests != 2) return 36;
    if (const auto again = tarot::searchStore(action, get, folder); !again || again->size() != 10 || requests != 2) return 37;
    for (const auto& entry : std::filesystem::directory_iterator(folder))
        std::filesystem::last_write_time(entry.path(), std::filesystem::file_time_type::clock::now() - std::chrono::hours(2));
    const glance::Get offline = [&](const std::wstring&, const std::wstring&, size_t) { ++requests; return HttpResponse{}; };
    if (const auto stale = tarot::searchStore(action, offline, folder); !stale || stale->size() != 10 || requests != 4) return 38;
    if (!tarot::searchStore(action, get, folder) || requests != 6) return 39; // Old copies are asked for again.
    std::filesystem::remove_all(folder);
    std::cout << "Tarot: deal, wish, library and store picks, Steam search rows passed\n";
    return 0;
}
