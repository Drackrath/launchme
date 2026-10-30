#pragma once
// Towarzyszka's mind between and within conversations (src/net/companion_mind*.cpp): the state that
// lives on its own, apart from what the model writes, so she is not reset with every reply.
//
// - Affect (Barrett): her mood as valence (unpleasant–pleasant) and arousal (calm–lively). It
//   carries over between replies and conversations and settles back to her character's baseline
//   over hours; emotions within a reply still change freely on top of it.
// - Drives and goals (Damasio): needs with set points (bond with the player, energy, curiosity,
//   pride in her taste, autonomy: time for her own things) and goals that last days — her own
//   life goals from character.txt (not about games), promises she made, her intention for the
//   relationship.
// - Self (Seth): what she has said about herself (so she keeps to it) and somatic markers (how
//   games and topics feel to her).
// - Her life outside the chat (episodes): what she did, with whom and where — planned in her
//   diary, filled in afterwards for a long gap, kept so she keeps to it; private ones stay closed.
// - Her life's state (companion_affinity.hpp: health, work, money, family, social energy) and her
//   relationship with the player (track, posture): they move where her mood rests and her energy.
// - Expectations (Clark): what she predicts the player will do or say, settled by the launcher's
//   telemetry or by her own appraisal; a broken one is a surprise that draws her attention.
// - System 1 (Kahneman): a quick local read of a message for her face before the model answers.
// - Curiosity (Clark & Brennan's grounding): now and then she asks back when she is not sure she
//   understood him, or carries the talk on herself; a question that does not fit the moment waits.
// - Her inner appraisal (<ocena>{…}</ocena>, JSON, before her words): the model's System 2 view
//   of the moment, applied to her mind and to what she knows about the player (Memory).
// - The global workspace (Dehaene): every part offers candidates; only a few of the most salient
//   enter her prompt, each tagged with how she knows it (know, heard, guess, gossip, feel, want,
//   remember). The rest stays in the background for her recall tool.
// Pure logic; the app loads, saves (DPAPI, <config>/companion_mind.dat) and drives it.
#include "companion.hpp"
#include "companion_feelings.hpp"
#include "companion_characters.hpp"
#include "companion_memory.hpp"
#include "companion_social.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace companion::mind {
inline constexpr size_t max_learned_concepts = 256;
inline constexpr size_t max_player_world = 128;
inline constexpr size_t max_key_memories = 256;
inline constexpr size_t max_shared_idioms = 128;

struct Affect { float valence = 0, arousal = 0.3f; uint64_t at = 0; };
// Her character's resting mood: where her affect settles when nothing happens.
Affect baseline(Persona persona);
// Her affect at `now`: halfway back to her baseline every four hours.
Affect settled(const Affect& affect, Persona persona, uint64_t now);

// Her life now (companion_affinity.hpp Relation; its defaults from character.txt): the state of
// her body and her life is the ground her feelings stand on (Damasio) — being ill or burnt out
// lowers where her mood rests and caps her energy, a bonus or inspiration lifts it.
struct Life {
    LifeHealth health = LifeHealth::fit;
    LifeWork work = LifeWork::steady;
    LifeWealth wealth = LifeWealth::comfortable;
    LifeFamily family = LifeFamily::calm;
    LifeSocial social = LifeSocial::balanced;
};
struct Tone { float valence = 0, arousal = 0, energy = 1; }; // Shifts to her baseline; energy: its cap (0–1).
Tone lifeTone(const Life& life);
// Her mood in words ("lekko rozdrażniona, ożywiona").
std::wstring describe(const Affect& affect, bool english);

// Needs, each 0–1: bond (missing the player: grows apart, eases while talking), energy (spent by
// talking, back with rest; her night or her morning, by chronotype, takes some), curiosity
// (about the player and the world: grows, eased by new things), pride (in her taste: suggestions
// played or ignored) and autonomy (wanting her own topics: grows when talk is only about games).
struct Drives { float bond = 0.3f, energy = 0.9f, curiosity = 0.4f, pride = 0.5f, autonomy = 0.3f; uint64_t at = 0; };

