#pragma once
// Gamification (Settings → "Grywalizacja"): each companion's sympathy for the person she talks
// with as points, shown as a level (1–50) and a small bar towards the next one; each level takes a
// little more than the last, so the top takes about three months of daily chats. Points come from
// messages, a daily visit (more for a streak of days in a row), playing a game she suggested or one
// from the backlog. The level sets the stage of their relationship in her prompt (strangers → a
// light flirt at 5 → more flirt at 10 → flirty friends at 20 → a crush at 30 → a long-distance
// relationship at 50, the last stage), unlocks costumes (companion_costumes.hpp) and photo folders.
// Below level 30 she may set a boundary when he goes much too far (flirting does not count): a
// yellow card first, costing nothing; another while it is live, a red one and a small penalty that
// never takes a level away. Kept in companion_affinity.txt; pure logic here, the app loads, saves
// and draws it (companion_affinity.cpp).
#include "companion.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <map>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace companion::affinity {
inline constexpr int max_level = 50;
// Points per source.
inline constexpr int64_t per_message = 10;      // Each message the player sends her.
inline constexpr int64_t suggested_game = 150;  // A session of a game she suggested (lately).
inline constexpr int64_t backlog_game = 100;    // A first session of a game never played before.
inline constexpr uint32_t game_minutes = 15;    // The least a session must last for those.
inline constexpr int suggestion_days = 14;      // How long her suggestion counts.
// The day's first message: 20, then 5 more for every day of the streak (up to 70).
inline constexpr int64_t dailyBonus(int streak) { return 20 + 5 * std::clamp(streak - 1, 0, 10); }

// Points from one level to the next, growing every level: 240 at level 1, 260 at 2, 280 at 3 …
// (level 5 ≈ 1 100 points, 10 ≈ 2 900, 20 ≈ 8 000, 30 ≈ 15 000, the top at 50 ≈ 35 300: with about
// 400 points a day — some thirty messages, the streak and a game — about three months).
inline constexpr int64_t step(int level) {
    const int64_t k = level - 1;
    return 240 + 20 * k;
}
// Points needed to reach a level.
inline constexpr int64_t threshold(int level) {
    int64_t total = 0;
    for (int at = 1; at < level; ++at) total += step(at);
    return total;
}
inline constexpr int64_t max_points = threshold(max_level);

struct Standing {
    int level = 1;
    float progress = 0; // 0–1 towards the next level; 1 at the top.
};
inline Standing standing(int64_t points) {
    points = std::clamp<int64_t>(points, 0, max_points);
    Standing out;
    while (out.level < max_level && points >= step(out.level)) points -= step(out.level++);
    out.progress = out.level >= max_level ? 1.0f : static_cast<float>(points) / static_cast<float>(step(out.level));
    return out;
}
inline int level(int64_t points) { return standing(points).level; }

// The stage of their relationship (companion.cpp words it for her prompt; her mind reads it too):
// strangers, a light flirt from 5, more flirt from 10, flirty friends from 20, a crush from 30 and,
// at the top, a long-distance relationship — they stop there (she lives in her own city). Non-romantic
// tracks (friend, coworker) word the same six steps their own way.
enum class Stage { strangers, light_flirt, flirt, friends, crush, long_distance };
inline constexpr int stage_count = 6;
inline constexpr Stage stage(int level) {
    return level >= max_level ? Stage::long_distance : level >= 30 ? Stage::crush : level >= 20 ? Stage::friends :
           level >= 10 ? Stage::flirt : level >= 5 ? Stage::light_flirt : Stage::strangers;
}
// Gamification off: no level, so a middle stage (flirty friends) stands in for her mind's gates.
inline constexpr Stage unlevelled_stage = Stage::friends;
// How strongly she may feel about him at a stage — her tender feelings in her hidden appraisal
// (affection, longing, jealousy): nobody adores a near stranger. No ceiling from a crush on.
inline constexpr float feelingCap(Stage stage) {
    switch (stage) {
    case Stage::strangers: return 0.3f;
    case Stage::light_flirt: return 0.5f;
    case Stage::flirt: return 0.65f;
    case Stage::friends: return 0.8f;
    default: return 1.0f;
    }
}

