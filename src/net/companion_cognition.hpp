#pragma once
// Towarzyszka's Cognitive Engine (src/net/companion_cognition.cpp):
// 1. HippoRAG + Spreading Activation: multi-hop associative memory graph with Ebbinghaus decay and
//    Hebbian synapse strengthening ("cells that fire together, wire together").
// 2. Episodic-to-Semantic Consolidation ("Sleep" / Reflection Trees): higher-order psychological
//    hypotheses about the player, evolving over time and confronting words with play telemetry.
// 3. Subconscious Inner Monologue (<mysl>…</mysl> / <thought>…</thought>): Theory of Mind and
//    hidden feelings that differ from her spoken words and trigger avatar micro-expressions.
// 4. Shared World Blackboard (inter-companion gossip across all 7 characters) & Ongoing Life Arcs.
#include "companion.hpp"
#include "companion_memory.hpp"
#include "companion_mind.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace companion::cognition {

struct Reflection {
    uint64_t at = 0;
    std::wstring text;
    std::string look;
    std::wstring evolved_from; // Earlier reflection on the same theme that this one superseded.
};

struct Thought {
    uint64_t at = 0;
    std::wstring text;
    std::string look;
    Emotion emotion = calm;
};

struct Gossip {
    uint64_t at = 0;
    std::string subject_look; // Which companion it happened with.
    std::string other_look;   // Optional second companion (e.g. a duo visit).
    std::string kind;         // "level", "card", "duo", "played"
    std::wstring detail;      // Game title, level number, etc.
};

struct Reading {
    uint64_t at = 0;
    int day = 0;              // yyyymmdd when she read it.
    std::string look;         // Which companion read it.
    std::wstring topic;       // What she searched for (her interest or the player's).
    std::wstring summary;     // What she found (title + short digest from Tavily).
    bool asked = false;       // Looked up in a chat with her web tool, not on her own.
};

struct HebbianSynapse {
    std::wstring a, b; // Folded stem words or game titles that co-occurred.
    float weight = 0.3f;
    uint64_t updated = 0;
};

// CLS (Hippocampus → Neocortex): a generalized causal rule/pattern abstracted from episodes ("Gdy X → to Y").
struct Schema {
    uint64_t at = 0;
    std::wstring text;
    std::string look;
};

struct ActivationHit {
    std::wstring text;
    std::wstring via; // The associative bridge concept that triggered this memory (hop 1/2).
    float score = 0;
    int hop = 0;      // 0 = direct seed match, 1 = 1-hop association, 2 = 2-hop association.
};

struct State {
    std::vector<Reflection> reflections; // Higher-order beliefs/intentions per companion (≤ 120).
    std::vector<Thought> thoughts;       // Subconscious inner thoughts (<mysl>) per companion (≤ 120).
    std::vector<Gossip> gossip;          // Shared blackboard across all companions (≤ 60).
    std::vector<Reading> readings;       // Articles/facts she read online via Tavily on her own (≤ 60).
    std::vector<HebbianSynapse> synapses; // Strengthened concept-to-concept synapses (≤ 400).
    std::vector<Schema> schemas;         // Generalized causal schemas abstracted across conversations (≤ 40).
    std::map<std::string, std::wstring, std::less<>> dispositions; // Each companion's evolving long-term emotional attitude toward the player.
    std::map<std::string, int, std::less<>> peer_tension; // Dynamic 7x7 inter-companion friction ("a:b" -> 0..100).

    // Records a higher-order reflection; if an older reflection of hers shares ≥2 stem words,
    // links the older one in `evolved_from` and replaces it (belief evolution!).
    void reflect(std::wstring_view text, uint64_t now, std::string_view look);
    // Records a generalized causal schema (CLS neocortical abstraction).
    void addSchema(std::wstring_view text, uint64_t now, std::string_view look);
    // Updates or queries dynamic inter-companion tension (0..100).
    static std::string peerKey(std::string_view a, std::string_view b);
    int peerTension(std::string_view a, std::string_view b) const;
    void adjustPeerTension(std::string_view a, std::string_view b, int delta);
    // Updates her evolving long-term emotional disposition/attitude toward the player.
    void setDisposition(std::string_view look, std::wstring_view text);
    std::wstring dispositionOf(std::string_view look) const;
    // Records a hidden subconscious thought (<mysl>…</mysl>).
    void think(std::wstring_view text, Emotion emotion, uint64_t now, std::string_view look);
    // Posts an event to the shared inter-companion gossip blackboard.
    void postGossip(std::string_view subject_look, std::string_view kind, std::wstring_view detail, uint64_t now, std::string_view other_look = {});
    // Records an article/snippet she read online via Tavily for `look` on `today_yyyymmdd`
    // (`asked`: looked up in a chat with her web tool).
    void readOnline(std::string_view look, int today_yyyymmdd, std::wstring_view topic, std::wstring_view summary, uint64_t now, bool asked = false);
    // Whether `look` may browse on her own now: up to six times on `today_yyyymmdd`, spread over
    // the day (at least `reading_gap` seconds after her last one).
    static constexpr int readings_per_day = 6;
    static constexpr uint64_t reading_gap = 100 * 60;
    bool dailyReadingDue(std::string_view look, int today_yyyymmdd, uint64_t now) const;
    // Hebbian learning: strengthens synapses between concepts that appear together in a message or note.
    void learnHebbian(std::wstring_view text, uint64_t now);
    // Removes a single reflection, subconscious thought, web reading, or causal schema (from the 3D Mind & Memory inspector).
    bool removeReflection(uint64_t at, std::wstring_view text);
    bool removeThought(uint64_t at, std::wstring_view text);
    bool removeReading(uint64_t at, std::wstring_view text);
    bool removeSchema(uint64_t at, std::wstring_view text);

