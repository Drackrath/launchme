#include "tarot.hpp"
#include "steam_tags_builtin.hpp"
#include "../i18n/i18n.hpp"
#include "../platform/atomic_file.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwchar>
#include <fstream>

namespace tarot {
namespace {
using S = Symbol;

constexpr Card intent_cards[] = {
    {trKey(L"Sakiewka"), trKey(L"Kupuję coś nowego na Steam"), S::pentacle, 0},
    {trKey(L"Pustelnik"), trKey(L"Gram w to, co już mam"), S::lantern, 1},
};
constexpr Card genre_cards[] = {
    {trKey(L"Wojownik"), trKey(L"Akcja i walka"), S::sword, 0, {L"Action", L"Hack and Slash", L"Shooter", L"Fighting", L"Action-Adventure"}},
    {trKey(L"Mag"), trKey(L"RPG i rozwój postaci"), S::wand, 0, {L"RPG", L"JRPG", L"Party-Based RPG", L"Action RPG", L"CRPG"}},
    {trKey(L"Cesarz"), trKey(L"Strategia i taktyka"), S::crown, 0, {L"Strategy", L"Turn-Based Strategy", L"Tactical", L"Grand Strategy", L"City Builder", L"RTS"}},
    {trKey(L"Wędrowiec"), trKey(L"Przygoda i eksploracja"), S::mountain, 0, {L"Adventure", L"Exploration", L"Open World", L"Point & Click", L"Walking Simulator"}},
    {trKey(L"Wieża"), trKey(L"Horror i groza"), S::tower, 0, {L"Horror", L"Psychological Horror", L"Survival Horror", L"Lovecraftian"}},
    {trKey(L"Koło Fortuny"), trKey(L"Roguelike i karcianki"), S::wheel, 0, {L"Roguelite", L"Roguelike", L"Roguelike Deckbuilder", L"Deckbuilding", L"Card Game"}},
    {trKey(L"Budowniczy"), trKey(L"Symulacja i budowanie"), S::hammer, 0, {L"Simulation", L"Building", L"Base Building", L"Sandbox", L"Management", L"Farming Sim"}},
    {trKey(L"Sfinks"), trKey(L"Zagadki i tajemnice"), S::eye, 0, {L"Puzzle", L"Mystery", L"Detective", L"Logic", L"Hidden Object"}},
    {trKey(L"Rydwan"), trKey(L"Wyścigi i sport"), S::arrow, 0, {L"Racing", L"Sports", L"Driving", L"Automobile Sim"}},
    {trKey(L"Gwiazda"), trKey(L"Platformówki"), S::star, 0, {L"Platformer", L"Metroidvania", L"2D Platformer", L"Precision Platformer", L"3D Platformer"}},
};
constexpr Card era_cards[] = {
    {trKey(L"Prastary"), trKey(L"Klasyka sprzed 2010"), S::hourglass, 1},
    {trKey(L"Złoty Wiek"), trKey(L"Lata 2010–2017"), S::sun, 2},
    {trKey(L"Nowy Świt"), trKey(L"Od 2018 do dziś"), S::dawn, 3},
    {trKey(L"Wieczność"), trKey(L"Czas nie ma znaczenia"), S::infinity, 0},
};
constexpr Card played_cards[] = {
    {trKey(L"Wspomnienie"), trKey(L"Gra, którą już znam"), S::moon, 1},
    {trKey(L"Nieznajomy"), trKey(L"Jeszcze nieuruchomiona"), S::keyhole, 2},
    {trKey(L"Los"), trKey(L"Wszystko jedno"), S::dice, 0},
};
constexpr Card length_cards[] = {
    {trKey(L"Iskra"), trKey(L"Krótka, do 10 godzin"), S::spark, 1},
    {trKey(L"Płomień"), trKey(L"Od 10 do 30 godzin"), S::flame, 2},
    {trKey(L"Pożoga"), trKey(L"Długa, ponad 30 godzin"), S::blaze, 3},
};
constexpr Card mood_cards[] = {
    {trKey(L"Ukojenie"), trKey(L"Spokój i relaks"), S::cup, 0, {L"Relaxing", L"Cozy", L"Casual", L"Wholesome", L"Farming Sim", L"Life Sim"}},
    {trKey(L"Burza"), trKey(L"Adrenalina"), S::lightning, 0, {L"Fast-Paced", L"Shooter", L"Bullet Hell", L"Fighting", L"Hack and Slash", L"Difficult"}},
    {trKey(L"Księżyc"), trKey(L"Mrok i tajemnica"), S::moon, 0, {L"Dark", L"Mystery", L"Atmospheric", L"Psychological Horror", L"Lovecraftian", L"Noir"}},
    {trKey(L"Serce"), trKey(L"Wzruszająca opowieść"), S::heart, 0, {L"Story Rich", L"Emotional", L"Choices Matter", L"Narration", L"Visual Novel", L"Great Soundtrack"}},
};
constexpr Card company_cards[] = {
    {trKey(L"Samotnik"), trKey(L"Sam, w ciszy"), S::figure, 0, {L"Singleplayer"}},
    {trKey(L"Drużyna"), trKey(L"Współpraca"), S::fellowship, 0, {L"Co-op", L"Online Co-Op", L"Local Co-Op", L"Co-op Campaign"}},
    {trKey(L"Arena"), trKey(L"Rywalizacja"), S::swords, 0, {L"PvP", L"Competitive", L"Multiplayer", L"Online PvP", L"eSports"}},
};
constexpr Card setting_cards[] = {
    {trKey(L"Zamek"), trKey(L"Fantasy i magia"), S::castle, 0, {L"Fantasy", L"Dark Fantasy", L"Magic", L"Medieval", L"Dragons", L"Mythology"}},
    {trKey(L"Otchłań"), trKey(L"Sci-fi i kosmos"), S::planet, 0, {L"Sci-fi", L"Space", L"Cyberpunk", L"Futuristic", L"Aliens"}},
    {trKey(L"Kronika"), trKey(L"Historia i wojna"), S::book, 0, {L"Historical", L"War", L"World War II", L"Medieval", L"Western", L"Military"}},
    {trKey(L"Ruiny"), trKey(L"Postapokalipsa i przetrwanie"), S::skull, 0, {L"Post-apocalyptic", L"Survival", L"Zombies", L"Crafting"}},
};
constexpr Card style_cards[] = {
    {trKey(L"Mozaika"), trKey(L"Pikselowa grafika"), S::mosaic, 0, {L"Pixel Graphics", L"Retro", L"2D"}},
    {trKey(L"Zwierciadło"), trKey(L"Realizm"), S::mirror, 0, {L"Realistic", L"Beautiful"}},
    {trKey(L"Baśń"), trKey(L"Ręcznie malowana"), S::quill, 0, {L"Hand-drawn", L"Cute", L"Colorful", L"Cartoony", L"Stylized", L"Anime"}},
    {trKey(L"Cień"), trKey(L"Mroczna estetyka"), S::eclipse, 0, {L"Dark", L"Atmospheric", L"Noir", L"Gothic", L"Dark Fantasy"}},
};
constexpr Card fame_cards[] = {
    {trKey(L"Korona"), trKey(L"Uznany hit"), S::crown, 1},
    {trKey(L"Skarb"), trKey(L"Ukryta perełka"), S::gem, 2},
    {trKey(L"Kości"), trKey(L"Niech zdecyduje los"), S::dice, 0},
};
constexpr Card price_cards[] = {
    {trKey(L"Dar"), trKey(L"Bez płacenia"), S::gift, 1},
    {trKey(L"Okazja"), trKey(L"Na promocji"), S::scales, 2},
    {trKey(L"Skarbiec"), trKey(L"Cena bez znaczenia"), S::pentacle, 0},
};
constexpr Card ready_cards[] = {
    {trKey(L"Brama"), trKey(L"Zainstalowana, od razu"), S::gate, 1},
    {trKey(L"Droga"), trKey(L"Może się pobrać"), S::road, 0},
};

// Facet order; question(facet) indexes it.
const Question deck[] = {
    {Facet::intent, trKey(L"Czy karty mają wskazać coś nowego do kupienia?"), intent_cards, 2, true, true, true},
    {Facet::genre, trKey(L"Który arkan cię przyzywa?"), genre_cards, 4, true, true, true},
    {Facet::era, trKey(L"Z której epoki ma przybyć?"), era_cards, 4, true, true, true},
    {Facet::played, trKey(L"Stary znajomy czy obcy?"), played_cards, 3, true, false, true},
    {Facet::length, trKey(L"Ile czasu dasz tej opowieści?"), length_cards, 3, true, false, false},
    {Facet::mood, trKey(L"Czego dziś szuka twoja dusza?"), mood_cards, 4, true, true, false},
    {Facet::company, trKey(L"Kto wyruszy razem z tobą?"), company_cards, 3, true, true, false},
    {Facet::setting, trKey(L"Dokąd mają cię zabrać karty?"), setting_cards, 4, true, true, false},
    {Facet::style, trKey(L"Jakie barwy ma mieć wizja?"), style_cards, 4, true, true, false},
    {Facet::fame, trKey(L"Sława czy ukryty skarb?"), fame_cards, 3, true, true, false},
    {Facet::price, trKey(L"Ile złota masz w sakiewce?"), price_cards, 3, false, true, false},
    {Facet::ready, trKey(L"Ma czekać gotowa do drogi?"), ready_cards, 2, true, false, false},
};
static_assert(std::size(deck) == static_cast<size_t>(Facet::ready) + 1);

// xorshift32: the same reading for the same seed on every machine.
struct Random {
    uint32_t state;
    uint32_t next() { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; }
    float unit() { return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f); }
    size_t below(size_t n) { return n ? next() % n : 0; }
    template <class T> void shuffle(std::vector<T>& values) {
        for (size_t i = values.size(); i > 1; --i) std::swap(values[i - 1], values[below(i)]);
    }
};
Random seeded(uint32_t seed, uint32_t salt) {
    uint32_t state = (seed ^ salt) * 2654435761u;
    if (!state) state = 0x9e3779b9u;
    Random result{state};
    result.next();
    return result;
}

Spread dealt(const Question& question, Random& random) {
    Spread spread{question.facet, {}};
    for (size_t i = 0; i < question.cards.size(); ++i) spread.cards.push_back(static_cast<int>(i));
    random.shuffle(spread.cards);
    spread.cards.resize(std::min(spread.cards.size(), std::max<size_t>(1, question.shown)));
    return spread;
}

void dealRest(Reading& reading) {
    auto rng = seeded(reading.seed, 0x51ed270bu);
    const bool library = reading.pool() == Pool::library;
    std::vector<Facet> always, maybe;
    for (const auto& item : deck) {
        if (item.facet == Facet::intent || !(library ? item.library : item.store)) continue;
        (item.always ? always : maybe).push_back(item.facet);
    }
    rng.shuffle(maybe);
    const size_t total = 5 + rng.below(4); // 5–8 cards, the intent included.
    std::vector<Facet> chosen = always;
    for (const auto facet : maybe) if (chosen.size() + 1 < total) chosen.push_back(facet);
    rng.shuffle(chosen);
    reading.spreads.resize(1);
    for (const auto facet : chosen) reading.spreads.push_back(dealt(question(facet), rng));
    reading.total = reading.spreads.size();
}

bool contains(const std::vector<std::wstring>& folded, const std::vector<std::wstring>& wanted) {
    for (const auto& label : folded)
        if (std::find(wanted.begin(), wanted.end(), label) != wanted.end()) return true;
    return false;
}

std::wstring joined(const std::vector<std::wstring>& parts) {
    std::wstring line;
    for (const auto& part : parts) if (!part.empty()) line += (line.empty() ? L"" : L" · ") + part;
    return line;
}

// A cached search page's name: FNV-1a of its URL.
std::wstring pageName(const std::wstring& url) {
    uint64_t hash = 14695981039346656037ull;
    for (const auto unit : url) { hash ^= static_cast<uint16_t>(unit); hash *= 1099511628211ull; }
    wchar_t text[17]{};
    swprintf_s(text, L"%016llx", static_cast<unsigned long long>(hash));
    return text;
}
std::string readPage(const std::filesystem::path& file, size_t limit) {
    std::error_code error;
    const auto size = std::filesystem::file_size(file, error);
    if (error || !size || size > limit) return {};
    std::string bytes(static_cast<size_t>(size), '\0');
    std::ifstream input(file, std::ios::binary);
    return input.read(bytes.data(), static_cast<std::streamsize>(bytes.size())) ? bytes : std::string{};
}
bool fresh(const std::filesystem::path& file) {
    std::error_code error;
    const auto stamp = std::filesystem::last_write_time(file, error);
    if (error) return false;
    const auto age = std::filesystem::file_time_type::clock::now() - stamp;
    return age >= decltype(age)::zero() && age < std::chrono::minutes(30);
}

// The best two, then a wild card from the next eight.
std::vector<Pick> spread(std::vector<Pick> ranked, Random& rng) {
    std::stable_sort(ranked.begin(), ranked.end(), [](const Pick& a, const Pick& b) { return a.score > b.score; });
    std::vector<Pick> picks;
    for (size_t i = 0; i < ranked.size() && picks.size() < 2; ++i) picks.push_back(ranked[i]);
    if (ranked.size() > 2) picks.push_back(ranked[2 + rng.below(std::min<size_t>(ranked.size() - 2, 8))]);
    return picks;
}
}

