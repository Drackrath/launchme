#pragma once
// Towarzyszka (Settings → System → Towarzyszka AI): an anime companion on the default home and
// in her own chat screen. This is the UI-free backend: the connection to a Foundry-proxied
// Gemini model (generateContent with a Bearer token, as opencode/Python reach Foundry), the
// prompt with a digest of the player's library and reply parsing (emotion tags, [[game]]
// suggestions). Her offline lines: companion_lines.cpp; her reactions to launcher events and
// the game roulette: companion_events.hpp. Workers call ask(); nothing here touches the window.
#include "http.hpp"
#include "json.hpp"
#include "../model/companion_reply_style.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace companion {
// The twelve pictures of each look, in the order of its sprite sheet (res/companion/<key>/costume_<NN>/).
// The first twelve every companion has; the rest (feelings and everyday moments: a sip of soda, a
// controller in hand) may be missing from older pictures, which then show her calm face.
enum class Emotion { smile, angry, sad, excited, shock, thinking, smug, shy, fear, sleepy, playful, cool,
                     love, laugh, cry, confused, determined, wave, drink, gaming };
inline constexpr int emotion_count = 20;
inline constexpr int classic_emotion_count = 12;
inline constexpr Emotion calm = Emotion::cool; // At rest.
// A companion: an index into characters() (companion_characters.hpp, any number of them, each from
// res/companion/<key>/). Each is a companion of her own, with her own character.
enum class Look : int {};
// Her character: persona=<personaKey()> in her character.txt, fixed for her. A new archetype needs
// code (its prompt here, offline lines, rest pace, its default voice in elevenlabs.cpp).
enum class Persona { genki, tsundere, yandere, kuudere, dandere, deredere, himedere, kichidere, kanedere };
inline constexpr int persona_count = 9;

// Relationship & preference axes (character.txt defaults + companion_affinity.hpp per-character state).
enum class Gender { unknown, female, male };
enum class Attraction { any, male, female, none };
enum class AttractionFlex { strict, rare_exception };
enum class RelationStyle { balanced, traditional, casual };
enum class RelationRole { auto_role, friend_role, coworker, romantic, secret_romance };
enum class RelationStance { open, dilemma, distanced, broken };
// Who she talks with (Settings → Asystent; companion_person.hpp words and personalizes it): how
// he or she wants to be called and his or her gender — never "the player" in her prompts.
struct Person {
    Gender gender = Gender::unknown;
    std::wstring name; // A name or a nickname; empty: unknown.
    bool operator==(const Person&) const = default;
};
// Her stance on him seeing other companions too (character.txt exclusivity=): monogamous wants him
// for herself (less so the closer and surer she gets), open does not mind (polyamorous), fluid is
// undecided and changes her mind. Shapes her jealousy (affinity::jealousy) and the "pairs" he has.
enum class Exclusivity { monogamous, open, fluid };

// Fluid life dimensions that can shift over time (health, work/studies, finances/standard of living, family, social/creative energy).
enum class LifeHealth { fit, tired, sick, injured, recovering };
enum class LifeWork { steady, busy, between_jobs, promoted, burnout };
enum class LifeWealth { comfortable, tight, broke, flush };
enum class LifeFamily { calm, visit, tension, homesick };
enum class LifeSocial { balanced, lonely, overwhelmed, inspired };