struct Expectation {
    std::string id;       // "o" and a number: her appraisal settles it by this id.
    std::wstring text;
    std::string game;     // gameKey: a session of that game settles it (telemetry).
    std::wstring title;
    float p = 0.5f;       // How likely she thought it was.
    uint64_t made = 0, due = 0;
    int outcome = -1;     // -1 open, 0 broken, 1 met, 2 lapsed (nothing could tell).
    uint64_t settled = 0;
    bool presence = false;    // "He will be back by `due`": his next message settles it.
    bool suggestion = false;  // About a game she suggested (her pride rides on it).
};
// Her own goals are her life outside the chat, never about games, and slow: one at a time, each
// taking one to three weeks (its stages at its thirds), a few days' rest between them. Her
// character.txt's goals only start her off (each used once); after them she sets her own — when
// she decides something in a talk (her appraisal) or in her diary after one — growing out of what
// she has achieved. What she achieves becomes a milestone of her life story, kept for good.
struct Goal {
    std::string id;       // "g" and a number.
    std::wstring text;
    enum class Kind { own, promise, intention } kind = Kind::own;
    enum class Status { open, done, dropped } status = Status::open;
    int stage = 0;        // Her own goal: 0 start, 1 a complication, 2 the finale.
    int span = 0;         // Her own goal: the days it takes (7–21).
    uint64_t made = 0, updated = 0, due = 0;
    std::wstring progress; // Her last word on it.
    int source = -1;      // A starting goal: its index in her character's goals (-1: her own idea).
};
struct Claim { uint64_t at = 0; std::wstring text; };       // Something she said about herself.
struct Marker {
    std::wstring subject;
    float valence = 0;             // Subcortical (amygdala) emotional weight (-1..1).
    uint64_t at = 0;
    float cortical_inhibition = 0; // Prefrontal (vmPFC) regulation (0..1); bypassed when cognitive control is low.
};

// Basal Ganglia conversational habit weights learned via dopaminergic Reward Prediction Error (RPE).
struct ConvPolicy {
    float teasing = 0.5f;        // Playful banter & teasing.
    float deep_questions = 0.5f; // Asking about the player's life, thoughts and feelings.
    float game_talk = 0.5f;      // Diving into game mechanics, backlog and recommendations.
    float own_life = 0.5f;       // Sharing her own daily life, hobbies and stories.
    float warmth = 0.5f;         // Comforting, caring and emotional openness.
    float baseline_reward = 0.5f; // Expected social reward (R_avg).
    std::string last_style;      // Last conversational strategy used ("teasing", "deep", "games", "life", "warmth").
};

// A concept, slang term or mechanic the player taught her; she re-uses it as her mastery grows.
struct LearnedConcept {
    std::wstring term;
    std::wstring meaning;
    float mastery = 0.35f; // 0..1: <0.5 tentative/in-character reinterpretation, >=0.6 confident use.
    uint64_t at = 0;
};

// Hawkins' Reference Frame of the player's world (A Thousand Brains): short named entities
// (people, pets, places, projects) in the player's life and how they relate to them.
struct WorldEntity {
    std::wstring name;     // Short label (≤ 36 chars, e.g. "Tomek (brat)", "Pies Burek").
    std::wstring relation; // Short context (≤ 80 chars, e.g. "grają razem w co-opy").
    uint64_t at = 0;
};

// A conscious fib, bluff or excuse she told the player (out of shame, pride, for a goal or to hide
// how much she cares) — kept distinct from true Self::claims so she remembers she lied, feels
// guilt or smug relief about it, and can confess the truth once trust grows or her control drops.
struct Untruth {
    std::string id;          // "u" and a number.
    uint64_t at = 0;         // When she told the fib.
    std::wstring said;       // What she claimed out loud ("Wcale nie zaspałam, od rana pracowałam").
    std::wstring truth;      // What was actually true ("Zaspałam do południa i było mi głupio").
    std::string reason;      // "shame" (wstyd), "pride" (duma), "goal" (cel), "care" (troska).
    std::string feeling;     // "guilt" (wina), "relief" (ulga), "satisfaction" (satysfakcja).
    int trust_at = 50;       // Relationship trust level (0–100) when she told the fib.
    bool confessed = false;  // True once the truth is out: she owned up, or he caught her (`caught`).
    bool peeked = false;     // True if the player uncovered it via Odkryj in the Mind view.
    bool caught = false;     // He confronted her with the truth he peeked at (she did not confess it herself).
};

