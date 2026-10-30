// The companion's mind: affect and its baseline, her body clock (nights, energy), attachment and
// time passing between messages (tick, onShift). Other parts: companion_mind_turns.cpp,
// companion_mind_appraisal.cpp, companion_mind_life.cpp, companion_mind_workspace.cpp and
// companion_mind_store.cpp; shared helpers in companion_mind_internal.hpp.
#include "companion_mind.hpp"
#include "companion_mind_internal.hpp"
#include "companion_affinity.hpp"
#include "companion_chats.hpp"
#include "../library/game_index.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cwctype>
#include <tuple>
#include <utility>

namespace companion::mind {
using namespace detail;

Attachment attachmentOf(const Context& context) {
    if (context.character) {
        const auto& a = context.character->attachment;
        if (a == "secure") return Attachment::secure;
        if (a == "anxious") return Attachment::anxious;
        if (a == "avoidant") return Attachment::avoidant;
        if (a == "fearful") return Attachment::fearful;
    }
    switch (context.persona) {
        case Persona::yandere: case Persona::dandere: return Attachment::anxious;
        case Persona::kuudere: case Persona::himedere: case Persona::kanedere: return Attachment::avoidant;
        case Persona::tsundere: case Persona::kichidere: return Attachment::fearful;
        default: return Attachment::secure;
    }
}

std::wstring attachmentName(Attachment attachment, bool english) {
    switch (attachment) {
        case Attachment::anxious: return english ? L"anxious" : L"lękowy";
        case Attachment::avoidant: return english ? L"avoidant" : L"unikający";
        case Attachment::fearful: return english ? L"fearful-avoidant" : L"lękowo-unikający";
        default: return english ? L"secure" : L"bezpieczny";
    }
}

uint64_t eventDue(std::wstring_view when, uint64_t now) {
    const auto w = spaced(when);
    // Today and the days ahead by name (UTC days; good enough to ask "how did it go" after it).
    const int today = static_cast<int>((now / day + 4) % 7); // 1970-01-01 was a Thursday; 0 = Sunday.
    static constexpr std::array<std::array<const wchar_t*, 4>, 7> names{{
        {L"niedziela", L"niedziele", L"sunday", L"sun"}, {L"poniedzialek", L"poniedzialku", L"monday", L"mon"},
        {L"wtorek", L"wtorku", L"tuesday", L"tue"}, {L"sroda", L"srode", L"wednesday", L"wed"},
        {L"czwartek", L"czwartku", L"thursday", L"thu"}, {L"piatek", L"piatku", L"friday", L"fri"},
        {L"sobota", L"sobote", L"saturday", L"sat"}}};
    for (int d = 0; d < 7; ++d)
        for (const auto* name : names[static_cast<size_t>(d)])
            if (w.find(std::wstring(L" ") + name + L" ") != std::wstring::npos) {
                int ahead = (d - today + 7) % 7;
                if (ahead == 0) ahead = 7;
                return now + static_cast<uint64_t>(ahead) * day + 6 * hour;
            }
    if (hasAny(w, {L"dzis", L"dzisiaj", L"wieczorem", L"za chwile", L"zaraz", L"today", L"tonight", L"later", L"soon"})) return now + 5 * hour;
    if (hasAny(w, {L"pojutrze", L"day after tomorrow"})) return now + 2 * day + 6 * hour;
    if (hasAny(w, {L"jutro", L"tomorrow"})) return now + day + 6 * hour;
    if (hasAny(w, {L"weekend", L"w weekend", L"weekendzie", L"this weekend"})) {
        const int to_sunday = (7 - today) % 7;
        return now + static_cast<uint64_t>(to_sunday == 0 ? 7 : to_sunday) * day + 6 * hour;
    }
    if (hasAny(w, {L"za tydzien", L"w przyszlym tygodniu", L"next week", L"in a week"})) return now + 7 * day;
    if (hasAny(w, {L"za miesiac", L"w przyszlym miesiacu", L"next month", L"in a month"})) return now + 30 * day;
    return now + 2 * day;
}

Affect baseline(Persona persona) {
    static constexpr std::array<std::pair<float, float>, persona_count> table{{
        {0.4f, 0.7f},   // genki
        {0.1f, 0.6f},   // tsundere
        {0.3f, 0.5f},   // yandere
        {0.0f, 0.2f},   // kuudere
        {-0.1f, 0.35f}, // dandere
        {0.5f, 0.6f},   // deredere
        {0.2f, 0.5f},   // himedere
        {0.0f, 0.75f},  // kichidere
        {0.0f, 0.25f},  // kanedere
    }};
    const auto [v, a] = table[static_cast<size_t>(persona) % table.size()];
    return {v, a, 0};
}

namespace {
Affect settleTo(const Affect& affect, const Affect& base, uint64_t now) {
    if (!affect.at) return {base.valence, base.arousal, now};
    if (now <= affect.at) return affect;
    const double keep = std::pow(0.5, static_cast<double>(now - affect.at) / (4.0 * hour));
    return {static_cast<float>(base.valence + (affect.valence - base.valence) * keep), static_cast<float>(base.arousal + (affect.arousal - base.arousal) * keep), now};
}
} // namespace

Affect settled(const Affect& affect, Persona persona, uint64_t now) { return settleTo(affect, baseline(persona), now); }

Tone lifeTone(const Life& life) {
    Tone tone;
    const auto add = [&](float valence, float arousal, float cap) {
        tone.valence += valence;
        tone.arousal += arousal;
        tone.energy = std::min(tone.energy, cap);
    };
    switch (life.health) {
    case LifeHealth::tired: add(-0.05f, -0.1f, 0.6f); break;
    case LifeHealth::sick: add(-0.2f, -0.2f, 0.4f); break;
    case LifeHealth::injured: add(-0.15f, -0.05f, 0.55f); break;
    case LifeHealth::recovering: add(-0.05f, -0.05f, 0.75f); break;
    case LifeHealth::fit: break;
    }
    switch (life.work) {
    case LifeWork::busy: add(0.0f, 0.1f, 0.75f); break;
    case LifeWork::between_jobs: add(-0.1f, 0.05f, 1.0f); break;
    case LifeWork::promoted: add(0.2f, 0.1f, 1.0f); break;
    case LifeWork::burnout: add(-0.25f, -0.15f, 0.45f); break;
    case LifeWork::steady: break;
    }
    switch (life.wealth) {
    case LifeWealth::tight: add(-0.05f, 0.0f, 1.0f); break;
    case LifeWealth::broke: add(-0.15f, 0.05f, 1.0f); break;
    case LifeWealth::flush: add(0.15f, 0.05f, 1.0f); break;
    case LifeWealth::comfortable: break;
    }
    switch (life.family) {
    case LifeFamily::visit: add(0.05f, 0.1f, 1.0f); break;
    case LifeFamily::tension: add(-0.15f, 0.15f, 1.0f); break;
    case LifeFamily::homesick: add(-0.15f, -0.05f, 1.0f); break;
    case LifeFamily::calm: break;
    }
    switch (life.social) {
    case LifeSocial::lonely: add(-0.1f, -0.05f, 1.0f); break;
    case LifeSocial::overwhelmed: add(-0.05f, 0.15f, 0.6f); break;
    case LifeSocial::inspired: add(0.15f, 0.15f, 1.0f); break;
    case LifeSocial::balanced: break;
    }
    tone.valence = std::clamp(tone.valence, -0.45f, 0.35f);
    tone.arousal = std::clamp(tone.arousal, -0.3f, 0.3f);
    return tone;
}

Affect baseline(const Context& context) {
    auto base = baseline(context.persona);
    const auto tone = lifeTone(context.life);
    base.valence = std::clamp(base.valence + tone.valence, -1.0f, 1.0f);
    base.arousal = clamp01(base.arousal + tone.arousal);
    return base;
}

Affect settled(const Affect& affect, const Context& context) { return settleTo(affect, baseline(context), context.now); }

namespace {
// One night of hers (her noon to noon): its hour shifts and whether she sleeps, the same all night.
uint64_t nightSeed(std::string_view look, int64_t night) {
    uint64_t hash = 1469598103934665603ull ^ (static_cast<uint64_t>(night) * 0x9E3779B97F4A7C15ull);
    for (const char ch : look) { hash ^= static_cast<unsigned char>(ch); hash *= 1099511628211ull; }
    hash ^= hash >> 31; hash *= 0xBF58476D1CE4E5B9ull; hash ^= hash >> 29;
    return hash;
}
} // namespace

std::optional<Night> nightAt(const Context& context) {
    if (!context.character || !context.now) return std::nullopt;
    const auto local_now = localSeconds(*context.character, context.now);
    if (!local_now) return std::nullopt;
    const int64_t local = static_cast<int64_t>(local_now);
    const int64_t offset = local - static_cast<int64_t>(context.now - context.now % 60); // Her clock against UTC (whole minutes).
    const int64_t one_day = static_cast<int64_t>(day), half = one_day / 2;
    const bool owl = context.character->night_owl;
    // From her noon: in bed at 23:00 (an owl 2:00), up at 7:00 (10:00), each ±30 min.
    const auto night = [&](int64_t index) {
        const uint64_t seed = nightSeed(context.look, index);
        const int64_t noon = index * one_day + half;
        const int64_t bed = noon + (owl ? 14 : 11) * static_cast<int64_t>(hour) + (static_cast<int64_t>(seed % 61) - 30) * 60;
        const int64_t up = noon + (owl ? 22 : 19) * static_cast<int64_t>(hour) + (static_cast<int64_t>((seed >> 12) % 61) - 30) * 60;
        return std::tuple{bed, up, (seed >> 24) % 1000 < 15};
    };
    int64_t index = (local - half) / one_day;
    auto [bed, up, sleepless] = night(index);
    if (local >= up) std::tie(bed, up, sleepless) = night(++index); // An owl's late morning: that night is over.
    if (bed - offset <= 0) return std::nullopt;
    return Night{static_cast<uint64_t>(bed - offset), static_cast<uint64_t>(up - offset), sleepless};
}

bool asleep(const Context& context) {
    const auto night = nightAt(context);
    return night && !night->sleepless && context.now >= night->from && context.now < night->to;
}

bool awake(const Context& context) { return !asleep(context); }

std::wstring describe(const Affect& affect, bool english) {
    const float v = affect.valence, a = affect.arousal;
    std::wstring mood;
    if (v <= -0.12f && a >= 0.65f) mood = english ? (v <= -0.5f ? L"really irritated" : L"irritated") : (v <= -0.5f ? L"bardzo rozdrażniona" : L"rozdrażniona");
    else if (v <= -0.6f) mood = english ? L"very low" : L"bardzo przygnębiona";
    else if (v <= -0.3f) mood = english ? L"low" : L"przygnębiona";
    else if (v <= -0.12f) mood = english ? L"a little down" : L"lekko przygaszona";
    else if (v < 0.12f) mood = english ? L"in a neutral mood" : L"w neutralnym nastroju";
    else if (v < 0.3f) mood = english ? L"in a decent mood" : L"w niezłym humorze";
    else if (v < 0.6f) mood = english ? L"in a good mood" : L"w dobrym humorze";
    else mood = english ? L"in a great mood" : L"w świetnym humorze";
    std::wstring energy;
    if (a < 0.2f) energy = english ? L"quiet and drowsy" : L"wyciszona i senna";
    else if (a < 0.4f) energy = english ? L"calm" : L"spokojna";
    else if (a >= 0.85f) energy = english ? L"very wound up" : L"bardzo nakręcona";
    else if (a >= 0.65f && !(v <= -0.12f)) energy = english ? L"lively" : L"ożywiona";
    return energy.empty() ? mood : mood + L", " + energy;
}

Self& State::of(std::string_view look) {
    const auto found = selves.find(look);
    if (found != selves.end()) return found->second;
    return selves.emplace(std::string(look), Self{}).first->second;
}

const Self* State::find(std::string_view look) const {
    const auto found = selves.find(look);
    return found == selves.end() ? nullptr : &found->second;
}

float energyNow(const Self& self, const Context& context) {
    // Sapolsky (Behave): accumulated sleep debt from late-night talks lowers energy even the next afternoon.
    float energy = self.drives.energy - 0.45f * clamp01(self.sleep_debt) - 0.15f * clamp01(self.stress_load);
    if (context.character) {
        if (const auto clock = localClock(*context.character, context.now)) {
            const int h = clock->hour;
            if (context.character->night_owl) energy -= h >= 7 && h < 12 ? 0.35f : h >= 5 && h < 7 ? 0.15f : 0.0f;
            else energy -= h >= 0 && h < 6 ? 0.35f : h >= 22 ? 0.15f : 0.0f;
        }
    }
    return clamp01(std::min(energy, lifeTone(context.life).energy));
}

float cognitiveControl(const Self& self, const Context& context) {
    const float energy = energyNow(self, context);
    const float arousal = settled(self.affect, context).arousal;
    // HPA-axis cortisol load (Sapolsky) further impairs prefrontal vmPFC top-down inhibition.
    return clamp01(energy * (1.0f - 0.55f * arousal - 0.22f * clamp01(self.stress_load)));
}

bool isFirstMeeting(const Self& self, const Memory& memory, std::string_view look, int64_t affinity_points) {
    if (affinity_points > 0 || self.last_talk > 0 || !self.claims.empty()) return false;
    for (const auto& s : memory.summaries)
        if (!s.look.empty() && s.look == look) return false;
    for (const auto& n : memory.notes)
        if (!n.look.empty() && n.look == look) return false;
    return true;
}

bool readyToConfess(const Untruth& untruth, const Self& self, const Context& context) {
    if (untruth.confessed || context.now <= untruth.at) return false;
    if (context.trust >= untruth.trust_at + 8) return true;
    if (context.trust >= 72 && context.now >= untruth.at + 2 * hour) return true;
    if (untruth.feeling == "guilt" && context.now >= untruth.at + 6 * hour) return true;
    if (cognitiveControl(self, context) < 0.42f) return true;
    return false;
}

bool needsDeliberation(const Self& self, const Context& context) {
    if (self.peek_caught || context.tension >= 55 || context.stance == RelationStance::dilemma) return true;
    if (!self.subtext_note.empty() && context.now <= self.subtext_at + 15 * 60) return true;
    for (const auto& u : self.untruths)
        if (readyToConfess(u, self, context)) return true;
    for (const auto& e : self.expectations) {
        if ((e.outcome == 0 || e.outcome == 1) && context.now <= e.settled + 12 * hour) {
            const float surprise = e.outcome == 1 ? 1.0f - e.p : e.p;
            if (surprise >= 0.6f) return true;
        }
    }
    return false;
}

void tick(Self& self, const Context& context) {
    const uint64_t now = context.now;
    self.affect = settled(self.affect, context);
    auto& d = self.drives;
    if (d.at && now > d.at) {
        const double hours = std::min<double>(static_cast<double>(now - d.at) / hour, 30.0 * 24);
        const bool apart = !self.last_talk || now > self.last_talk + 2 * hour;
        // Missing him grows faster for an anxious attachment, slower for an avoidant one.
        const auto attachment = attachmentOf(context);
        const double missing = attachment == Attachment::anxious ? 1.4 : attachment == Attachment::fearful ? 1.2 : attachment == Attachment::avoidant ? 0.6 : 1.0;
        if (apart) d.bond = clamp01(d.bond + static_cast<float>(hours / 72.0 * 0.6 * missing));
        d.energy = clamp01(d.energy + static_cast<float>(hours * 0.25));
        // Curiosity comes back by itself: halfway to her character's own level in two hours (it never
        // stays spent for days), and grows a little beyond it while they are apart.
        {
            float base = 0.5f;
            switch (context.persona) {
                case Persona::genki: case Persona::yandere: base = 0.6f; break;
                case Persona::kichidere: base = 0.65f; break;
                case Persona::deredere: base = 0.55f; break;
                case Persona::dandere: case Persona::tsundere: base = 0.45f; break;
                case Persona::kuudere: case Persona::himedere: case Persona::kanedere: base = 0.4f; break;
            }
            if (d.curiosity < base) d.curiosity = base - (base - d.curiosity) * static_cast<float>(std::pow(0.5, hours / 2.0));
            if (apart) d.curiosity = clamp01(d.curiosity + static_cast<float>(hours / 24.0 * 0.1));
        }
        const auto drift = [&](float& value, float to, double per_day) {
            const double keep = std::pow(1.0 - per_day, hours / 24.0);
            value = static_cast<float>(to + (value - to) * keep);
        };
        drift(d.pride, 0.5f, 0.1);
        drift(d.autonomy, 0.3f, 0.2);
        // Panksepp's PLAY circuit decays over ~1.5 hours between exchanges; Sapolsky's HPA stress_load
        // decays slowly over ~24 hours, and sleep_debt clears only after a long uninterrupted rest (>= 6h apart).
        self.play_momentum = static_cast<float>(self.play_momentum * std::pow(0.5, hours / 1.5));
        if (self.play_momentum < 0.03f) self.play_momentum = 0.0f;
        self.stress_load = static_cast<float>(self.stress_load * std::pow(0.5, hours / 24.0));
        if (self.stress_load < 0.03f) self.stress_load = 0.0f;
        if (!self.last_talk || now >= self.last_talk + 6 * hour) {
            self.sleep_debt = clamp01(self.sleep_debt - static_cast<float>(hours / 16.0));
        }
    }
    d.at = now;

    // Her own goal: one at a time, slowly (one to three weeks, its stages at its thirds), a few
    // days' rest between. Next: one she set herself, else a starting goal of her character's not
    // yet taken up; after those she has none until she sets one (in a talk or her diary).
    const bool english = context.english;
    const auto* character = context.character;
    const auto& seeds = character ? character->goals(english) : std::vector<std::wstring>{};
    const auto openOwn = [&] {
        return std::find_if(self.goals.begin(), self.goals.end(), [](const Goal& g) { return g.kind == Goal::Kind::own && g.status == Goal::Status::open; });
    };
    auto open_own = openOwn();
    if (open_own == self.goals.end() && now >= self.goal_rest) {
        if (!self.aspirations.empty()) {
            auto next = std::move(self.aspirations.front());
            self.aspirations.erase(self.aspirations.begin());
            startOwnGoal(self, std::move(next), -1, now);
        } else {
            for (int index = 0; index < static_cast<int>(seeds.size()); ++index) {
                if (std::find(self.seeds_used.begin(), self.seeds_used.end(), index) != self.seeds_used.end()) continue;
                self.seeds_used.push_back(index);
                startOwnGoal(self, seeds[static_cast<size_t>(index)], index, now);
                break;
            }
        }
        open_own = openOwn();
    }
    if (open_own != self.goals.end()) {
        if (open_own->source >= 0 && static_cast<size_t>(open_own->source) < seeds.size()) open_own->text = seeds[static_cast<size_t>(open_own->source)];
        if (open_own->span <= 0) open_own->span = 7 + static_cast<int>(textHash(open_own->text, open_own->made / day) % 15);
        const int days = herDaysSince(open_own->made, context);
        open_own->stage = std::clamp(days * 3 / open_own->span, 0, 2);
        if (days >= open_own->span) finishOwnGoal(self, *open_own, context, english);
    }
    for (auto& goal : self.goals) {
        if (goal.status != Goal::Status::open) continue;
        // Passing a promise's deadline does not establish failure. Keep it until resolved.
        if (goal.kind == Goal::Kind::intention && now > goal.made + 7 * day) { goal.status = Goal::Status::dropped; goal.updated = now; }
    }
    std::erase_if(self.goals, [&](const Goal& g) { return g.status != Goal::Status::open && now > g.updated + 14 * day; });
    while (self.goals.size() > goal_limit) {
        const auto old = std::find_if(self.goals.begin(), self.goals.end(), [](const Goal& g) { return g.status != Goal::Status::open; });
        self.goals.erase(old != self.goals.end() ? old : self.goals.begin());
    }

    // Expectations past their time.
    for (auto& e : self.expectations) {
        if (e.outcome != -1 || !e.due || now <= e.due) continue;
        e.settled = now;
        if (e.presence) {
            e.outcome = 0;
            d.bond = clamp01(d.bond + 0.1f);
            self.affect.valence = std::clamp(self.affect.valence - 0.08f * e.p, -1.0f, 1.0f);
        } else if (!e.game.empty()) {
            e.outcome = 0;
            if (e.suggestion) d.pride = clamp01(d.pride - 0.05f);
        } else e.outcome = 2;
    }
    std::erase_if(self.expectations, [&](const Expectation& e) { return e.outcome != -1 && now > e.settled + 7 * day; });
    while (self.expectations.size() > expectation_limit) {
        const auto old = std::find_if(self.expectations.begin(), self.expectations.end(), [](const Expectation& e) { return e.outcome != -1; });
        self.expectations.erase(old != self.expectations.end() ? old : self.expectations.begin());
    }

    // Her plans whose time has come: they happened, or (now and then) fell through — more often
    // when she is unwell or swamped. A shared one goes the same way in both minds (its id decides).
    for (auto& e : self.episodes) {
        if (e.state != Episode::State::planned || e.at > now) continue;
        int chance = 12;
        if (e.partner.empty()) {
            if (context.life.health == LifeHealth::sick || context.life.health == LifeHealth::injured) chance += 25;
            else if (context.life.work == LifeWork::busy || context.life.health == LifeHealth::tired) chance += 10;
        }
        e.state = static_cast<int>(textHash(wide(e.id), 7) % 100) < chance ? Episode::State::fell_through : Episode::State::happened;
        if (e.state == Episode::State::fell_through) {
            // Causal life arc: a failed shared episode causes inter-companion friction; a failed
            // work/study or family episode leaves a real downstream consequence.
            if (!e.partner.empty() && std::find(self.failed_partners.begin(), self.failed_partners.end(), e.partner) == self.failed_partners.end())
                self.failed_partners.push_back(e.partner);
            const auto words = spaced(e.what + L" " + e.where + L" " + e.with);
            if (hasAny(words, {L"praca", L"pracy", L"zmiana", L"zlecenie", L"uczelnia", L"uczelni", L"wyklad", L"egzamin", L"warsztat", L"butik",
                               L"work", L"shift", L"class", L"exam", L"study", L"project"})) {
                self.affect.valence = std::clamp(self.affect.valence - 0.08f, -1.0f, 1.0f);
                self.affect.arousal = clamp01(self.affect.arousal + 0.08f);
                self.affect.at = now;
                self.stress_load = clamp01(self.stress_load + 0.18f);
            } else if (hasAny(words, {L"rodzina", L"rodzice", L"brat", L"bratem", L"siostra", L"mama", L"tata", L"family", L"parents", L"brother", L"sister"})) {
                self.affect.valence = std::clamp(self.affect.valence - 0.1f, -1.0f, 1.0f);
                self.affect.at = now;
                self.stress_load = clamp01(self.stress_load + 0.16f);
            }
        }
    }
    std::erase_if(self.episodes, [&](const Episode& e) { return e.state != Episode::State::planned && now > e.at + 30 * day; });
    std::erase_if(self.untruths, [&](const Untruth& u) { return u.confessed && now > u.at + 30 * day; });
    // Her curiosity: a question he never answered goes by; one set aside too long is let go.
    for (auto& c : self.curious) {
        if (c.outcome != -1) continue;
        if (c.at && now > c.at + 6 * hour) { c.outcome = 0; c.settled = now; }
        else if (!c.at && now > c.later_at + 3 * day) { c.outcome = 2; c.settled = now; }
    }
    std::erase_if(self.curious, [&](const CuriousAct& c) { return c.outcome != -1 && now > c.settled + 7 * day; });
    // His follow-ups: past their time she lets them go; threads of his fade within a day.
    for (auto& f : self.follow_ups)
        if (f.state == 0 && now > f.due + 4 * day) { f.state = 2; f.settled = now; }
    std::erase_if(self.follow_ups, [&](const FollowUp& f) { return f.state != 0 && now > f.settled + 14 * day; });
    std::erase_if(self.threads, [&](const Thread& t) { return now > t.at + day; });
    fadeBids(self.bids, now);

    // DMN (Default Mode Network): background rumination while apart when tension, guilt over a fib, or a broken expectation lingers.
    if (self.last_talk > 0 && now >= self.last_talk + 4 * hour && now >= self.last_rumination + 8 * hour) {
        std::wstring rumination;
        for (auto it = self.untruths.rbegin(); it != self.untruths.rend(); ++it) {
            if (!it->confessed && (it->feeling == "guilt" || it->reason == "shame" || it->reason == "care")) {
                rumination = english
                    ? L"While you were away, that little fib you told (" + it->said + L" — when really: " + it->truth + L") kept bothering your conscience."
                    : L"Gdy ‹go|jej› nie było, cały czas gryzło cię to drobne kłamstwo („" + it->said + L"”, chociaż naprawdę: „" + it->truth + L"”) i zastanawiałaś się, czy ‹mu|jej› się nie przyznać.";
                break;
            }
        }
        if (rumination.empty() && (context.stance == RelationStance::dilemma || context.tension >= 50)) {
            rumination = english ? L"While you were away, you kept turning over in your head what is happening between the two of you and why they make you hesitate."
                                 : L"Gdy ‹go|jej› nie było, cały czas mieliłaś w głowie to, co się między wami dzieje, i czemu tak się przy ‹nim|niej› wahasz.";
        } else if (rumination.empty()) {
            for (const auto& e : self.expectations) {
                if (e.outcome == 0 && e.settled >= self.last_talk && e.p >= 0.55f) {
                    rumination = english ? L"In the quiet between talks, you wondered why it didn't go the way you expected: " + e.text
                                         : L"W ciszy między rozmowami zastanawiałaś się, czemu nie wyszło tak, jak się spodziewałaś: " + e.text;
                    break;
                }
            }
        }
        if (!rumination.empty()) {
            self.last_rumination = now;
            self.ruminations.push_back({now, std::move(rumination)});
            if (self.ruminations.size() > 8) self.ruminations.erase(self.ruminations.begin());
        }
    }
}

void onShift(Self& self, const Context& context, const Shift& shift) {
    if (!shift.any()) return;
    const bool en = context.english;
    const uint64_t now = context.now;
    float dv = 0, da = 0;
    const auto add = [&](float valence, float arousal) { dv += valence; da += arousal; };
    if (shift.stance) switch (*shift.stance) {
        case RelationStance::dilemma: add(-0.1f, 0.2f); break;
        case RelationStance::distanced:
            add(-0.2f, -0.1f);
            self.drives.autonomy = clamp01(self.drives.autonomy + 0.15f);
            break;
        case RelationStance::broken: {
            add(0.35f, 0.3f);
            const std::wstring ms = en ? L"I let my guard down — something more than friendship began between us"
                                       : L"Przełamałam się — między nami zaczęło się coś więcej niż przyjaźń";
            addMilestone(self, ms, now);
            addKeyMemory(self, ms, now);
            break;
        }
        case RelationStance::open: add(0.15f, 0.05f); break;
    }
    if (shift.role) switch (*shift.role) {
        case RelationRole::romantic: {
            add(0.25f, 0.2f);
            const std::wstring ms = en ? L"‹He|She|They› and I decided it is more than friendship" : L"‹Ustaliliśmy z nim|Ustaliłyśmy z nią›, że to coś więcej niż przyjaźń";
            addMilestone(self, ms, now);
            addKeyMemory(self, ms, now);
            break;
        }
        case RelationRole::secret_romance: {
            add(0.2f, 0.25f);
            const std::wstring ms = en ? L"A secret romance with ‹him|her|them› began" : L"Zaczął się mój sekretny romans z ‹nim|nią›";
            addMilestone(self, ms, now);
            addKeyMemory(self, ms, now);
            break;
        }
        default: break;
    }
    if (shift.health) switch (*shift.health) {
        case LifeHealth::sick: add(-0.15f, -0.1f); break;
        case LifeHealth::injured: add(-0.2f, 0.1f); break;
        case LifeHealth::tired: add(-0.05f, -0.1f); self.sleep_debt = clamp01(self.sleep_debt + 0.25f); break;
        case LifeHealth::recovering: add(0.05f, 0.0f); break;
        case LifeHealth::fit: add(0.1f, 0.05f); break;
    }
    if (shift.work) switch (*shift.work) {
        case LifeWork::promoted:
            add(0.3f, 0.2f);
            addMilestone(self, en ? L"I had a big success at work" : L"Odniosłam duży sukces w pracy", now);
            self.stress_load = clamp01(self.stress_load - 0.15f);
            break;
        case LifeWork::burnout: add(-0.2f, -0.1f); self.stress_load = clamp01(self.stress_load + 0.35f); break;
        case LifeWork::between_jobs: add(-0.2f, 0.15f); self.stress_load = clamp01(self.stress_load + 0.28f); break;
        case LifeWork::busy: add(0.0f, 0.1f); self.stress_load = clamp01(self.stress_load + 0.18f); break;
        case LifeWork::steady: add(0.05f, 0.0f); break;
    }
    if (shift.wealth) switch (*shift.wealth) {
        case LifeWealth::broke: add(-0.2f, 0.1f); self.stress_load = clamp01(self.stress_load + 0.22f); break;
        case LifeWealth::flush: add(0.25f, 0.15f); break;
        case LifeWealth::tight: add(-0.05f, 0.0f); self.stress_load = clamp01(self.stress_load + 0.1f); break;
        case LifeWealth::comfortable: add(0.05f, 0.0f); break;
    }
    if (shift.family) switch (*shift.family) {
        case LifeFamily::tension: add(-0.2f, 0.2f); self.stress_load = clamp01(self.stress_load + 0.25f); break;
        case LifeFamily::visit: add(0.15f, 0.15f); break;
        case LifeFamily::homesick: add(-0.15f, -0.05f); break;
        case LifeFamily::calm: add(0.05f, 0.0f); break;
    }
    if (shift.social) switch (*shift.social) {
        case LifeSocial::lonely: add(-0.15f, -0.05f); self.drives.bond = clamp01(self.drives.bond + 0.1f); break;
        case LifeSocial::overwhelmed: add(-0.1f, 0.2f); break;
        case LifeSocial::inspired: add(0.2f, 0.2f); break;
        case LifeSocial::balanced: add(0.05f, 0.0f); break;
    }
    self.affect = settled(self.affect, context);
    self.affect.valence = std::clamp(self.affect.valence + std::clamp(dv, -0.5f, 0.5f), -1.0f, 1.0f);
    self.affect.arousal = clamp01(self.affect.arousal + std::clamp(da, -0.4f, 0.4f));
    self.affect.at = now;
}
} // namespace companion::mind