const char* genderKey(Gender gender);
std::optional<Gender> genderFromKey(std::string_view key);
const char* attractionKey(Attraction attraction);
std::optional<Attraction> attractionFromKey(std::string_view key);
const char* attractionFlexKey(AttractionFlex flex);
std::optional<AttractionFlex> attractionFlexFromKey(std::string_view key);
const char* relationStyleKey(RelationStyle style);
std::optional<RelationStyle> relationStyleFromKey(std::string_view key);
const char* relationRoleKey(RelationRole role);
std::optional<RelationRole> relationRoleFromKey(std::string_view key);
const char* relationStanceKey(RelationStance stance);
std::optional<RelationStance> relationStanceFromKey(std::string_view key);
const char* exclusivityKey(Exclusivity exclusivity);
std::optional<Exclusivity> exclusivityFromKey(std::string_view key);
const char* lifeHealthKey(LifeHealth v);
std::optional<LifeHealth> lifeHealthFromKey(std::string_view key);
const char* lifeWorkKey(LifeWork v);
std::optional<LifeWork> lifeWorkFromKey(std::string_view key);
const char* lifeWealthKey(LifeWealth v);
std::optional<LifeWealth> lifeWealthFromKey(std::string_view key);
const char* lifeFamilyKey(LifeFamily v);
std::optional<LifeFamily> lifeFamilyFromKey(std::string_view key);
const char* lifeSocialKey(LifeSocial v);
std::optional<LifeSocial> lifeSocialFromKey(std::string_view key);

const char* emotionKey(Emotion emotion);
std::optional<Emotion> emotionFromKey(std::string_view key);
int lookCount();                       // Every character, "soon" ones too.
std::string_view lookKey(Look look);   // Her folder's name under res/companion/.
std::optional<Look> lookFromKey(std::string_view key);
const char* personaKey(Persona persona);
std::optional<Persona> personaFromKey(std::string_view key);
const wchar_t* personaName(Persona persona); // "Tsundere": the archetype, the same in every language.
const wchar_t* name(Look look);        // Her name= (character.txt).

// Models offered in Settings (Foundry language-model IDs without the RID prefix).
struct ModelInfo { const char* id; const wchar_t* label; };
inline constexpr std::array<ModelInfo, 5> models{{
    {"gemini-3-8-flash", L"Gemini 3.8 Flash"},
    {"gemini-3-7-flash", L"Gemini 3.7 Flash"},
    {"gemini-3-5-flash", L"Gemini 3.5 Flash"},
    {"gemini-3-1-flash-lite", L"Gemini 3.1 Flash Lite"},
    {"gemini-3-1-pro", L"Gemini 3.1 Pro"},
}};
inline constexpr const char* default_model = "gemini-3-8-flash";
std::wstring modelLabel(std::string_view id);
// Our IDs as Google's and xAI's APIs name them: "gemini-3-8-flash" → "gemini-3.8-flash", "grok-4-7" → "grok-4.7".
std::string geminiModel(std::string_view id);
inline std::string dottedModel(std::string_view id) { return geminiModel(id); }

// Grok models (xAI): our IDs as Foundry's RIDs end ("grok-4-7"); xAI's API names them with a dot
// ("grok-4.7", dottedModel). Foundry offers the first one through its xAI proxy.
inline constexpr std::array<ModelInfo, 2> xai_models{{
    {"grok-4-7", L"Grok 4.7"},
    {"grok-4-6", L"Grok 4.6"},
}};
inline constexpr const char* default_xai_model = "grok-4-7";
inline bool grokModel(std::string_view id) { return id.starts_with("grok") || id.find(".grok") != std::string_view::npos; }

// Who answers for her (Settings → System → Towarzyszka AI → "Dostawca modelu"): Gemini or Grok
// through Foundry's proxies (Bearer token), Gemini with a Google AI Studio key, Claude with an
// Anthropic key, Grok with an xAI key, or an Ollama server (local, home network or cloud). Her
// tools work with all of them.
enum class Provider { foundry, gemini, claude, ollama, xai };
inline constexpr int provider_count = 5;
const char* providerKey(Provider provider);
std::optional<Provider> providerFromKey(std::string_view key);
const wchar_t* providerName(Provider provider); // "Foundry", "Gemini", "Claude", "Ollama", "xAI"
// The wire format a connection speaks (companion_providers.cpp): Gemini's generateContent (Foundry's
// Google proxy, Google), Anthropic Messages, Ollama /api/chat, or the Responses API (Grok: xAI and
// Foundry's xAI proxy).
enum class Dialect { gemini, claude, ollama, responses };
// Claude models chosen by family: the newest one the key can use (GET /v1/models).
struct ClaudeModel { const char* id; const wchar_t* label; };
inline constexpr std::array<ClaudeModel, 3> claude_models{{
    {"auto-opus", L"Claude Opus"}, {"auto-sonnet", L"Claude Sonnet"}, {"auto-haiku", L"Claude Haiku"},
}};
inline constexpr const char* default_claude_model = "auto-opus";