// Per-character relationship track, player-imposed cap, her internal stance, unspoken tension
// and fluid life state (health, work/studies, finances, family, social energy).
struct Relation {
    RelationRole role = RelationRole::auto_role;
    RelationStance stance = RelationStance::open;
    int player_cap = 0; // 0: no cap; > 0: max level allowed for prompt stage (e.g. 9 = light flirt, 19 = flirt).
    int tension = 0;    // 0–100: cross-boundary tension (coworker romance, friend-zone dilemma, rare orientation exception).
    LifeHealth health = LifeHealth::fit;
    LifeWork work = LifeWork::steady;
    LifeWealth wealth = LifeWealth::comfortable;
    LifeFamily family = LifeFamily::calm;
    LifeSocial social = LifeSocial::balanced;
    int trust = 50;    // 0–100: epistemic & emotional trust (unlocks private episodes at >=65; drops on peeked leaks or boundary cards).
    int respect = 60;  // 0–100: agency & respect (rises when player defends own taste; drops on blind sycophancy).
    int security = 50; // 0–100: attachment security (rises with streaks/consistency; drops on long absence or rival gossip).
    bool defaultLife() const {
        return health == LifeHealth::fit && work == LifeWork::steady && wealth == LifeWealth::comfortable &&
               family == LifeFamily::calm && social == LifeSocial::balanced;
    }
    bool defaultAxes() const {
        return trust == 50 && respect == 60 && security == 50;
    }
    bool defaultValue() const {
        return role == RelationRole::auto_role && stance == RelationStance::open && player_cap <= 0 && tension <= 0 && defaultLife() && defaultAxes();
    }
    bool operator==(const Relation&) const = default;
};

// Whether her attraction matches `player_gender` without needing a rare-exception breakthrough.
inline constexpr bool compatibleAttraction(Attraction attraction, Gender player_gender) {
    switch (attraction) {
    case Attraction::any: return true;
    case Attraction::none: return false;
    case Attraction::male: return player_gender != Gender::female;
    case Attraction::female: return player_gender == Gender::female;
    }
    return true;
}

// Resolves the active relationship track from player choice, character.txt defaults, attraction and breakthroughs.
inline constexpr RelationRole effectiveRole(RelationRole stored, RelationRole character_default, Attraction attraction,
                                            Gender player_gender, RelationStance stance) {
    const RelationRole base = stored != RelationRole::auto_role ? stored : character_default;
    if (stance == RelationStance::broken) {
        return base == RelationRole::coworker || base == RelationRole::secret_romance ? RelationRole::secret_romance : RelationRole::romantic;
    }
    if (base != RelationRole::auto_role) return base;
    if (!compatibleAttraction(attraction, player_gender)) return RelationRole::friend_role;
    return RelationRole::romantic;
}

// Effective stage after applying style pace (traditional slow-burn vs casual), player distance cap,
// dilemma glass ceiling and her own emotional pullback (distanced).
inline constexpr Stage effectiveStage(int level, const Relation& rel, RelationStyle style = RelationStyle::balanced) {
    int eff = std::clamp(level, 1, max_level);
    if (rel.player_cap > 0) eff = std::min(eff, rel.player_cap);
    Stage s = Stage::strangers;
    // Slow-burn (traditional) a little later, casual a little sooner; the long-distance relationship
    // only at the top for everyone.
    if (style == RelationStyle::traditional && rel.stance != RelationStance::broken) {
        s = eff >= max_level ? Stage::long_distance : eff >= 33 ? Stage::crush : eff >= 22 ? Stage::friends :
            eff >= 12 ? Stage::flirt : eff >= 7 ? Stage::light_flirt : Stage::strangers;
    } else if (style == RelationStyle::casual) {
        s = eff >= max_level ? Stage::long_distance : eff >= 28 ? Stage::crush : eff >= 18 ? Stage::friends :
            eff >= 9 ? Stage::flirt : eff >= 4 ? Stage::light_flirt : Stage::strangers;
    } else {
        s = stage(eff);
    }
    // An unresolved dilemma holds a glass ceiling at flirty friends until broken or distanced.
    if (rel.stance == RelationStance::dilemma && s > Stage::friends) s = Stage::friends;
    // When she decides to catch distance on her own, she steps the emotional closeness down by one stage.
    if (rel.stance == RelationStance::distanced && s > Stage::strangers)
        s = static_cast<Stage>(static_cast<int>(s) - 1);
    return s;
}