// Her curiosity, one act at a time (two separate paths, CuriousMove): after the player's message
// she asked back because she was not sure she understood him (clarify), or she carried the talk
// on herself — a curious question about what he said or a thread of her own (lead). A question
// that did not fit the moment she sets aside for later (`at` stays 0 until she asks it). His next
// message settles an asked one: he explained / took it up, or let it pass. The Mind view shows them.
struct CuriousAct {
    std::string id;           // "c" and a number.
    CuriousMove move = CuriousMove::lead;
    std::wstring about;       // What she asked, or where she took the talk (short).
    std::wstring why;         // What prompted it ("odpowiedział niejednoznacznie").
    uint64_t at = 0;          // When she asked; 0 while it waits for a better moment.
    uint64_t later_at = 0;    // When she set it aside (0: asked at once).
    int outcome = -1;         // -1 open (waiting for his answer, or for her moment); 0 he let it pass; 1 he answered / took it up; 2 never asked.
    uint64_t settled = 0;
    bool returned = false;    // Left unanswered and she came back to it once (or let it go).
    bool deferred() const { return at == 0 && outcome == -1; }
};
// What System 1 makes of his message for her next reply's curiosity (curiosityUrge): no act yet,
// only the urge; her reply and appraisal turn it into a CuriousAct.
struct Urge {
    CuriousMove move = CuriousMove::none;
    std::wstring why;          // Worded for her prompt and her Mind view.
    bool deeper = false;       // Lead: dig into what he said (else: he closed the topic, a thread of hers).
    bool gentle = false;       // He is down or opened up: softly.
    CuriousCarry carry = CuriousCarry::none; // What the lead brings back instead of a new thread…
    std::string carry_id;      // …its id (a CuriousAct, a FollowUp; empty for a thread)…
    std::wstring carried;      // …and its words.
    uint64_t at = 0;
};

// Something coming up in his life (an exam, an interview, a trip, a match) that she asks about
// after it — prospective memory for his life ("Mówiłeś, że w czwartek masz rozmowę — i jak?").
struct FollowUp {
    std::string id;            // "f" and a number.
    std::wstring what;         // "rozmowa o pracę"
    std::wstring when;         // As he said it ("w czwartek").
    uint64_t made = 0, due = 0; // When she heard it; when it should be over by.
    int state = 0;             // 0 waiting, 1 she asked how it went, 2 let go (too long ago), 3 he told her himself.
    bool wished = false;       // She wished him luck before it.
    uint64_t settled = 0;
};
// Bids for connection (Gottman): how he answers what she reaches out with (a question, something
// of her own) — turning toward, away or against — fading over a week; and his own bids she noticed.
struct Bids {
    float toward = 0, away = 0, against = 0;
    int his = 0;
    bool open = false;         // Her last reply reached out: his next message answers it.
    uint64_t at = 0;
};
// How he writes (communication accommodation: she tunes in, keeping her character), averaged.
struct Style { float words = 0, exclaim = 0, lower = 0, plain = 0, laughs = 0; int samples = 0; };
// The topic of the moment (his content words) and how many of his messages stayed on it.
struct Topic { std::vector<std::wstring> words; int turns = 0; };
// A thread he opened that the talk did not pick up: she may come back to it ("wracając do…").
struct Thread { uint64_t at = 0; std::wstring text; bool used = false; };
// Her attachment style (Bowlby; Hazan & Shaver): how his absence, his brush-offs and her own
// openness move her. character.txt attachment=, else her character's (-dere) default.
enum class Attachment { secure, anxious, avoidant, fearful };

// Her life outside the chat: what she did, with whom, where — kept, so she keeps to it. Planned
// ones (her diary's plans) happen when their time passes, or fall through now and then. Another
// companion in one (online only: they live in different cities) has it too, with the same id.
// A private one (a date, a family row, her health…) she does not bring up herself, and the Mind
// view keeps it closed until she tells the player or he opens it there (she does not know he did).
struct Episode {
    std::string id;          // "e" and a hash of when and what: the same in both minds when shared.
    uint64_t at = 0;         // When it happens or happened (Unix seconds).
    std::wstring what, with, where;
    std::string partner;     // Another companion in it (her key).
    enum class State { planned, happened, fell_through } state = State::planned;
    bool hidden = false;     // Private.
    bool date = false;       // A date or a flirt with someone (always private; never while she is with the player).
    bool told = false;       // She told the player about it.
    bool peeked = false;     // The player opened it in the Mind view.
    uint64_t made = 0;
};