// Where a credential came from. The token itself never leaves the worker that sends it.
enum class Source { none, settings, environment, opencode };
struct Connection {
    Provider provider = Provider::foundry;
    std::wstring host;  // Foundry: https://<stack>.palantirfoundry.<tld>/; Ollama: its server (trailing slash kept)
    std::wstring token; // Bearer token / API key (optional for Ollama); never logged, drawn or stored in Evidence
    std::string model = default_model; // Foundry/Gemini: our ID; Claude: an ID or auto-<family>; Ollama: a name or empty (first installed)
    Source host_source = Source::none, token_source = Source::none;
    bool ready() const;
    Dialect dialect() const; // Foundry: Gemini, or the Responses API for a Grok model.
    // Foundry: https://<host>/api/v2/llm/proxy/google/v1/models/ri.language-model-service..language-model.<model>:generateContent,
    // Grok: https://<host>/api/v2/llm/proxy/xai/v1/responses (xAI: https://api.x.ai/v1/responses).
    std::wstring endpoint() const;
    std::string wireModel() const; // The model named in a Responses body: Foundry's RID or xAI's name.
    std::wstring headers() const; // Authorization for the provider and JSON content type.
    std::wstring label() const;   // "Gemini 3.8 Flash · Foundry", "llama3.1 · Ollama"
};
// What resolve() may read; injectable so tests never see this machine's environment.
struct Probe {
    std::function<std::wstring(const wchar_t*)> environment;           // Empty when unset.
    std::function<std::optional<std::string>(const std::filesystem::path&)> read_file; // Small text files.
    std::filesystem::path opencode_dir; // %USERPROFILE%\.config\opencode (or XDG_CONFIG_HOME\opencode)
};
Probe systemProbe();
// Token: Settings (DPAPI) → FOUNDRY_TOKEN → opencode (plugin literal, then a Foundry provider's
// apiKey). Host: Settings → FOUNDRY_HOST → a Foundry provider's baseURL in opencode.json.
Connection resolve(const std::wstring& stored_host, const std::wstring& stored_token, std::string_view model, const Probe& probe);
// Any provider. Gemini: Settings → GEMINI_API_KEY → GOOGLE_API_KEY; Claude: Settings →
// ANTHROPIC_API_KEY; Ollama: Settings → OLLAMA_HOST → http://127.0.0.1:11434/, key Settings →
// OLLAMA_API_KEY (only cloud servers need one). Foundry: as above.
Connection resolve(Provider provider, const std::wstring& stored_host, const std::wstring& stored_token, std::string_view model, const Probe& probe);
// Normalizes a typed host ("stack.palantirfoundry.co.uk", "https://…/api/…") to https://authority/.
std::wstring normalizeHost(std::wstring_view text);
// A server the user named (Ollama): http or https; without a scheme http for this machine
// and the home network, https otherwise. Empty when it is not an address.
std::wstring normalizeServer(std::wstring_view text);

// Settings → her provider's token or key: encrypted for this Windows user (DPAPI) in the config
// directory, one file per provider (companion_token.dat is Foundry's).
std::wstring storedToken(const std::filesystem::path& config, Provider provider = Provider::foundry);
bool saveToken(const std::filesystem::path& config, std::wstring_view token, Provider provider = Provider::foundry); // Empty removes it.
bool hasStoredToken(const std::filesystem::path& config, Provider provider = Provider::foundry);