std::span<const Question> questions() { return deck; }
const Question& question(Facet facet) { return deck[static_cast<size_t>(facet)]; }

Pool Reading::pool() const {
    const auto* card = answer(Facet::intent);
    return card && card->value == 0 ? Pool::store : Pool::library;
}

const Card* Reading::answer(Facet facet) const {
    for (size_t i = 0; i < answers.size() && i < spreads.size(); ++i) {
        if (spreads[i].facet != facet) continue;
        const auto cards = question(facet).cards;
        const auto index = answers[i];
        return index >= 0 && static_cast<size_t>(index) < cards.size() ? &cards[static_cast<size_t>(index)] : nullptr;
    }
    return nullptr;
}

Reading begin(uint32_t seed) {
    Reading reading;
    reading.seed = seed;
    auto rng = seeded(seed, 0x2545f491u);
    reading.spreads.push_back(dealt(question(Facet::intent), rng));
    return reading;
}

bool choose(Reading& reading, size_t card) {
    const auto* current = reading.current();
    if (!current || card >= current->cards.size()) return false;
    reading.answers.push_back(current->cards[card]);
    if (reading.answers.size() == 1) dealRest(reading);
    return true;
}

bool undo(Reading& reading) {
    if (reading.answers.empty()) return false;
    reading.answers.pop_back();
    if (reading.answers.empty()) { reading.spreads.resize(1); reading.total = 0; }
    return true;
}

