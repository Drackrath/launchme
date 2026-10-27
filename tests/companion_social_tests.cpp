#include "companion_fixture.hpp"
#include "../src/net/companion_social.hpp"
#include "../src/net/companion_mind.hpp"
#include "../src/net/companion_cognition.hpp"
#include "../src/net/companion_memory.hpp"
#include <iostream>
#include <stdexcept>

namespace s = companion::social;
namespace {
void check(bool value, const char* name) { if (!value) throw std::runtime_error(name); }
s::Update update(std::string_view json) { const auto parsed = json::parse(json); check(parsed.has_value(), "fixture JSON"); return s::parse(&*parsed); }
void run() {
    s::State state;
    state.seed(companion::affinity::Stage::friends);
    companion::affinity::Relation relation;
    check(state.stage(relation) == companion::affinity::Stage::friends, "migrated stage");
    state.seed(companion::affinity::Stage::strangers);
    check(state.stage(relation) == companion::affinity::Stage::friends, "migration is one time");
    s::Context context;
    context.now = 1'800'000'000; context.turn = "turn1"; context.player_text = L"I am busy, talk later";
    context.busy = true;
    const auto decision = s::plan(state, context);
    check(decision.preferred == s::Intent::defer && std::find(decision.allowed.begin(), decision.allowed.end(), s::Intent::tease) == decision.allowed.end(), "busy action preconditions");
    auto result = s::apply(state, update(R"({"intent":"tease","signal":"busy","confidence":0.9,"quote":"I am busy"})"), decision, context, L"See you later", [](auto) { return 0; });
    check(result.changed && !result.reward && result.delta.trust == 0 && state.last_intent == s::Intent::defer, "busy is not rejection; invalid intent cannot win");
    const auto events = state.events.size();
    s::apply(state, {}, decision, context, L"Duplicate", [](auto) { return 0; });
    check(state.events.size() == events, "duplicate completion");
    context.turn = "turn2"; context.now += 100; context.player_text = L"Please do not bring up my employer";
    result = s::apply(state, update(R"({"topic":{"name":"employer","state":"blocked","quote":"do not bring up my employer"}})"), decision, context, L"Understood", [](auto) { return 0; });
    check(!state.allowsTopic(L"How is your employer?", context.now + 86400, true), "blocked topic persists");
    context.turn = "turn3"; context.now += 100; context.player_text = L"Let us talk about my employer again";
    s::apply(state, update(R"({"topic":{"name":"employer","state":"active","quote":"talk about my employer again"}})"), decision, context, L"Your employer?", [](auto) { return 0; });
    check(state.allowsTopic(L"employer", context.now, true), "explicit reopening");
    context.turn = "turn4"; context.now += 100; context.player_text = L"We agree to stay friends";
    result = s::apply(state, update(R"({"agreement":{"role":"romantic","mutual":true,"quote":"invented words"},"own_feeling":"affection"})"), decision, context, L"Yes", [](auto) { return 0; });
    check(!state.agreed_role && !result.agreement && state.own_feeling == L"affection", "one-sided affection is not mutual agreement");
    context.turn = "turn5";
    result = s::apply(state, update(R"({"agreement":{"role":"friend","mutual":true,"quote":"agree to stay friends"}})"), decision, context, L"Yes", [](auto) { return 0; });
    check(result.agreement == companion::RelationRole::friend_role, "evidenced agreement");
    context.turn = "turn6"; context.player_text = L"Let us play on Friday";
    const auto due = [&](auto) { return context.now + 86400; };
    s::apply(state, update(R"({"commitments":[{"action":"propose","actor":"player","text":"Play together","when":"Friday","quote":"play on Friday"}]})"), decision, context, L"Maybe", due);
    check(state.commitments.size() == 1 && state.commitments[0].status == s::CommitmentStatus::proposed, "proposal is not agreement");
    const auto id = state.commitments[0].id;
    s::Update accept;
    accept.commitments.push_back({id, "accept", "both", {}, L"Yes, Friday works", {}});
    context.turn = "turn7"; context.player_text = L"Yes, Friday works";
    s::apply(state, accept, decision, context, L"Great", due);
    check(state.commitments[0].status == s::CommitmentStatus::agreed, "accept proposal");
    context.turn = "turn8"; context.player_text = L"Move our game to Saturday";
    s::Update postpone; postpone.commitments.push_back({id, "reschedule", "both", {}, L"Move our game to Saturday", L"Saturday"});
    s::apply(state, postpone, decision, context, L"Okay", [&](auto) { return context.now + 2 * 86400; });
    check(state.commitments[0].previous_due.size() == 1 && state.commitments[0].status == s::CommitmentStatus::deferred, "reschedule keeps original date");
    check(state.prompt(context.now + 3 * 86400, true).find(L"outcome unknown") != std::wstring::npos, "passing deadline is not failure");
    const auto snapshot = json::serialize(state.save());
    const auto snapshot_json = json::parse(snapshot);
    const auto restored = s::State::load(&*snapshot_json);
    check(json::serialize(restored.save()) == snapshot, "social snapshot round trip");
    const auto evidence = state.evidence(context.now);
    check(state.reflectionDue(context.now), "significant events trigger reflection");
    std::vector<std::string> offered; for (const auto& e : evidence) offered.push_back(e.id);
    check(!state.validEvidence({offered[0], "invented"}, offered), "unsupported reflection");
    std::vector<std::string> two; std::string first;
    for (const auto& e : evidence) if (two.empty() || e.turn != first) { two.push_back(e.id); first = e.turn; if (two.size() == 2) break; }
    check(state.validEvidence(two, offered), "two independent turns ground reflection");
    auto cursor = state;
    cursor.reflected_through = cursor.next_id - 1;
    cursor.record("new-at-same-time", "support", "player", L"New evidence", context.now, {}, 5);
    check(cursor.evidence(context.now).size() == 1, "reflection cursor must preserve newer same-second events");
    state.reflection_attempt = context.now;
    check(!state.reflectionDue(context.now + 60), "reflection cooldown");

    companion::Memory memory;
    companion::Memory::Facts fact; fact.look = "hana"; fact.fact_key = "employment.company";
    memory.note(L"Works at Atlas", 100, fact);
    memory.note(L"Works at Borealis", 200, fact);
    check(memory.notes.size() == 1 && memory.notes[0].previous.size() == 1 && memory.notes[0].previous[0].text == L"Works at Atlas", "versioned fact slot");
    check(memory.historical("hana", L"Atlas before").size() == 1 && memory.historical("rin", L"Atlas before").empty(), "history ownership");
    check(memory.prompt(true, 300, false, true, "hana").find(L"Atlas") == std::wstring::npos, "old fact not current context");
    fact.source = "inferred"; fact.confidence = 0.2f;
    memory.note(L"Works at Comet", 300, fact);
    check(memory.notes[0].text == L"Works at Borealis", "weak guess cannot replace direct fact");
    const auto reload = companion::Memory::load(memory.save());
    check(reload && reload->save() == memory.save(), "fact history round trip");
    companion::mind::State mind; mind.of("hana").social = restored;
    const auto mind2 = companion::mind::State::load(mind.save());
    check(mind2 && json::serialize(mind2->find("hana")->social.save()) == snapshot, "mind owns social persistence");
    const auto legacy = companion::mind::State::load(R"({"version":1,"selves":{"hana":{"felt":"legacy"}}})");
    check(legacy && !legacy->find("hana")->social.initialized && legacy->find("hana")->felt == L"legacy", "legacy mind survives");
    const auto queries = s::queries(restored, {{true, L"I have an interview at Atlas"}, {false, L"How did it go?"}, {true, L"And then?"}}, context.now);
    check(queries.size() >= 2 && queries[1].find(L"interview") != std::wstring::npos, "contextual recall");
    const auto hidden = s::queries(restored, {{false, L"<ocena>{\"secret\":\"hidden-detail\"}</ocena>Hello"}, {true, L"And then?"}}, context.now);
    check(hidden.size() > 1 && hidden[1].find(L"hidden-detail") == std::wstring::npos, "recall query must use visible conversation context");
    const auto deadline = s::deadline(L"2027-02-01T19:30Z", 1'800'000'000);
    check(deadline != 0 && s::deadline(L"2027-02-30T19:30Z", 1'800'000'000) == 0 && s::deadline(L"sometime", context.now) == 0, "strict real calendar dates");
    auto privacy = restored;
    s::Update fake_accept;
    fake_accept.commitments.push_back({privacy.commitments.front().id, "complete", "companion", {}, L"We completed it", {}});
    auto current = context; current.turn = "bad-self-confirmation"; current.player_text = L"Hello";
    s::apply(privacy, fake_accept, decision, current, L"We completed it", due);
    check(privacy.commitments.front().status != s::CommitmentStatus::done, "companion cannot self-confirm the player's plan");
    companion::cognition::State cognition;
    cognition.reflect(L"Supported insight", context.now, "hana", two);
    cognition.addSchema(L"Supported pattern", context.now, "hana", two);
    const auto cognitive_copy = companion::cognition::State::load(cognition.save());
    check(cognitive_copy && cognitive_copy->reflections[0].evidence_ids == two && cognitive_copy->schemas[0].evidence_ids == two, "reflection provenance round trip");
}
}
int main() {
    const companion_fixture::Cast cast;
    try { run(); } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
    return 0;
}