// The player's library as the model sees it: counts, recently played games with play time and
// achievements (local data only), installed games never started and the untouched backlog.
struct GameFact {
    std::wstring title;
    std::string key;        // gameKey() of the library entry, for launching a suggestion.
    std::wstring store;     // "Steam", "GOG", …
    bool installed = false;
    uint64_t last_played = 0; // Unix seconds; 0 = never.
    std::optional<uint32_t> minutes;
    uint32_t achievements_unlocked = 0, achievements_total = 0;
    uint32_t app_id = 0;
};
struct Digest {
    size_t total = 0, installed = 0, never_played = 0;
    std::vector<GameFact> recent;    // Most recently played first.
    std::vector<GameFact> untouched; // Installed and never played.
    std::vector<GameFact> backlog;   // A sample of owned games never played.
    std::vector<std::string> resting; // Keys she suggested lately: offline lines pick others while they can.
    std::wstring text(bool english, uint64_t now) const;
    // Every listed game (a suggestion must be one of them).
    std::vector<const GameFact*> all() const;
    const GameFact* find(std::wstring_view title) const; // Exact, then case-insensitive.
};
// The UI's game list → digest (stats come later from readSteamStats on the worker). Games she
// suggested lately (`resting`) give way to others in the samples.
Digest digest(std::vector<GameFact> games, uint32_t seed, std::vector<std::string> resting = {});