struct Self {
    social::State social;
    struct FeelingMoment {
        Feeling kind = Feeling::calm;
        std::optional<float> intensity;
        std::wstring reason;
        uint64_t at = 0;
        bool inferred = false; // Recognized legacy free-text label, rather than an explicit model key.
    };
    std::vector<FeelingMoment> feelings; // Last 80 named moments, persisted independently of sprite IDs.
    Affect affect;
    Drives drives;
    ConvPolicy policy;
    std::vector<Expectation> expectations;       // ≤ 40
    std::vector<Goal> goals;                     // ≤ 30
    std::vector<Claim> claims;                   // ≤ 120
    std::vector<Untruth> untruths;               // Conscious fibs/bluffs/excuses she told (≤ 24).
    std::vector<Claim> milestones;               // What she achieved in her life, kept for good (≤ 60).
    std::vector<std::wstring> aspirations;       // New own goals she set herself, waiting their turn (≤ 5).
    std::vector<Marker> markers;                 // ≤ 60
    std::vector<Episode> episodes;               // Her life outside the chat, by time (≤ 60, a month).
    std::vector<LearnedConcept> learned_concepts; // Concepts/terms taught by the player (≤ max_learned_concepts).
    std::vector<WorldEntity> player_world;       // Short entities from the player's world (≤ max_player_world).
    std::vector<Claim> key_memories;             // Self-defining memories with the player (≤ max_key_memories).
    std::vector<Claim> shared_idioms;            // Shared inside jokes, nicknames & rituals (≤ max_shared_idioms).
    std::vector<Claim> ruminations;              // DMN offline ruminations during unresolved tension/broken expectations (≤ 8).
    std::vector<Claim> rethought;                // Topics she re-thought / re-conceptualized after time (≤ 20).
    std::vector<CuriousAct> curious;             // Her questions back and the talk she carried on (≤ 16).
    Urge urge;                                   // This reply's curiosity, from his last message (not saved).
    int curious_gap = 99;                        // Her replies since her last curious act (not saved): curiosity now and then, not in every reply.
    // What her appraisal says she does in the reply being applied (not saved): it becomes a curious
    // act only when her words really ask (a reflection needs no question) — saying so is not asking.
    struct PendingCurious { CuriousMove move = CuriousMove::none; std::wstring about, why; } pending_curious;
    std::vector<FollowUp> follow_ups;            // What is coming up in his life, to ask about after (≤ 12).
    Bids bids;                                   // How he answers her bids for connection (Gottman).
    Style his_style;                             // How he writes (accommodation).
    Topic topic;                                 // The topic of the moment (not saved).
    std::vector<Thread> threads;                 // Threads he opened that went nowhere (≤ 5).
    float interest = 0.5f;                       // Her interest in what he writes: surprisingly rich replies raise it (reward prediction error).
    float interest_rpe = 0;                      // The last surprise in it (+ more interesting than expected; not saved).
    uint64_t interest_at = 0;
    uint64_t opened_at = 0;                      // Her last emotionally open reply (an avoidant one pulls back after).
    uint64_t his_open_at = 0;                    // His last personal opening-up (confession, his life).
    uint64_t bonded_at = 0;                      // Reciprocal self-disclosure (hyperpersonal effect): closeness, calm.
    bool retold = false;                         // Her last reply told him again something he knew (not saved; metrics).
    float play_momentum = 0.0f;                  // Panksepp's subcortical PLAY circuit momentum (0..1; gated off by negative affect).
    float sleep_debt = 0.0f;                     // Sapolsky's multi-day sleep debt (0..1; accumulated by late-night talks, lowers energy next day).
    float stress_load = 0.0f;                    // Sapolsky's HPA-axis / cortisol allostatic load (0..1; slow 24h decay from burnout/spats).
    int rejected_picks_in_row = 0;               // Berne's "Why Don't You — Yes, But" counter: consecutive rejected game suggestions.
    uint64_t lived_until = 0;                    // Her life is filled in up to here (a long gap is filled once).
    std::wstring felt;                           // The feeling she named in her last appraisal…
    uint64_t felt_at = 0;
    std::wstring player_read;                    // …and how she read the player then.
    uint64_t player_read_at = 0;
    std::wstring counterfactual;                 // Unchosen impulse from deliberation mode ("waha_sie").
    uint64_t counterfactual_at = 0;
    std::wstring subtext_note;                   // Micro-chronemic / nonverbal subtext perceived in player's message.
    uint64_t subtext_at = 0;
    uint64_t harsh_at = 0;                       // When she last snapped or replied coldly due to her own bad mood/fatigue.
    std::wstring harsh_reason;                   // Why she was in a bad mood then (for later apology after cooling down).
    bool last_reply_vulnerable = false;          // Whether her last reply opened up emotionally or shared a secret.
    bool peek_caught = false;                    // Player referenced a private episode or fib he peeked at via Odkryj!
    std::wstring peek_caught_what;
    int trust_delta = 0, respect_delta = 0, security_delta = 0; // Pending relationship axis adjustments.
    std::vector<std::string> failed_partners;    // Partners whose shared online episode just fell through (for peer_tension).
    uint64_t last_talk = 0;                      // Her last exchange with the player.
    uint64_t last_rumination = 0;                // Last time DMN generated an offline rumination.
    uint64_t last_initiative = 0;                // Last time she spontaneously initiated a message after silence.
    uint64_t goal_rest = 0;                      // Her own goal: the next one starts after this.
    std::vector<int> seeds_used;                 // Her character's starting goals already taken up (indices).
    int next_id = 1;
};

struct State {
    std::map<std::string, Self, std::less<>> selves; // By her key.
    Self& of(std::string_view look);
    const Self* find(std::string_view look) const;
    bool empty() const { return selves.empty(); }
    std::string save() const;
    static std::optional<State> load(std::string_view json);
};
State loadState(const std::filesystem::path& config);
bool saveState(const std::filesystem::path& config, const State& state);
bool clearState(const std::filesystem::path& config);

