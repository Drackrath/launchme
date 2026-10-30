// The mind's file: State::save/load (versioned JSON, protected on disk) and its load/save/clear.
#include "companion_mind_internal.hpp"
#include "json.hpp"
#include "../platform/protected_file.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace companion::mind {
using namespace detail;

std::string State::save() const {
    auto root = json::object();
    root.properties["version"] = json::number(1);
    auto all = json::object();
    for (const auto& [look, self] : selves) {
        auto s = json::object();
        if (self.social.initialized || !self.social.events.empty()) s.properties["social"] = self.social.save();
        const auto affect = [](const Affect& a) {
            auto o = json::object();
            o.properties["v"] = json::number(std::round(a.valence * 1000) / 1000.0);
            o.properties["a"] = json::number(std::round(a.arousal * 1000) / 1000.0);
            o.properties["at"] = json::number(static_cast<double>(a.at));
            return o;
        };
        s.properties["affect"] = affect(self.affect);
        auto drives = json::object();
        drives.properties["bond"] = json::number(std::round(self.drives.bond * 1000) / 1000.0);
        drives.properties["energy"] = json::number(std::round(self.drives.energy * 1000) / 1000.0);
        drives.properties["curiosity"] = json::number(std::round(self.drives.curiosity * 1000) / 1000.0);
        drives.properties["pride"] = json::number(std::round(self.drives.pride * 1000) / 1000.0);
        drives.properties["autonomy"] = json::number(std::round(self.drives.autonomy * 1000) / 1000.0);
        drives.properties["at"] = json::number(static_cast<double>(self.drives.at));
        s.properties["drives"] = std::move(drives);
        auto expectations = json::array();
        for (const auto& e : self.expectations) {
            auto o = json::object();
            o.properties["id"] = json::string(e.id);
            o.properties["text"] = json::string(utf8(e.text));
            if (!e.game.empty()) o.properties["game"] = json::string(e.game);
            if (!e.title.empty()) o.properties["title"] = json::string(utf8(e.title));
            o.properties["p"] = json::number(std::round(e.p * 100) / 100.0);
            o.properties["made"] = json::number(static_cast<double>(e.made));
            o.properties["due"] = json::number(static_cast<double>(e.due));
            o.properties["outcome"] = json::number(e.outcome);
            o.properties["settled"] = json::number(static_cast<double>(e.settled));
            if (e.presence) o.properties["presence"] = json::boolean(true);
            if (e.suggestion) o.properties["suggestion"] = json::boolean(true);
            expectations.elements.push_back(std::move(o));
        }
        s.properties["expectations"] = std::move(expectations);
        auto goals = json::array();
        for (const auto& g : self.goals) {
            auto o = json::object();
            o.properties["id"] = json::string(g.id);
            o.properties["text"] = json::string(utf8(g.text));
            o.properties["kind"] = json::string(g.kind == Goal::Kind::promise ? "promise" : g.kind == Goal::Kind::intention ? "intention" : "own");
            o.properties["status"] = json::string(g.status == Goal::Status::done ? "done" : g.status == Goal::Status::dropped ? "dropped" : "open");
            o.properties["stage"] = json::number(g.stage);
            o.properties["made"] = json::number(static_cast<double>(g.made));
            o.properties["updated"] = json::number(static_cast<double>(g.updated));
            o.properties["due"] = json::number(static_cast<double>(g.due));
            if (!g.progress.empty()) o.properties["progress"] = json::string(utf8(g.progress));
            o.properties["source"] = json::number(g.source);
            o.properties["span"] = json::number(g.span);
            goals.elements.push_back(std::move(o));
        }
        s.properties["goals"] = std::move(goals);
        const auto stamped = [](const std::vector<Claim>& list) {
            auto array = json::array();
            for (const auto& c : list) {
                auto o = json::object();
                o.properties["at"] = json::number(static_cast<double>(c.at));
                o.properties["text"] = json::string(utf8(c.text));
                array.elements.push_back(std::move(o));
            }
            return array;
        };
        s.properties["claims"] = stamped(self.claims);
        s.properties["milestones"] = stamped(self.milestones);
        auto aspirations = json::array();
        for (const auto& a : self.aspirations) aspirations.elements.push_back(json::string(utf8(a)));
        s.properties["aspirations"] = std::move(aspirations);
        auto markers = json::array();
        for (const auto& m : self.markers) {
            if (std::abs(m.valence) < 0.01f) continue;
            auto o = json::object();
            o.properties["subject"] = json::string(utf8(m.subject));
            o.properties["v"] = json::number(std::round(m.valence * 100) / 100.0);
            o.properties["at"] = json::number(static_cast<double>(m.at));
            if (m.cortical_inhibition > 0.01f) o.properties["inh"] = json::number(std::round(m.cortical_inhibition * 100) / 100.0);
            markers.elements.push_back(std::move(o));
        }
        s.properties["markers"] = std::move(markers);
        {
            auto pol = json::object();
            pol.properties["teasing"] = json::number(std::round(self.policy.teasing * 1000) / 1000.0);
            pol.properties["deep"] = json::number(std::round(self.policy.deep_questions * 1000) / 1000.0);
            pol.properties["games"] = json::number(std::round(self.policy.game_talk * 1000) / 1000.0);
            pol.properties["life"] = json::number(std::round(self.policy.own_life * 1000) / 1000.0);
            pol.properties["warmth"] = json::number(std::round(self.policy.warmth * 1000) / 1000.0);
            pol.properties["base"] = json::number(std::round(self.policy.baseline_reward * 1000) / 1000.0);
            if (!self.policy.last_style.empty()) pol.properties["last"] = json::string(self.policy.last_style);
            s.properties["policy"] = std::move(pol);
        }
        if (!self.learned_concepts.empty()) {
            auto concepts = json::array();
            for (const auto& c : self.learned_concepts) {
                auto o = json::object();
                o.properties["term"] = json::string(utf8(c.term));
                if (!c.meaning.empty()) o.properties["meaning"] = json::string(utf8(c.meaning));
                o.properties["mastery"] = json::number(std::round(c.mastery * 100) / 100.0);
                o.properties["at"] = json::number(static_cast<double>(c.at));
                concepts.elements.push_back(std::move(o));
            }
            s.properties["learned_concepts"] = std::move(concepts);
        }
        if (!self.untruths.empty()) {
            auto untruths = json::array();
            for (const auto& u : self.untruths) {
                auto o = json::object();
                o.properties["id"] = json::string(u.id);
                o.properties["at"] = json::number(static_cast<double>(u.at));
                o.properties["said"] = json::string(utf8(u.said));
                o.properties["truth"] = json::string(utf8(u.truth));
                if (!u.reason.empty()) o.properties["reason"] = json::string(u.reason);
                if (!u.feeling.empty()) o.properties["feeling"] = json::string(u.feeling);
                o.properties["trust_at"] = json::number(u.trust_at);
                if (u.confessed) o.properties["confessed"] = json::boolean(true);
                if (u.peeked) o.properties["peeked"] = json::boolean(true);
                if (u.caught) o.properties["caught"] = json::boolean(true);
                untruths.elements.push_back(std::move(o));
            }
            s.properties["untruths"] = std::move(untruths);
        }
        if (!self.curious.empty()) {
            auto curious = json::array();
            for (const auto& c : self.curious) {
                auto o = json::object();
                o.properties["id"] = json::string(c.id);
                if (!c.about.empty()) o.properties["about"] = json::string(utf8(c.about));
                if (!c.why.empty()) o.properties["why"] = json::string(utf8(c.why));
                if (c.at) o.properties["at"] = json::number(static_cast<double>(c.at));
                if (c.later_at) o.properties["later_at"] = json::number(static_cast<double>(c.later_at));
                o.properties["outcome"] = json::number(c.outcome);
                if (c.settled) o.properties["settled"] = json::number(static_cast<double>(c.settled));
                if (c.returned) o.properties["returned"] = json::boolean(true);
                o.properties["move"] = json::string(c.move == CuriousMove::clarify ? "clarify" : c.move == CuriousMove::reflect ? "reflect" : "lead");
                curious.elements.push_back(std::move(o));
            }
            s.properties["curious"] = std::move(curious);
        }
        if (!self.follow_ups.empty()) {
            auto list = json::array();
            for (const auto& f : self.follow_ups) {
                auto o = json::object();
                o.properties["id"] = json::string(f.id);
                o.properties["what"] = json::string(utf8(f.what));
                if (!f.when.empty()) o.properties["when"] = json::string(utf8(f.when));
                o.properties["made"] = json::number(static_cast<double>(f.made));
                o.properties["due"] = json::number(static_cast<double>(f.due));
                o.properties["state"] = json::number(f.state);
                if (f.wished) o.properties["wished"] = json::boolean(true);
                if (f.settled) o.properties["settled"] = json::number(static_cast<double>(f.settled));
                list.elements.push_back(std::move(o));
            }
            s.properties["follow_ups"] = std::move(list);
        }
        {
            auto bids = json::object();
            bids.properties["toward"] = json::number(std::round(self.bids.toward * 100) / 100.0);
            bids.properties["away"] = json::number(std::round(self.bids.away * 100) / 100.0);
            bids.properties["against"] = json::number(std::round(self.bids.against * 100) / 100.0);
            bids.properties["his"] = json::number(self.bids.his);
            bids.properties["at"] = json::number(static_cast<double>(self.bids.at));
            s.properties["bids"] = std::move(bids);
        }
        if (self.his_style.samples) {
            auto style = json::object();
            const auto put = [&](const char* name, float value) { style.properties[name] = json::number(std::round(value * 100) / 100.0); };
            put("words", self.his_style.words); put("exclaim", self.his_style.exclaim); put("lower", self.his_style.lower);
            put("plain", self.his_style.plain); put("laughs", self.his_style.laughs);
            style.properties["samples"] = json::number(self.his_style.samples);
            s.properties["his_style"] = std::move(style);
        }
        if (!self.threads.empty()) {
            auto list = json::array();
            for (const auto& t : self.threads) {
                auto o = json::object();
                o.properties["at"] = json::number(static_cast<double>(t.at));
                o.properties["text"] = json::string(utf8(t.text));
                if (t.used) o.properties["used"] = json::boolean(true);
                list.elements.push_back(std::move(o));
            }
            s.properties["threads"] = std::move(list);
        }
        s.properties["interest"] = json::number(std::round(self.interest * 100) / 100.0);
        if (self.interest_at) s.properties["interest_at"] = json::number(static_cast<double>(self.interest_at));
        if (self.opened_at) s.properties["opened_at"] = json::number(static_cast<double>(self.opened_at));
        if (self.his_open_at) s.properties["his_open_at"] = json::number(static_cast<double>(self.his_open_at));
        if (self.bonded_at) s.properties["bonded_at"] = json::number(static_cast<double>(self.bonded_at));
        if (!self.shared_idioms.empty()) s.properties["shared_idioms"] = stamped(self.shared_idioms);
        if (!self.key_memories.empty()) s.properties["key_memories"] = stamped(self.key_memories);
        if (!self.player_world.empty()) {
            auto world = json::array();
            for (const auto& w : self.player_world) {
                auto o = json::object();
                o.properties["name"] = json::string(utf8(w.name));
                if (!w.relation.empty()) o.properties["rel"] = json::string(utf8(w.relation));
                o.properties["at"] = json::number(static_cast<double>(w.at));
                world.elements.push_back(std::move(o));
            }
            s.properties["player_world"] = std::move(world);
        }
        if (self.play_momentum > 0.01f) s.properties["play"] = json::number(std::round(self.play_momentum * 100) / 100.0);
        if (self.sleep_debt > 0.01f) s.properties["sleep_debt"] = json::number(std::round(self.sleep_debt * 100) / 100.0);
        if (self.stress_load > 0.01f) s.properties["stress_load"] = json::number(std::round(self.stress_load * 100) / 100.0);
        if (self.rejected_picks_in_row > 0) s.properties["rejected_picks"] = json::number(self.rejected_picks_in_row);
        if (!self.ruminations.empty()) s.properties["ruminations"] = stamped(self.ruminations);
        if (!self.rethought.empty()) s.properties["rethought"] = stamped(self.rethought);
        if (!self.counterfactual.empty()) {
            s.properties["counterfactual"] = json::string(utf8(self.counterfactual));
            s.properties["counterfactual_at"] = json::number(static_cast<double>(self.counterfactual_at));
        }
        if (self.harsh_at) {
            s.properties["harsh_at"] = json::number(static_cast<double>(self.harsh_at));
            if (!self.harsh_reason.empty()) s.properties["harsh_reason"] = json::string(utf8(self.harsh_reason));
        }
        if (self.last_reply_vulnerable) s.properties["last_reply_vulnerable"] = json::boolean(true);
        if (self.last_rumination) s.properties["last_rumination"] = json::number(static_cast<double>(self.last_rumination));
        if (self.last_initiative) s.properties["last_initiative"] = json::number(static_cast<double>(self.last_initiative));
        auto episodes = json::array();
        for (const auto& e : self.episodes) {
            auto o = json::object();
            o.properties["id"] = json::string(e.id);
            o.properties["at"] = json::number(static_cast<double>(e.at));
            o.properties["what"] = json::string(utf8(e.what));
            if (!e.with.empty()) o.properties["with"] = json::string(utf8(e.with));
            if (!e.where.empty()) o.properties["where"] = json::string(utf8(e.where));
            if (!e.partner.empty()) o.properties["partner"] = json::string(e.partner);
            o.properties["state"] = json::string(e.state == Episode::State::planned ? "planned" : e.state == Episode::State::fell_through ? "fell_through" : "happened");
            if (e.hidden) o.properties["private"] = json::boolean(true);
            if (e.date) o.properties["date"] = json::boolean(true);
            if (e.told) o.properties["told"] = json::boolean(true);
            if (e.peeked) o.properties["peeked"] = json::boolean(true);
            o.properties["made"] = json::number(static_cast<double>(e.made));
            episodes.elements.push_back(std::move(o));
        }
        s.properties["episodes"] = std::move(episodes);
        if (self.lived_until) s.properties["lived_until"] = json::number(static_cast<double>(self.lived_until));
        if (!self.felt.empty()) { s.properties["felt"] = json::string(utf8(self.felt)); s.properties["felt_at"] = json::number(static_cast<double>(self.felt_at)); }
        auto feeling_records = json::array();
        for (const auto& moment : self.feelings) {
            auto entry = json::object();
            entry.properties["key"] = json::string(feelingInfo(moment.kind).key);
            entry.properties["at"] = json::number(static_cast<double>(moment.at));
            entry.properties["reason"] = json::string(utf8(moment.reason));
            entry.properties["inferred"] = json::boolean(moment.inferred);
            if (moment.intensity) entry.properties["intensity"] = json::number(*moment.intensity);
            feeling_records.elements.push_back(std::move(entry));
        }
        s.properties["feelings"] = std::move(feeling_records);
        if (!self.player_read.empty()) {
            s.properties["player_read"] = json::string(utf8(self.player_read));
            s.properties["player_read_at"] = json::number(static_cast<double>(self.player_read_at));
        }
        s.properties["last_talk"] = json::number(static_cast<double>(self.last_talk));
        s.properties["goal_rest"] = json::number(static_cast<double>(self.goal_rest));
        auto used = json::array();
        for (const int index : self.seeds_used) used.elements.push_back(json::number(index));
        s.properties["seeds_used"] = std::move(used);
        s.properties["next_id"] = json::number(self.next_id);
        all.properties[look] = std::move(s);
    }
    root.properties["selves"] = std::move(all);
    return json::serialize(root);
}