struct Turn { bool user = true; std::wstring text; };
// Another companion in her chat for a while: the player called her, or she dropped in by herself.
struct Guest { Look look{}; Persona persona = Persona::yandere; };
// Her curiosity after the player's message (companion_mind.hpp CuriousAct): she asks back because
// she is not sure she understood him (clarify), she carries the talk on herself with a curious
// question or a thread of her own (lead), or — when he opens up — she gives back in her own words
// what she heard and the feeling in it (reflect: active listening).
enum class CuriousMove { none, clarify, lead, reflect };
// What a lead may bring back instead of a new thread: a question she set aside, how something in
// his life went (a follow-up), a question he left unanswered, a thread he opened that went nowhere.
enum class CuriousCarry { none, question, follow_up, returning, thread };
struct PromptExtras {
    bool tools = false;          // She can look the library up (companion_tools.hpp) beyond the summary.
    std::wstring memory;         // What she remembers (companion_memory.hpp), already worded; empty: nothing.
    // A guest in the chat: the model writes for both of them, a few short messages, each line
    // signed with the speaker's name ("Akane: [smug]…"); they talk to the player and each other.
    std::optional<Guest> guest;
    bool burst = false;          // Alone: this reply as two very short messages, as in a messenger.
    bool web = false;            // With her tools: she can search the web too (szukaj_w_sieci, a Tavily key is there).
    // Today's local date and time: "Teraz: czwartek, 15.10.2026, godz. 14:05". Without a year only
    // the weekday and the full hour are told (her training date would pass for today).
    struct Today { int year = 0, month = 0, day = 0, minute = 0; };
    Today today;
    // Gamification on (companion_affinity.hpp): her level with the player (0: off), the days in a
    // row he has written to her, a guest's own level (their rivalry) and her card for him still
    // live (affinity::Card: 0 none, 1 yellow, 2 red). Below affinity::boundary_level, alone, she
    // may set a boundary with [granica] when he goes much too far.
    struct Bond {
        int level = 0, streak = 0, guest_level = 0, card = 0;
        RelationRole role = RelationRole::auto_role;     // Active relationship track (friend, coworker, romantic, secret_romance).
        RelationRole guest_role = RelationRole::auto_role;
        RelationStance stance = RelationStance::open;    // Her internal posture (open, dilemma, distanced, broken).
        int player_cap = 0;                              // Max level stage cap set by the player (0: uncapped; e.g. 24 or 49: player keeps distance).
        int tension = 0;                                 // 0–100: cross-boundary / unspoken tension towards a dilemma or breakthrough.
        Gender player_gender = Gender::unknown;
        LifeHealth health = LifeHealth::fit;
        LifeWork work = LifeWork::steady;
        LifeWealth wealth = LifeWealth::comfortable;
        LifeFamily family = LifeFamily::calm;
        LifeSocial social = LifeSocial::balanced;
        int trust = 50, respect = 60, security = 50;     // Multi-dimensional bond axes (0–100).
        bool first_meet = false;                         // True when meeting for the very first time (0 points, no history with her).
        std::wstring known_companion;                    // Name of another companion the player already knows, if any.
        // The other companions and his level with each (his "pairs": flirty friends, a crush, a
        // long-distance relationship); she knows of them from the girls and minds them as her
        // stance on exclusivity (character.txt exclusivity=) and their stage allow.
        struct Other { Look look{}; int level = 0; };
        std::vector<Other> others;
    };
    Bond bond;
    std::wstring outfit;      // A costume instead of her basic outfit: its name ("Kimono")…
    std::wstring outfit_look; // …and, from its costume.txt, the outfit in words ("Masz na sobie …").
    // Already worded, each its own line (empty: none): how she feels as this conversation starts
    // (companion_mood.hpp), the game the player is in the middle of (companion_tools.hpp
    // inProgress) and the player's last week for small talk (companion_week.hpp).
    std::wstring mood, playing, week;
    // Her mind (companion_mind.hpp): before her words she writes her hidden appraisal and thought
    // (<ocena>{…}</ocena><mysl>…</mysl>); her topics come through her workspace (in `memory`)
    // rather than as a section of their own. `appraisal_extra`: more keys for her appraisal,
    // already worded (another part of her may ask for them; empty: none).
    bool inner = false;
    bool deliberate = false;        // System 2 counterfactual deliberation mode ("waha_sie" in <ocena>).
    float cognitive_control = 1.0f; // vmPFC cognitive control (0–1): when low (<0.42), her persona mask cracks ("drop the act").
    std::wstring appraisal_extra;
    // This reply's curiosity (mind::curiosityUrge; alone in the chat only): ask back (clarify),
    // carry the talk on (lead) or reflect what he said (reflect); `curiosity_why`: what prompted it,
    // worded; `curiosity_deeper`: lead by digging into what he just said (else he closed the topic: a
    // thread of her own); `curiosity_gentle`: he is down or opened up — softly, no questioning;
    // `curiosity_carry`: what the lead may bring back instead (its kind, worded; question when unset).
    CuriousMove curiosity = CuriousMove::none;
    std::wstring curiosity_why, curiosity_carry;
    CuriousCarry curiosity_carry_kind = CuriousCarry::none;
    bool curiosity_deeper = false, curiosity_gentle = false;
    // A few messages before her break (natural closing): she may signal it's winding down, not end it.
    bool pre_closing = false;
    // The real weather where she is (companion_weather.hpp text), already worded; empty: unknown —
    // then her city's usual sights and sounds stand in, never a made-up forecast.
    std::wstring weather;
    uint64_t utc = 0; // Unix seconds now: her own local time where she lives (0: not told).
    // Her voice speaks with a model that acts (ElevenLabs v3/v4): she may add short cues in braces
    // ({laughs}, {whispers}) where they happen; heard, never shown (Reply::cues).
    bool voice_cues = false;
    ReplyLength reply_length = ReplyLength::full; // Public chat text only, independent of the hidden appraisal.
    Person person;               // Who she talks with: the prompt says who it is and how to call them.
};
// The system prompt of her diary after a chat ("Sleep" consolidation) or about her days: who she
// is, writing for herself; with `extras` (its bond's life state and utc) also where she lives, her
// clock there, her roots, work and life now, and the real-world rule.
std::wstring diaryPrompt(Persona persona, Look look, bool english, const PromptExtras* extras = nullptr);
std::wstring systemPrompt(Persona persona, Look look, const Digest& library, bool english, uint64_t now_local_seconds, int weekday, int hour,
                         const PromptExtras& extras = {});