struct Context {
    std::string look;                      // Her key.
    Persona persona = Persona::genki;
    const Character* character = nullptr;  // Her character.txt (goals, chronotype, timezone); may be missing.
    uint64_t now = 0;                      // Unix seconds.
    bool english = false;
    Life life;                                    // Her life's state now.
    RelationRole role = RelationRole::auto_role;  // Her track with the player now (affinity::effectiveRole).
    RelationStance stance = RelationStance::open; // Her posture in it.
    int tension = 0;                              // 0–100: unspoken / cross-boundary tension.
    int trust = 50, respect = 60, security = 50;  // Multi-dimensional bond axes (0–100).
    int level = 1;                                // Relationship level (1–50).
    // Their stage (affinity::Stage as a number: 0 strangers, 1 light flirt, 2 flirt, 3 flirty friends,
    // 4 crush, 5 long-distance relationship; gamification off: 3): how far her longing, jealousy and
    // tender feelings about him may go.
    int stage = 3;
    Gender player_gender = Gender::unknown;       // Known player gender (unknown until told or inferred).
    float known = 0;                              // 0–1: how much she knows about him (his notes and world): Loewenstein's information gap.
};
Attachment attachmentOf(const Context& context);
std::wstring attachmentName(Attachment attachment, bool english);
// When something he said is coming up ("jutro", "w czwartek", "w weekend", "za tydzień") is over by.
uint64_t eventDue(std::wstring_view when, uint64_t now);
// Where her mood rests now: her character's baseline moved by her life.
Affect baseline(const Context& context);
// Her affect at `context.now`, settling to that.
Affect settled(const Affect& affect, const Context& context);

// Time passes: her affect settles, her drives move, her own goal goes on by her days, promises
// lapse, expectations past their time are broken (telemetry ones) or lapse, her plans happen
// (or fall through, with causal life consequences), DMN ruminates on unresolved tension and old episodes fade.
void tick(Self& self, const Context& context);
// Her energy now: her stored energy less what her night (or, for a night owl, her morning) takes,
// at most what her life leaves her.
float energyNow(const Self& self, const Context& context);
// Prefrontal cognitive control (vmPFC over amygdala, 0–1): high when rested and calm, low when
// exhausted late at night or emotionally overwhelmed — when low (<0.42), her archetype's mask cracks ("drop the act").
float cognitiveControl(const Self& self, const Context& context);
// Her night by her own clock: in bed from about 23:00 until about 7:00 (a night owl: about 2:00
// until 10:00), each half an hour either way and the same all that night; now and then (1.5%)
// a sleepless one. The night she is in, or the next one; nullopt when her clock is unknown.
struct Night {
    uint64_t from = 0, to = 0; // Unix seconds.
    bool sleepless = false;
};
std::optional<Night> nightAt(const Context& context);
// Whether she sleeps now: within her night, and not a sleepless one.
bool asleep(const Context& context);
// Whether she is up now (not asleep; true when her clock is unknown).
bool awake(const Context& context);
// Whether this is her very first meeting with the player (no history, no notes, 0 points).
bool isFirstMeeting(const Self& self, const Memory& memory, std::string_view look, int64_t affinity_points = 0);
// Whether the current moment calls for System 2 Counterfactual Deliberation ("waha_sie").
bool needsDeliberation(const Self& self, const Context& context);

// Her relationship or her life just turned (her [dylemat]/[dystans]/[przelamanie]/[otwarcie],
// [relacja:…] and [zycie:…] tags; companion_affinity.hpp keeps the state): a jolt to her affect;
// a breakthrough, a new romance or a promotion becomes a milestone of her life.
struct Shift {
    std::optional<RelationStance> stance;
    std::optional<RelationRole> role;
    std::optional<LifeHealth> health;
    std::optional<LifeWork> work;
    std::optional<LifeWealth> wealth;
    std::optional<LifeFamily> family;
    std::optional<LifeSocial> social;
    bool any() const { return stance || role || health || work || wealth || family || social; }
};
void onShift(Self& self, const Context& context, const Shift& shift);

