#pragma once
// What Towarzyszka remembers between conversations (Settings → System → Towarzyszka AI → Pamięć):
// short-term, her recent remarks and the games she suggested (so she does not repeat herself and
// a suggested game rests for a few days, offline lines too); long-term, opt-in, what she knows
// about the player (her "zapamietaj" tool and her inner appraisal, <ocena>) and summaries of past
// chats. Kept for this Windows user only (DPAPI, <config>/companion_memory.dat); it goes to the
// model inside her prompt.
//
// What she knows about the player is hers: a note belongs to the companion who heard it (both
// when a guest was in the chat); another companion learns it only by gossip (companion_mind.hpp),
// and then as hearsay. Each note says how she knows it (heard it, saw it in the launcher's data,
// guesses it) and how sure she is, so her prompt can tell knowledge from assumption.
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace companion {
inline constexpr size_t max_notes_capacity = 5000;
inline constexpr size_t max_summaries_capacity = 5000;
inline constexpr size_t max_summaries_per_look = 1000;
inline constexpr size_t max_picks_capacity = 1000;
inline constexpr size_t max_memory_file_bytes = 32 * 1024 * 1024;

struct Memory {
    // Another companion who knows a note: a guest who heard it too, or hearsay from `from`.
    struct Knower {
        std::string look;
        bool hearsay = false;
        std::string from; // hearsay: who told her.
        bool operator==(const Knower&) const = default;
    };
    // look: whose it is (a companion's key); empty: shared (older versions kept no owner).
    // emotion: how she felt when this memory was formed ("shy", "angry", "playful", …).
    // feeling: her personal 1st-person emotional reaction at that moment.
    // The rest is for notes about the player only.
    struct Entry {
        uint64_t at = 0;
        std::wstring text;
        std::string look;
        std::string emotion;
        std::wstring feeling;
        std::string id;           // "n" and four base-36 characters: how her inner appraisal names it.
        std::string source;       // "told" (the player said it), "observed" (launcher data), "inferred" (her guess); empty: told.
        float confidence = 0;     // 0–1; 0: not given (an older note), counts as told and fairly sure.
        bool secret = false;      // The player asked her to keep it: never gossiped to another companion.
        bool important = false;   // Self-defining / foundational memory: high retention priority and "Ważne wspomnienie" in UI.
        std::string topic;        // "place": where the player lives; empty: anything else.
        std::vector<Knower> known; // Other companions who know it.
        uint32_t confirmations = 1; // Repeated direct evidence in distinct turns, capped at 64; recall alone never reinforces a note.
    };
    struct Pick { uint64_t at = 0; std::string key; std::wstring title; std::string look; };
    std::vector<Entry> remarks;   // Her recent replies, shortened; newest last (≤ 30).
    std::vector<Pick> picks;      // Games she suggested; newest last (≤ max_picks_capacity).
    std::vector<Entry> notes;     // Facts about the player, each known by its companions (≤ max_notes_capacity).
    std::vector<Entry> summaries; // Past chats in a sentence or two (≤ max_summaries_capacity, ≤ max_summaries_per_look each).

    // How she came to know a fact (her inner appraisal says it; the tool means told).
    struct Facts {
        std::string look;          // Who heard it (empty: shared, as older versions kept it).
        std::string guest;         // A guest in the chat heard it too.
        std::string emotion;
        std::wstring feeling;
        std::string source = "told";
        float confidence = 0.8f;
        bool secret = false;
        bool important = false;
        std::string topic;
    };

    void said(std::wstring_view text, uint64_t now);
    void suggested(std::string key, std::wstring title, uint64_t now, std::string look = {});
    // A new fact: false when empty, too long, a duplicate (case and punctuation aside; it then
    // learns who else knows it), sensitive-looking or too weak to retain in a full archive. A note that expands an existing one updates
    // it in place; one that says the opposite (a "nie"/"not" more or less) replaces it — the newer
    // word wins; notes with different numbers stay apart.
    bool note(std::wstring_view text, uint64_t now, std::string look = {}, std::string emotion = {}, std::wstring feeling = {});
    bool note(std::wstring_view text, uint64_t now, const Facts& facts);
    // Her inner appraisal corrects a note she knows by its id: new text and/or confidence; an empty
    // text or a confidence under 0.15 forgets it (hers only when others know it too).
    bool revise(std::string_view id, std::wstring_view text, std::optional<float> confidence, std::string_view look, uint64_t now);
    // Memory reconsolidation (Nader): recalling an older note or summary (`query`) re-saves it
    // tinted by her current affect (`valence`, `emotion`) and relationship context (`tint`).
    int reconsolidate(std::string_view look, std::wstring_view query, float valence, std::string_view emotion, std::wstring_view tint, uint64_t now);
    void summarize(std::wstring_view text, uint64_t now, std::string look = {}, bool important = false);
    // Removes one specific note, summary or suggested game pick; true when found and removed.
    bool removeNote(uint64_t at, std::wstring_view text);
    bool removeSummary(uint64_t at, std::wstring_view text);
    bool removePick(std::string_view key, uint64_t at = 0);
    // Games she suggested in the last `days` days (by gameKey).
    std::vector<std::string> resting(uint64_t now, uint64_t days = 3) const;
    // Whether `look` knows a note (older notes without an owner: everyone); hearsay counts.
    static bool knows(const Entry& note, std::string_view look);
    // How `look` knows it: the knower entry for her (nullptr: her own note, or shared).
    static const Knower* knower(const Entry& note, std::string_view look);
    // The notes `look` knows (every note when `look` is empty), oldest first.
    std::vector<const Entry*> notesOf(std::string_view look) const;
    // Whether `look` knows where the player lives (a note on topic "place").
    bool knowsPlace(std::string_view look) const;
    // Deterministic retention: importance, stable facts, evidence quality, confirmations and recency.
    // The bounded recall bonus breaks relevance ties; an unrelated entry always scores zero.
    static float retentionWeight(const Entry& entry, uint64_t now);
    static float recallWeight(const Entry& entry, int matches, uint64_t now);
    // Her prompt's memory section: up to 8 high-priority facts within 30 core/recent notes, 3 latest summaries,
    // plus at most 8 older notes and 4 summaries recalled from the full archive. Larger storage does not enlarge the prompt.
    // Each note shows its id and how she knows it; `name` names a companion by her key (hearsay).
    std::wstring prompt(bool english, uint64_t now, bool recent, bool notes, std::string_view look = {}, std::wstring_view query = {},
                        const std::function<std::wstring(std::string_view)>& name = {}) const;
    bool empty() const { return remarks.empty() && picks.empty() && notes.empty() && summaries.empty(); }

    std::string save() const;                               // JSON
    static std::optional<Memory> load(std::string_view json);
};
Memory loadMemory(const std::filesystem::path& config);
bool saveMemory(const std::filesystem::path& config, const Memory& memory);
bool clearMemory(const std::filesystem::path& config);
}