std::pair<int, int> eraYears(int era) {
    switch (era) {
    case 1: return {1970, 2009};
    case 2: return {2010, 2017};
    case 3: return {2018, 9999};
    default: return {0, 0};
    }
}

uint32_t steamTagId(std::wstring_view name) {
    const auto wanted = game_index::fold(name);
    for (const auto& tag : game_index::builtinSteamTags()) if (game_index::fold(tag.name) == wanted) return tag.id;
    return 0;
}

Wish wish(const Reading& reading) {
    Wish result;
    result.pool = reading.pool();
    for (size_t i = 1; i < reading.answers.size() && i < reading.spreads.size(); ++i) {
        const auto facet = reading.spreads[i].facet;
        const auto* card = reading.answer(facet);
        if (!card) continue;
        switch (facet) {
        case Facet::genre: case Facet::mood: case Facet::company: case Facet::setting: case Facet::style: {
            Labels labels;
            labels.facet = facet;
            labels.weight = facet == Facet::genre ? 4.0f : facet == Facet::mood || facet == Facet::company ? 2.5f : facet == Facet::setting ? 2.0f : 1.5f;
            labels.meaning = tr(card->meaning);
            for (const auto* tag : card->tags) {
                if (!tag) break;
                labels.folded.push_back(game_index::fold(tag));
                if (const auto id = steamTagId(tag)) labels.steam_ids.push_back(id);
            }
            if (facet == Facet::genre && card->tags[0]) result.genre_tag = steamTagId(card->tags[0]);
            result.labels.push_back(std::move(labels));
            break;
        }
        case Facet::era: result.era = card->value; break;
        case Facet::played: result.played = card->value; break;
        case Facet::length: result.length = card->value; break;
        case Facet::fame: result.fame = card->value; break;
        case Facet::price: result.price = card->value; break;
        case Facet::ready: result.installed = card->value == 1; break;
        default: break;
        }
    }
    return result;
}