// Morning 5–11, afternoon 12–17, evening 18–22, night 23–4 (local hours).
enum class DayPart { morning, afternoon, evening, night };
DayPart dayPart(int hour);
// Hidden user turns that start a conversation (never shown in the chat log). The home greeting
// mentions the time of day when `hour` is known (0–23); `first_meet` starts a first-acquaintance opening.
enum class Kickoff { chat, home, test };
std::wstring kickoff(Kickoff kind, bool english, int hour = -1, bool first_meet = false, std::wstring_view known_companion = {});
// The guest's moments: she drops in by herself, comes when the player calls her, runs off by
// herself, is sent off by the player, or has gone (the host is alone again).
enum class GuestMoment { drop_in, called, leave, dismissed, gone };
// The hidden turn telling the model about it (after the player's message, or on its own).
std::wstring guestKickoff(GuestMoment moment, Look host, Look guest, bool english);
// The hidden turn when she sends the player her photo (companion_photos): what it shows (its <stem>.txt),
// so she says something about it in character — and remembers it if the player asks.
std::wstring photoKickoff(std::wstring_view description, bool english);

// Her tools for one request (companion_tools.hpp): their declarations and who answers a call.
struct Tools {
    std::string declarations; // The functionDeclarations JSON array.
    std::function<json::Value(const std::string& name, const json::Value& args)> run;
    int rounds = 3;           // Rounds of calls before she must answer in words.
};
struct RequestOptions {
    std::vector<std::string> contents; // Raw content objects after the turns: the model's calls, verbatim, and their answers.
    const Tools* tools = nullptr;
    bool tools_off = false;            // Tools declared, but she must answer now.
    bool no_thinking = false;          // thinkingConfig.thinkingBudget = 0 (Flash).
};
std::string requestBody(const std::wstring& system, const std::vector<Turn>& turns, int max_tokens, float temperature, const RequestOptions& options = {});
// The model's function calls in a response: its whole content (verbatim, with thought
// signatures, which Gemini requires back) and each call. Empty when it answered in words.
struct FunctionCall { std::string id, name; json::Value args; };
struct Calls { std::string content; std::vector<FunctionCall> calls; };
std::optional<Calls> functionCalls(const HttpResponse& response);
// The conversation as a model gets it: starting with the player, alternating, repeats merged.
std::vector<Turn> alternate(const std::vector<Turn>& turns);