    // Multi-hop Spreading Activation over `Memory` + `State`:
    // Hop 0 excites nodes matching `query`; Hop 1 and Hop 2 propagate activation along shared
    // concepts and Hebbian synapses, weighted by Ebbinghaus retention and Long-Term Potentiation.
    std::vector<ActivationHit> spreadActivation(const Memory& memory, std::string_view look, std::wstring_view query, uint64_t now) const;

    // Builds the cognitive prompt block for `look`:
    // - Higher-order reflections & belief evolution,
    // - Her recent subconscious thoughts,
    // - Multi-hop Spreading Activation associations for `query`,
    // - Inter-companion gossip from the shared blackboard.
    std::wstring prompt(const Memory& memory, std::string_view look, std::wstring_view query, uint64_t now, bool english,
                        const std::function<std::wstring(std::string_view)>& look_name = {}) const;
    // The same, as candidates for her global workspace (companion_mind.hpp): her attitude, latest
    // reflections, last thought, associations `query` lights up, readings and gossip — including
    // what another companion let slip about the player (kind "secret", to `look` as other_look).
    std::vector<mind::Candidate> candidates(const Memory& memory, std::string_view look, std::wstring_view query, uint64_t now, bool english,
                                            const std::function<std::wstring(std::string_view)>& look_name = {}) const;

    bool empty() const {
        return reflections.empty() && thoughts.empty() && gossip.empty() && readings.empty() &&
               synapses.empty() && schemas.empty() && dispositions.empty() && peer_tension.empty();
    }
    std::string save() const;
    static std::optional<State> load(std::string_view json);
};

State loadState(const std::filesystem::path& config);
bool saveState(const std::filesystem::path& config, const State& state);
bool clearState(const std::filesystem::path& config);

// Concise web search queries for `look`'s own browsing, in an order turned by `seed`: her
// interests (character.txt likes, topics, the real things she loves, what is on in her city),
// her own goal now (`goal`) and what the player loves or is playing (`Memory`).
std::vector<std::wstring> dailyReadingQueries(Look look, const Memory& memory, std::wstring_view current_game, bool english, uint32_t seed,
                                              std::wstring_view goal = {});
// Condenses a `web_search::search` JSON response into a short, readable 1–2 sentence digest for `State::readOnline`.
std::wstring summarizeTavilyResult(const json::Value& response);

// Prompt for the end-of-chat "Sleep" consolidation request, written in her diary as herself
// (produces <podsumowanie>, <refleksja>, <nastawienie>; with `life` — her own goal and milestones,
// mind::lifeStory — and `invite_goal`, also <nowy_cel>: a new goal of her own growing out of them;
// with `plan_rules` — mind::episodeRules — also <plany>: her plans for the next day).
std::wstring sleepConsolidationPrompt(bool english, std::wstring_view telemetry_hint = {}, std::wstring_view current_disposition = {},
                                      std::wstring_view life = {}, bool invite_goal = false, std::wstring_view plan_rules = {});

struct Consolidated {
    std::wstring summary;
    std::wstring reflection;
    std::wstring disposition;
    std::wstring goal;        // <nowy_cel>: a new goal of her own (empty: none).
    std::wstring plans;       // <plany>: her plans, one a line (mind::parseEpisodes; empty: none).
    std::wstring schema;      // <schemat>: a generalized causal rule ("Gdy gracz… → …"; empty: none).
    std::wstring inside_joke; // <nasz_zart>: a shared inside joke, nickname or dyadic phrase (empty: none).
    std::wstring key_memory;  // <wazne_wspomnienie>: a self-defining / foundational memory from this talk (McAdams/Tulving; empty: none).
};
Consolidated parseConsolidation(std::wstring_view raw);
// Human-readable Polish/English label for how she felt (e.g. Emotion::shy -> "Zawstydzona" / "Flustered").
std::wstring emotionFeelingLabel(Emotion emotion, bool english);

} // namespace companion::cognition