std::vector<Pick> pickLibrary(std::span<const game_index::Record* const> records, const Wish& wish, uint32_t seed, bool english) {
    auto rng = seeded(seed, 0x68e31da4u);
    const auto [from, to] = eraYears(wish.era);
    std::vector<Pick> ranked;
    for (const auto* record : records) {
        if (!record || !record->owned || record->title.empty()) continue;
        std::vector<std::wstring> folded;
        for (const auto* list : {&record->genres, &record->tags})
            for (const auto& label : *list) folded.push_back(game_index::fold(game_index::canonicalGenre(label)));
        float score = 0;
        for (const auto& labels : wish.labels) {
            if (folded.empty()) score -= 0.3f * labels.weight; // Nothing known: below any real match.
            else if (contains(folded, labels.folded)) score += labels.weight;
            else score -= labels.facet == Facet::genre ? 1.5f : 0.4f;
        }
        if (wish.era) {
            if (record->release.known()) score += record->release.year >= from && record->release.year <= to ? 3.0f : -3.5f;
            else score -= 0.8f;
        }
        if (wish.played == 1) score += record->played() ? 5.0f : -6.0f;
        else if (wish.played == 2) score += record->played() ? -6.0f : 5.0f;
        if (wish.length) {
            if (record->hltb_main && *record->hltb_main) {
                const float hours = static_cast<float>(*record->hltb_main) / 60.0f;
                const bool fits = wish.length == 1 ? hours <= 10 : wish.length == 2 ? hours > 10 && hours <= 30 : hours > 30;
                score += fits ? 3.0f : -2.5f;
            } else score -= 0.6f;
        }
        const float rating = game_index::ratingScore(*record);
        if (wish.fame == 1) score += rating >= 80 ? 3.0f : rating >= 65 ? 1.0f : rating < 0 ? -0.5f : -1.5f;
        else if (wish.fame == 2) {
            if (record->review_count && *record->review_count < 20000 && record->review_percent && *record->review_percent >= 85) score += 3.0f;
            else if (record->review_count && *record->review_count >= 50000) score -= 1.5f;
            else if (rating >= 80) score += 1.0f;
        }
        if (wish.installed) score += record->installed ? 5.0f : -6.0f;
        score += rating >= 0 ? rating / 100.0f * 1.2f : 0.4f;
        score += rng.unit() * 1.6f;

        std::vector<std::wstring> parts;
        if (!record->genres.empty()) parts.push_back(game_index::genreLabel(record->genres.front(), english));
        if (record->release.known()) parts.push_back(std::to_wstring(record->release.year));
        if (record->hltb_main && *record->hltb_main >= 30) parts.push_back(std::to_wstring((*record->hltb_main + 30) / 60) + L" h");
        if (record->review_percent) parts.push_back(std::to_wstring(*record->review_percent) + L"%");
        else if (record->metacritic) parts.push_back(L"Metacritic " + std::to_wstring(*record->metacritic));
        if (!record->played()) parts.push_back(tr(L"jeszcze niegrana"));
        if (parts.size() > 3) parts.resize(3);
        ranked.push_back({record->key, 0, record->title, {}, joined(parts), score});
    }
    return spread(std::move(ranked), rng);
}

