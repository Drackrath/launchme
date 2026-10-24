#pragma once
// Her voice from ElevenLabs (elevenlabs.io): the UI-free client for src/app/companion_voice.cpp.
// She speaks the interface's language (Polish or English); in a duet each companion speaks with
// her own voice. Each character's voice comes from her character.txt (elevenlabs_* lines,
// res/companion/README.md): a voice from the ElevenLabs Voice Library (paid plans only through the
// API), a built-in one for free accounts, an optional model, voice settings and how she sounds
// (an audio tag for the v3/v4 models). Her line is split into chunks — the first sentence alone,
// so it plays early, then the rest of each message — and each chunk becomes 24 kHz 16-bit mono
// WAV (POST /v1/text-to-speech/<voice>?output_format=wav_24000). Network calls go through
// `Request`, on workers only. The key: Settings (DPAPI) or ELEVENLABS_API_KEY.
#include "companion.hpp"
#include "companion_characters.hpp"
#include "http.hpp"

#include <array>
#include <filesystem>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace elevenlabs {
inline constexpr const wchar_t* environment = L"ELEVENLABS_API_KEY";
inline constexpr const wchar_t* api = L"https://api.elevenlabs.io/";

// The key saved in Settings (encrypted for this Windows account), else ELEVENLABS_API_KEY.
std::wstring storedKey(const std::filesystem::path& config);
bool saveKey(const std::filesystem::path& config, std::wstring_view key); // Empty: forgets it.
std::wstring key(const std::filesystem::path& config, bool* from_environment = nullptr);

// Text-to-speech models offered in Settings (GET /v1/models lists them all). `tags`: audio tags
// such as [excited] direct her delivery (v3, v4); `stitching`: previous_text / next_text keep
// neighbouring chunks flowing (not v3).
struct Model { const char* id; const wchar_t* label; bool tags; bool stitching; };
inline constexpr std::array<Model, 5> models{{
    {"eleven_v4_turbo", L"Eleven v4 Turbo", true, true},
    {"eleven_v4", L"Eleven v4", true, true},
    {"eleven_v3", L"Eleven v3", true, false},
    {"eleven_multilingual_v2", L"Eleven Multilingual v2", false, true},
    {"eleven_flash_v2_5", L"Eleven Flash v2.5", false, true},
}};
inline constexpr const char* default_model = "eleven_v4_turbo";
Model model(std::string_view id); // Another ID: its own label, no tags, stitching on.
bool modelId(std::string_view id);  // a-z 0-9 _ . -, 1–64 characters.
bool voiceId(std::string_view id);  // A-Z a-z 0-9, 1–64 characters.

// Voice settings (voice_settings): stability (lower: livelier), similarity to the voice, style
// exaggeration, speed (0.7–1.2) and speaker boost.
struct Settings {
    float stability = 0.5f, similarity = 0.75f, style = 0.0f, speed = 1.0f;
    bool speaker_boost = true;
    bool operator==(const Settings&) const = default;
};
// "stability=0.4, similarity=0.8, style=0.3, speed=1.05, speaker_boost=1" over `base`; unknown or
// out-of-range parts are skipped.
Settings parseSettings(std::string_view text, Settings base);
Settings personaSettings(companion::Persona persona);   // Without character.txt settings.
const wchar_t* personaDelivery(companion::Persona persona); // Without elevenlabs_tag: "bubbly, energetic".
// Her feeling as an audio tag (v3/v4; nullptr: none) and as a change of settings (the v2 models).
const wchar_t* emotionTag(companion::Emotion emotion);
Settings tuned(Settings base, companion::Emotion emotion);

// A built-in (premade) voice every account has, by her character: free accounts cannot use the
// Voice Library through the API.
struct Builtin { const char* id; const wchar_t* name; };
inline constexpr std::array<Builtin, 7> builtin_voices{{
    {"cgSgspJ2msm6clMCkdW9", L"Jessica"}, {"FGY2WhTYpPnrIDTdsKH5", L"Laura"}, {"EXAVITQu4vr4xnSDxMaL", L"Sarah"},
    {"Xb7hH8MSUJpSbSDYk0k2", L"Alice"}, {"pFZP5JQG7iQjIQuC4Bku", L"Lily"}, {"hpp4J3VqNfWAUOO0d1Us", L"Bella"},
    {"XrExE9yKIg1WjnnlVkGX", L"Matilda"},
}};
const Builtin& builtinVoice(companion::Persona persona);