struct Segment { size_t at = 0; Emotion emotion = calm; };
// A cue for her voice ({laughs}, {whispers}, {sarcastic}): heard where it stands, never shown. `at`
// is its offset in the displayed text (UTF-16 code units), `text` as she wrote it.
struct Cue {
    size_t at = 0;
    std::wstring text;
    bool operator==(const Cue&) const = default;
};
// A [[Title]] in the displayed text, in UTF-16 code units (DirectWrite's coordinates).
struct GameMention {
    size_t at = 0, length = 0;
    bool operator==(const GameMention&) const = default;
};
struct Reply {
    bool ok = false;
    std::wstring raw;              // The model's text with its tags (the conversation sent back).
    std::wstring text;             // Tags removed; [[Title]] becomes the plain title.
    std::vector<Segment> emotions; // Emotion from each character offset on (first at 0).
    std::vector<std::wstring> games; // Suggested titles, as written in the reply.
    std::wstring error;            // ok == false: a short, user-facing reason.
    uint32_t status = 0;           // HTTP status of the failed request (0 = none/network).
    bool truncated = false;        // The token limit cut the reply (thoughts count against it too).
    std::vector<std::string> tools; // The tools she called before answering, in order.
    bool boundary = false;         // Her [granica] / [boundary] tag: the player went much too far (companion_affinity cards).
    std::optional<RelationStance> stance_change; // Her [dylemat], [dystans], [przelamanie] or [otwarcie] tag: her internal relationship shift.
    std::optional<RelationRole> role_change;     // [relacja:praca|przyjaciel|romans] set in conversation.
    std::optional<bool> player_distance_change;  // [relacja:dystans] / [relacja:blizej] when the player asks for distance or closeness in chat.
    std::optional<LifeHealth> health_change;     // [zycie:chora|zdrowa|przemeczona|kontuzja]
    std::optional<LifeWork> work_change;         // [zycie:nawal_pracy|brak_pracy|awans|wypalenie|stabilna_praca]
    std::optional<LifeWealth> wealth_change;     // [zycie:krucho|splukana|premia|stabilnie]
    std::optional<LifeFamily> family_change;     // [zycie:rodzina_spiecie|rodzina_wizyta|tesknota|rodzina_spokoj]
    std::optional<LifeSocial> social_change;     // [zycie:samotna|przebodzcowana|wena|rownowaga]
    std::vector<GameMention> mentions;
    std::vector<Cue> cues;             // Her voice's cues ({laughs}): removed from `text`, offsets into it.
    std::vector<std::wstring> prompts; // Optional contextual reply chips (<opcje>A|B</opcje> / <options>A|B</options>).
    std::wstring subconscious;         // Hidden inner monologue (<mysl>…</mysl> / <thought>…</thought>).
    std::optional<Emotion> subconscious_emotion; // True inner feeling before her outward spoken tag.
    // Her inner appraisal before her words (<ocena>{…}</ocena>), its JSON as she wrote it: the
    // mind reads its keys (companion_mind.hpp parseAppraisal); other parts may read keys of their
    // own from it. Empty when she wrote none.
    std::wstring appraisal;
    std::wstring full;             // The whole reply as she wrote it, hidden parts too (her own turn in the history).
};
Reply parseText(std::wstring_view raw); // Tags and suggestions; ok = !text.empty().
// The reply without her hidden parts (<ocena>, <mysl>): what may be shown or split into messages.
std::wstring withoutHidden(std::wstring_view raw);
// One message of a reply in the chat log: who says it and what (tags parsed).
struct Line { Look speaker{}; std::wstring text; std::vector<Segment> emotions; std::vector<GameMention> mentions; std::vector<Cue> cues = {}; };
// A reply (Reply::raw) as chat messages: each non-empty line ("||" too) is one; "Name:" signs it
// (also "**Name**:" or after a leading tag), an unsigned line keeps the previous speaker (first:
// the host) and her feeling when it has no tag of its own. Lines of a companion who is not here
// go, unless nothing else is left. `limit` (0: none) first joins one speaker's neighbouring
// messages, then drops the rest. `question_apart` (alone only): a question ending her last message
// after something else becomes a message of its own (one more than `limit`), as in a messenger.
std::vector<Line> lines(std::wstring_view raw, Look host, std::optional<Look> guest = std::nullopt, size_t limit = 0, bool question_apart = false);
// A reply the token limit cut off: its complete sentences only (an unfinished hidden part goes);
// empty when not even one sentence is complete.
std::wstring completeSentences(std::wstring_view text);
Reply parseResponse(const HttpResponse& response); // Gemini (Foundry and Google).
// Shared by every provider: her words (a reply the limit cut keeps its complete sentences;
// `reason` names an empty reply) and a failed request's short reason.
Reply finishReply(std::wstring text, bool cut, std::string_view reason);
Reply failedReply(const HttpResponse& response);

// An empty body asks with GET (model lists).
using Post = std::function<HttpResponse(const std::wstring& url, const std::wstring& headers, const std::string& body)>;

// Her words as they arrive (Gemini's streamGenerateContent, server-sent events): `transport` POSTs
// and hands every piece of the body to `piece` as it comes (returning the whole response too);
// `text` gets her new words (thoughts and tool calls left out). Other providers, and a proxy that
// refuses streaming, answer whole as before.
struct Stream {
    using Piece = std::function<void(std::string_view bytes)>;
    std::function<HttpResponse(const std::wstring& url, const std::wstring& headers, const std::string& body, const Piece& piece)> transport;
    std::function<void(std::wstring_view words)> text;
    std::function<void()> restart; // The words so far only led up to tool calls: forget them.
};
// The production transport (httpPostStream, 60 s).
HttpResponse postStream(const std::wstring& url, const std::wstring& headers, const std::string& body, const Stream::Piece& piece);
// Server-sent events cut from a body as it arrives: each complete event's "data:" payload.
class EventReader {
public:
    std::vector<std::string> feed(std::string_view bytes);
    std::vector<std::string> finish(); // What is left when the body ends without a blank line.
private:
    std::string buffer_;
};
// Her words in one streamed chunk (a GenerateContentResponse): text parts, thoughts left out.
std::wstring streamWords(std::string_view chunk);
// The chunks of a stream as the one generateContent response they add up to: text parts joined,
// calls (with their thought signatures) kept in order, the last finishReason and usageMetadata.
std::string joinStream(const std::vector<std::string>& chunks);
// How much of a reply arriving so far can be shown: whole words only, never inside an unfinished
// [tag] or [[Title]].
size_t speakablePrefix(std::wstring_view text);

