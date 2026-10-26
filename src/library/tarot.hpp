#pragma once
// Tarot (src/app/tarot.cpp, drawn by src/ui/tarot_renderer.cpp): a short card reading that
// recommends at most three games. The first card always decides the deck: buy something new on
// Steam, or play what the library already holds. Then 4–7 more questions, drawn at random from
// those that deck can answer, narrow it down: genre, era, played or not, length, mood, company,
// setting, style, fame, price, readiness. Pure: the deck, the wish the chosen cards make, the
// library scoring over game index records, the Steam search URL and its scoring. The only I/O is
// searchStore, through an injected transport.
#include "game_index.hpp"
#include "../net/glance.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

namespace tarot {
enum class Pool : uint8_t { store, library };
enum class Facet : uint8_t { intent, genre, era, played, length, mood, company, setting, style, fame, price, ready };
// The picture in the middle of a card (tarot_renderer.cpp draws each one).
enum class Symbol : uint8_t {
    pentacle, lantern, sword, wand, crown, mountain, tower, wheel, hammer, eye, arrow, star,
    hourglass, sun, dawn, infinity, moon, keyhole, dice, spark, flame, blaze, cup, lightning, heart,
    figure, fellowship, swords, castle, planet, book, skull, mosaic, mirror, quill, eclipse, gem,
    gift, scales, gate, road
};
inline constexpr size_t symbol_count = static_cast<size_t>(Symbol::road) + 1;

inline constexpr size_t max_tags = 6;
struct Card {
    const wchar_t* name;    // trKey: the arcana ("Wojownik").
    const wchar_t* meaning; // trKey: what it asks for ("Akcja i walka").
    Symbol symbol;
    // The facet's choice: intent 0 store / 1 library; era 0 any, 1 before 2010, 2 2010–2017,
    // 3 since 2018; played 0 any, 1 played, 2 never; length 0 any, 1 short, 2 medium, 3 long;
    // fame 0 any, 1 acclaimed, 2 hidden gem; price 0 any, 1 free, 2 on sale; ready 0 any, 1 installed.
    int value = 0;
    // Genre, mood, company, setting, style: Steam genre/tag names (English) it favours; the first
    // one is the Steam search's tag filter for a genre.
    std::array<const wchar_t*, max_tags> tags{};
};
struct Question {
    Facet facet;
    const wchar_t* prompt; // trKey
    std::span<const Card> cards;
    size_t shown;          // Cards dealt: a random subset when the question has more.
    bool library, store;   // The decks that ask it.
    bool always;           // Asked in every reading of those decks.
};
// Every question; the first is the intent (buy or play).
std::span<const Question> questions();
const Question& question(Facet facet);

struct Spread {
    Facet facet = Facet::intent;
    std::vector<int> cards; // Indices into question(facet).cards, in the order dealt.
};
struct Reading {
    uint32_t seed = 0;
    std::vector<Spread> spreads;
    std::vector<int> answers; // Per answered spread: the chosen card's index into its question's cards.
    size_t total = 0;         // Spreads in the whole reading (5–8) once the intent is answered.
    bool complete() const { return !spreads.empty() && answers.size() >= spreads.size() && answers.size() > 1; }
    const Spread* current() const { return answers.size() < spreads.size() ? &spreads[answers.size()] : nullptr; }
    Pool pool() const;
    const Card* answer(Facet facet) const;
};
// A new reading: its first spread, the intent.
Reading begin(uint32_t seed);
// Answers the current spread with `card` (an index into the spread). The intent's answer deals
// the rest of the reading. False: no spread to answer or no such card.
bool choose(Reading& reading, size_t card);
// Takes the last answer back; taking back the intent drops the rest of the deal. False: none.
bool undo(Reading& reading);

struct Labels {
    Facet facet = Facet::genre;
    std::vector<std::wstring> folded; // game_index::fold of each favoured tag.
    std::vector<uint32_t> steam_ids;  // The same tags as Steam store tag IDs (when known).
    float weight = 0;
    std::wstring meaning;             // The card's meaning (translated), for the picks' lines.
};
struct Wish {
    Pool pool = Pool::library;
    std::vector<Labels> labels; // Genre, mood, company, setting, style.
    int era = 0, played = 0, length = 0, fame = 0, price = 0;
    bool installed = false;
    uint32_t genre_tag = 0;     // The store search's tag filter; 0 none.
};
Wish wish(const Reading& reading);
// The release years an era card asks for, inclusive; {0, 0} any.
std::pair<int, int> eraYears(int era);
// A Steam store tag's ID by its English name (the built-in table); 0 unknown.
uint32_t steamTagId(std::wstring_view name);

struct Pick {
    std::string key;       // gameKey() of a library game; "steam:<id>" for a store pick.
    uint32_t app_id = 0;   // Store picks.
    std::wstring title, image, detail; // detail: genre, year, rating… joined with " · ".
    float score = 0;
};
// The best three of the library for the wish, the third a wild card from the next best. Records
// that are no longer owned are left out. The scores' noise comes from `seed`.
std::vector<Pick> pickLibrary(std::span<const game_index::Record* const> records, const Wish& wish, uint32_t seed, bool english);
// Steam's search results page for the wish (JSON with results_html), from `start`.
std::wstring storeUrl(const Wish& wish, int start);
// The best three store games for the wish that the player does not own.
std::vector<Pick> pickStore(std::span<const glance::Item> items, const Wish& wish, const std::unordered_set<uint32_t>& owned, uint32_t seed, bool english);
// Worker-only: two pages of Steam's search for the wish; nullopt when neither could be read.
// With a `cache` folder each page is kept there and reused for 30 minutes (Deal again, the same
// cards); an older copy stands in when Steam cannot be reached.
std::optional<std::vector<glance::Item>> searchStore(const Wish& wish, const glance::Get& get, const std::filesystem::path& cache = {});
}