// Days as yyyymmdd.
inline std::chrono::year_month_day date(int yyyymmdd) {
    return {std::chrono::year{yyyymmdd / 10000}, std::chrono::month{static_cast<unsigned>(yyyymmdd / 100 % 100)}, std::chrono::day{static_cast<unsigned>(yyyymmdd % 100)}};
}
// Whole days from `from` to `to`; a large number when either is not a date.
inline int daysBetween(int from, int to) {
    const auto a = date(from), b = date(to);
    if (!a.ok() || !b.ok()) return 1'000'000;
    return static_cast<int>((std::chrono::sys_days{b} - std::chrono::sys_days{a}).count());
}
// Whether `today` is the day after `last`.
inline bool dayAfter(int last, int today) { return daysBetween(last, today) == 1; }

// How much jealousy fits now, from the stage and her stance on exclusivity (character.txt
// exclusivity=, companion.hpp Exclusivity): none with a near stranger; a joke while they flirt; real
// but light among flirty friends for a monogamous one; real in a crush — and less again the nearer
// she gets to the top, where she feels sure of him. An open one never minds beyond a playful act
// (at the top not even that); a fluid one is undecided, her day (`day`) tips it.
enum class Jealousy { none, joke, light, real };
inline constexpr Jealousy jealousy(Stage stage, Exclusivity exclusivity, int level, uint32_t day = 0) {
    switch (stage) {
    case Stage::strangers: return Jealousy::none;
    case Stage::light_flirt: case Stage::flirt: return Jealousy::joke;
    case Stage::friends:
        return exclusivity == Exclusivity::open ? Jealousy::joke : exclusivity == Exclusivity::fluid ? (day % 2 ? Jealousy::light : Jealousy::joke) : Jealousy::light;
    case Stage::crush:
        if (exclusivity == Exclusivity::open) return Jealousy::joke;
        if (exclusivity == Exclusivity::fluid) return day % 3 == 0 ? Jealousy::real : day % 3 == 1 ? Jealousy::light : Jealousy::joke;
        return level >= 40 ? Jealousy::light : Jealousy::real;
    case Stage::long_distance:
        return exclusivity == Exclusivity::open ? Jealousy::none : exclusivity == Exclusivity::fluid ? (day % 2 ? Jealousy::light : Jealousy::joke) : Jealousy::joke;
    }
    return Jealousy::none;
}

// He keeps her at a distance ([relacja:dystans], the chat's "…" menu): her stage stops where it is
// now — at least a light flirt's top (level 9), at most a flirt's (19).
inline constexpr int distanceCap(int level) { return std::clamp(level, 9, 19); }

// Her boundaries (her [granica] tag, companion.cpp): below this level (a crush and beyond) someone
// who goes much too far gets a card; from it on nothing he says is too forward any more.
inline constexpr int boundary_level = 30;
inline constexpr int card_days = 1;        // A card stays live the day it is given and the next.
inline constexpr int penalty_percent = 10; // A red card: this share of her current level's step.
// Points a red card takes: a small share of the current level's step (at least a message's worth),
// never below the level's threshold, so no level, costume or photo folder is ever taken away.
inline int64_t penalty(int64_t points) {
    points = std::clamp<int64_t>(points, 0, max_points);
    const int at = level(points);
    if (at >= max_level) return 0;
    return std::min(std::max(step(at) * penalty_percent / 100, per_message), points - threshold(at));
}
enum class Card { none, yellow, red };
struct Booking { int day = 0; bool red = false; }; // Her last card for the player and when.

// Points per character (by her key), the streak of days and her recent suggestions.
using Points = std::map<std::string, int64_t, std::less<>>;
inline int64_t pointsOf(const Points& points, std::string_view key) {
    const auto found = points.find(key);
    return found == points.end() ? 0 : found->second;
}
struct Pick { int day = 0; std::string key; }; // A game she suggested (gameKey) and when.
struct State {
    Points points{};
    int day = 0;    // yyyymmdd of the last day with a message to her.
    int streak = 0; // Days in a row with one, up to `day`.
    std::vector<Pick> picks; // Newest last, at most 40.
    std::map<std::string, Booking, std::less<>> cards; // Each character's last card, by her key.
    std::map<std::string, Relation, std::less<>> relations; // Per-character track, stance, cap and tension.
    Gender player_gender = Gender::unknown;