// Where one request's time went (the chat's diagnostics): every HTTP call to the provider with
// its size and the tokens it reports, and every tool she ran in between.
struct Trace {
    struct Call {
        uint32_t ms = 0, status = 0;
        size_t sent = 0, received = 0;                         // Bytes.
        int prompt_tokens = -1, output_tokens = -1, thought_tokens = -1; // As the provider counts them; -1: not reported.
        int copies = 0, answered_by = 0;                       // hedged(): copies sent, and which answered (0: the original).
    };
    struct Tool { std::string name; uint32_t ms = 0; };
    std::vector<Call> calls;
    std::vector<Tool> tools;
};
// The token counts in a provider's response: Gemini usageMetadata, Claude usage, Ollama eval counts.
void readUsage(const HttpResponse& response, Trace::Call& call);

// A request raced against copies of itself: after each delay (from the start of the call) with
// no answer, the same request goes out again; the first good answer (HTTP 200) wins, a failure
// counts once nothing else is still trying. The copies left behind stop at their next read;
// `transport` must stay callable from other threads after the call. Not used for her model: on
// Gemini 3.8 Flash (Foundry) the slow answers come in stretches, and copies sent then are just as
// slow (measured with companion_tests --live-latency, 2026-10-15).
struct HedgeLog {
    std::mutex mutex;
    std::vector<std::pair<int, int>> calls; // Per call, in order: copies sent, which answered (0: the original).
};
Post hedged(Post transport, std::vector<uint32_t> after_ms, std::shared_ptr<HedgeLog> log = {});
// Private (no redirects); cloud APIs up to 60 s, a server on this machine or the home network
// (Ollama) up to 120 s.
HttpResponse post(const std::wstring& url, const std::wstring& headers, const std::string& body);
// One request to the provider (a transient failure is tried once more); `trace` records it.
HttpResponse send(const Connection& connection, const std::string& body, const Post& transport, Trace* trace = nullptr);
// Tokens added to a Flash request's limit for the thoughts it may still spend.
inline constexpr int thought_room = 1024;
// With `tools`, she may first call them (up to tools->rounds rounds); each call is answered
// locally and the conversation goes back to the model until she answers in words. `trace`
// records each call and tool. `stream` (Gemini only): her words reach stream->text as they arrive;
// once any did, a reply the limit cut is not asked again (it is already being shown).
Reply ask(const Connection& connection, const std::wstring& system, const std::vector<Turn>& turns, const Post& transport,
          int max_tokens = 400, float temperature = 1.0f, const Tools* tools = nullptr, Trace* trace = nullptr, const Stream* stream = nullptr);

// No model (companion_lines.cpp): canned lines in the persona's voice with suggestions from the
// digest (tags included, so they go through parseText like a model reply). A home greeting at
// night (`hour`) is a goodnight line.
std::wstring offline(Persona persona, Kickoff kind, const Digest& library, bool english, uint32_t seed, int hour = -1);
// A canned answer to a typed message while offline.
std::wstring offlineAnswer(Persona persona, const Digest& library, bool english, uint32_t seed);
// What she says as she sends a photo in the chat (tags included).
std::wstring photoLine(Persona persona, bool english, uint32_t seed);
// The guest's moment offline, signed like a duo reply: her line (arriving: then the host's reaction).
// Empty for GuestMoment::gone.
std::wstring guestLine(GuestMoment moment, Look host, Persona host_persona, Look guest, Persona guest_persona, bool english, uint32_t seed);
} // namespace companion