std::wstring storeUrl(const Wish& wish, int start) {
    std::wstring url = L"https://store.steampowered.com/search/results/?query&start=" + std::to_wstring(std::max(0, start)) +
                       L"&count=100&dynamic_data=&sort_by=Reviews_DESC";
    if (wish.genre_tag) url += L"&tags=" + std::to_wstring(wish.genre_tag);
    if (wish.price == 1) url += L"&maxprice=free";
    else if (wish.price == 2) url += L"&specials=1";
    return url + L"&category1=998&infinite=1&cc=pl&l=english";
}

std::vector<Pick> pickStore(std::span<const glance::Item> items, const Wish& wish, const std::unordered_set<uint32_t>& owned, uint32_t seed, bool english) {
    (void)english; // Store rows carry no genre names; their year and reviews read the same in both languages.
    auto rng = seeded(seed, 0x7f4a7c15u);
    const auto [from, to] = eraYears(wish.era);
    std::unordered_set<uint32_t> seen;
    std::vector<Pick> ranked;
    for (const auto& item : items) {
        if (!item.app_id || item.title.empty() || owned.contains(item.app_id) || !seen.insert(item.app_id).second) continue;
        float score = 0;
        for (const auto& labels : wish.labels) {
            if (wish.genre_tag && labels.facet == Facet::genre) continue; // The search already filtered by it.
            const bool known = !item.tag_ids.empty() && !labels.steam_ids.empty();
            const bool fits = known && std::any_of(labels.steam_ids.begin(), labels.steam_ids.end(),
                [&](uint32_t id) { return std::find(item.tag_ids.begin(), item.tag_ids.end(), id) != item.tag_ids.end(); });
            score += !known ? -0.3f * labels.weight : fits ? labels.weight : -0.4f;
        }
        const int year = item.release.precision != glance::ReleasePrecision::unknown ? item.release.year : 0;
        if (wish.era) score += year ? (year >= from && year <= to ? 3.0f : -4.0f) : -1.0f;
        const int percent = item.review_percent.value_or(-1);
        const uint32_t count = item.review_count.value_or(0);
        if (wish.fame == 1) score += percent >= 90 && count >= 20000 ? 3.0f : percent >= 85 && count >= 5000 ? 1.5f : -0.5f;
        else if (wish.fame == 2) score += percent >= 90 && count >= 300 && count < 20000 ? 3.0f : percent >= 85 && count < 50000 ? 1.0f : count >= 100000 ? -1.5f : 0.0f;
        score += percent >= 0 ? static_cast<float>(percent) / 100.0f * 1.5f : 0.3f;
        score += std::log10(static_cast<float>(count) + 1.0f) * 0.15f;
        score += rng.unit() * 1.4f;
        std::vector<std::wstring> parts;
        if (year) parts.push_back(std::to_wstring(year));
        if (percent >= 0) parts.push_back(std::to_wstring(percent) + L"% " + tr(L"pozytywnych"));
        ranked.push_back({"steam:" + std::to_string(item.app_id), item.app_id, item.title, item.image, joined(parts), score});
    }
    return spread(std::move(ranked), rng);
}

