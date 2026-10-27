#pragma once
// Her conversations, kept between starts (src/app/companion_chat_store.cpp): one file per conversation,
// <config>/companion/chats/<key>/<yyyymmdd-hhmmss>.dat, DPAPI like companion_memory.dat (this Windows
// user only). It holds what the chat shows (her and the player's messages, a landed roulette, her photo
// as a path relative to the photo folder), her hidden context for the model, and whether it ended.
// A conversation's base threshold is message_cap messages (everyone's), scaled by reply length; then she says goodbye and rests a
// while (her level and character decide how long). The next day starts a new one; older ones stay
// readable from the chat's "…" → "Historia czatu".
#include "companion.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace companion::chats {
inline constexpr int message_cap = 40;       // Standard length; concise ×1.5 (60), brief ×3 (120). Both sides count.
inline constexpr size_t context_turn_limit = 160; // Room for the longest short-message conversation and hidden notes.
inline constexpr size_t keep = 1000;         // Conversations kept per companion; older files go.
inline constexpr uint64_t return_gap = 30 * 60; // After this long away a hidden note tells her the player is back.
inline constexpr uint64_t day_starts = 4 * 3600; // A day of chats starts at 4:00 (a late night still belongs to it).

struct Line {
    bool user = false;
    std::wstring text;
    std::vector<Segment> emotions;
    bool error = false;
    Look speaker{};
    // A landed roulette: its pick (title and gameKey) instead of text.
    std::optional<std::wstring> roulette;
    std::string roulette_key;
    bool photo = false; // Her photo (Conversation::photo).
    bool free = false;  // A remark after a game session: shown in the chat, never counted against message_cap.
    std::vector<GameMention> mentions;
};

struct Conversation {
    Look look{};
    uint64_t started = 0, updated = 0; // Local wall-clock seconds (localSeconds).
    std::vector<Line> lines;
    std::vector<Turn> turns;   // Her context for the model, hidden kickoffs included.
    size_t summarized = 0;     // turns already summarized into her memory.
    std::wstring photo;        // Her photo: relative to the photo folder ("<key>/00-09/a.png"); empty: none (or not kept).
    std::wstring photo_description;
    bool ended = false;
    uint64_t rest_until = 0;   // Ended: she rests until then.

    std::string save() const;  // JSON
    static std::optional<Conversation> load(std::string_view json);
};

// A conversation's file name, from when it started: "20261014-153000".
std::wstring fileStem(uint64_t started);
std::optional<uint64_t> parseStem(std::wstring_view stem);
std::filesystem::path folder(const std::filesystem::path& config, Look look);
std::filesystem::path file(const std::filesystem::path& config, Look look, uint64_t started);

bool write(const std::filesystem::path& config, const Conversation& conversation);
std::optional<Conversation> read(const std::filesystem::path& file);
// Her conversations' start times, newest first (from the file names; nothing is decrypted).
std::vector<uint64_t> list(const std::filesystem::path& config, Look look);
std::optional<Conversation> latest(const std::filesystem::path& config, Look look);
// Keeps the newest `count` files of hers.
void prune(const std::filesystem::path& config, Look look, size_t count = keep);

// Her tool przypomnij: lines of her earlier conversations (newest `files`, not the one started at
// `skip`) that share the most words with `topic` (three letters or more, endings aside), at most
// `limit`, the best first and then the newest.
struct Found { uint64_t started = 0; bool user = false; std::wstring text; int score = 0; };
std::vector<Found> search(const std::filesystem::path& config, Look look, std::wstring_view topic, uint64_t skip = 0, size_t files = keep, size_t limit = 6);
// The words a search looks for: folded, at least three letters, a long word's ending cut, words
// that say nothing about a topic left out ("nie", "się", "gracz", "the" …); at most `limit`.
std::vector<std::wstring> searchWords(std::wstring_view text, size_t limit = 8);
// Whether a folded word says nothing about a topic (function words, "gracz"/"player").
bool commonWord(std::wstring_view folded);
// Stems of one word cut at different lengths: one starts the other (three letters or more).
bool sameStem(std::wstring_view a, std::wstring_view b);
// How many of `words` (searchWords) occur in `text` as one of its words, endings aside.
int matches(const std::vector<std::wstring>& words, std::wstring_view text);
// Shared stems of two word lists over the shorter one (0–1); `shared` gets their count.
float overlap(const std::vector<std::wstring>& a, const std::vector<std::wstring>& b, size_t* shared = nullptr);

// Seconds since 1970 of a local date and time (no time zone: it is only compared with itself).
uint64_t localSeconds(int year, int month, int day, int hour, int minute, int second);
// The day a moment belongs to (it starts at 4:00).
uint64_t dayOf(uint64_t local_seconds);
// "14.10 · 15:30" ("Oct 14 · 15:30").
std::wstring label(uint64_t local_seconds, bool english);
// "dziś", "wczoraj", "3 dni temu".
std::wstring ago(uint64_t then, uint64_t now, bool english);
// "25 min", "1 godz. 10 min".
std::wstring duration(uint64_t seconds, bool english);

// How long she rests after a conversation: shorter as she grows fond of him (level 1–50),
// her character's own pace, and her mood (the seed). 5–120 minutes.
uint64_t restSeconds(Persona persona, int level, uint32_t seed);
// The hidden note on her last answer: say goodbye after it (`bedtime`: she is off to bed).
std::wstring farewellKickoff(bool english, bool bedtime = false);
// The hidden note when the player went quiet past her bedtime: her goodnight, unasked.
std::wstring bedtimeKickoff(bool english);
// Her goodbye offline (or when the model fails), tagged like a model reply (`bedtime`: goodnight).
std::wstring farewellLine(Persona persona, bool english, uint32_t seed, bool bedtime = false);
// The hidden note when the player comes back after a while.
std::wstring returnNote(uint64_t away_seconds, bool english);
}