std::optional<State> State::load(std::string_view text) {
    const auto root = json::parse(text, 32 * 1024 * 1024);
    if (!root || root->type != json::Value::Type::object || !root->get("version") || root->get("version")->intOr() != 1) return std::nullopt;
    State out;
    const auto* all = root->get("selves");
    if (!all || all->type != json::Value::Type::object) return out;
    const auto number = [](const json::Value* parent, const char* name, double fallback = 0) {
        const auto* v = parent ? parent->get(name) : nullptr;
        return v && v->type == json::Value::Type::number ? v->numeric : fallback;
    };
    const auto unix = [&](const json::Value* parent, const char* name) {
        const double v = number(parent, name);
        return v > 0 ? static_cast<uint64_t>(v) : uint64_t{0};
    };
    const auto text16 = [](const json::Value* parent, const char* name) {
        const auto* v = parent ? parent->get(name) : nullptr;
        return v && v->type == json::Value::Type::string ? wide(v->text) : std::wstring{};
    };
    const auto text8 = [](const json::Value* parent, const char* name) {
        const auto* v = parent ? parent->get(name) : nullptr;
        return v && v->type == json::Value::Type::string && v->text.size() <= 64 ? v->text : std::string{};
    };
    for (const auto& [look, s] : all->properties) {
        if (look.empty() || look.size() > 32 || s.type != json::Value::Type::object) continue;
        Self self;
        self.social = social::State::load(s.get("social"));
        if (const auto* a = s.get("affect")) self.affect = {std::clamp(static_cast<float>(number(a, "v")), -1.0f, 1.0f), clamp01(static_cast<float>(number(a, "a", 0.3))), unix(a, "at")};
        if (const auto* d = s.get("drives")) {
            self.drives.bond = clamp01(static_cast<float>(number(d, "bond", 0.3)));
            self.drives.energy = clamp01(static_cast<float>(number(d, "energy", 0.9)));
            self.drives.curiosity = clamp01(static_cast<float>(number(d, "curiosity", 0.4)));
            self.drives.pride = clamp01(static_cast<float>(number(d, "pride", 0.5)));
            self.drives.autonomy = clamp01(static_cast<float>(number(d, "autonomy", 0.3)));
            self.drives.at = unix(d, "at");
        }
        if (const auto* list = s.get("expectations"); list && list->type == json::Value::Type::array)
            for (const auto& o : list->elements) {
                Expectation e;
                e.id = text8(&o, "id");
                e.text = text16(&o, "text");
                if (e.id.empty() || e.text.empty()) continue;
                e.game = text8(&o, "game");
                e.title = text16(&o, "title");
                e.p = clamp01(static_cast<float>(number(&o, "p", 0.5)));
                e.made = unix(&o, "made");
                e.due = unix(&o, "due");
                e.outcome = std::clamp(static_cast<int>(number(&o, "outcome", -1)), -1, 2);
                e.settled = unix(&o, "settled");
                if (const auto* p = o.get("presence"); p && p->type == json::Value::Type::boolean) e.presence = p->flag;
                if (const auto* p = o.get("suggestion"); p && p->type == json::Value::Type::boolean) e.suggestion = p->flag;
                if (self.expectations.size() < expectation_limit) self.expectations.push_back(std::move(e));
            }
        if (const auto* list = s.get("goals"); list && list->type == json::Value::Type::array)
            for (const auto& o : list->elements) {
                Goal g;
                g.id = text8(&o, "id");
                g.text = text16(&o, "text");
                if (g.id.empty() || g.text.empty()) continue;
                const auto kind = text8(&o, "kind"), status = text8(&o, "status");
                g.kind = kind == "promise" ? Goal::Kind::promise : kind == "intention" ? Goal::Kind::intention : Goal::Kind::own;
                g.status = status == "done" ? Goal::Status::done : status == "dropped" ? Goal::Status::dropped : Goal::Status::open;
                g.stage = std::clamp(static_cast<int>(number(&o, "stage")), 0, 2);
                g.made = unix(&o, "made");
                g.updated = unix(&o, "updated");
                g.due = unix(&o, "due");
                g.progress = text16(&o, "progress");
                g.source = static_cast<int>(number(&o, "source", -1));
                g.span = std::clamp(static_cast<int>(number(&o, "span")), 0, 60);
                if (self.goals.size() < goal_limit) self.goals.push_back(std::move(g));
            }
        if (const auto* list = s.get("claims"); list && list->type == json::Value::Type::array)
            for (const auto& o : list->elements)
                if (auto t = text16(&o, "text"); !t.empty() && self.claims.size() < claim_limit) self.claims.push_back({unix(&o, "at"), std::move(t)});
        if (const auto* list = s.get("milestones"); list && list->type == json::Value::Type::array)
            for (const auto& o : list->elements)
                if (auto t = text16(&o, "text"); !t.empty() && self.milestones.size() < 60) self.milestones.push_back({unix(&o, "at"), std::move(t)});
        if (const auto* list = s.get("aspirations"); list && list->type == json::Value::Type::array)
            for (const auto& v : list->elements)
                if (v.type == json::Value::Type::string && !v.text.empty() && self.aspirations.size() < 5) self.aspirations.push_back(wide(v.text));
        if (const auto* list = s.get("markers"); list && list->type == json::Value::Type::array)
            for (const auto& o : list->elements)
                if (auto t = text16(&o, "subject"); !t.empty() && self.markers.size() < marker_limit)
                    self.markers.push_back({std::move(t), std::clamp(static_cast<float>(number(&o, "v")), -1.0f, 1.0f), unix(&o, "at"),
                                            clamp01(static_cast<float>(number(&o, "inh", 0)))});
        if (const auto* pol = s.get("policy"); pol && pol->type == json::Value::Type::object) {
            self.policy.teasing = clamp01(static_cast<float>(number(pol, "teasing", 0.5)));
            self.policy.deep_questions = clamp01(static_cast<float>(number(pol, "deep", 0.5)));
            self.policy.game_talk = clamp01(static_cast<float>(number(pol, "games", 0.5)));
            self.policy.own_life = clamp01(static_cast<float>(number(pol, "life", 0.5)));
            self.policy.warmth = clamp01(static_cast<float>(number(pol, "warmth", 0.5)));
            self.policy.baseline_reward = clamp01(static_cast<float>(number(pol, "base", 0.5)));
            self.policy.last_style = text8(pol, "last");
        }
        if (const auto* list = s.get("learned_concepts"); list && list->type == json::Value::Type::array)
            for (const auto& o : list->elements)
                if (auto t = text16(&o, "term"); !t.empty() && self.learned_concepts.size() < max_learned_concepts)
                    self.learned_concepts.push_back({std::move(t), text16(&o, "meaning"), clamp01(static_cast<float>(number(&o, "mastery", 0.35))), unix(&o, "at")});
        if (const auto* list = s.get("untruths"); list && list->type == json::Value::Type::array)
            for (const auto& o : list->elements) {
                Untruth u;
                u.id = text8(&o, "id");
                u.at = unix(&o, "at");
                u.said = text16(&o, "said");
                u.truth = text16(&o, "truth");
                if (u.id.empty() || u.said.empty() || u.truth.empty()) continue;
                u.reason = text8(&o, "reason");
                u.feeling = text8(&o, "feeling");
                u.trust_at = std::clamp(static_cast<int>(number(&o, "trust_at", 50)), 0, 100);
                if (const auto* c = o.get("confessed"); c && c->type == json::Value::Type::boolean) u.confessed = c->flag;
                if (const auto* p = o.get("peeked"); p && p->type == json::Value::Type::boolean) u.peeked = p->flag;
                if (const auto* c = o.get("caught"); c && c->type == json::Value::Type::boolean) u.caught = c->flag;
                if (self.untruths.size() < 24) self.untruths.push_back(std::move(u));
            }
        if (const auto* list = s.get("curious"); list && list->type == json::Value::Type::array)
            for (const auto& o : list->elements) {
                CuriousAct c;
                c.id = text8(&o, "id");
                c.at = unix(&o, "at");
                c.later_at = unix(&o, "later_at");
                if (c.id.empty() || (!c.at && !c.later_at)) continue;
                const auto move = text8(&o, "move");
                c.move = move == "clarify" ? CuriousMove::clarify : move == "reflect" ? CuriousMove::reflect : CuriousMove::lead;
                c.about = text16(&o, "about");
                c.why = text16(&o, "why");
                c.outcome = std::clamp(static_cast<int>(number(&o, "outcome", -1)), -1, 2);
                c.settled = unix(&o, "settled");
                if (const auto* r = o.get("returned"); r && r->type == json::Value::Type::boolean) c.returned = r->flag;
                if (self.curious.size() < curious_limit) self.curious.push_back(std::move(c));
            }
        if (const auto* list = s.get("follow_ups"); list && list->type == json::Value::Type::array)
            for (const auto& o : list->elements) {
                FollowUp f;
                f.id = text8(&o, "id");
                f.what = text16(&o, "what");
                if (f.id.empty() || f.what.empty()) continue;
                f.when = text16(&o, "when");
                f.made = unix(&o, "made");
                f.due = unix(&o, "due");
                f.state = std::clamp(static_cast<int>(number(&o, "state")), 0, 3);
                if (const auto* w = o.get("wished"); w && w->type == json::Value::Type::boolean) f.wished = w->flag;
                f.settled = unix(&o, "settled");
                if (self.follow_ups.size() < 12) self.follow_ups.push_back(std::move(f));
            }
        if (const auto* b = s.get("bids"); b && b->type == json::Value::Type::object) {
            self.bids.toward = std::max(0.0f, static_cast<float>(number(b, "toward")));
            self.bids.away = std::max(0.0f, static_cast<float>(number(b, "away")));
            self.bids.against = std::max(0.0f, static_cast<float>(number(b, "against")));
            self.bids.his = std::max(0, static_cast<int>(number(b, "his")));
            self.bids.at = unix(b, "at");
        }
        if (const auto* st = s.get("his_style"); st && st->type == json::Value::Type::object) {
            self.his_style.words = std::max(0.0f, static_cast<float>(number(st, "words")));
            self.his_style.exclaim = clamp01(static_cast<float>(number(st, "exclaim")));
            self.his_style.lower = clamp01(static_cast<float>(number(st, "lower")));
            self.his_style.plain = clamp01(static_cast<float>(number(st, "plain")));
            self.his_style.laughs = clamp01(static_cast<float>(number(st, "laughs")));
            self.his_style.samples = std::clamp(static_cast<int>(number(st, "samples")), 0, 1000);
        }
        if (const auto* list = s.get("threads"); list && list->type == json::Value::Type::array)
            for (const auto& o : list->elements)
                if (auto t = text16(&o, "text"); !t.empty() && self.threads.size() < 5) {
                    const auto* u = o.get("used");
                    self.threads.push_back({unix(&o, "at"), std::move(t), u && u->type == json::Value::Type::boolean && u->flag});
                }
        self.interest = clamp01(static_cast<float>(number(&s, "interest", 0.5)));
        self.interest_at = unix(&s, "interest_at");
        self.opened_at = unix(&s, "opened_at");
        self.his_open_at = unix(&s, "his_open_at");
        self.bonded_at = unix(&s, "bonded_at");
        if (const auto* list = s.get("shared_idioms"); list && list->type == json::Value::Type::array)
            for (const auto& o : list->elements)
                if (auto t = text16(&o, "text"); !t.empty() && self.shared_idioms.size() < max_shared_idioms) self.shared_idioms.push_back({unix(&o, "at"), std::move(t)});
        if (const auto* list = s.get("key_memories"); list && list->type == json::Value::Type::array)
            for (const auto& o : list->elements)
                if (auto t = text16(&o, "text"); !t.empty() && self.key_memories.size() < max_key_memories) self.key_memories.push_back({unix(&o, "at"), std::move(t)});
        if (const auto* list = s.get("player_world"); list && list->type == json::Value::Type::array)
            for (const auto& o : list->elements)
                if (auto n = text16(&o, "name"); !n.empty() && self.player_world.size() < max_player_world)
                    self.player_world.push_back({std::move(n), text16(&o, "rel"), unix(&o, "at")});
        self.play_momentum = clamp01(static_cast<float>(number(&s, "play", 0.0)));
        self.sleep_debt = clamp01(static_cast<float>(number(&s, "sleep_debt", 0.0)));
        self.stress_load = clamp01(static_cast<float>(number(&s, "stress_load", 0.0)));
        self.rejected_picks_in_row = std::clamp(static_cast<int>(number(&s, "rejected_picks", 0)), 0, 20);
        if (const auto* list = s.get("ruminations"); list && list->type == json::Value::Type::array)
            for (const auto& o : list->elements)
                if (auto t = text16(&o, "text"); !t.empty() && self.ruminations.size() < 8) self.ruminations.push_back({unix(&o, "at"), std::move(t)});
        if (const auto* list = s.get("rethought"); list && list->type == json::Value::Type::array)
            for (const auto& o : list->elements)
                if (auto t = text16(&o, "text"); !t.empty() && self.rethought.size() < 20) self.rethought.push_back({unix(&o, "at"), std::move(t)});
        self.counterfactual = text16(&s, "counterfactual");
        self.counterfactual_at = unix(&s, "counterfactual_at");
        self.harsh_at = unix(&s, "harsh_at");
        self.harsh_reason = text16(&s, "harsh_reason");
        if (const auto* v = s.get("last_reply_vulnerable"); v && v->type == json::Value::Type::boolean) self.last_reply_vulnerable = v->flag;
        self.last_rumination = unix(&s, "last_rumination");
        self.last_initiative = unix(&s, "last_initiative");
        if (const auto* list = s.get("episodes"); list && list->type == json::Value::Type::array)
            for (const auto& o : list->elements) {
                Episode e;
                e.id = text8(&o, "id");
                e.what = text16(&o, "what");
                e.at = unix(&o, "at");
                if (e.id.empty() || e.what.empty() || !e.at) continue;
                e.with = text16(&o, "with");
                e.where = text16(&o, "where");
                e.partner = text8(&o, "partner");
                const auto state = text8(&o, "state");
                e.state = state == "planned" ? Episode::State::planned : state == "fell_through" ? Episode::State::fell_through : Episode::State::happened;
                const auto flag = [&](const char* name) { const auto* v = o.get(name); return v && v->type == json::Value::Type::boolean && v->flag; };
                e.hidden = flag("private");
                e.date = flag("date");
                e.told = flag("told");
                e.peeked = flag("peeked");
                e.made = unix(&o, "made");
                if (self.episodes.size() < episode_limit) self.episodes.push_back(std::move(e));
            }
        keepEpisodes(self); // Older saves may hold one plan written twice.
        self.lived_until = unix(&s, "lived_until");
        self.felt = text16(&s, "felt");
        self.felt_at = unix(&s, "felt_at");
        if (const auto* list = s.get("feelings"); list && list->type == json::Value::Type::array) {
            for (const auto& entry : list->elements) {
                const auto kind = feelingFromKey(text8(&entry, "key"));
                if (!kind) continue;
                Self::FeelingMoment moment{*kind, {}, clean(text16(&entry, "reason"), 200), unix(&entry, "at")};
                if (const auto* intensity = entry.get("intensity"); intensity && intensity->type == json::Value::Type::number && std::isfinite(intensity->numeric))
                    moment.intensity = std::clamp(static_cast<float>(intensity->numeric), 0.0f, 1.0f);
                if (const auto* inferred = entry.get("inferred")) moment.inferred = inferred->type == json::Value::Type::boolean && inferred->flag;
                self.feelings.push_back(std::move(moment));
                if (self.feelings.size() > 80) self.feelings.erase(self.feelings.begin());
            }
        }
        self.player_read = text16(&s, "player_read");
        self.player_read_at = unix(&s, "player_read_at");
        self.last_talk = unix(&s, "last_talk");
        self.goal_rest = unix(&s, "goal_rest");
        if (const auto* list = s.get("seeds_used"); list && list->type == json::Value::Type::array)
            for (const auto& v : list->elements) if (v.type == json::Value::Type::number && self.seeds_used.size() < 64) self.seeds_used.push_back(static_cast<int>(v.numeric));
        self.next_id = std::max(1, static_cast<int>(number(&s, "next_id", 1)));
        out.selves.emplace(look, std::move(self));
    }
    return out;
}

namespace {
std::filesystem::path file(const std::filesystem::path& config) { return config / L"companion_mind.dat"; }
}

State loadState(const std::filesystem::path& config) {
    const auto bytes = protected_file::read(file(config), 32 * 1024 * 1024);
    if (!bytes) return {};
    return State::load(*bytes).value_or(State{});
}

bool saveState(const std::filesystem::path& config, const State& state) {
    if (state.empty()) return clearState(config);
    std::error_code ec;
    std::filesystem::create_directories(config, ec);
    return protected_file::write(file(config), state.save(), L"Launchme companion mind");
}

bool clearState(const std::filesystem::path& config) {
    std::error_code ec;
    std::filesystem::remove(file(config), ec);
    return !ec;
}
} // namespace companion::mind