// Her episodes as she writes them, one a line (her diary's <plany>, a fill-in's <epizody>):
// "jutro 18:30 | z bratem | targ Marché des Enfants Rouges | zakupy na kolację | prywatne"
// — when (dziś/jutro/pojutrze/wczoraj/przedwczoraj and her local HH:MM or a part of the day),
// with whom, where, what, and optionally "prywatne" or "randka".
struct EpisodeLine { int day = 0; int minute = -1; std::wstring with, where, what; bool hidden = false, date = false; };
std::vector<EpisodeLine> parseEpisodes(std::wstring_view text);
// Adds them to her life at her local times: `planned`, the ones still ahead (from `context.now`);
// else the ones that happened between `from` and now. A date is dropped while she is with the
// player (romance, secret romance or a breakthrough) or when she had one in the last two weeks.
// `companion_of` finds another companion named in "with" (her key; empty: none): online only.
// Returns what was added (for another companion's mind: shareEpisode).
std::vector<Episode> addEpisodes(Self& self, const Context& context, const std::vector<EpisodeLine>& lines, bool planned, uint64_t from = 0,
                                 const std::function<std::string(std::wstring_view)>& companion_of = {});
// Another companion's episode with her in it (`host`: that one's key and name): hers too. False when she has it.
bool shareEpisode(Self& self, const Episode& episode, std::string_view host, std::wstring_view host_name);
// A stretch of her life with nothing in it since her last episode or talk (at most three days
// back) longer than eight hours: worth filling in (lifeRequest); nullopt otherwise.
struct Gap { uint64_t from = 0, to = 0; };
std::optional<Gap> lifeGap(const Self& self, const Context& context);
// The rules for writing episodes (planning ahead or looking back), with the other companions
// (`others`: "Saki (Paryż), …") and her track with the player.
std::wstring episodeRules(const Context& context, std::wstring_view others, bool planning);
// Her diary's plan rules (episodeRules, planning): empty when the coming day already holds two
// plans or more (asked again, she would restate them); with one, a reminder not to repeat it.
std::wstring planRules(const Self& self, const Context& context, std::wstring_view others);
// The hidden turn asking her diary to fill in a gap (answer: <epizody>lines</epizody>).
std::wstring lifeRequest(const Self& self, const Context& context, const Gap& gap, std::wstring_view others);
// When an episode is (was) in her words, by her clock: "wczoraj wieczorem (ok. 19:30)".
std::wstring episodeWhen(const Episode& episode, const Context& context);
// The town she is in on a trip right now (an episode of travel that started and is not over: "na 3
// dni", a weekend, else two days; a day trip till the evening): the first part of its place; empty
// when she is at home.
std::wstring travelPlace(const Self& self, const Context& context);
// The player opened a private episode or unconfessed fib in the Mind view (she does not know). False when none.
bool peekEpisode(Self& self, std::string_view id);
// Whether an unconfessed fib/bluff is ready for her to confess (trust grew, guilt lingered, or cognitive control dropped).
bool readyToConfess(const Untruth& untruth, const Self& self, const Context& context);
// Her reply told the player about a private episode (its words in the reply): told. The count.
int noticeTold(Self& self, std::wstring_view reply);
// Her reply owned up to an unconfessed fib/bluff (its truth words in the reply): confessed. The count.
int noticeConfessed(Self& self, std::wstring_view reply, uint64_t now);
// Her reply apologized after she was previously harsh/cold due to bad mood or fatigue.
bool noticeApology(Self& self, std::wstring_view reply, uint64_t now);
// Detects if the player's message mentions a private episode or fib that was peeked at via Odkryj (!told/!confessed && peeked).
bool noticePeekedLeak(Self& self, std::wstring_view player_message, uint64_t now);
// Records a shared inside joke / dyadic phrase, a concept taught by the player, a key self-defining memory, or a short entity from the player's world.
bool addSharedIdiom(Self& self, std::wstring_view text, uint64_t now);
bool learnConcept(Self& self, std::wstring_view term, std::wstring_view meaning, uint64_t now);
bool addKeyMemory(Self& self, std::wstring_view text, uint64_t now);
bool addWorldEntity(Self& self, std::wstring_view name, std::wstring_view relation, uint64_t now);
// Spontaneous initiative (#21): when the chat pauses or ends and the player is quiet, builds a hidden
// kickoff prompt tailored to her current mind state ("hey are you there?", "I was just sitting here thinking about…",
// "listen, I was recently at…", or confessing/apologizing).
std::wstring spontaneousPrompt(const Self& self, const Context& context, uint32_t seed);

