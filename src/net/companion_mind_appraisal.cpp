// The slow appraisal (System 2): the model's hidden <ocena> block parsed and applied to her
// beliefs, goals, claims and curiosity; aspirations and what he teaches her (idioms, concepts,
// key memories, his world).
#include "companion_mind_internal.hpp"
#include "companion_affinity.hpp"
#include "json.hpp"

#include <algorithm>
#include <cmath>
#include <functional>

namespace companion::mind {
using namespace detail;

Appraisal parseAppraisal(std::wstring_view text) {
    Appraisal out;
    const auto open = text.find(L'{'), close = text.rfind(L'}');
    if (open == std::wstring_view::npos || close == std::wstring_view::npos || close < open) return out;
    const auto parsed = json::parse(utf8(text.substr(open, close - open + 1)), 64 * 1024);
    if (!parsed || parsed->type != json::Value::Type::object) return out;
    const auto& root = *parsed;
    out.social = social::parse(root.get("social"));
    const auto str = [](const json::Value* v, size_t max) { return v && v->type == json::Value::Type::string ? clean(wide(v->text), max) : std::wstring{}; };
    const auto num = [](const json::Value* v) -> std::optional<float> {
        if (!v) return std::nullopt;
        if (v->type == json::Value::Type::number) return static_cast<float>(v->numeric);
        if (v->type == json::Value::Type::string) { try { return std::stof(v->text); } catch (...) {} }
        return std::nullopt;
    };
    const auto list = [](const json::Value* v) {
        std::vector<const json::Value*> items;
        if (!v) return items;
        if (v->type == json::Value::Type::array) for (const auto& item : v->elements) items.push_back(&item);
        else if (v->type != json::Value::Type::null_value) items.push_back(v);
        return items;
    };
    if (const auto v = num(root.get("v"))) out.valence = std::clamp(*v, -1.0f, 1.0f);
    if (const auto a = num(root.get("a"))) out.arousal = std::clamp(*a, 0.0f, 1.0f);
    out.feels = str(root.get("czuje"), 60);
    if (out.feels.empty()) out.feels = str(root.get("feels"), 60);
    if (const auto* kind = root.get("uczucie")) out.feeling = feelingFromKey(kind->stringOr());
    if (!out.feeling) if (const auto* kind = root.get("feeling")) out.feeling = feelingFromKey(kind->stringOr());
    if (!out.feeling) { out.feeling = feelingFromText(out.feels); out.feeling_inferred = out.feeling.has_value(); }
    if (const auto strength = num(root.get("sila_uczucia")); strength && std::isfinite(*strength)) out.feeling_intensity = std::clamp(*strength, 0.0f, 1.0f);
    out.player = str(root.get("jego_stan"), 140);
    if (out.player.empty()) out.player = str(root.get("gracz"), 140); // Older replies' key.
    for (const auto* key : {"spelnione", "zlamane"})
        for (const auto* item : list(root.get(key)))
            if (item->type == json::Value::Type::string && !item->text.empty() && item->text.size() <= 12)
                (std::string_view(key) == "spelnione" ? out.met : out.broken).push_back(bareId(item->text));
    for (const auto* item : list(root.get("wiem"))) {
        Appraisal::Belief belief;
        if (item->type == json::Value::Type::string) belief.text = clean(wide(item->text), 200);
        else if (item->type == json::Value::Type::object) {
            belief.text = str(item->get("t"), 200);
            if (const auto* key = item->get("fact"); key && key->type == json::Value::Type::string && key->text.size() <= 64 &&
                std::all_of(key->text.begin(), key->text.end(), [](unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'; }))
                belief.fact_key = key->text;
            if (const auto* z = item->get("z"); z && z->type == json::Value::Type::string) belief.source = sourceOf(wide(z->text));
            if (const auto p = num(item->get("p"))) belief.p = std::clamp(*p, 0.05f, 1.0f);
            if (const auto* s = item->get("sekret"); s && s->type == json::Value::Type::boolean) belief.secret = s->flag;
            const auto kind = game_index::fold(str(item->get("k"), 20));
            if (kind == L"miejsce" || kind == L"place" || kind == L"lokalizacja" || kind == L"location") belief.topic = "place";
        }
        if (belief.source == "inferred" && !num(item->type == json::Value::Type::object ? item->get("p") : nullptr)) belief.p = 0.5f;
        if (game_index::fold(belief.text).size() >= 4 && out.beliefs.size() < 4) out.beliefs.push_back(std::move(belief));
    }
    for (const auto* item : list(root.get("zmien"))) {
        if (item->type != json::Value::Type::object) continue;
        Appraisal::Revision revision;
        if (const auto* id = item->get("id"); id && id->type == json::Value::Type::string) revision.id = bareId(id->text);
        revision.text = str(item->get("t"), 200);
        revision.p = num(item->get("p"));
        if (!revision.id.empty() && out.revisions.size() < 4) out.revisions.push_back(std::move(revision));
    }
    for (const auto* item : list(root.get("spodziewam"))) {
        Appraisal::Prediction prediction;
        if (item->type == json::Value::Type::string) prediction.text = clean(wide(item->text), 160);
        else if (item->type == json::Value::Type::object) {
            prediction.text = str(item->get("t"), 160);
            if (const auto p = num(item->get("p"))) prediction.p = std::clamp(*p, 0.05f, 0.95f);
            prediction.due = str(item->get("do"), 20);
            prediction.game = str(item->get("gra"), 120);
        }
        if (game_index::fold(prediction.text).size() >= 4 && out.predictions.size() < 3) out.predictions.push_back(std::move(prediction));
    }
    for (const auto* item : list(root.get("o_sobie")))
        if (item->type == json::Value::Type::string && out.claims.size() < 4)
            if (auto claim = clean(wide(item->text), 160); game_index::fold(claim).size() >= 6) out.claims.push_back(std::move(claim));
    out.intention = str(root.get("zamiar"), 160);
    out.new_goal = str(root.get("nowy_cel"), 160);
    if (const auto* promise = root.get("obiecuje")) {
        Appraisal::Promise p;
        if (promise->type == json::Value::Type::string) p.text = clean(wide(promise->text), 160);
        else if (promise->type == json::Value::Type::object) { p.text = str(promise->get("t"), 160); p.due = str(promise->get("do"), 20); }
        if (game_index::fold(p.text).size() >= 4) out.promise = std::move(p);
    }
    for (const auto* item : list(root.get("cel"))) {
        if (item->type != json::Value::Type::object) continue;
        Appraisal::Progress progress;
        if (const auto* id = item->get("id"); id && id->type == json::Value::Type::string) progress.id = bareId(id->text);
        progress.note = str(item->get("postep"), 160);
        progress.result = str(item->get("efekt"), 180);
        const auto status = game_index::fold(str(item->get("status"), 20));
        progress.done = status == L"done" || status == L"zrobione" || status == L"gotowe" || status == L"spelnione" || !progress.result.empty();
        progress.dropped = !progress.done && (status == L"dropped" || status == L"porzucone");
        if ((!progress.note.empty() || progress.done || progress.dropped) && out.goals.size() < 3) out.goals.push_back(std::move(progress));
    }
    for (const auto* key : {"mowie", "tell"})
        for (const auto* item : list(root.get(key)))
            if (item->type == json::Value::Type::string && !item->text.empty() && item->text.size() <= 12 && out.told.size() < 4) out.told.push_back(bareId(item->text));
    for (const auto* key : {"klamstwo", "untruth", "fib"})
        if (const auto* u = root.get(key); u && u->type == json::Value::Type::object) {
            Appraisal::Fib fib;
            fib.said = str(u->get("powiedziala"), 160);
            if (fib.said.empty()) fib.said = str(u->get("said"), 160);
            fib.truth = str(u->get("prawda"), 160);
            if (fib.truth.empty()) fib.truth = str(u->get("truth"), 160);
            const auto r = game_index::fold(str(u->get("powod") ? u->get("powod") : u->get("reason"), 24));
            if (r.starts_with(L"dum") || r == L"pride") fib.reason = "pride";
            else if (r.starts_with(L"cel") || r == L"goal") fib.reason = "goal";
            else if (r.starts_with(L"trosk") || r == L"care") fib.reason = "care";
            else fib.reason = "shame";
            const auto f = game_index::fold(str(u->get("odczucie") ? u->get("odczucie") : u->get("feeling"), 24));
            if (f.starts_with(L"ulg") || f == L"relief") fib.feeling = "relief";
            else if (f.starts_with(L"satysfak") || f.starts_with(L"dum") || f == L"satisfaction") fib.feeling = "satisfaction";
            else fib.feeling = "guilt";
            if (game_index::fold(fib.said).size() >= 4 && game_index::fold(fib.truth).size() >= 4) out.untruth = std::move(fib);
        }
    for (const auto* key : {"przyznaje", "confess"})
        for (const auto* item : list(root.get(key)))
            if (item->type == json::Value::Type::string && !item->text.empty() && item->text.size() <= 12 && out.confessed.size() < 4)
                out.confessed.push_back(bareId(item->text));
    for (const auto* key : {"nauczyl", "taught"})
        for (const auto* item : list(root.get(key))) {
            Appraisal::Taught t;
            if (item->type == json::Value::Type::object) {
                t.term = str(item->get("pojecie"), 60);
                if (t.term.empty()) t.term = str(item->get("term"), 60);
                t.meaning = str(item->get("znaczenie"), 160);
                if (t.meaning.empty()) t.meaning = str(item->get("meaning"), 160);
            } else if (item->type == json::Value::Type::string) {
                const auto whole = clean(wide(item->text), 200);
                const auto colon = whole.find(L':');
                t.term = clean(colon == std::wstring::npos ? whole : whole.substr(0, colon), 60);
                if (colon != std::wstring::npos) t.meaning = clean(whole.substr(colon + 1), 160);
            }
            if (game_index::fold(t.term).size() >= 3 && out.taught.size() < 3) out.taught.push_back(std::move(t));
        }
    for (const auto* key : {"przemyslenie", "rethink"})
        if (const auto* r = root.get(key); r && r->type == json::Value::Type::object) {
            Appraisal::Rethink rt;
            rt.topic = str(r->get("temat"), 80);
            if (rt.topic.empty()) rt.topic = str(r->get("topic"), 80);
            rt.view = str(r->get("nowe_spojrzenie"), 180);
            if (rt.view.empty()) rt.view = str(r->get("view"), 180);
            if (game_index::fold(rt.view).size() >= 6) out.rethink = std::move(rt);
        }
    out.inside_joke = str(root.get("nasz_zart"), 140);
    if (out.inside_joke.empty()) out.inside_joke = str(root.get("inside_joke"), 140);
    out.key_memory = str(root.get("wazne_wspomnienie"), 180);
    if (out.key_memory.empty()) out.key_memory = str(root.get("key_memory"), 180);
    for (const auto* key : {"swiat", "world", "swiat_gracza"})
        for (const auto* item : list(root.get(key))) {
            Appraisal::WorldItem w;
            if (item->type == json::Value::Type::object) {
                w.name = str(item->get("nazwa") ? item->get("nazwa") : item->get("kto") ? item->get("kto") : item->get("name"), 36);
                w.relation = str(item->get("opis") ? item->get("opis") : item->get("relacja") ? item->get("relacja") : item->get("relation"), 80);
            } else if (item->type == json::Value::Type::string) {
                const auto whole = clean(wide(item->text), 120);
                const auto sep = whole.find_first_of(L":-—");
                w.name = clean(sep == std::wstring::npos ? whole : whole.substr(0, sep), 36);
                if (sep != std::wstring::npos) w.relation = clean(whole.substr(sep + 1), 80);
            }
            if (game_index::fold(w.name).size() >= 2 && out.player_world.size() < 3) out.player_world.push_back(std::move(w));
        }
    for (const auto* key : {"waha_sie", "hesitation"})
        for (const auto* item : list(root.get(key)))
            if (item->type == json::Value::Type::string && out.hesitation.size() < 2)
                if (auto h = clean(wide(item->text), 160); game_index::fold(h).size() >= 4) out.hesitation.push_back(std::move(h));
    for (const auto* key : {"plec", "gender"}) {
        const auto g = game_index::fold(str(root.get(key), 24));
        if (g == L"mezczyzna" || g == L"male" || g == L"chlopak" || g == L"facet" || g == L"man" || g == L"guy") out.player_gender = Gender::male;
        else if (g == L"kobieta" || g == L"female" || g == L"dziewczyna" || g == L"woman" || g == L"girl") out.player_gender = Gender::female;
    }
    for (const auto* key : {"ciekawosc", "curiosity"}) {
        const auto* c = root.get(key);
        if (!c) continue;
        Appraisal::Curious curious;
        if (c->type == json::Value::Type::object) {
            const auto field = [&](const char* pl, const char* en) { const auto* v = c->get(pl); return v ? v : c->get(en); };
            const auto move = game_index::fold(str(field("ruch", "move"), 24));
            if (move.starts_with(L"dopyt") || move.starts_with(L"pyt") || move.starts_with(L"clarif") || move.starts_with(L"ask"))
                curious.move = CuriousMove::clarify;
            else if (move.starts_with(L"ciag") || move.starts_with(L"pociag") || move.starts_with(L"prowadz") || move.starts_with(L"dalej") || move.starts_with(L"lead"))
                curious.move = CuriousMove::lead;
            else if (move.starts_with(L"odbij") || move.starts_with(L"odzwierc") || move.starts_with(L"parafraz") || move.starts_with(L"reflect"))
                curious.move = CuriousMove::reflect;
            curious.about = str(field("o_co", "about"), 140);
            if (const auto sure = num(field("rozumiem", "understood")); sure && std::isfinite(*sure)) curious.understood = std::clamp(*sure, 0.0f, 1.0f);
            const auto when = game_index::fold(str(field("kiedy", "when"), 16));
            curious.later = when.starts_with(L"pozn") || when == L"later";
        } else if (c->type == json::Value::Type::string) {
            curious.about = str(c, 140);
        }
        if (curious.move != CuriousMove::none || game_index::fold(curious.about).size() >= 3) out.curious = std::move(curious);
        break;
    }
    for (const auto* key : {"jego_plan", "his_plan", "plan_gracza"})
        for (const auto* item : list(root.get(key))) {
            Appraisal::Plan plan;
            if (item->type == json::Value::Type::object) {
                plan.what = str(item->get("t") ? item->get("t") : item->get("co"), 120);
                plan.when = str(item->get("kiedy") ? item->get("kiedy") : item->get("when"), 40);
            } else if (item->type == json::Value::Type::string) plan.what = clean(wide(item->text), 120);
            if (game_index::fold(plan.what).size() >= 4 && out.plans.size() < 3) out.plans.push_back(std::move(plan));
        }
    out.ok = true;
    return out;
}

Applied apply(Self& self, Memory& memory, const Appraisal& appraisal, const Context& context, std::string_view guest,
              const std::function<std::string(std::wstring_view)>& game_key) {
    Applied applied;
    if (!appraisal.ok) return applied;
    const uint64_t now = context.now;
    // Her own read of her feelings moves her mood most of the way (emotions within a reply are hers to change).
    self.affect = settled(self.affect, context);
    if (appraisal.valence) self.affect.valence = std::clamp(0.4f * self.affect.valence + 0.6f * *appraisal.valence, -1.0f, 1.0f);
    if (appraisal.arousal) self.affect.arousal = clamp01(0.4f * self.affect.arousal + 0.6f * *appraisal.arousal);
    self.affect.at = now;
    if (!appraisal.feels.empty()) { self.felt = appraisal.feels; self.felt_at = now; }
    else if (appraisal.feeling) { const auto& info = feelingInfo(*appraisal.feeling); self.felt = context.english ? info.en : info.pl; self.felt_at = now; }
    if (appraisal.feeling) {
        // Her tender feelings about him only as strong as their stage allows (affinity::feelingCap):
        // a near stranger does not stir tenderness, longing or jealousy of 90%.
        auto intensity = appraisal.feeling_intensity;
        const auto kind = *appraisal.feeling;
        if (intensity && (kind == Feeling::affection || kind == Feeling::longing || kind == Feeling::jealousy))
            intensity = std::min(*intensity, affinity::feelingCap(static_cast<affinity::Stage>(std::clamp(context.stage, 0, affinity::stage_count - 1))));
        if (self.feelings.empty() || self.feelings.back().at != now || self.feelings.back().kind != *appraisal.feeling || self.feelings.back().reason != appraisal.feels)
            self.feelings.push_back({*appraisal.feeling, intensity, appraisal.feels, now, appraisal.feeling_inferred});
        if (self.feelings.size() > 80) self.feelings.erase(self.feelings.begin(), self.feelings.end() - 80);
    }
    if (!appraisal.player.empty()) { self.player_read = appraisal.player; self.player_read_at = now; }

    for (const auto& belief : appraisal.beliefs) {
        Memory::Facts facts;
        facts.look = context.look;
        facts.guest = std::string(guest);
        facts.source = belief.source;
        facts.confidence = belief.p;
        facts.secret = belief.secret;
        facts.topic = belief.topic;
        facts.fact_key = belief.fact_key;
        if (appraisal.feeling) { facts.emotion = feelingInfo(*appraisal.feeling).key; facts.feeling = appraisal.feels; }
        if (memory.note(belief.text, now, facts)) {
            ++applied.beliefs;
            // A fact eases her curiosity only a little: knowing more about him opens new questions (Loewenstein).
            self.drives.curiosity = clamp01(self.drives.curiosity - 0.03f);
        }
    }
    for (const auto& revision : appraisal.revisions)
        if (memory.revise(revision.id, revision.text, revision.p, context.look, now)) ++applied.revisions;

    const auto settle = [&](const std::string& id, bool met) {
        const auto found = std::find_if(self.expectations.begin(), self.expectations.end(), [&](const Expectation& e) { return e.id == id && e.outcome == -1; });
        if (found == self.expectations.end()) return false;
        found->outcome = met ? 1 : 0;
        found->settled = now;
        const float surprise = met ? 1 - found->p : found->p;
        self.affect.arousal = clamp01(self.affect.arousal + 0.15f * surprise);
        if (met) self.drives.pride = clamp01(self.drives.pride + 0.03f); // She read him right.
        return true;
    };
    for (const auto& id : appraisal.met) if (settle(id, true)) ++applied.met;
    for (const auto& id : appraisal.broken) if (settle(id, false)) ++applied.broken;

    for (const auto& prediction : appraisal.predictions) {
        const size_t open = static_cast<size_t>(std::count_if(self.expectations.begin(), self.expectations.end(), [](const Expectation& e) { return e.outcome == -1; }));
        if (open >= 12) break;
        Expectation e;
        e.id = newId(self, 'o');
        e.text = prediction.text;
        e.p = prediction.p;
        e.made = now;
        e.due = dueFrom(prediction.due, now);
        if (!prediction.game.empty() && game_key) {
            e.game = game_key(prediction.game);
            if (!e.game.empty()) e.title = prediction.game;
        }
        self.expectations.push_back(std::move(e));
        ++applied.predictions;
    }
    const auto untruth_folded = appraisal.untruth ? game_index::fold(appraisal.untruth->said) : std::wstring{};
    for (const auto& claim : appraisal.claims) {
        if (!untruth_folded.empty() && game_index::fold(claim) == untruth_folded) continue; // A conscious fib is not a permanent biographical truth!
        addClaim(self, claim, now);
        ++applied.claims;
    }
    if (applied.claims) self.drives.autonomy = clamp01(self.drives.autonomy - 0.08f);

    if (appraisal.untruth) {
        const auto said_f = game_index::fold(appraisal.untruth->said);
        std::erase_if(self.untruths, [&](const Untruth& u) { return game_index::fold(u.said) == said_f; });
        Untruth u;
        u.id = newId(self, 'u');
        u.at = now;
        u.said = appraisal.untruth->said;
        u.truth = appraisal.untruth->truth;
        u.reason = appraisal.untruth->reason;
        u.feeling = appraisal.untruth->feeling;
        u.trust_at = context.trust;
        self.untruths.push_back(std::move(u));
        if (self.untruths.size() > 24) self.untruths.erase(self.untruths.begin());
        if (appraisal.untruth->feeling == "guilt" || context.persona == Persona::dandere || context.persona == Persona::deredere) {
            self.affect.valence = std::clamp(self.affect.valence - 0.06f, -1.0f, 1.0f);
            self.affect.arousal = clamp01(self.affect.arousal + 0.05f);
        } else {
            self.drives.pride = clamp01(self.drives.pride + 0.05f);
        }
    }
    for (const auto& id : appraisal.confessed) {
        for (auto& u : self.untruths) {
            if (u.id == id && !u.confessed) {
                u.confessed = true;
                self.trust_delta += 4;
                self.security_delta += 3;
                self.affect.valence = std::clamp(self.affect.valence + 0.08f, -1.0f, 1.0f);
                addClaim(self, u.truth, now);
            }
        }
    }

    if (!appraisal.intention.empty()) {
        for (auto& goal : self.goals)
            if (goal.kind == Goal::Kind::intention && goal.status == Goal::Status::open) { goal.status = Goal::Status::dropped; goal.updated = now; }
        Goal goal;
        goal.id = newId(self, 'g');
        goal.text = appraisal.intention;
        goal.kind = Goal::Kind::intention;
        goal.made = goal.updated = now;
        self.goals.push_back(std::move(goal));
    }
    if (appraisal.promise) {
        const auto folded = game_index::fold(appraisal.promise->text);
        const bool known = std::any_of(self.goals.begin(), self.goals.end(), [&](const Goal& g) {
            return g.kind == Goal::Kind::promise && g.status == Goal::Status::open && game_index::fold(g.text) == folded; });
        if (!known) {
            Goal goal;
            goal.id = newId(self, 'g');
            goal.text = appraisal.promise->text;
            goal.kind = Goal::Kind::promise;
            goal.made = goal.updated = now;
            goal.due = dueFrom(appraisal.promise->due, now);
            self.goals.push_back(std::move(goal));
        }
    }
    for (const auto& progress : appraisal.goals) {
        auto found = std::find_if(self.goals.begin(), self.goals.end(), [&](const Goal& g) { return g.id == progress.id && g.status == Goal::Status::open; });
        if (found == self.goals.end() && progress.id.empty())
            found = std::find_if(self.goals.begin(), self.goals.end(), [](const Goal& g) { return g.kind == Goal::Kind::own && g.status == Goal::Status::open; });
        if (found == self.goals.end()) continue;
        if (!progress.note.empty()) found->progress = progress.note;
        found->updated = now;
        // Life takes time: her own goal is done at the earliest halfway through its span (before
        // that, "done" is only progress — and the finale is near).
        const bool ripe = found->kind != Goal::Kind::own || herDaysSince(found->made, context) * 2 >= std::max(found->span, 2);
        if (progress.done && ripe) {
            if (found->kind == Goal::Kind::own) finishOwnGoal(self, *found, context, context.english, progress.result);
            else { found->status = Goal::Status::done; self.affect.valence = std::clamp(self.affect.valence + 0.05f, -1.0f, 1.0f); }
        } else if (progress.done) {
            if (!progress.result.empty()) found->progress = progress.result;
            found->stage = 2;
        } else if (progress.dropped) {
            found->status = Goal::Status::dropped;
            if (found->kind == Goal::Kind::own) self.goal_rest = now + 3 * day;
        }
    }
    if (!appraisal.new_goal.empty()) aspire(self, context, appraisal.new_goal);
    for (const auto& id : appraisal.told)
        for (auto& e : self.episodes)
            if (e.id == id) e.told = true;
    for (const auto& t : appraisal.taught) learnConcept(self, t.term, t.meaning, now);
    if (!appraisal.inside_joke.empty()) addSharedIdiom(self, appraisal.inside_joke, now);
    if (!appraisal.key_memory.empty()) addKeyMemory(self, appraisal.key_memory, now);
    for (const auto& w : appraisal.player_world) addWorldEntity(self, w.name, w.relation, now);
    if (appraisal.rethink) {
        const std::wstring entry = appraisal.rethink->topic.empty()
                                       ? appraisal.rethink->view
                                       : appraisal.rethink->topic + L": " + appraisal.rethink->view;
        const auto folded = game_index::fold(entry);
        std::erase_if(self.rethought, [&](const Claim& c) { return game_index::fold(c.text) == folded; });
        self.rethought.push_back({now, clean(entry, 180)});
        if (self.rethought.size() > 20) self.rethought.erase(self.rethought.begin());
        if (!appraisal.rethink->topic.empty()) {
            if (auto* m = markerOf(self, appraisal.rethink->topic)) {
                m->cortical_inhibition = clamp01(m->cortical_inhibition + 0.35f);
                m->at = now;
            }
        }
    }
    if (!appraisal.hesitation.empty()) {
        self.counterfactual = appraisal.hesitation.front();
        self.counterfactual_at = now;
    }
    // Her curiosity: she asks back / carries the talk on now, or sets a question aside for later.
    if (appraisal.curious) {
        const auto& curious = *appraisal.curious;
        const auto move = curious.move != CuriousMove::none ? curious.move : self.urge.move != CuriousMove::none ? self.urge.move : CuriousMove::lead;
        std::wstring why = self.urge.move == move ? self.urge.why : std::wstring{};
        if (why.empty())
            why = move == CuriousMove::clarify ? (context.english ? L"she wasn't sure she understood ‹him|her|them›" : L"nie była pewna, czy dobrze ‹go|ją› rozumie")
                                               : (context.english ? L"out of her own curiosity" : L"z własnej ciekawości");
        if (curious.later) {
            const auto folded = game_index::fold(curious.about);
            const bool known = std::any_of(self.curious.begin(), self.curious.end(), [&](const CuriousAct& c) { return c.deferred() && game_index::fold(c.about) == folded; });
            if (folded.size() >= 4 && !known) {
                CuriousAct act;
                act.id = newId(self, 'c');
                act.move = move;
                act.about = curious.about;
                act.why = context.english ? L"it wasn't the right moment" : L"to nie była dobra chwila";
                act.later_at = now;
                self.curious.push_back(std::move(act));
                // At most three set aside: the oldest is let go.
                std::vector<size_t> waiting;
                for (size_t i = 0; i < self.curious.size(); ++i) if (self.curious[i].deferred()) waiting.push_back(i);
                if (waiting.size() > 3) { self.curious[waiting.front()].outcome = 2; self.curious[waiting.front()].settled = now; }
            }
        } else {
            // Her words decide (onReply): only a real question (or a reflection) is a curious act.
            self.pending_curious = {move, curious.about, std::move(why)};
        }
        keepCurious(self);
    }
    // Something coming up in his life: she will ask how it went after it.
    for (const auto& plan : appraisal.plans) {
        const auto folded = game_index::fold(plan.what);
        if (std::any_of(self.follow_ups.begin(), self.follow_ups.end(), [&](const FollowUp& f) { return f.state == 0 && game_index::fold(f.what) == folded; })) continue;
        FollowUp f;
        f.id = newId(self, 'f');
        f.what = plan.what;
        f.when = plan.when;
        f.made = now;
        f.due = eventDue(plan.when, now);
        self.follow_ups.push_back(std::move(f));
        if (self.follow_ups.size() > 12) {
            const auto old = std::find_if(self.follow_ups.begin(), self.follow_ups.end(), [](const FollowUp& x) { return x.state != 0; });
            self.follow_ups.erase(old != self.follow_ups.end() ? old : self.follow_ups.begin());
        }
    }
    return applied;
}

bool aspire(Self& self, const Context& context, std::wstring_view goal) {
    auto text = clean(goal, 160);
    const auto folded = game_index::fold(text);
    if (folded.size() < 6) return false;
    const auto same = [&](std::wstring_view other) { return game_index::fold(other) == folded; };
    if (std::any_of(self.goals.begin(), self.goals.end(), [&](const Goal& g) { return g.kind == Goal::Kind::own && g.status == Goal::Status::open && same(g.text); }) ||
        std::any_of(self.aspirations.begin(), self.aspirations.end(), same))
        return false;
    const bool busy = std::any_of(self.goals.begin(), self.goals.end(), [](const Goal& g) { return g.kind == Goal::Kind::own && g.status == Goal::Status::open; });
    if (!busy) {
        // Deciding on something new ends her rest.
        startOwnGoal(self, std::move(text), -1, context.now);
        self.goal_rest = 0;
        return true;
    }
    if (self.aspirations.size() >= 5) self.aspirations.erase(self.aspirations.begin());
    self.aspirations.push_back(std::move(text));
    return true;
}

bool addSharedIdiom(Self& self, std::wstring_view text, uint64_t now) {
    auto line = clean(text, 140);
    const auto folded = game_index::fold(line);
    if (folded.size() < 4 || line == L"-") return false;
    std::erase_if(self.shared_idioms, [&](const Claim& c) { return game_index::fold(c.text) == folded; });
    self.shared_idioms.push_back({now, std::move(line)});
    if (self.shared_idioms.size() > max_shared_idioms) self.shared_idioms.erase(self.shared_idioms.begin());
    return true;
}

bool learnConcept(Self& self, std::wstring_view term, std::wstring_view meaning, uint64_t now) {
    auto t = clean(term, 60);
    auto m = clean(meaning, 160);
    const auto folded = game_index::fold(t);
    if (folded.size() < 3) return false;
    for (auto& existing : self.learned_concepts) {
        if (game_index::fold(existing.term) == folded) {
            if (!m.empty()) existing.meaning = std::move(m);
            existing.mastery = clamp01(existing.mastery + 0.2f);
            existing.at = now;
            return true;
        }
    }
    self.learned_concepts.push_back({std::move(t), std::move(m), 0.35f, now});
    if (self.learned_concepts.size() > max_learned_concepts) self.learned_concepts.erase(self.learned_concepts.begin());
    return true;
}

bool addKeyMemory(Self& self, std::wstring_view text, uint64_t now) {
    auto line = clean(text, 180);
    const auto folded = game_index::fold(line);
    if (folded.size() < 6 || line == L"-") return false;
    std::erase_if(self.key_memories, [&](const Claim& c) { return game_index::fold(c.text) == folded; });
    self.key_memories.push_back({now, std::move(line)});
    if (self.key_memories.size() > max_key_memories) self.key_memories.erase(self.key_memories.begin());
    return true;
}

bool addWorldEntity(Self& self, std::wstring_view name, std::wstring_view relation, uint64_t now) {
    auto n = clean(name, 36);
    auto r = clean(relation, 80);
    const auto folded = game_index::fold(n);
    if (folded.size() < 2 || n == L"-") return false;
    for (auto& existing : self.player_world) {
        if (game_index::fold(existing.name) == folded) {
            existing.name = std::move(n);
            if (!r.empty()) existing.relation = std::move(r);
            existing.at = now;
            return true;
        }
    }
    self.player_world.push_back({std::move(n), std::move(r), now});
    if (self.player_world.size() > max_player_world) self.player_world.erase(self.player_world.begin());
    return true;
}
} // namespace companion::mind
