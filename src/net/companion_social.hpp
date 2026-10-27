#pragma once
// Local social state: typed actions, causal history, topic timing and negotiated commitments.
// Gemini proposes updates in the existing appraisal. The engine validates their evidence and effects.
#include "companion_affinity.hpp"
#include "json.hpp"
#include <optional>
#include <functional>

namespace companion::social {
enum class Intent { answer, listen, comfort, clarify, ask, tease, share, apologize, defer, boundary };
enum class Signal { neutral, engaged, supportive, busy, distressed, rejected, hostile, apology };
enum class TopicStatus { active, deferred, closed, blocked };
enum class CommitmentStatus { proposed, agreed, deferred, done, cancelled, declined };
struct Delta { int trust = 0, respect = 0, security = 0; };
struct Event {
    std::string id, turn, kind, actor;
    uint64_t at = 0;
    std::wstring text;
    Delta delta;
    int importance = 1;
};
struct Topic {
    std::wstring name;
    TopicStatus status = TopicStatus::active;
    uint64_t at = 0, until = 0, mentioned = 0;
    uint64_t sticky_through = 0;
};
struct Commitment {
    std::string id, legacy_id, actor = "both";
    std::wstring text;
    uint64_t made = 0, updated = 0, due = 0;
    CommitmentStatus status = CommitmentStatus::proposed;
    std::vector<uint64_t> previous_due;
};
struct State {
    bool initialized = false;
    int familiarity = 0, gains_today = 0;
    uint64_t gain_day = 0, turn_count = 0;
    std::vector<Event> events;
    std::vector<Topic> topics;
    std::vector<Commitment> commitments;
    std::wstring own_feeling, perceived_player;
    float perceived_confidence = 0;
    std::optional<RelationRole> agreed_role;
    std::wstring agreement_quote;
    uint64_t agreement_at = 0, reflected_through = 0, reflection_attempt = 0;
    uint64_t next_id = 1;
    std::string last_turn;
    Intent last_intent = Intent::answer;
    void seed(affinity::Stage previous, std::optional<RelationRole> previous_agreement = {});
    affinity::Stage stage(const affinity::Relation& relation) const;
    std::string record(std::string turn, std::string kind, std::string actor, std::wstring text, uint64_t now, Delta delta = {}, int importance = 1);
    bool allowsTopic(std::wstring_view text, uint64_t now, bool explicit_request = false) const;
    void mentioned(std::wstring_view text, uint64_t now);
    bool reflectionDue(uint64_t now) const;
    std::vector<Event> evidence(uint64_t now) const;
    bool validEvidence(const std::vector<std::string>& ids, const std::vector<std::string>& offered) const;
    std::wstring prompt(uint64_t now, bool english) const;
    json::Value save() const;
    static State load(const json::Value* value);
};
struct Context {
    uint64_t now = 0;
    std::string turn;
    std::wstring player_text;
    bool distressed = false, busy = false, asks = false;
    float energy = 1;
    int trust = 50, tension = 0;
    bool owes_apology = false;
};
struct Plan { std::vector<Intent> allowed; Intent preferred = Intent::answer; };
struct TopicUpdate { std::wstring name, quote; TopicStatus status = TopicStatus::active; int hours = 24; };
struct CommitmentUpdate {
    std::string id, action, actor = "both";
    std::wstring text, quote, when;
};
struct Update {
    std::optional<Intent> intent;
    Signal signal = Signal::neutral;
    float confidence = 0;
    std::wstring quote, own_feeling, perceived_player;
    std::optional<TopicUpdate> topic;
    std::optional<RelationRole> agreement;
    std::wstring agreement_quote;
    bool mutual = false;
    std::vector<CommitmentUpdate> commitments;
};
struct Applied {
    Delta delta;
    std::optional<float> reward;
    std::optional<RelationRole> agreement;
    bool changed = false;
};
std::string_view key(Intent value);
uint64_t deadline(std::wstring_view iso_utc, uint64_t now);
Plan plan(const State&, const Context&);
std::wstring instructions(const Plan&, bool english);
Update parse(const json::Value* value);
Applied apply(State&, const Update&, const Plan&, const Context&, std::wstring_view reply,
              const std::function<uint64_t(std::wstring_view)>& due);
std::vector<std::wstring> queries(const State&, const std::vector<Turn>& turns, uint64_t now);
}