    Relation relationOf(std::string_view key) const {
        const auto found = relations.find(key);
        return found == relations.end() ? Relation{} : found->second;
    }
    Relation& relation(const std::string& key) { return relations[key]; }
    void pruneRelation(const std::string& key) {
        if (const auto found = relations.find(key); found != relations.end() && found->second.defaultValue())
            relations.erase(found);
    }
    // Applies an internal stance shift ([dylemat], [dystans], [przelamanie], [otwarcie]); returns true when changed.
    bool applyStance(const std::string& key, RelationStance next, RelationRole default_role = RelationRole::auto_role) {
        if (key.empty()) return false;
        auto& rel = relations[key];
        const auto before = rel;
        rel.stance = next;
        if (next == RelationStance::dilemma) {
            rel.tension = std::clamp(std::max(rel.tension + 35, 55), 0, 100);
        } else if (next == RelationStance::distanced) {
            rel.tension = std::clamp(rel.tension - 25, 0, 100);
        } else if (next == RelationStance::broken) {
            rel.tension = 100;
            const auto base = rel.role != RelationRole::auto_role ? rel.role : default_role;
            rel.role = (base == RelationRole::coworker || base == RelationRole::secret_romance)
                           ? RelationRole::secret_romance
                           : RelationRole::romantic;
        } else if (next == RelationStance::open) {
            rel.tension = std::clamp(rel.tension - 15, 0, 100);
        }
        const bool changed = !(rel == before);
        pruneRelation(key);
        return changed;
    }
    // Her card still live on `today` (given that day or the day before); none after that.
    Card card(std::string_view key, int today) const {
        const auto found = cards.find(key);
        if (found == cards.end()) return Card::none;
        const int age = daysBetween(found->second.day, today);
        return age >= 0 && age <= card_days ? (found->second.red ? Card::red : Card::yellow) : Card::none;
    }
    // She set a boundary on `today`: a yellow card first (nothing lost); while a card is live, a
    // red one and penalty(). From boundary_level on nothing when open/romantic, but non-romantic
    // tracks (friend/coworker), player-capped or distanced relationships still enforce boundaries.
    struct Booked { Card card = Card::none; int64_t lost = 0; };
    Booked book(const std::string& key, int today) {
        const auto rel = relationOf(key);
        const bool guarded_high = rel.role == RelationRole::friend_role || rel.role == RelationRole::coworker ||
                                  rel.stance == RelationStance::distanced || rel.player_cap > 0;
        if (today <= 0 || (!guarded_high && level(pointsOf(points, key)) >= boundary_level)) return {};
        const bool red = card(key, today) != Card::none;
        cards[key] = {today, red};
        adjustAxes(key, red ? -18 : -8, red ? -10 : -4, 0);
        if (!red) return {Card::yellow, 0};
        auto& value = points[key];
        const int64_t lost = penalty(value);
        value -= lost;
        return {Card::red, lost};
    }
    bool adjustAxes(const std::string& key, int d_trust, int d_respect, int d_security) {
        if (key.empty() || (!d_trust && !d_respect && !d_security)) return false;
        auto& rel = relations[key];
        const auto before = rel;
        rel.trust = std::clamp(rel.trust + d_trust, 0, 100);
        rel.respect = std::clamp(rel.respect + d_respect, 0, 100);
        rel.security = std::clamp(rel.security + d_security, 0, 100);
        const bool changed = !(rel == before);
        pruneRelation(key);
        return changed;
    }
    // The day's first message: the streak goes on (yesterday), or starts again. Returns the bonus,
    // 0 when today was counted already.
    int64_t visit(int today) {
        if (today <= 0 || day == today) return 0;
        streak = day > 0 && dayAfter(day, today) ? streak + 1 : 1;
        day = today;
        return dailyBonus(streak);
    }
    // Today, or yesterday's streak still open; 0 once a day was missed.
    int currentStreak(int today) const { return day == today || (day > 0 && dayAfter(day, today)) ? streak : 0; }
    void suggested(std::string key, int today) {
        if (key.empty()) return;
        std::erase_if(picks, [&](const Pick& pick) { return pick.key == key; });
        picks.push_back({today, std::move(key)});
        if (picks.size() > 250) picks.erase(picks.begin());
    }
    // Played: was it hers lately? (Each suggestion pays once.)
    bool takeSuggestion(const std::string& key, int today) {
        const auto found = std::find_if(picks.begin(), picks.end(), [&](const Pick& pick) { return pick.key == key; });
        if (found == picks.end()) return false;
        const int age = daysBetween(found->day, today);
        const bool fresh = age >= 0 && age <= suggestion_days;
        picks.erase(found);
        return fresh;
    }
};

