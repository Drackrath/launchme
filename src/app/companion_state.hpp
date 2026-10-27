#pragma once
// Towarzyszka's state in Launcher (src/app/companion*.cpp): her chat and home tile, the requests in
// flight, memory, reactions to the launcher, voice and photos. Launcher::companion_ owns one.
#include "worker_messages.hpp"
#include "../net/companion.hpp"
#include "../net/companion_affinity.hpp"
#include "../net/companion_characters.hpp"
#include "../net/companion_chats.hpp"
#include "../net/companion_cognition.hpp"
#include "../net/companion_costumes.hpp"
#include "../net/companion_events.hpp"
#include "../net/companion_memory.hpp"
#include "../net/companion_semantic.hpp"
#include "../net/companion_mood.hpp"
#include "../net/companion_photos.hpp"
#include "../net/elevenlabs.hpp"
#include "../platform/user_config.hpp"
#include "../ui/avatar.hpp"
#include "../ui/companion_view.hpp"
#include "../ui/companion_feed_view.hpp"
#include "../ui/spring.hpp"

#include <Windows.h>

#include <array>
#include <atomic>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

// Her voice (src/app/companion_voice.cpp): the ElevenLabs key's check and her current line.
struct CompanionVoice {
    // missing: no key; failed: the key's check or her last line went wrong (`problem`).
    enum class Engine { unknown, checking, ready, missing, failed } engine = Engine::unknown;
    std::vector<elevenlabs::Voice> voices;  // The account's voices (empty: not listed).
    elevenlabs::Problem problem = elevenlabs::Problem::none;
    uint32_t status = 0;                    // Its HTTP status (0: none).
    std::set<std::string> blocked;          // Voices her plan refused (or gone): the next one in her chain speaks.
    std::map<int, std::string> spoke;       // companion::Look → the voice that last spoke for her.
    uint32_t generation = 0;                // Her current line; a newer one silences it.
    bool pending = false;                   // Her text waits for the first sentence.
    float waited = 0;
    std::shared_ptr<std::atomic_bool> cancel = std::make_shared<std::atomic_bool>(false);
};

// A guest in her chat (src/app/companion_guest.cpp): another companion, for a while. The player
// calls her from the "…" menu (she stays until sent off), or once a day she drops in by herself
// after a few messages and runs off again a little later.
struct CompanionGuest {
    std::optional<companion::Look> look; // In the conversation: the model writes both of them.
    bool invited = false;  // Called by the player: she stays until sent off.
    int stay = 0;          // Dropped in: the player's messages before she runs off.
    float quiet = 0;       // Dropped in: seconds of silence (a long one and she runs off too).
    bool leaving = false;  // Her goodbye is on its way or being said; then she goes.
    bool shown = false;    // Beside the host (else away, or running off).
    float motion = 0;      // 0 away … 1 beside her (her entrance, exit and the host making room).
    avatar::Rig rig{31};
};

// One answer in the chat's diagnostics ("…" → Pokaż diagnostykę).
struct CompanionDiagnostic {
    CompanionMessage::Purpose purpose = CompanionMessage::Purpose::chat;
    CompanionMessage::Timing timing;
    uint32_t total_ms = 0;   // From asking to the answer arriving.
    std::wstring model;      // "Gemini 3.8 Flash · Foundry"; empty: no model (offline).
    bool ok = false;
    uint32_t status = 0;
    bool voiced = false;     // Her voice reads it: her text waits for its first sentence (≤4 s).
    uint64_t arrived = 0;
    std::optional<uint32_t> voice_ms; // Until that first sentence was ready.
    float typing_seconds = 0; // Her typewriter, all messages of it.
    size_t chars = 0, messages = 1;
};