std::optional<std::vector<glance::Item>> searchStore(const Wish& wish, const glance::Get& get, const std::filesystem::path& cache) {
    constexpr size_t page_limit = 2 * 1024 * 1024;
    std::vector<glance::Item> items;
    bool any = false;
    for (const int start : {0, 100}) {
        if (httpCancelled()) break;
        const auto url = storeUrl(wish, start);
        const auto file = cache.empty() ? std::filesystem::path{} : cache / (L"search-" + pageName(url) + L".json");
        const auto saved = file.empty() ? std::string{} : readPage(file, page_limit);
        auto page = saved.empty() ? std::nullopt : glance::parseUpcomingSearch(saved);
        if (!page || !fresh(file)) {
            HttpBudget budget(9000);
            const auto response = get(url, L"", page_limit);
            const std::string bytes(response.body.begin(), response.body.end());
            if (auto fetched = response.status == 200 ? glance::parseUpcomingSearch(bytes) : std::nullopt) {
                page = std::move(fetched);
                if (!file.empty() && !httpCancelled()) atomic_file::write(file, bytes);
            } // Else the older copy, when there is one.
        }
        if (!page) { if (!any) continue; break; }
        any = true;
        for (auto& item : page->items) items.push_back(std::move(item));
        if (page->start + 100 >= page->total) break;
    }
    if (!any) return std::nullopt;
    return items;
}
}