// companion_affinity.txt: "<key>=1200" for any character key, "day=20261015", "streak=3",
// "pick=20261014 steam:620", "card=20261014 yellow hana" (or red) (so no character is called day,
// streak, pick or card: companion::characterKey).
// The first version's looks white and red are Yuki's and Akane's points (the larger one wins).
inline State parse(std::string_view text) {
    State state;
    const auto number = [](std::string_view value, int64_t& out) { return std::from_chars(value.data(), value.data() + value.size(), out).ec == std::errc{}; };
    while (!text.empty()) {
        const auto end = text.find('\n');
        auto line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        const auto equals = line.find('=');
        if (equals == std::string_view::npos) continue;
        const auto key = line.substr(0, equals), value = line.substr(equals + 1);
        int64_t parsed = 0;
        if (key == "pick") {
            const auto space = value.find(' ');
            if (space == std::string_view::npos || space + 1 >= value.size() || !number(value.substr(0, space), parsed) || state.picks.size() >= 250) continue;
            state.picks.push_back({static_cast<int>(parsed), std::string(value.substr(space + 1))});
            continue;
        }
        if (key == "card") { // "<yyyymmdd> yellow|red <key>"
            const auto first = value.find(' '), second = first == std::string_view::npos ? first : value.find(' ', first + 1);
            if (second == std::string_view::npos || !number(value.substr(0, first), parsed)) continue;
            const auto color = value.substr(first + 1, second - first - 1), who = value.substr(second + 1);
            if ((color != "yellow" && color != "red") || who.empty() || who.size() > 32) continue;
            state.cards[std::string(who)] = {static_cast<int>(std::clamp<int64_t>(parsed, 0, 99991231)), color == "red"};
            continue;
        }
        if (key == "_gender") {
            if (const auto g = genderFromKey(value)) state.player_gender = *g;
            continue;
        }
        if (key == "_rel") { // "<who> <role> <stance> <player_cap> <tension> [<health> <work> <wealth> <family> <social>]"
            auto rest = value;
            const auto nextToken = [&]() -> std::string_view {
                while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
                const auto sp = rest.find(' ');
                const auto tok = rest.substr(0, sp);
                rest = sp == std::string_view::npos ? std::string_view{} : rest.substr(sp + 1);
                return tok;
            };
            const auto who = nextToken(), role_tok = nextToken(), stance_tok = nextToken(), cap_tok = nextToken(), ten_tok = nextToken();
            const auto health_tok = nextToken(), work_tok = nextToken(), wealth_tok = nextToken(), family_tok = nextToken(), social_tok = nextToken();
            if (who.empty() || who.size() > 32) continue;
            Relation rel = state.relationOf(who);
            if (const auto r = relationRoleFromKey(role_tok)) rel.role = *r;
            if (const auto s = relationStanceFromKey(stance_tok)) rel.stance = *s;
            int64_t cap_val = 0, ten_val = 0;
            if (!cap_tok.empty() && number(cap_tok, cap_val)) rel.player_cap = static_cast<int>(std::clamp<int64_t>(cap_val, 0, max_level));
            if (!ten_tok.empty() && number(ten_tok, ten_val)) rel.tension = static_cast<int>(std::clamp<int64_t>(ten_val, 0, 100));
            if (const auto h = lifeHealthFromKey(health_tok)) rel.health = *h;
            if (const auto w = lifeWorkFromKey(work_tok)) rel.work = *w;
            if (const auto wl = lifeWealthFromKey(wealth_tok)) rel.wealth = *wl;
            if (const auto f = lifeFamilyFromKey(family_tok)) rel.family = *f;
            if (const auto so = lifeSocialFromKey(social_tok)) rel.social = *so;
            if (!rel.defaultValue()) state.relations[std::string(who)] = rel;
            else state.relations.erase(std::string(who));
            continue;
        }
        if (key == "_axes") { // "<who> <trust> <respect> <security>"
            auto rest = value;
            const auto nextToken = [&]() -> std::string_view {
                while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
                const auto sp = rest.find(' ');
                const auto tok = rest.substr(0, sp);
                rest = sp == std::string_view::npos ? std::string_view{} : rest.substr(sp + 1);
                return tok;
            };
            const auto who = nextToken(), t_tok = nextToken(), r_tok = nextToken(), s_tok = nextToken();
            if (who.empty() || who.size() > 32) continue;
            Relation rel = state.relationOf(who);
            int64_t t_val = 50, r_val = 60, s_val = 50;
            if (!t_tok.empty() && number(t_tok, t_val)) rel.trust = static_cast<int>(std::clamp<int64_t>(t_val, 0, 100));
            if (!r_tok.empty() && number(r_tok, r_val)) rel.respect = static_cast<int>(std::clamp<int64_t>(r_val, 0, 100));
            if (!s_tok.empty() && number(s_tok, s_val)) rel.security = static_cast<int>(std::clamp<int64_t>(s_val, 0, 100));
            if (!rel.defaultValue()) state.relations[std::string(who)] = rel;
            else state.relations.erase(std::string(who));
            continue;
        }
        if (!number(value, parsed)) continue;
        if (key == "day") state.day = static_cast<int>(std::clamp<int64_t>(parsed, 0, 99991231));
        else if (key == "streak") state.streak = static_cast<int>(std::clamp<int64_t>(parsed, 0, 100000));
        else if (!key.empty() && key.size() <= 32 && std::all_of(key.begin(), key.end(), [](char ch) {
                     return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-'; })) {
            const std::string_view look = key == "white" ? std::string_view("yuki") : key == "red" ? std::string_view("akane") : key;
            auto& points = state.points[std::string(look)];
            points = std::max(points, std::clamp<int64_t>(parsed, 0, max_points));
        }
    }
    return state;
}
inline std::string serialize(const State& state) {
    std::string out;
    for (const auto& [key, points] : state.points) out += key + '=' + std::to_string(points) + '\n';
    out += "day=" + std::to_string(state.day) + "\nstreak=" + std::to_string(state.streak) + '\n';
    for (const auto& pick : state.picks) out += "pick=" + std::to_string(pick.day) + ' ' + pick.key + '\n';
    for (const auto& [key, booking] : state.cards) out += "card=" + std::to_string(booking.day) + (booking.red ? " red " : " yellow ") + key + '\n';
    if (state.player_gender != Gender::unknown) out += std::string("_gender=") + genderKey(state.player_gender) + '\n';
    for (const auto& [key, rel] : state.relations) {
        if (rel.defaultValue()) continue;
        const bool has_rel = !(rel.role == RelationRole::auto_role && rel.stance == RelationStance::open && rel.player_cap <= 0 && rel.tension <= 0 && rel.defaultLife());
        if (has_rel || rel.defaultAxes()) {
            out += "_rel=" + key + ' ' + relationRoleKey(rel.role) + ' ' + relationStanceKey(rel.stance) + ' ' +
                   std::to_string(std::clamp(rel.player_cap, 0, max_level)) + ' ' + std::to_string(std::clamp(rel.tension, 0, 100));
            if (!rel.defaultLife()) {
                out += std::string(" ") + lifeHealthKey(rel.health) + ' ' + lifeWorkKey(rel.work) + ' ' +
                       lifeWealthKey(rel.wealth) + ' ' + lifeFamilyKey(rel.family) + ' ' + lifeSocialKey(rel.social);
            }
            out += '\n';
        }
        if (!rel.defaultAxes()) {
            out += "_axes=" + key + ' ' + std::to_string(std::clamp(rel.trust, 0, 100)) + ' ' +
                   std::to_string(std::clamp(rel.respect, 0, 100)) + ' ' + std::to_string(std::clamp(rel.security, 0, 100)) + '\n';
        }
    }
    return out;
}
} // namespace companion::affinity