// System 1: a quick read of the player's message, before the model answers — the feeling that
// shows on her face for a moment (a micro-expression: the true one, whatever her character
// shows on purpose later), micro-chronemic subtext (dismissive 1-word reply after vulnerability
// or late-night confession), ACC irony/sarcasm conflict detection, Berne's "Yes, But" game rejection,
// Panksepp's PLAY banter, sycophancy vs. independent opinion, and a nudge to her affect.
struct Quick {
    std::optional<Emotion> flash;
    float valence = 0, arousal = 0;
    bool dismissive = false;      // Short 1–2 word brush-off right after she opened up emotionally.
    bool confession = false;      // Long, personal wall of text from the player.
    bool sad_or_stressed = false; // Player expresses sadness, exhaustion or distress (triggers co-regulation when trusted).
    bool irony_or_vent = false;   // ACC conflict detector (McGilchrist/Grice): hyperbolic praise ("no super", "świetnie") clashing with frustration/failure.
    bool skeptical_irony = false; // Playful/skeptical implicature ("ta jasne", "yeah right", "chciałabyś").
    bool rejected_pick = false;   // Berne's "Why Don't You — Yes, But": player turns down a game suggestion with an excuse.
    bool accepted_pick = false;   // Player accepts or praises a game suggestion.
    bool playful = false;         // Panksepp's PLAY circuit: laughter, teasing banter.
    int respect_delta = 0;        // Anti-sycophancy (<0) vs. defending own taste/opinion (>0).
    // Grounding (Clark & Brennan): what may keep her from being sure she understood him — an
    // ambivalent answer, a reference to something he did not name, a thought broken off.
    enum class Unclear { none, ambivalent, vague, unfinished } unclear = Unclear::none;
    bool closed = false;          // A short reply that closes the topic ("ok", "spoko", "nic ciekawego").
    bool hook = false;            // He told her something of his own life / plans (worth digging into).
    bool asks = false;            // He asked her something ('?').
    bool bid = false;             // His bid for connection (Gottman): showing her something, "guess what", "listen…".
    bool hostile = false;         // Insults, "shut up": turning against.
    int words = 0;                // His message's word count.
};
// `stage` (Context::stage): a declaration of love from a near stranger flusters and amuses her
// rather than delights her; the further they are, the warmer it lands.
Quick quick(std::wstring_view message, const Self& self, int stage = 3);
// Her curiosity for this reply (two separate paths): ask back when his message leaves her unsure
// what he meant (clarify), or carry the talk on (lead) when he closed the topic or told her
// something worth asking about. Now and then, not every reply (her last curious act, her
// curiosity drive, her character, her energy and `seed` decide); never on top of hurt, and lead
// never when he asked her something. A question she set aside earlier comes back with a lead.
Urge curiosityUrge(const Self& self, const Context& context, const Quick& quick, uint32_t seed);
// The player wrote to her: the quick read nudges her affect, missing him eases, a presence
// expectation is met, Basal Ganglia RPE updates her conversational policy and peeked leaks are checked.
void onPlayerMessage(Self& self, const Context& context, const Quick& quick, std::wstring_view message = {});
// Her answer: talking spends energy; only games makes her want her own topics; tracks vulnerability & policy style.
void onReply(Self& self, const Context& context, bool about_games, std::wstring_view reply_text = {}, bool vulnerable = false);
// She suggested games (gameKey, title): she expects him to play them within a few days.
void onSuggested(Self& self, const Context& context, const std::vector<std::pair<std::string, std::wstring>>& games);
// A session ended: expectations about the game are met (pride when it was her suggestion); the
// game feels good to her (a marker).
void onPlayed(Self& self, const Context& context, std::string_view key, std::wstring_view title, uint32_t minutes);
// A conversation ended: a player who comes every day is expected back tomorrow.
void onConversationEnd(Self& self, const Context& context, int streak);