// One voice of the account (GET /v1/voices): premade, cloned, designed or added from the library.
struct Voice {
    std::string id;
    std::wstring name;          // "Jessica - Playful, Bright, Warm"
    std::string category, gender, age, language;
    bool operator==(const Voice&) const = default;
};
std::optional<std::vector<Voice>> parseVoices(std::string_view json);
std::wstring shortName(std::wstring_view name); // "Jessica - Playful, Bright, Warm" → "Jessica"

// Who speaks a chunk and how: her voices in order (Settings' choice, her character.txt voice, its
// free fallback, a built-in one), her model, settings and delivery.
struct Speaker {
    std::vector<std::string> voices;
    std::vector<std::wstring> names; // Beside `voices` (may be empty: unknown).
    std::string model = default_model;
    Settings settings;
    std::wstring delivery;
};
// Her voice for a character: the voice picked in Settings first (empty: hers), then character.txt,
// then a built-in one; `model_override` from Settings (empty: hers, else the default).
Speaker speaker(const companion::Character* character, companion::Persona persona, std::string_view chosen_voice,
                std::string_view model_override, const std::vector<Voice>& known);

// Her lines as spoken sentences: very short pieces (< 6 characters) join the next one.
std::vector<std::wstring> sentences(std::wstring_view text);
// What a voice can say: emoji, symbols, brackets and markdown stars removed, spaces tidied.
std::wstring speakable(std::wstring_view text);
// One request's worth of her line: who speaks, the text with its audio tags (tag models), its
// neighbours in the same message (stitching) and the feeling it starts with (the v2 models' settings).
struct Chunk {
    companion::Look speaker{};
    std::wstring text, previous, next;
    companion::Emotion emotion = companion::calm;
    bool first = false; // The first chunk of a message.
};
// Each message's first sentence alone (it plays early), then the rest of the message in one chunk
// (the v2 models: one per feeling; at most ~400 characters each). With v3/v4 her voice acts: each
// chunk starts with [her delivery, her feeling], and a new feeling or a cue she wrote ({laughs},
// Line::cues) becomes an audio tag exactly where it stands.
std::vector<Chunk> chunks(const std::vector<companion::Line>& lines, const std::function<Speaker(companion::Look)>& speaker_of);

// Settings → "Posłuchaj": a short line in her character's voice with a cue ({giggles}, parsed like
// her replies), and its feeling.
struct Sample { const wchar_t* text; companion::Emotion emotion; };
Sample sample(companion::Persona persona, bool english);

// A request to the API: GET without a body, else POST; `path` is relative to `api`.
using Request = std::function<HttpResponse(const std::wstring& key, const std::wstring& path, const std::string* body)>;
HttpResponse request(const std::wstring& key, const std::wstring& path, const std::string* body);

// What went wrong, for Settings and the chat (the UI words it).
enum class Problem { none, no_key, key_refused, permission, quota, paid_voice, busy, offline, failed };
Problem problem(const HttpResponse& response);

// The key's check: its voices (the list may stay empty when the key may not read them).
struct Status { bool ok = false; Problem problem = Problem::none; uint32_t status = 0; std::vector<Voice> voices; };
Status check(const Request& request, const std::wstring& key);

// The request body for one chunk (tests read it).
std::string body(const Chunk& chunk, std::string_view model, const Settings& settings, bool english, bool stitched = true);
// One chunk as WAV with her first voice that answers: a voice the plan refuses (402) or that is
// gone (404) is skipped and noted in `blocked`, which also holds the ones refused earlier.
struct Spoken {
    std::vector<uint8_t> wav;
    std::string voice;           // The voice that spoke.
    std::vector<std::string> refused; // Newly refused on the way.
    Problem problem = Problem::none;
    uint32_t status = 0;
};
Spoken speak(const Request& request, const std::wstring& key, const Speaker& speaker, const Chunk& chunk, bool english,
             const std::set<std::string>& blocked);
} // namespace elevenlabs