struct CompanionState {
    bool backup_busy = false;
    struct FeedState {
        companion_ui::Feed view;
        std::string look;
        std::wstring source, reference, reference_quote;
        uint32_t generation = 0;
        uint64_t next_poll = 0, next_private = 0;
        bool claiming = false;
        std::wstring private_pending;
        std::wstring private_context;
        std::map<std::string, std::wstring> drafts;
        std::set<std::string> requested_images;
        std::shared_ptr<std::atomic_bool> cancel = std::make_shared<std::atomic_bool>(false);
    } feed;
    // Her chat screen, her home tile and bubble, her rig and the Settings previews.
    companion_ui::Chat chat;
    companion_ui::Bubble bubble;
    companion_ui::Picker picker;
    std::optional<std::pair<float, float>> link_pointer; // In the current chat/home drawing coordinates.
    std::vector<companion::Turn> turns; // Her conversation, including hidden kickoffs.
    int typed = 0;          // The player's messages in this conversation.
    int burst_in = 3;       // Her replies until the next comes as two short messages (every 3rd–4th).
    float chat_quiet = 0;   // Seconds of silence in the open chat while the player is not typing.
    float chat_nudge_after = 90.0f; // Seconds of silence before she spontaneously speaks up on her own.
    int chat_nudges = 0;    // How many spontaneous nudges she sent in this conversation (≤ 2).
    bool next_reply_free = false;   // True when the reply in flight is her spontaneous initiative (does not count towards message_cap).
    CompanionGuest guest;
    int guest_after = 5;    // A guest may drop in after this message of the player's (4th–6th)…
    int guest_day = -1;     // …once a day: yyyymmdd of the last time (companion_guest.txt; -1: not read yet).
    companion::Digest library;          // The last digest a reply was based on (suggestions).
    avatar::Rig rig{17};
    std::vector<avatar::Rig> previews; // Settings: one per character (companionPreviews()).
    float preview_clock = 0;
    int preview_emotion = 0;
    int preview_look = -1;
    bool hover = false, busy = false, greeted = false;
    bool dozing = false; // Sleepy after a long quiet on the home; any input wakes her.
    Spring hover_motion;
    float tile_progress = 1, age = 0;
    float entrance = 0; // Her jump out of the tile, once per start (0 below it, 1 in place).
    // Requests to her model: the one in flight, its status and the model that last answered.
    std::wstring status;
    bool offline = false;
    companion::ReplyLength reply_length = companion::ReplyLength::full; // Captured for the reply in flight, including its streamed bubbles.
    uint32_t generation = 0, seed = 1;
    std::shared_ptr<std::atomic_bool> cancel = std::make_shared<std::atomic_bool>(false);
    std::wstring model_label; // "Gemini 3.8 Flash · Foundry".
    uint64_t asked = 0;       // GetTickCount64 of the request in flight.
    std::vector<CompanionDiagnostic> diagnostics; // Her last answers, newest last (at most 6).
    // The game roulette (src/app/companion_roulette.cpp): her verdict waits until the reel stops.
    std::unique_ptr<CompanionMessage> held;
    // Her memory (src/net/companion_memory.hpp): loaded on first use, saved on a worker in order.
    std::optional<companion::Memory> memory;
    std::shared_ptr<companion::semantic::Service> semantic;
    struct MemoryIo { std::mutex mutex; std::atomic<uint64_t> generation{0}; };
    std::shared_ptr<MemoryIo> memory_io = std::make_shared<MemoryIo>();
    size_t summarized = 0; // turns already summarized.
    uint64_t memory_epoch = 0;
    struct SummaryPending { uint64_t chat_started = 0; size_t through = 0; };
    std::map<std::string, SummaryPending> summary_pending;
    // Her cognitive engine (src/net/companion_cognition.hpp): HippoRAG spreading activation,
    // higher-order reflections, subconscious thoughts (<mysl>), daily Tavily readings and gossip.
    std::optional<companion::cognition::State> cognition;
    std::shared_ptr<MemoryIo> cognition_io = std::make_shared<MemoryIo>();
    std::wstring last_query; // The player's last message, for highlighting Spreading Activation on the 3D sphere.
    // Her mind (src/net/companion_mind.hpp, src/app/companion_mind.cpp): affect, drives, goals,
    // expectations, what she said about herself; loaded on first use, saved on a worker in order.
    std::optional<companion::mind::State> mind;
    std::shared_ptr<MemoryIo> mind_io = std::make_shared<MemoryIo>();
    // Her life in the background (companion_mind.cpp): her web reading (up to six times a day,
    // while she is up) and her diary filling in a long gap; one of each at a time.
    bool reading_busy = false, life_busy = false;
    // The real weather where she is (companion_weather.cpp): the last report and its place, one fetch at a time.
    std::optional<companion::weather::Report> weather;
    bool weather_busy = false;
    uint64_t weather_tried = 0; // Unix seconds of the last attempt (found or not).
    uint64_t reading_tried = 0; // Unix seconds of her last reading attempt (found or not).
    uint64_t life_poll = 0;     // GetTickCount64 of the next background check (pollCompanionLife).
    // Her mood (src/net/companion_mood.hpp): loaded on first use, saved on a worker in order. The
    // conversation's mood and weekly small talk go into each of its prompts (PromptExtras).
    std::optional<companion::mood::State> mood_state;
    std::shared_ptr<MemoryIo> mood_io = std::make_shared<MemoryIo>();
    std::wstring mood, week;
    bool mood_spent = false; // Her first reply of the conversation has had the mood: it is said once, not in every reply.
    bool week_asked = false; // This conversation's first request checks whether a week passed.
    // Her reply as it streams in (Gemini): what came so far, and how many of the log's messages
    // (at its end) show it; the whole reply replaces them when it lands.
    std::wstring heard;
    size_t streamed = 0;
    // Gamification (src/app/companion_affinity.cpp): each companion's sympathy points, loaded on
    // first use from companion_affinity.txt and saved on a worker in order.
    std::optional<companion::affinity::State> affinity;
    std::shared_ptr<MemoryIo> affinity_io = std::make_shared<MemoryIo>();
    float affinity_pulse = 0;      // 1 → 0 after a level up: the badge glows.
    float affinity_card_pulse = 0; // 1 → 0 after her card (companionBoundary): the badge flashes its colour.
    // Her costumes: empty → the embedded list (UI tests set their own).
    std::optional<std::vector<companion::costumes::Costume>> costumes;
    // Her reactions (src/app/companion_events.cpp).
    companion::EventQueue events;
    companion::LibraryWatch watch;
    std::vector<std::string> added_keys;                 // Seen first while the library was still scanning.
    std::map<std::string, companion::Progress> baselines; // A game's progress when its session started.
    std::optional<companion::Event> last_session;         // Most recent finished game session, for chat context.
    uint64_t last_session_at = 0;                         // GetTickCount64 when last_session ended.
    bool bubble_session = false;                          // The home remark in flight is for a finished session.
    bool premieres_requested = false;
    int goodnight_day = 0;   // yyyymmdd of her last goodnight.
    uint64_t quiet_until = 0; // No remark before this tick (GetTickCount64).
    std::function<SYSTEMTIME()> clock = [] { SYSTEMTIME local{}; GetLocalTime(&local); return local; };
    CompanionVoice voice;
    // Her photos (src/app/companion_photos.cpp).
    uint32_t photo_chat = 0;  // Grows with every new conversation.
    int photo_turn = 0, photo_typed = 0;
    bool photo_asked = false;
    std::shared_ptr<const std::vector<uint8_t>> photo, photo_pending;
    std::wstring photo_description; // The pending photo's <stem>.txt: the model talks about it.
    std::shared_ptr<std::mutex> photo_io = std::make_shared<std::mutex>();
    std::wstring photo_file, photo_file_pending; // Her photo in this chat, relative to the photo folder (empty: not kept).
    // Her conversation kept between starts (src/app/companion_chat_store.cpp, net/companion_chats.hpp):
    // whose it is, when it started and was last written to (local seconds, companionNow), her rest
    // after it ended, and whether her next answer is its last.
    std::optional<companion::Look> chat_look; // nullopt: none loaded yet.
    uint64_t chat_started = 0, chat_updated = 0, rest_until = 0;
    bool closing = false;
    uint64_t pre_closed_for = 0; // The conversation (chat_started) in which she already signalled its end was near.
    // Her conversation's measurements (Diagnostyka): one per reply of hers, this conversation only.
    struct TurnMetric {
        uint64_t conversation = 0;            // chat_started
        int his_words = 0, her_words = 0;
        bool question = false;                // Her reply asked something.
        bool repeated_opening = false;        // It began like one of her earlier replies in this conversation.
        bool retold = false;                  // It told him again something he knew about her.
        companion::CuriousMove curiosity = companion::CuriousMove::none; // The act it recorded (none: none).
        std::vector<std::string> workspace;   // What she had in mind.
        std::wstring opening;                 // Her reply's first two words (folded), to spot repeats.
    };
    std::vector<TurnMetric> turn_metrics; // ≤ 120
    // Saves are scoped to character and start time: two characters can begin in the same second.
    struct ChatIo { std::mutex mutex; uint64_t counter = 0; std::map<std::pair<companion::Look, uint64_t>, uint64_t> newest; };
    std::shared_ptr<ChatIo> chat_io = std::make_shared<ChatIo>();
    // "…" → "Historia czatu": an older conversation shown read-only while the live one waits here.
    std::optional<companion_ui::Chat> live_chat;
    std::shared_ptr<const std::vector<uint8_t>> live_photo;
    std::vector<uint64_t> history; // The history menu's conversations (start times), newest first.
    uint64_t archive_started = 0;  // The one shown.
    // Injectable: UI tests answer with fixtures and never read this machine's credentials.
#ifdef LAUNCHME_UI_TEST
    bool persist = false;  // Conversations on disk only where a test asks for them.
    int message_cap = 1000;
#else
    bool persist = true;
    int message_cap = companion::chats::message_cap;
#endif
#ifdef LAUNCHME_UI_TEST
    companion::Post post = [](const std::wstring&, const std::wstring&, const std::string&) { return HttpResponse{}; };
    std::function<HttpResponse(const std::wstring&, size_t)> link_get = [](const std::wstring&, size_t) { return HttpResponse{}; };
    std::function<companion::Probe()> probe = [] { return companion::Probe{}; };
    elevenlabs::Request voice_request = [](const std::wstring&, const std::wstring&, const std::string*) { return HttpResponse{}; };
    std::function<std::wstring()> voice_key = [] { return std::wstring{}; }; // Never this machine's key.
    decltype(companion::Stream::transport) stream_post; // Empty: whole answers only (a test may set one).
    companion::weather::Get weather_get = [](const std::wstring&) { return HttpResponse{}; }; // Never the network.
#else
    companion::Post post = companion::post;
    std::function<HttpResponse(const std::wstring&, size_t)> link_get = httpGetPage;
    std::function<companion::Probe()> probe = companion::systemProbe;
    elevenlabs::Request voice_request = elevenlabs::request;
    std::function<std::wstring()> voice_key = [] { return elevenlabs::key(userConfigDirectory()); };
    decltype(companion::Stream::transport) stream_post = companion::postStream;
    companion::weather::Get weather_get = [](const std::wstring& url) { return httpGet(url, L"", 256 * 1024); };
#endif
};