// Her inner appraisal (<ocena>{…}</ocena>, JSON with short Polish keys; see systemPrompt).
struct Appraisal {
    social::Update social;
    std::optional<float> valence, arousal;
    std::optional<Feeling> feeling;
    std::optional<float> feeling_intensity;
    bool feeling_inferred = false;
    std::wstring feels, player;
    std::vector<std::string> met, broken;
    struct Belief { std::wstring text; std::string source = "told"; float p = 0.8f; bool secret = false; std::string topic, fact_key; };
    std::vector<Belief> beliefs;
    struct Revision { std::string id; std::wstring text; std::optional<float> p; };
    std::vector<Revision> revisions;
    struct Prediction { std::wstring text; float p = 0.5f; std::wstring due; std::wstring game; };
    std::vector<Prediction> predictions;
    std::vector<std::wstring> claims;
    std::wstring intention;
    std::wstring new_goal;    // "nowy_cel": a new goal of her own she just set herself.
    struct Promise { std::wstring text; std::wstring due; };
    std::optional<Promise> promise;
    // "cel": her own goal moved on; "efekt": what came of it when done ("Kupiłam wymarzony motocykl").
    struct Progress { std::string id; std::wstring note, result; bool done = false, dropped = false; };
    std::vector<Progress> goals;
    std::vector<std::string> told; // "mowie": #ids of her private episodes she just told the player about.
    // "klamstwo" / "untruth": a conscious fib, bluff or excuse she just told out of shame, pride, for a goal or care.
    struct Fib {
        std::wstring said, truth;
        std::string reason = "shame";  // "shame", "pride", "goal", "care".
        std::string feeling = "guilt"; // "guilt", "relief", "satisfaction".
    };
    std::optional<Fib> untruth;
    std::vector<std::string> confessed; // "przyznaje": #ids of her untruths she just confessed to the player.
    // "nauczyl": concepts or terms the player taught her in this turn.
    struct Taught { std::wstring term, meaning; };
    std::vector<Taught> taught;
    // "przemyslenie": re-conceptualization / rethinking of a topic after time.
    struct Rethink { std::wstring topic, view; };
    std::optional<Rethink> rethink;
    // "nasz_zart": a shared inside joke or nickname born in this turn.
    std::wstring inside_joke;
    // "wazne_wspomnienie" / "key_memory": a self-defining / foundational memory from this turn (McAdams/Tulving).
    std::wstring key_memory;
    // "swiat" / "world": short entities from the player's world (Hawkins' reference frames: people, pets, places, projects).
    struct WorldItem { std::wstring name, relation; };
    std::vector<WorldItem> player_world;
    // "waha_sie": counterfactual deliberation options (unchosen impulse vs. chosen response).
    std::vector<std::wstring> hesitation;
    // "plec": player's gender when they state it or answer her question ("mezczyzna" / "kobieta").
    std::optional<Gender> player_gender;
    // "ciekawosc": {"ruch":"dopytuje|ciagne","o_co":"…","rozumiem":0-1,"kiedy":"teraz|pozniej"} —
    // she asks back / carries the talk on in this reply, or sets a question aside for later.
    struct Curious {
        CuriousMove move = CuriousMove::none; // none: not said (her urge decides).
        std::wstring about;
        std::optional<float> understood;      // How sure she is she understood him (0–1).
        bool later = false;
    };
    std::optional<Curious> curious;
    // "jego_plan": [{"t":"rozmowa o pracę","kiedy":"czwartek"}] — something coming up in his life.
    struct Plan { std::wstring what, when; };
    std::vector<Plan> plans;
    bool ok = false;
};
Appraisal parseAppraisal(std::wstring_view json);
struct Applied { int beliefs = 0, revisions = 0, met = 0, broken = 0, predictions = 0, claims = 0; };
// Applies it to her mind and to what she knows about the player (beliefs go to Memory as hers;
// `guest` heard them too). `game_key` finds a library game by title (empty: none).
Applied apply(Self& self, Memory& memory, const Appraisal& appraisal, const Context& context, std::string_view guest,
              const std::function<std::string(std::wstring_view)>& game_key = {});

// Gossip (Seth's social self): another companion may pass on what the player told her — the more
// gossipy she is, the likelier. At most one note a conversation, never a secret; the note becomes
// hearsay for `look`. Returns who passed it on and what (nullopt: nothing this time).
struct Leak { std::string from; std::wstring text; };
std::optional<Leak> leak(Memory& memory, std::string_view look, const std::function<float(std::string_view)>& gossip_of, uint32_t seed, uint64_t now);

// The global workspace.
enum class Status { know, heard, guess, gossip, feel, want, remember };
struct Candidate {
    std::string source;   // Which part of her offers it ("affect", "goal", "memory", …): two at most from each.
    std::wstring text;
    float salience = 0;   // 0–1.
    Status status = Status::remember;
};
// What her own mind offers for this moment (`query`: the player's last message).
std::vector<Candidate> candidates(const Self& self, const Context& context, const Memory& memory, std::wstring_view query);
// The few most salient (fewer when she is very aroused), worded for her prompt; empty when none.
std::wstring workspace(std::vector<Candidate> candidates, bool english, float arousal);
// The parts of her that made it into her workspace (workspace's own choice), for her diagnostics.
std::vector<std::string> workspaceSources(std::vector<Candidate> candidates, float arousal);
// How he writes, in a few words ("krótko, małymi literami, ze śmiechem"); empty: nothing notable yet.
std::wstring styleText(const Style& style, bool english);
// Her own goal in words for her Mind view ("etap 2/3: komplikacja").
std::wstring goalStage(const Goal& goal, bool english);
// She set herself a new goal of her own (in a talk or in her diary): it starts now when she has
// none, else it waits its turn. False when empty or already hers.
bool aspire(Self& self, const Context& context, std::wstring_view goal);
// Her life for her diary: her current own goal and her latest milestones, in words (the diary may
// set her a new goal that grows out of them); `invite` is true when she has no goal now.
std::wstring lifeStory(const Self& self, bool english, bool* invite = nullptr);
// Her latest episodes and her plans in words (her diary's context: the people and places to keep to).
std::wstring recentLife(const Self& self, const Context& context);
} // namespace companion::mind
