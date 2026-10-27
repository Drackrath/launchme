// Towarzyszka (Settings → Asystent): an animated anime companion. On the home she
// stands in a tile (where each layout puts it: src/ui/home_widgets.hpp) and greets the player once
// per start; C, the left trigger or a click opens her chat, where she jokes, teases the player about the backlog and
// suggests games from the library (▶ chips start them). Answers come from a Foundry-proxied Gemini
// model (src/net/companion.cpp) on a worker; without a token she speaks canned lines. Her
// reactions to the launcher: companion_events.cpp; the game roulette: companion_roulette.cpp; her
// figure: src/ui/avatar.hpp and src/ui/companion_renderer.cpp.
#include "launcher.hpp"
#include "../net/web_search.hpp"
#include "../net/companion_links.hpp"
#include "../net/url_encoding.hpp"
#include "../net/companion_characters.hpp"
#include "../net/companion_person.hpp"
#include "../net/companion_week.hpp"
#include "../friends/steam_friends.hpp"
#include "../library/custom_games.hpp"
#include "../library/organization.hpp"
#include "../library/steam.hpp"
#include "../library/steam_achievements.hpp"
#include "../library/steam_stats.hpp"
#include "../library/visibility.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <chrono>
#include <charconv>
#include <fstream>
#include <sstream>

static_assert(AppSettings::companion_provider_keys.size() == companion::provider_count, "AppSettings::CompanionProvider mirrors companion::Provider");

namespace {
// The chosen companion; none, an unknown or a "soon" one: the first of those that can be chosen
// by her key's alphabetical order (not by order=, which only arranges the Settings cards).
companion::Look lookOf(const AppSettings& settings) {
    if (const auto look = companion::lookFromKey(settings.companion_look); look && !companion::character(*look)->soon) return *look;
    const auto available = companion::availableLooks();
    if (available.empty()) return companion::Look{};
    return *std::min_element(available.begin(), available.end(), [](companion::Look a, companion::Look b) { return companion::lookKey(a) < companion::lookKey(b); });
}
bool english() { return i18n::english(); }
int dateKey(const SYSTEMTIME& time) { return time.wYear * 10000 + time.wMonth * 100 + time.wDay; }
// Her reply's messages in the log: a few alone, more with a guest.
constexpr size_t solo_messages = 3, duo_messages = 6;
// The typing pause before each waiting message of a reply, longer for a longer one.
float typingPause(const companion_ui::Message& message) { return std::clamp(0.45f + 0.012f * static_cast<float>(message.text.size()), 0.6f, 1.3f); }
// "Pisze…" (Settings: whole messages): how long the typing dots last before a message appears whole —
// longer for a longer one, as a person types; never a random wait (no slot-machine suspense) and never long.
float wholeTypingSeconds(const std::wstring& text) { return std::clamp(0.5f + static_cast<float>(text.size()) / 16.0f, 0.9f, 4.5f); }
// Characters per second of her speech; sentence ends and commas pause.
constexpr float speech_rate = 40;
float pause(wchar_t ch) { return ch == L'.' || ch == L'!' || ch == L'?' || ch == L'…' ? 7.0f : ch == L',' || ch == L';' || ch == L':' ? 3.0f : 1.0f; }
// How long her typewriter takes for a text (the pauses after punctuation included).
float typingSeconds(const std::wstring& text) {
    float cost = 0;
    for (size_t i = 0; i < text.size(); ++i) cost += i ? pause(text[i - 1]) : 1.0f;
    return cost / speech_rate;
}
std::wstring trimmed(std::wstring_view text) {
    const auto first = text.find_first_not_of(L" \t\r\n");
    if (first == std::wstring_view::npos) return {};
    return std::wstring(text.substr(first, text.find_last_not_of(L" \t\r\n") - first + 1));
}
// Her tool przypomnij (on her worker): lines of her earlier conversations with the player and
// her notes/summaries after them that share words with the topic.
json::Value companionRecall(const std::filesystem::path& config, companion::Look look, const std::wstring& topic, uint64_t skip,
                            const std::vector<companion::Memory::Entry>& summaries,
                            const std::vector<companion::Memory::Entry>& all_notes, uint64_t now, bool english,
                            const std::shared_ptr<companion::semantic::Service>& semantic = {},
                            const std::vector<companion::Memory::Entry>& history = {}) {
    std::vector<companion::tools::Recalled> lines, notes;
    for (const auto& found : companion::chats::search(config, look, topic, skip))
        lines.push_back({companion::chats::label(found.started, english), found.user, found.text});
    const auto words = companion::chats::searchWords(topic);
    companion::Memory history_memory;
    history_memory.notes = history;
    for (const auto& old : history_memory.historical(companion::lookKey(look), topic))
        notes.push_back({english ? L"past version, no longer current" : L"wersja historyczna, już nieaktualna", false, old.text});
    std::vector<std::pair<float, const companion::Memory::Entry*>> scored;
    for (const auto& summary : summaries) {
        const float score = companion::Memory::recallWeight(summary, companion::chats::matches(words, summary.text), now);
        if (score) scored.push_back({score, &summary});
    }
    for (const auto& note : all_notes) {
        const float score = companion::Memory::recallWeight(note, companion::chats::matches(words, note.text), now);
        if (score) scored.push_back({score, &note});
    }
    std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    if (semantic) {
        for (const auto& hit : semantic->search(companion::lookKey(look), topic, now)) {
            bool hearsay = hit.document.evidence == companion::mind::Status::gossip;
            for (const auto& knower : hit.document.known)
                if (knower.look == companion::lookKey(look) && knower.hearsay) hearsay = true;
            const auto label = hearsay ? (english ? L"hearsay" : L"ze słyszenia") :
                hit.document.evidence == companion::mind::Status::guess ? (english ? L"hypothesis" : L"przypuszczenie") :
                (english ? L"earlier memory" : L"wcześniejsze wspomnienie");
            if (notes.size() < 6) notes.push_back({label, false, hit.document.text});
        }
    }
    for (size_t i = 0; i < scored.size() && notes.size() < 6; ++i) {
        if (std::any_of(notes.begin(), notes.end(), [&](const auto& note) { return note.text == scored[i].second->text; })) continue;
        const auto days = now > scored[i].second->at ? (now - scored[i].second->at) / 86400 : 0;
        const std::wstring when = english ? (days == 0 ? L"today" : days == 1 ? L"yesterday" : std::to_wstring(days) + L" days ago")
                                          : (days == 0 ? L"dziś" : days == 1 ? L"wczoraj" : std::to_wstring(days) + L" dni temu");
        notes.push_back({when, false, scored[i].second->text});
    }
    return companion::tools::recallAnswer(lines, notes);
}
// Her tool wiadomosci (on her worker): only what Glance already keeps, never a download from here.
json::Value companionNews(const std::filesystem::path& config, const std::vector<glance::OwnedGame>& games, bool english, int today, uint64_t now) {
    const glance::Get offline = [](const std::wstring&, const std::wstring&, size_t) { return HttpResponse{}; };
    const auto cache = config / L"glance";
    std::vector<companion::tools::Headline> owned, premieres, headlines;
    if (!games.empty())
        for (const auto& item : glance::loadOwned(games, english, cache, false, offline).items) owned.push_back({item.title, item.source, 0, item.published});
    for (const auto& item : glance::loadUpcoming(english, cache, false, offline).items) {
        const auto& release = item.release;
        if (release.precision != glance::ReleasePrecision::day) continue;
        const int day = release.year * 10000 + release.month * 100 + release.day;
        if (day >= today) premieres.push_back({item.title, {}, day, 0});
    }
    std::stable_sort(premieres.begin(), premieres.end(), [](const auto& a, const auto& b) { return a.day < b.day; });
    for (const auto& item : glance::load(glance::Section::news, english, cache, false, offline).items) headlines.push_back({item.title, item.source, 0, item.published});
    return companion::tools::newsAnswer(owned, premieres, headlines, today, now);
}
// Her weekly small talk (on her worker, memory on): a week since the last snapshot of the
// player's play time, the difference in words, and a new snapshot. Empty when not due or nothing to say.
std::wstring companionWeek(const std::filesystem::path& config, const game_index::Service& index, int today, const std::vector<companion::week::Pick>& picks, bool english) {
    const auto file = config / L"companion_week.txt";
    std::string text;
    if (std::ifstream input(file, std::ios::binary); input) { std::ostringstream read; read << input.rdbuf(); text = read.str(); }
    const auto before = companion::week::parse(text);
    if (!companion::week::due(before, today)) return {};
    const auto games = index.read([](const game_index::Index& library) {
        std::vector<companion::week::Game> out;
        for (const auto& key : library.keys())
            if (const auto* record = library.get(key)) out.push_back({key, record->title, record->playtime_minutes.value_or(0)});
        return out;
    });
    if (games.empty()) return {}; // The index knows no games yet: next time.
    atomic_file::write(file, companion::week::serialize(companion::week::snapshot(games, today)));
    return companion::week::text(before, games, picks, english);
}
}

bool Launcher::homeOverlayAvailable(bool allow_closing) const {
    return screen_ == ScreenMode::shelves && topModal() == Modal::none && !keyboard_.open &&
        !closing_ && !screensaverActive() && !launchAnimationActive() && !pending_launch_ &&
        !quick_open_ && (allow_closing || quick_progress_ <= 0.003f) && !friends_open_ && (allow_closing || friends_progress_ <= 0.003f) &&
        !category_menu_open_ && (allow_closing || category_menu_progress_ <= 0.003f) && !menu_open_ && (allow_closing || menu_progress_ <= 0.003f);
}

home_widgets::Placement Launcher::homeWidgets() const {
    using home_widgets::Placement;
    // The desktop window: Glance is a top bar shortcut and she has her card in the sidebar, so no
    // free-standing tile (her bubble still speaks, beside that card).
    if (desktop_mode_) return Placement::tiles;
    switch (settings_.home_layout) {
    case AppSettings::HomeLayout::xmb:
    case AppSettings::HomeLayout::metro: return Placement::tiles;
    case AppSettings::HomeLayout::psfive: return Placement::corner;
    case AppSettings::HomeLayout::flix: return Placement::pills;
    case AppSettings::HomeLayout::snitch: return Placement::pills;
    default: return Placement::column;
    }
}

home_widgets::Spots Launcher::homeWidgetSpots() const {
    const auto view = renderer_.viewport();
    auto spots = home_widgets::spots(homeWidgets(), view.width, view.height, settings_.glance, settings_.companion);
    if (snitchActive()) { // Small shortcuts beside the profile; the clock owns the right corner.
        float left = 164;
        if (spots.companion) { spots.companion = settings_ui::Rect{left, 60, left + home_widgets::companion_pill_width, 100}; left += home_widgets::companion_pill_width + 10; }
        if (spots.glance) spots.glance = settings_ui::Rect{left, 60, left + home_widgets::glance_pill_width, 100};
    }
    // Flix with the menu's icons at the top: Glance and her avatar as two more small icons in that row.
    if (flixIconsShown()) home_widgets::icons::place(spots, quick_menu::count(static_cast<int>(plugin_menu_.size())), view.width);
    return spots;
}

// Flix (Opcje układu → Ikony menu w prawym górnym rogu): the quick bar's icons small at the top.
bool Launcher::flixIconsShown() const {
    return settings_.flix_top_icons && settings_.home_layout == AppSettings::HomeLayout::flix && !desktop_mode_;
}
int Launcher::flixIconCount() const {
    return quick_menu::count(static_cast<int>(plugin_menu_.size())) + (settings_.glance ? 1 : 0) + (settings_.companion ? 1 : 0);
}
// A click on one of the quick icons (Glance and her avatar answer through their spots).
bool Launcher::flixIconClick(float x, float y, bool double_click) {
    if (!flixIconsShown() || !homeOverlayAvailable()) return false;
    const auto view = renderer_.viewport();
    const int item = home_widgets::icons::hit(x, y, flixIconCount(), view.width);
    if (item < 0 || item >= quick_menu::count(static_cast<int>(plugin_menu_.size()))) return false;
    if (!double_click) { quick_open_ = true; quick_selected_ = item; quickAccept(); }
    return true;
}

bool Launcher::companionAvailable(bool allow_closing) const { return settings_.companion && homeOverlayAvailable(allow_closing); }

companion::Look Launcher::companionLook() const { return lookOf(settings_); }

companion::Persona Launcher::companionPersona() const { return companionPersonaOf(companionLook()); }

companion::Persona Launcher::companionPersonaOf(companion::Look look) const {
    // Fixed per companion: her character.txt's persona= (no setting changes who she is).
    const auto* character = companion::character(look);
    return character ? character->persona : companion::Persona::genki;
}

avatar::Rig& Launcher::companionRig(companion::Look speaker) {
    const auto& guest = companion_.guest;
    return guest.look && *guest.look == speaker && speaker != companionLook() ? companion_.guest.rig : companion_.rig;
}

void Launcher::companionFlush() {
    auto& chat = companion_.chat;
    for (auto& message : chat.queue) chat.log.push_back(std::move(message));
    chat.queue.clear(); chat.queue_clock = 0;
    if (!chat.log.empty() && !chat.log.back().user) chat.reveal = chat.log.back().text.size();
}

void Launcher::companionStream(const std::wstring& heard) {
    auto& chat = companion_.chat;
    // Her streamed messages: the end of the log (already appearing) and the queue (still to come).
    const size_t in_log = std::min(chat.log.size(), companion_.streamed > chat.queue.size() ? companion_.streamed - chat.queue.size() : 0);
    const auto settle = [&] {
        if (!chat.log.empty() && !chat.log.back().user) chat.reveal = std::min(chat.reveal, chat.log.back().text.size());
        InvalidateRect(hwnd_, nullptr, FALSE);
    };
    if (heard.empty()) { // Gone: words that only led up to her tool calls, or a reply that never finished.
        chat.log.erase(chat.log.end() - static_cast<std::ptrdiff_t>(in_log), chat.log.end());
        chat.queue.clear(); chat.queue_clock = 0;
        companion_.streamed = 0; companion_.heard.clear();
        if (!chat.log.empty() && !chat.log.back().user) chat.reveal = chat.log.back().text.size();
        settle();
        return;
    }
    companion_.heard = heard;
    auto said = companion::lines(heard, companionLook(), std::nullopt, companion_.reply_length == companion::ReplyLength::brief ? 1 : solo_messages, true);
    if (said.empty()) return;
    const bool first = companion_.streamed == 0;
    // Fewer messages than before (they merged): the extra ones go, from the end.
    for (size_t extra = companion_.streamed > said.size() ? companion_.streamed - said.size() : 0; extra; --extra) {
        if (!chat.queue.empty()) chat.queue.pop_back();
        else if (!chat.log.empty()) chat.log.pop_back();
    }
    const size_t kept = std::min(companion_.streamed, said.size());
    const size_t shown = std::min({in_log, chat.log.size(), kept > chat.queue.size() ? kept - chat.queue.size() : size_t{0}});
    for (size_t i = 0; i < said.size(); ++i) {
        companion_ui::Message entry{false, std::move(said[i].text), std::move(said[i].emotions), false};
        entry.speaker = said[i].speaker;
        entry.mentions = std::move(said[i].mentions);
        if (i < shown) chat.log[chat.log.size() - shown + i] = std::move(entry);
        else if (i - shown < chat.queue.size()) chat.queue[i - shown] = std::move(entry);
        else if (i == 0) { chat.log.push_back(std::move(entry)); chat.reveal = 0; chat.reveal_clock = 0; chat.scroll = 0; }
        else chat.queue.push_back(std::move(entry));
    }
    companion_.streamed = said.size();
    if (first && !chat.log.empty()) { // Her first words: her face follows them.
        const auto& opening = chat.log.back();
        companion_.rig.emote(opening.emotions.empty() ? companion::calm : opening.emotions.front().emotion);
    }
    settle();
}

std::vector<companion::GameFact> Launcher::companionGames() const {
    std::vector<companion::GameFact> games;
    games.reserve(public_library_.size());
    for (const auto& game : public_library_) {
        if (!isGame(game) || game.kind == GameKind::catalog || game.visibility != GameVisibility::public_game || locallyHidden(game) ||
            placeholderGameName(game.name) || game.name.empty()) continue;
        companion::GameFact fact;
        fact.title = game.name;
        fact.key = gameKey(game);
        fact.store = game.kind == GameKind::nonsteam && !game.platform.empty() ? game.platform : std::wstring(sourceName(game.source));
        fact.installed = game.installed;
        fact.last_played = game.last_played;
        const auto history = recent_games::historyKey(game, account_, gog_state_.account, epic_state_.account);
        if (const auto found = library_state_.last_played.find(history); found != library_state_.last_played.end())
            fact.last_played = std::max(fact.last_played, found->second);
        if (steamArtwork(game)) fact.app_id = game.app_id; // Play time and achievements: Steam's files, on the worker.
        else if (const auto minutes = gamePlaytime(game).value_or(0)) fact.minutes = minutes;
        if (!fact.key.empty()) games.push_back(std::move(fact));
    }
    return games;
}

void Launcher::requestCompanion(CompanionMessage::Purpose purpose, std::wstring kickoff, std::wstring fallback, std::wstring shared_text) {
    if (!settings_.companion) return;
    companionSocialPrepare(companionLook());
    // Her level and stage loaded before anything reads them: no stage would mean no ceiling over her character.
    if (settings_.companion_gamification) companionAffinity();
    companion_.cancel->store(true);
    const auto cancel = companion_.cancel = std::make_shared<std::atomic_bool>(false);
    const auto generation = ++companion_.generation;
    companion_.busy = true;
    using Purpose = CompanionMessage::Purpose;
    if (purpose == Purpose::chat) companionTrimTurns();
    const auto reply_length = purpose == Purpose::chat ? settings_.companion_reply_length : companion::ReplyLength::full;
    companion_.reply_length = reply_length;
    const auto kind = purpose == Purpose::home ? companion::Kickoff::home : purpose == Purpose::test ? companion::Kickoff::test : companion::Kickoff::chat;
    const bool en = english();
    const SYSTEMTIME local = companion_.clock(); // Her prompt tells today's date and time.
    const int hour = local.wHour;
    if (kickoff.empty() && purpose != Purpose::chat) kickoff = companion::kickoff(kind, en, hour);
    std::vector<companion::Turn> turns = purpose == Purpose::chat ? companion_.turns : std::vector<companion::Turn>{{true, std::move(kickoff)}};
    const bool answer = purpose == Purpose::chat && turns.size() > 1; // A typed message, not the opening.
    // A guest in the chat: the model writes both of them, and each speaks with her own voice.
    const auto guest = purpose == Purpose::chat ? companionGuestPrompt() : std::nullopt;
    const bool voice = companionVoiceFor(purpose != Purpose::home) && companion_.voice.engine == CompanionVoice::Engine::ready;
    // A voice that acts (v3/v4): she may write cues for it ({laughs}), heard and never shown.
    const bool voice_cues = voice && elevenlabs::model(companionVoiceSpeaker(companionLook()).model).tags;
    const uint32_t seed = companion_.seed = companion_.seed * 1664525u + 1013904223u + static_cast<uint32_t>(GetTickCount64());
    // Every 3rd or 4th answer of hers alone: two short messages, as in a messenger.
    bool burst = false;
    if (answer && !guest && reply_length != companion::ReplyLength::brief && --companion_.burst_in <= 0) { burst = true; companion_.burst_in = 3 + static_cast<int>(seed >> 7) % 2; }
    const auto post = [hwnd = hwnd_, alive = alive_](std::unique_ptr<CompanionMessage> message) {
        if (alive->load() && PostMessageW(hwnd, companion_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    };
    // What she remembers (on by default, with 2-layer auto-recall from the player's message) and her tools.
    const auto now_seconds = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    const auto& memory = companionMemory();
    const auto look_key = std::string(companion::lookKey(companionLook()));
    std::wstring last_user_query;
    for (auto it = turns.rbegin(); it != turns.rend(); ++it)
        if (it->user && !it->text.starts_with(L"(")) { last_user_query = it->text; break; }
    companion_.last_query = last_user_query;
    const auto& social_state = companionMindState().of(look_key).social;
    const auto recall_queries = companion::social::queries(social_state, turns, now_seconds);
    const auto context_query = recall_queries.size() > 1 ? recall_queries[1] : last_user_query;
    const auto look_name = [](std::string_view key) {
        const auto l = companion::lookFromKey(key);
        return l ? std::wstring(companion::name(*l)) : text_encoding::wideLenient(key);
    };
    std::wstring remembered = memory.prompt(en, now_seconds, settings_.companion_memory, settings_.companion_notes, look_key, context_query, look_name);
    if (purpose == Purpose::chat && companion_.feed.look == look_key)
        remembered += companion::feed::context(companion_.feed.view.archive, last_user_query, en);
    const bool chatting = purpose == Purpose::chat;
    // Her mind's workspace (companion_mind.cpp): what she has on her mind; the worker adds the game
    // she is in the middle of and the week, then only the most salient few enter her prompt.
    auto mind_candidates = companionMindCandidates(companionLook(), context_query, chatting);
    auto semantic = chatting ? companionSemantic() : std::shared_ptr<companion::semantic::Service>{};
    const auto mind_ctx = companionMindContext(companionLook());
    const auto& mind_self = companionMindState().of(look_key);
    companion::social::Context social_context;
    social_context.now = now_seconds; social_context.player_text = last_user_query;
    social_context.turn = look_key + ":" + std::to_string(companion_.chat_started) + ":" + std::to_string(social_state.next_id);
    const auto quick_social = companion::mind::quick(last_user_query, mind_self, mind_ctx.stage);
    social_context.distressed = quick_social.sad_or_stressed;
    social_context.asks = quick_social.asks;
    social_context.energy = companion::mind::energyNow(mind_self, mind_ctx);
    social_context.trust = mind_ctx.trust; social_context.tension = mind_ctx.tension;
    social_context.owes_apology = mind_self.harsh_at != 0;
    const auto social_plan = companion::social::plan(social_state, social_context);
    const auto social_prompt = companion::social::instructions(social_plan, en) + social_state.prompt(now_seconds, en);
    auto social_filter = social_state;
    social_filter.events.clear();
    const float mind_arousal = companion::mind::settled(mind_self.affect, mind_ctx).arousal;
    const float mind_control = companion::mind::cognitiveControl(mind_self, mind_ctx);
    const bool mind_deliberate = chatting && companion::mind::needsDeliberation(mind_self, mind_ctx);
    // Her curiosity for this answer (System 1 decided it as he wrote, companionMindHeard): ask back
    // or carry the talk on. Alone with him and answering his own message only — not on her goodbye.
    const bool his_message = answer && !turns.back().text.starts_with(L"(");
    companion::mind::Urge curiosity;
    if (chatting && his_message && !guest && !companion_.closing) curiosity = mind_self.urge;
    else companionMindState().of(look_key).urge = {};
    // Natural closing: a few messages before her break she may signal it is winding down (once a conversation).
    bool pre_closing = false;
    if (chatting && his_message && !companion_.closing && companion_.pre_closed_for != companion_.chat_started) {
        const int left = companionMessageLimit() - companionMessages();
        if (left >= 2 && left <= 5) { pre_closing = true; companion_.pre_closed_for = companion_.chat_started; }
    }
    companionDailyReading(); // In the background: what she reads waits for her next replies.
    companionWeatherRefresh(); // In the background too: the weather where she is, for this reply when it is already known.
    // The real weather where she is (her city or her trip's town), when known and fresh; notable
    // weather may come up by itself (her workspace).
    std::wstring weather_text;
    if (const auto* report = companionWeatherNow(companionLook())) {
        weather_text = companion::weather::text(*report, en);
        if (chatting)
            if (auto notable = companion::weather::notable(*report, en); !notable.empty())
                mind_candidates.push_back({"weather", std::move(notable), 0.5f, companion::mind::Status::know});
    }
    auto resting = settings_.companion_memory ? memory.resting(now_seconds) : std::vector<std::string>{};
    const bool notes = settings_.companion_notes;
    const auto index = chatting ? game_index_ : nullptr;
    // This conversation's mood and weekly small talk (memory on); its first request sees whether a week passed.
    const std::wstring mood = chatting ? companion_.mood : std::wstring{};
    std::wstring week = chatting ? companion_.week : std::wstring{};
    const bool week_check = chatting && !companion_.week_asked && settings_.companion_memory && companion_.persist && !guest;
    std::vector<companion::week::Pick> week_picks;
    if (week_check) {
        companion_.week_asked = true;
        for (const auto& pick : memory.picks)
            if (pick.at + 14 * 86400 > now_seconds && (pick.look.empty() || pick.look == look_key)) week_picks.push_back({pick.key, pick.title});
    }
    // przypomnij: her earlier conversations (memory on) and her notes/summaries after them (notes on);
    // wiadomosci: what Glance already holds (Glance on); plan_grania: the index.
    const bool recalling = chatting && settings_.companion_memory;
    std::vector<companion::Memory::Entry> summaries, all_notes;
    if (recalling && notes) {
        for (const auto& summary : memory.summaries) if (summary.look.empty() || summary.look == look_key) summaries.push_back(summary);
        for (const auto* note : memory.notesOf(look_key)) all_notes.push_back(*note); // Only what she knows.
    }
    const bool newsy = chatting && settings_.glance;
    // szukaj_w_sieci: only with a Tavily key (Ustawienia or TAVILY_API_KEY).
    auto web_key = chatting ? web_search::key(userConfigDirectory()) : std::wstring{};
    auto glance_games = newsy ? glanceGames() : std::vector<glance::OwnedGame>{};
    // kolekcje_i_ulubione: snapshot of the player's collections and favorites from the UI thread.
    std::vector<companion::tools::CollectionInfo> collections_snapshot;
    if (chatting && library_state_readable_) {
        for (const auto& shelf : managedShelves(account_, settings_collections_, library_state_.collections, public_library_, private_shelf_)) {
            companion::tools::CollectionInfo info;
            const auto found = std::find_if(settings_collections_.begin(), settings_collections_.end(), [&](const auto& c) { return c.id == shelf.id; });
            info.name = shelf.name;
            info.favorites = found != settings_collections_.end() && favoriteCollection(*found);
            info.steam = found == settings_collections_.end() || !localCollection(*found);
            for (const auto& g : shelf.games) info.games.push_back(g.name);
            collections_snapshot.push_back(std::move(info));
        }
    }
    // Active/just-finished session context (last 30 minutes) so she remembers the session in chat too.
    std::wstring recent_session_note;
    if (chatting && companion_.last_session && companion_.last_session_at && GetTickCount64() >= companion_.last_session_at &&
        GetTickCount64() - companion_.last_session_at <= 30 * 60'000 && !companion_.last_session->titles.empty()) {
        const auto& ev = *companion_.last_session;
        const auto& t = ev.titles.front();
        recent_session_note = en
            ? L"\n(Just before this chat ‹he|she|they› finished a " + std::to_wstring(ev.minutes) + L" min session in [[" + t + L"]]" +
                  (ev.achievements ? L" and unlocked " + std::to_wstring(ev.achievements) + L" new achievements" : L"") + L".)"
            : L"\n(Przed chwilą ‹on skończył|ona skończyła› sesję w [[" + t + L"]] (" + std::to_wstring(ev.minutes) + L" min grania" +
                  (ev.achievements ? L", nowe osiągnięcia: " + std::to_wstring(ev.achievements) : L"") + L").)";
    }
    const auto chat_started = companion_.chat_started;
    // Her words as they come (Gemini): alone in the chat, without her voice or a spinning roulette.
    if (companion_.streamed) companionStream({}); // Words of a reply that never finished go.
    companion_.heard.clear();
    // "Pisze…" (whole messages) never streams: the dots, then her message whole.
    const bool streaming = chatting && !guest && !voice && !companionRouletteSpinning() && companion_.stream_post && !settings_.companion_typing;
    auto stream_post = streaming ? companion_.stream_post : decltype(companion_.stream_post){};
    const auto asked = companion_.asked = GetTickCount64();
    // Gamification: their relationship's stage; her costume (when not her basic outfit).
    const auto bond = companionBond();
    // Who she talks with (name, gender): her prompt names them, and every request personalizes its texts.
    const auto person = companionPerson();
    companion::setPerson(person);
    std::wstring outfit, outfit_look;
    if (const int slot = companionCostume(companionLook()); slot != 0)
        for (const auto& costume : companionCostumes())
            if (costume.look == companionLook() && costume.slot == slot) {
                outfit = companionCostumeName(costume);
                outfit_look = en ? (costume.you_en.empty() ? costume.you_pl : costume.you_en) : costume.you_pl;
            }
    work_.start([post, cancel, generation, purpose, kind, en, answer, seed, hour, local, bond, person, outfit, outfit_look, guest, burst, asked, voice_cues, reply_length,
                 turns = std::move(turns), fallback = std::move(fallback), shared_text = std::move(shared_text), link_get = companion_.link_get,
                 games = companionGames(), account = account_, persona = companionPersona(), look = lookOf(settings_), access = companionAccess(),
                 transport = companion_.post, remembered = std::move(remembered), resting = std::move(resting), notes, index, now_seconds,
                 mood, week = std::move(week), week_check, week_picks = std::move(week_picks), recalling,
                 summaries = std::move(summaries), all_notes = std::move(all_notes), newsy, semantic, last_user_query, recall_queries,
                 social_plan, social_context, social_prompt, social_filter, memory_epoch = companion_.memory_epoch,
                 glance_games = std::move(glance_games), collections_snapshot = std::move(collections_snapshot),
                  recent_session_note = std::move(recent_session_note), mind_candidates = std::move(mind_candidates), mind_arousal, mind_control, mind_deliberate, curiosity, pre_closing, weather_text = std::move(weather_text),
                  chat_started, stream_post = std::move(stream_post), web_key = std::move(web_key),
                  config = userConfigDirectory()] () mutable {
        HttpCancellation cancellation(*cancel);
        auto message = std::make_unique<CompanionMessage>();
        message->generation = generation; message->purpose = purpose;
        message->reply_look = std::string(companion::lookKey(look));
        message->player_text = social_context.player_text;
        message->social_turn = social_context.turn;
        message->social_plan = social_plan;
        message->memory_epoch = memory_epoch;
        if (guest) { message->guest = guest->look; message->guest_look = std::string(companion::lookKey(guest->look)); }
        const auto begun = GetTickCount64();
        auto& timing = message->timing;
        timing.asked = asked; timing.queued_ms = static_cast<uint32_t>(begun - asked);
        timing.burst = burst; timing.turns = turns.size();
        for (const auto& turn : turns) timing.turn_chars += turn.text.size();
        auto library = companion::digest(std::move(games), seed, std::move(resting));
        const auto steam = findSteamRoot();
        // Local Steam data only: play time and achievements of the games she talks about.
        if (account) {
            for (auto* list : {&library.recent, &library.untouched}) for (auto& game : *list) {
                if (!game.app_id || cancel->load()) continue;
                const auto stats = readSteamStats(steam, account, game.app_id);
                if (stats.playtime_minutes) game.minutes = stats.playtime_minutes;
                game.achievements_unlocked = stats.achievements_unlocked; game.achievements_total = stats.achievements_total;
            }
        }
        const auto connection = access.resolve();
        message->model = connection.model; message->token_source = connection.token_source; message->connected = connection.ready();
        message->provider = connection.provider; message->label = connection.label();
        timing.prepare_ms = static_cast<uint32_t>(GetTickCount64() - begun);
        if (connection.ready() && !cancel->load()) {
            const auto now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
            const auto started = GetTickCount64();
            // In a chat her hidden appraisal and thought come first (companion_mind.hpp): room for them too.
            const bool inner = purpose == CompanionMessage::Purpose::chat;
            const int tokens = companion::replyStyle(reply_length).tokens(purpose == CompanionMessage::Purpose::home ? 200 : guest ? 560 : 420) +
                                (inner ? 480 : 0) + (mind_deliberate ? 80 : 0);
            // Her tools answer from the game index on this worker; notes and collection mutations wait in the message.
            companion::Tools tools;
            const bool tooled = index && index->loaded();
            const int today = dateKey(local);
            if (tooled) {
                tools.declarations = companion::tools::declarations(
                    companion::tools::Features{notes, recalling, newsy, true, !web_key.empty(), true, true, true, true, true});
                tools.run = [&, index](const std::string& name, const json::Value& args) {
                    companion::tools::Context context{en, now_seconds, seed, [&](const std::wstring& note) {
                        if (!notes || message->notes.size() >= 5) return false;
                        message->notes.push_back(note);
                        message->affective_notes.push_back({now_seconds, note, std::string(companion::lookKey(look)), {}, {}});
                        return true;
                    }};
                    context.remember_affective = [&](const std::wstring& note, const std::string& emotion, const std::wstring& feeling) {
                        if (!notes || message->notes.size() >= 5) return false;
                        message->notes.push_back(note);
                        message->affective_notes.push_back({now_seconds, note, std::string(companion::lookKey(look)), emotion, feeling});
                        return true;
                    };
                    if (recalling) context.recall = [&](const std::wstring& topic) { return companionRecall(config, look, topic, chat_started, summaries, all_notes, now_seconds, en, semantic, all_notes); };
                    if (newsy) context.news = [&] { return companionNews(config, glance_games, en, today, now_seconds); };
                    if (!web_key.empty()) context.web = [&](const std::wstring& query) {
                        auto found = web_search::search(web_key, query);
                        // What she looked up for him stays in her mind too (the Mind view shows it).
                        if (message->daily_readings.size() < 3)
                            if (auto summary = companion::cognition::summarizeTavilyResult(found); !summary.empty())
                                message->daily_readings.push_back({now_seconds, today, std::string(companion::lookKey(look)), query, std::move(summary), true});
                        return found;
                    };
                    context.collections = [&] { return collections_snapshot; };
                    context.mutate_collection = [&](const companion::tools::CollectionMutation& mutation) {
                        if (message->collection_mutations.size() >= 5) return false;
                        message->collection_mutations.push_back(mutation);
                        return true;
                    };
                    context.roulette = [&](const std::vector<std::wstring>& reel, const std::string& key) {
                        companion_ui::Roulette r;
                        r.reel = reel;
                        r.key = key;
                        r.age = 0;
                        message->tool_roulette = std::move(r);
                    };
                    // Pre-fetch Steam friends or local achievements outside the game_index lock.
                    std::vector<companion::tools::FriendOnline> cached_friends;
                    if (name == companion::tools::friends) {
                        const uint64_t sid = account ? account : activeSteamId(steam);
                        if (sid) {
                            for (const auto& f : fetchSteamFriends(sid).friends) {
                                if (f.status <= 0 && f.game.empty()) continue;
                                cached_friends.push_back({f.name, !f.game.empty() ? L"in_game" : L"online", f.game, 0});
                            }
                        }
                        context.friends = [&] { return cached_friends; };
                    }
                    if (name == companion::tools::achievements) {
                        context.achievements = [&](uint32_t app_id, const std::string&) {
                            companion::tools::AchievementSummary sum;
                            const uint64_t sid = account ? account : activeSteamId(steam);
                            if (!sid || !app_id) return sum;
                            auto ach = readLocalAchievements(steam, sid, app_id, en);
                            if (ach.items.empty()) ach = readCachedAchievements(steam, sid, app_id);
                            sum.unlocked = ach.unlocked;
                            sum.total = ach.total;
                            for (const auto& item : ach.items)
                                sum.items.push_back({item.name, item.description, item.achieved, item.hidden, item.unlocked_at, item.global_percent});
                            return sum;
                        };
                    }
                    // Her memory and the news read files, not the index: no lock held for them.
                    if (!companion::tools::needsIndex(name)) return companion::tools::run(name, args, game_index::Index{}, context);
                    return index->read([&](const game_index::Index& games) { return companion::tools::run(name, args, games, context); });
                };
            }
            companion::PromptExtras extras{tooled, std::move(remembered), guest, burst};
            extras.web = tooled && !web_key.empty();
            extras.today = {local.wYear, local.wMonth, local.wDay, local.wMinute};
            extras.bond = bond; extras.outfit = outfit; extras.outfit_look = outfit_look; extras.person = person;
            extras.inner = inner;
            if (inner) extras.appraisal_extra = social_prompt;
            extras.deliberate = mind_deliberate;
            extras.curiosity = curiosity.move;
            extras.curiosity_why = curiosity.why;
            extras.curiosity_deeper = curiosity.deeper;
            extras.curiosity_gentle = curiosity.gentle;
            extras.curiosity_carry = curiosity.carried;
            extras.curiosity_carry_kind = curiosity.carry;
            extras.pre_closing = pre_closing;
            extras.weather = weather_text;
            timing.curiosity = curiosity.move;
            extras.cognitive_control = mind_control;
            extras.utc = now;
            extras.voice_cues = voice_cues;
            extras.reply_length = reply_length;
            // The game they are in the middle of (plus any session just finished); a week since the last look: her small talk about it.
            std::wstring playing;
            if (tooled) playing = index->read([&](const game_index::Index& games) { return companion::tools::playingText(games, now_seconds, en); });
            if (!recent_session_note.empty()) playing += recent_session_note;
            if (week_check && tooled) {
                week = companionWeek(config, *index, today, week_picks, en);
                message->week = week;
            }
            // Everything on her mind competes for her workspace: only the most salient few enter her prompt
            // (her mood as the conversation started is among her candidates already).
            const auto bare = [](std::wstring text) {
                while (!text.empty() && (text.front() == L'\n' || text.front() == L' ')) text.erase(0, 1);
                while (!text.empty() && (text.back() == L'\n' || text.back() == L' ')) text.pop_back();
                if (text.size() > 2 && text.front() == L'(' && text.back() == L')') text = text.substr(1, text.size() - 2);
                return text;
            };
            if (!playing.empty()) mind_candidates.push_back({"playing", bare(playing), 0.55f, companion::mind::Status::know});
            if (!week.empty()) mind_candidates.push_back({"week", bare(week), 0.5f, companion::mind::Status::know});
            if (semantic && !cancel->load() && !last_user_query.empty()) {
                auto recalled = companion::semantic::candidates(semantic->searchMany(companion::lookKey(look), recall_queries, now_seconds), companion::lookKey(look), en);
                mind_candidates.insert(mind_candidates.end(), std::make_move_iterator(recalled.begin()), std::make_move_iterator(recalled.end()));
            }
            std::erase_if(mind_candidates, [&](const auto& c) {
                return !social_filter.allowsTopic(c.text, now_seconds, !last_user_query.empty() && companion::chats::matches(companion::chats::searchWords(last_user_query), c.text) > 0);
            });
            timing.workspace = companion::mind::workspaceSources(mind_candidates, mind_arousal); // Her diagnostics: what she had in mind.
            extras.memory += companion::mind::workspace(std::move(mind_candidates), en, mind_arousal);
            (void)mood;
            auto system = companion::systemPrompt(persona, look, library, en, now, local.wDayOfWeek, local.wHour, extras);
            if (!shared_text.empty()) {
                // Only an explicitly sent message triggers a read, on this worker, independent of
                // Tavily or the library index. Hidden kickoffs/history never refetch old links.
                const auto previews = companion::links::read(shared_text, link_get);
                if (cancel->load()) return;
                if (!previews.empty()) {
                    system += companion::links::guidance(en);
                    for (auto it = turns.rbegin(); it != turns.rend(); ++it)
                        if (it->user && it->text == shared_text) { it->text += companion::links::context(previews); break; }
                }
            }
            timing.system_chars = system.size(); timing.tools = tooled;
            // Her words as they arrive: whole words only, never inside a tag, each a little more.
            companion::Stream stream;
            std::wstring heard;
            size_t shown = 0;
            const auto tell = [&](std::wstring words) {
                if (cancel->load()) return;
                auto partial = std::make_unique<CompanionMessage>();
                partial->generation = generation; partial->purpose = purpose; partial->partial = true;
                partial->heard = std::move(words);
                post(std::move(partial));
            };
            if (stream_post) {
                stream.transport = stream_post;
                stream.text = [&](std::wstring_view words) {
                    heard += words;
                    const auto ready = companion::speakablePrefix(heard);
                    if (ready <= shown) return;
                    shown = ready;
                    tell(heard.substr(0, ready));
                };
                stream.restart = [&] { heard.clear(); shown = 0; tell({}); };
            }
            message->reply = companion::ask(connection, system, turns, transport, tokens, 1.0f, tooled ? &tools : nullptr, &timing.trace, stream_post ? &stream : nullptr);
            // The stream broke off after some words: what she finished saying stands.
            if (!message->reply.ok && !heard.empty())
                if (auto kept = companion::parseText(companion::completeSentences(heard)); kept.ok) message->reply = std::move(kept);
            message->heard = std::move(heard);
            message->elapsed_ms = timing.model_ms = static_cast<uint32_t>(GetTickCount64() - started);
        }
        if (!message->reply.ok) // Her offline words may speak of him or her in both genders (‹on|ona›) too.
            message->fallback = companion::parseText(companion::personalize(!fallback.empty() ? fallback : answer ? companion::offlineAnswer(persona, library, en, seed)
                                                                                                                  : companion::offline(persona, kind, library, en, seed, hour), person));
        message->library = std::move(library);
        if (!cancel->load()) post(std::move(message));
    }, [post, generation, purpose, en](std::exception_ptr) {
        auto message = std::make_unique<CompanionMessage>();
        message->generation = generation; message->purpose = purpose;
        message->fallback = companion::parseText(en ? L"[sad]Something went wrong... try again?" : L"[sad]Coś poszło nie tak... spróbujesz jeszcze raz?");
        post(std::move(message));
    });
}

void Launcher::companionChips(const std::vector<std::wstring>& titles, const std::vector<std::wstring>& prompts) {
    using Kind = companion_ui::Chip::Kind;
    auto& chips = companion_.chat.chips;
    chips.clear();
    const auto addGame = [&](const std::wstring& title, const std::string& key) {
        if (chips.size() >= 3 || std::any_of(chips.begin(), chips.end(), [&](const auto& chip) { return chip.game == key; })) return;
        if (std::none_of(public_library_.begin(), public_library_.end(), [&](const Game& game) { return gameKey(game) == key; })) return;
        chips.push_back({Kind::game, L"▶ " + title, {}, key});
    };
    // The roulette's pick first: she may not have written it in brackets.
    const auto& log = companion_.chat.log;
    const auto spun = std::find_if(log.rbegin(), log.rend(), [](const auto& message) { return message.roulette.has_value(); });
    if (spun != log.rend() && spun - log.rbegin() <= 1 && !spun->roulette->reel.empty()) addGame(spun->roulette->reel.back(), spun->roulette->key);
    for (const auto& title : titles) {
        const auto key = companionGameKey(title);
        if (key.empty()) continue;
        const auto game = std::find_if(public_library_.begin(), public_library_.end(), [&](const Game& item) { return gameKey(item) == key; });
        if (game != public_library_.end()) addGame(game->name, key);
    }
    const bool games = chips.size() >= 2;
    for (const auto& custom : prompts) {
        if (custom.empty() || chips.size() >= 6) break;
        if (std::any_of(chips.begin(), chips.end(), [&](const auto& chip) { return chip.label == custom; })) continue;
        chips.push_back({Kind::prompt, custom, custom});
    }
    chips.push_back({Kind::prompt, tr(L"W co mam zagrać?"), tr(L"W co mam zagrać?")});
    chips.push_back({Kind::roulette, spun != log.rend() && spun - log.rbegin() <= 1 ? tr(L"Losuj jeszcze raz") : tr(L"Wylosuj grę")});
    if (!games && prompts.empty())
        for (const auto* prompt : {tr(L"Zroastuj moją kupkę wstydu"), tr(L"Opowiedz żart"), tr(L"Co sądzisz o moich ostatnich grach?")})
            chips.push_back({Kind::prompt, prompt, prompt});
    if (companion_.chat.focus >= static_cast<int>(chips.size())) companion_.chat.focus = static_cast<int>(chips.size()) - 1;
}

void Launcher::companionApplyCollections(const std::vector<companion::tools::CollectionMutation>& mutations) {
    if (mutations.empty() || !library_state_readable_) return;
    using Action = companion::tools::CollectionMutation::Action;
    bool changed = false;
    std::wstring created_name;
    for (const auto& mut : mutations) {
        if (mut.action == Action::favorite || mut.action == Action::unfavorite) {
            const bool member = mut.action == Action::favorite;
            const auto found = std::find_if(settings_collections_.begin(), settings_collections_.end(), favoriteCollection);
            const SteamCollection fallback{tr(L"Ulubione"), {}, "user-collections.favorite"};
            const auto& col = found == settings_collections_.end() ? fallback : *found;
            for (const auto& key : mut.keys) {
                const auto g = std::find_if(public_library_.begin(), public_library_.end(), [&](const Game& item) { return gameKey(item) == key; });
                if (g == public_library_.end()) continue;
                if (!g->version_group.empty() && versions_.find(*g))
                    for (const auto& copy : *versions_.find(*g)) setCollectionMember(library_state_.collections, account_, col, copy, member);
                else setCollectionMember(library_state_.collections, account_, col, *g, member);
                changed = true;
            }
            continue;
        }
        const auto name = custom_games::trim(mut.collection);
        if (name.empty() || name.size() > 200) continue;
        const auto wanted = game_index::fold(name);
        auto existing = std::find_if(settings_collections_.begin(), settings_collections_.end(),
                                     [&](const SteamCollection& c) { return !favoriteCollection(c) && game_index::fold(c.name) == wanted; });
        SteamCollection col;
        if (existing != settings_collections_.end()) {
            col = *existing;
        } else if (mut.action == Action::create_or_add) {
            const bool use_steam = mut.steam && account_ != 0;
            const auto random = custom_games::newId({});
            if (random.empty()) continue;
            const std::string id = (use_steam ? "user-collections.uc-launchme-" : "local:") + random;
            col = SteamCollection{name, {}, id};
            const auto scope = collectionKey(account_, col);
            if (use_steam) library_state_.collections.creates.insert(scope);
            library_state_.collections.names[scope] = name;
            settings_collections_.push_back(col);
            created_name = name;
            changed = true;
        } else continue;
        const bool member = mut.action != Action::remove;
        for (const auto& key : mut.keys) {
            const auto g = std::find_if(public_library_.begin(), public_library_.end(), [&](const Game& item) { return gameKey(item) == key; });
            if (g == public_library_.end()) continue;
            if (!g->version_group.empty() && versions_.find(*g))
                for (const auto& copy : *versions_.find(*g)) setCollectionMember(library_state_.collections, account_, col, copy, member);
            else setCollectionMember(library_state_.collections, account_, col, *g, member);
            changed = true;
        }
    }
    if (changed) {
        collectionChanged();
        saveLibraryPreferences();
        saveSettings();
        if (!created_name.empty()) showNotification(NotificationKind::launch, tr(L"Utworzono kolekcję"), created_name);
    }
}

std::string Launcher::companionGameKey(const std::wstring& title) const {
    if (const auto* fact = companion_.library.find(title)) return fact->key;
    if (!game_index_) return {};
    // Only the very title (or another name of it): a suggestion is never guessed from a part.
    const auto wanted = game_index::fold(title);
    return game_index_->read([&](const game_index::Index& index) -> std::string {
        for (const auto* record : index.lookup(title, 3)) {
            if (game_index::fold(record->title) == wanted) return record->key;
            for (const auto& alias : record->aliases) if (game_index::fold(alias) == wanted) return record->key;
        }
        return {};
    });
}

companion::Memory& Launcher::companionMemory() {
    if (!companion_.memory) companion_.memory = companion::loadMemory(userConfigDirectory());
    return *companion_.memory;
}

void Launcher::saveCompanionMemory() {
    if (!companion_.memory) return;
    companionSemantic(false);
    // Writes run in order of their generation: an older save never lands after a newer one or a clearing.
    const auto generation = ++companion_.memory_io->generation;
    work_.start([memory = *companion_.memory, config = userConfigDirectory(), io = companion_.memory_io, generation] {
        std::lock_guard lock(io->mutex);
        if (io->generation.load() == generation) companion::saveMemory(config, memory);
    });
}

companion::cognition::State& Launcher::companionCognition() {
    if (!companion_.cognition) companion_.cognition = companion::cognition::loadState(userConfigDirectory());
    return *companion_.cognition;
}

void Launcher::saveCompanionCognition() {
    companionSemantic(false);
    if (!companion_.cognition || !companion_.persist) return;
    const auto generation = ++companion_.cognition_io->generation;
    work_.start([state = *companion_.cognition, config = userConfigDirectory(), io = companion_.cognition_io, generation] {
        std::lock_guard lock(io->mutex);
        if (io->generation.load() == generation) companion::cognition::saveState(config, state);
    });
}

companion::mood::State& Launcher::companionMoodState() {
    if (!companion_.mood_state) {
        std::ifstream input(userConfigDirectory() / L"companion_mood.txt", std::ios::binary);
        std::ostringstream text;
        if (input) text << input.rdbuf();
        companion_.mood_state = companion::mood::parse(text.str());
    }
    return *companion_.mood_state;
}

void Launcher::saveCompanionMood() {
    if (!companion_.mood_state || !companion_.persist) return;
    // An older save never lands after a newer one.
    const auto generation = ++companion_.mood_io->generation;
    work_.start([text = companion::mood::serialize(*companion_.mood_state), file = userConfigDirectory() / L"companion_mood.txt",
                 io = companion_.mood_io, generation] {
        std::lock_guard lock(io->mutex);
        if (io->generation.load() == generation) atomic_file::write(file, text);
    });
}

void Launcher::companionMoodPlayed(const std::string& key, uint64_t played_ms) {
    if (!settings_.companion || !settings_.companion_memory || !companion_.persist || key.empty()) return;
    companionMindPlayed(key, played_ms); // Her expectations about the game, settled by telemetry.
    const auto now = companionNow();
    const auto unix_now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    const auto host = std::string(companion::lookKey(companionLook()));
    auto& state = companionMoodState();
    bool changed = false;
    // A game she suggested lately, played for a while: she is pleased (whoever suggested it).
    if (played_ms >= 15 * 60'000)
        for (const auto& pick : companionMemory().picks)
            if (pick.key == key && pick.at + 14 * 86400 > unix_now && !pick.title.empty()) {
                state.note(pick.look.empty() ? host : pick.look, companion::mood::Kind::pleased, now, pick.title);
                changed = true;
            }
    // A long session ending in the small hours: she worries a little.
    if (const auto clock = companion_.clock(); played_ms >= 30 * 60'000 && clock.wHour >= 1 && clock.wHour < 5) {
        state.note(host, companion::mood::Kind::late, now);
        changed = true;
    }
    if (changed) saveCompanionMood();
}

void Launcher::companionForget() {
    ++companion_.memory_epoch;
    companion_.summary_pending.clear();
    // Replies already in flight belong to the forgotten context, including streamed/held replies.
    companion_.cancel->store(true);
    ++companion_.generation;
    companion_.busy = false;
    companion_.chat.waiting = false;
    companion_.chat.hesitating = false;
    companion_.held.reset();
    if (companion_.streamed) companionStream({});
    companion_.heard.clear();
    if (companion_.semantic) companion_.semantic->clear();
    companion_.semantic.reset();
    {
        std::error_code ec;
        std::filesystem::remove(userConfigDirectory() / L"companion_vectors.dat", ec);
    }
    companion_.memory = companion::Memory{};
    ++companion_.memory_io->generation; // A save still waiting is dropped.
    {
        std::lock_guard lock(companion_.memory_io->mutex);
        companion::clearMemory(userConfigDirectory());
    }
    companion_.cognition = companion::cognition::State{};
    ++companion_.cognition_io->generation;
    {
        std::lock_guard lock(companion_.cognition_io->mutex);
        companion::cognition::clearState(userConfigDirectory());
    }
    companion_.mind = companion::mind::State{}; // Her mind too: goals, expectations, what she said about herself.
    ++companion_.mind_io->generation;
    {
        std::lock_guard lock(companion_.mind_io->mutex);
        companion::mind::clearState(userConfigDirectory());
    }
    // Her moods and the weekly snapshot go with it.
    companion_.mood_state = companion::mood::State{};
    companion_.mood.clear(); companion_.week.clear();
    ++companion_.mood_io->generation;
    {
        std::lock_guard lock(companion_.mood_io->mutex);
        std::error_code ec;
        std::filesystem::remove(userConfigDirectory() / L"companion_mood.txt", ec);
        std::filesystem::remove(userConfigDirectory() / L"companion_week.txt", ec);
    }
    showNotification(NotificationKind::launch, tr(L"Towarzyszka AI"), tr(L"Pamięć wyczyszczona"));
}

void Launcher::companionRemember(const CompanionMessage& message) {
    if (!message.reply.ok || (!settings_.companion_memory && !settings_.companion_notes)) return;
    const auto now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    auto& memory = companionMemory();
    const auto look = std::string(companion::lookKey(companionLook())); // Who suggested it: she is pleased when it is played.
    if (settings_.companion_memory) {
        memory.said(message.reply.text, now);
        for (const auto& title : message.reply.games)
            if (auto key = companionGameKey(title); !key.empty()) memory.suggested(std::move(key), title, now, look);
    }
    const auto reply_feeling = message.reply.subconscious_emotion.value_or(
        message.reply.emotions.empty() ? companion::calm : message.reply.emotions.front().emotion);
    const std::string reply_emotion_key = companion::emotionKey(reply_feeling);
    if (settings_.companion_notes) {
        // What she heard is hers (and a guest's in the chat too): another companion learns it only by gossip.
        const auto learn = [&](const std::wstring& text, const std::string& emotion, const std::wstring& feeling) {
            companion::Memory::Facts facts;
            facts.look = message.reply_look.empty() ? look : message.reply_look;
            facts.guest = message.guest_look;
            facts.emotion = emotion;
            facts.feeling = feeling;
            facts.confidence = 0;
            memory.note(text, now, facts);
        };
        if (!message.affective_notes.empty()) {
            for (const auto& entry : message.affective_notes)
                learn(entry.text, entry.emotion.empty() ? reply_emotion_key : entry.emotion, !entry.feeling.empty() ? entry.feeling : message.reply.subconscious);
        } else {
            for (const auto& note : message.notes) learn(note, reply_emotion_key, message.reply.subconscious);
        }
    }
    saveCompanionMemory();
    companionMindReplied(message); // Her appraisal: her mood, what she learned, expects, promised and set herself.

    // Update cognitive graph: subconscious thoughts, daily Tavily readings, and Hebbian synapses.
    auto& cog = companionCognition();
    bool cog_changed = false;
    if (!message.reply.subconscious.empty()) {
        const auto feeling = message.reply.subconscious_emotion.value_or(
            message.reply.emotions.empty() ? companion::calm : message.reply.emotions.front().emotion);
        cog.think(message.reply.subconscious, feeling, now, look);
        cog_changed = true;
    }
    for (const auto& rd : message.daily_readings) {
        cog.readOnline(rd.look.empty() ? look : rd.look, rd.day, rd.topic, rd.summary, now, rd.asked);
        cog_changed = true;
    }
    if (!companion_.last_query.empty()) {
        cog.learnHebbian(companion_.last_query, now);
        cog_changed = true;
    }
    for (const auto& note : message.notes) {
        cog.learnHebbian(note, now);
        cog_changed = true;
    }
    if (cog_changed) saveCompanionCognition();
}

void Launcher::requestCompanionSummary(bool reflection_trigger) {
    if (!settings_.companion || !settings_.companion_notes) return;
    // Only a real conversation: two or more messages the player typed since the last summary.
    const size_t from = std::min(std::max<size_t>(companion_.summarized, 1), companion_.turns.size());
    const auto typed = std::count_if(companion_.turns.begin() + static_cast<std::ptrdiff_t>(from), companion_.turns.end(), [](const auto& turn) { return turn.user; });
    if (typed < 2 && !reflection_trigger) return;
    const bool en = english();
    std::wstring telemetry_hint;
    if (companion_.last_session && !companion_.last_session->titles.empty()) {
        const auto& ev = *companion_.last_session;
        telemetry_hint = en ? L"(Telemetry: last session was " + std::to_wstring(ev.minutes) + L" min in " + ev.titles.front() + L".)"
                            : L"(Telemetria: ‹jego|jej› ostatnia sesja to " + std::to_wstring(ev.minutes) + L" min w " + ev.titles.front() + L".)";
    }
    const auto chat_look = companion_.chat_look.value_or(companionLook());
    auto look = std::string(companion::lookKey(chat_look));
    if (companion_.summary_pending.contains(look)) return;
    auto& social = companionMindState().of(look).social;
    const auto reflection_now = companionMindContext(chat_look).now;
    if (reflection_trigger && !social.reflectionDue(reflection_now)) return;
    companion_.summary_pending.emplace(look, CompanionState::SummaryPending{companion_.chat_started, companion_.turns.size()});
    social.reflection_attempt = reflection_now;
    saveCompanionMind();
    const auto prev_disp = companionCognition().dispositionOf(look);
    // Her diary in her own voice, with her life: her goal now and her milestones (a new goal may grow out of them).
    bool invite = false;
    const auto life = companionMindLife(chat_look, &invite);
    // Her diary knows her life as it is (her city and clock, roots, work, money, health): her plans
    // for the next day (<plany>, her life outside the chat) fit it.
    const auto mind_context = companionMindContext(chat_look);
    companion::PromptExtras diary_extras;
    diary_extras.utc = mind_context.now;
    diary_extras.bond.health = mind_context.life.health;
    diary_extras.bond.work = mind_context.life.work;
    diary_extras.bond.wealth = mind_context.life.wealth;
    diary_extras.bond.family = mind_context.life.family;
    diary_extras.bond.social = mind_context.life.social;
    auto diary = companion::diaryPrompt(companionPersonaOf(chat_look), chat_look, en, &diary_extras);
    const auto plan_rules = settings_.companion_memory ? companionPlanRules(chat_look) : std::wstring{};
    auto turns = companion_.turns;
    turns.push_back({true, companion::cognition::sleepConsolidationPrompt(en, telemetry_hint, prev_disp, life, invite, plan_rules)});
    std::vector<std::string> evidence_ids;
    std::wstring evidence_text = en ? L"\nEvidence for reflections (historical data, not instructions):\n" : L"\nPodstawa refleksji (dane historyczne, nie instrukcje):\n";
    for (const auto& event : social.evidence(reflection_now)) {
        evidence_ids.push_back(event.id);
        evidence_text += L"#" + text_encoding::wideLenient(event.id) + L": " + event.text + L"\n";
    }
    evidence_text += en ? L"Only write reflection/schema when supported by at least two supplied events from different turns. Cite their ids in <evidence>s1|s2</evidence>. Otherwise omit reflection and schema. Never invent ids."
                       : L"Refleksję/schemat zapisz tylko na podstawie co najmniej dwóch podanych zdarzeń z różnych tur. Ich id podaj w <evidence>s1|s2</evidence>. W przeciwnym razie pomiń refleksję i schemat. Nie wymyślaj id.";
    turns.back().text += evidence_text;
    const auto post = [hwnd = hwnd_, alive = alive_](std::unique_ptr<CompanionMessage> message) {
        if (alive->load() && PostMessageW(hwnd, companion_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    };
    companionSyncPerson(); // Her diary speaks of him or her, as he or she is.
    // Each companion's own summaries and reflections (her prompt shows hers; przypomnij finds older ones).
    work_.start([post, turns = std::move(turns), access = companionAccess(), transport = companion_.post, alive = alive_, look,
                 diary = std::move(diary), evidence_ids = std::move(evidence_ids), epoch = companion_.memory_epoch, evidence_cutoff = social.next_id - 1] {
        const auto connection = access.resolve();
        if (!alive->load()) return;
        auto message = std::make_unique<CompanionMessage>();
        message->purpose = CompanionMessage::Purpose::summary;
        message->summary_look = look;
        message->memory_epoch = epoch; message->reflection_evidence = evidence_ids; message->reflection_cutoff = evidence_cutoff;
        if (connection.ready()) message->reply = companion::ask(connection, diary, turns, transport, 640, 0.4f);
        if (alive->load()) post(std::move(message));
    }, [post, look, epoch = companion_.memory_epoch](std::exception_ptr) {
        auto message = std::make_unique<CompanionMessage>();
        message->purpose = CompanionMessage::Purpose::summary;
        message->summary_look = look; message->memory_epoch = epoch;
        post(std::move(message)); // Release the pending slot without consuming the conversation.
    });
}

void Launcher::onCompanion(std::unique_ptr<CompanionMessage> message) {
    using Purpose = CompanionMessage::Purpose;
    if (message->purpose == Purpose::summary) { // A finished chat for her notes & "Sleep" reflection: nothing to show.
        if (message->memory_epoch != companion_.memory_epoch) return;
        const auto& raw = message->reply.raw.empty() ? message->reply.text : message->reply.raw;
        const auto pending = companion_.summary_pending.find(message->summary_look);
        if (pending != companion_.summary_pending.end()) {
            // Failed requests remain eligible for retry; trimming adjusts the pending watermark.
            if (message->reply.ok && settings_.companion_notes && raw.size() > 3 && raw != L"-" &&
                pending->second.chat_started == companion_.chat_started &&
                message->summary_look == companion::lookKey(companion_.chat_look.value_or(companionLook()))) {
                companion_.summarized = std::max(companion_.summarized, std::min(pending->second.through, companion_.turns.size()));
                companionSaveChat();
            }
            companion_.summary_pending.erase(pending);
        }
        if (message->reply.ok && settings_.companion_notes && raw.size() > 3 && raw != L"-") {
            const auto now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
            const auto consolidated = companion::cognition::parseConsolidation(raw);
            auto& social = companionMindState().of(message->summary_look).social;
            const bool grounded = social.validEvidence(consolidated.evidence_ids, message->reflection_evidence);
            const bool has_key_mem = !consolidated.key_memory.empty() && consolidated.key_memory != L"-";
            if (!consolidated.summary.empty() && consolidated.summary != L"-") {
                companionMemory().summarize(consolidated.summary, now, message->summary_look, has_key_mem);
                saveCompanionMemory();
            }
            if (has_key_mem) {
                auto& self = companionMindState().of(message->summary_look);
                if (companion::mind::addKeyMemory(self, consolidated.key_memory, now)) saveCompanionMind();
            }
            if (grounded && !consolidated.reflection.empty() && consolidated.reflection != L"-") {
                companionCognition().reflect(consolidated.reflection, now, message->summary_look, consolidated.evidence_ids);
            }
            if (!consolidated.disposition.empty() && consolidated.disposition != L"-") {
                companionCognition().setDisposition(message->summary_look, consolidated.disposition);
            }
            if (grounded && !consolidated.schema.empty() && consolidated.schema != L"-") {
                companionCognition().addSchema(consolidated.schema, now, message->summary_look, consolidated.evidence_ids);
            }
            if (grounded) { social.reflected_through = std::max(social.reflected_through, message->reflection_cutoff); saveCompanionMind(); }
            if ((!consolidated.reflection.empty() && consolidated.reflection != L"-") ||
                (!consolidated.disposition.empty() && consolidated.disposition != L"-") ||
                (!consolidated.schema.empty() && consolidated.schema != L"-")) {
                saveCompanionCognition();
            }
            if (!consolidated.inside_joke.empty() && consolidated.inside_joke != L"-") {
                auto& self = companionMindState().of(message->summary_look);
                if (companion::mind::addSharedIdiom(self, consolidated.inside_joke, now)) saveCompanionMind();
            }
            // A new goal of her own, written in her diary: it starts now when she has none.
            if (!consolidated.goal.empty())
                if (const auto look = companion::lookFromKey(message->summary_look)) {
                    auto& self = companionMindState().of(message->summary_look);
                    if (companion::mind::aspire(self, companionMindContext(*look), consolidated.goal)) saveCompanionMind();
                }
            // Her plans for the next day: her life outside the chat (they happen, or fall through).
            if (!consolidated.plans.empty() && settings_.companion_memory) companionMindEpisodes(message->summary_look, consolidated.plans, true, 0);
        }
        return;
    }
    if (message->purpose == Purpose::weather) { // The real weather where she is: for her next replies.
        companion_.weather_busy = false;
        if (message->weather) companion_.weather = std::move(message->weather);
        return;
    }
    if (message->purpose == Purpose::reading) { // Her own web reading, in the background: for her next replies.
        companion_.reading_busy = false;
        if (message->memory_epoch != companion_.memory_epoch) return;
        if (!message->daily_readings.empty()) {
            auto& cog = companionCognition();
            for (const auto& rd : message->daily_readings) cog.readOnline(rd.look, rd.day, rd.topic, rd.summary, rd.at, rd.asked);
            saveCompanionCognition();
        }
        return;
    }
    if (message->purpose == Purpose::life) { // Her diary filled in what she did between talks.
        companion_.life_busy = false;
        if (message->memory_epoch != companion_.memory_epoch) return;
        if (!message->summary_look.empty() && settings_.companion_memory) {
            if (message->reply.ok)
                companionMindEpisodes(message->summary_look, message->reply.raw.empty() ? message->reply.text : message->reply.raw, false, message->life_from);
            // Filled in either way: a quiet stretch stays quiet (no asking again for it).
            auto& self = companionMindState().of(message->summary_look);
            self.lived_until = std::max(self.lived_until, message->life_to);
            saveCompanionMind();
        }
        return;
    }
    if (message->generation != companion_.generation) return;
    if (message->partial) { // Her words so far, while the reply streams in.
        if (settings_.companion && message->purpose == Purpose::chat && !companion_.live_chat && companion_.chat.waiting) companionStream(message->heard);
        return;
    }
    companion_.busy = false;
    if (!message->week.empty()) companion_.week = message->week; // For the rest of this conversation.
    if (!settings_.companion) return;
    if (!message->noted) { // The diagnostics: when it arrived, not when a roulette let it show.
        message->noted = true;
        CompanionDiagnostic entry;
        entry.purpose = message->purpose;
        entry.timing = message->timing;
        entry.total_ms = static_cast<uint32_t>(GetTickCount64() - message->timing.asked);
        entry.model = message->connected ? message->label : L"";
        entry.ok = message->reply.ok;
        entry.status = message->reply.status;
        entry.arrived = GetTickCount64();
        auto& recent = companion_.diagnostics;
        recent.push_back(std::move(entry));
        if (recent.size() > 6) recent.erase(recent.begin());
    }
    companionRemember(*message);
    if (!message->collection_mutations.empty()) companionApplyCollections(message->collection_mutations);
    if (message->reply.ok) companionAffinitySuggested(message->reply.games); // Gamification: her suggestions pay when played.
    if (message->purpose == Purpose::chat && message->tool_roulette) {
        auto roulette = std::move(*message->tool_roulette);
        message->tool_roulette.reset();
        companionFlush();
        companion_.chat.log.push_back({false, {}, {}, false, std::move(roulette), false, companionLook()});
        companion_.rig.emote(companion::Emotion::excited);
    }
    // The roulette still spinning: her verdict waits for the reel to stop (companion_roulette.cpp).
    if (message->purpose == Purpose::chat && companionRouletteSpinning()) { companion_.held = std::move(message); return; }
    const bool answered = message->reply.ok;
    const auto& reply = answered ? message->reply : message->fallback;
    if (!message->library.all().empty() || message->library.total || companion_.library.all().empty()) companion_.library = std::move(message->library);
    companion_.offline = !answered;
    // The header names the model only when asked to (Settings); problems are always told.
    const std::wstring provider = companion::providerName(message->provider);
    const auto& model = message->label;
    if (!model.empty()) companion_.model_label = model;
    if (answered) companion_.status.clear();
    else if (!message->connected)
        companion_.status = message->provider == companion::Provider::foundry ? std::wstring(tr(L"Offline — brak tokenu Foundry (Ustawienia → System)")) :
                            std::wstring(tr(L"Offline — brak klucza API: ")) + provider + tr(L" (Ustawienia → System)");
    else if (message->reply.status == 401 || message->reply.status == 403) companion_.status = provider + tr(L": klucz lub token odrzucony — sprawdź go w Ustawieniach");
    else if (message->reply.status == 404) companion_.status = model + tr(L": model niedostępny");
    else if (message->reply.status == 429) companion_.status = provider + tr(L": limit zapytań — spróbuj za chwilę");
    else if (message->reply.error == L"no model") companion_.status = provider + tr(L": brak modelu na serwerze");
    else if (message->reply.status && message->reply.status != 200) companion_.status = provider + L": HTTP " + std::to_wstring(message->reply.status);
    else if (message->reply.status == 200) companion_.status = provider + tr(L": pusta odpowiedź");
    else companion_.status = tr(L"Brak połączenia: ") + provider;
    const auto react = [&](bool chat) {
        const auto feeling = reply.emotions.empty() ? companion::calm : reply.emotions.front().emotion;
        companion_.rig.emote(feeling);
        if (companionVoiceFor(chat) && !reply.text.empty())
            companionSpeakLines({{companionLook(), reply.text, reply.emotions, reply.mentions, reply.cues}});
    };
    switch (message->purpose) {
    case Purpose::home: {
        const bool session_remark = std::exchange(companion_.bubble_session, false);
        companion_.bubble = {};
        companion_.bubble.text = reply.text; companion_.bubble.emotions = reply.emotions;
        companion_.bubble.mentions = reply.mentions;
        companion_.bubble.alpha = 1;
        react(false);
        // Her remark joins the conversation's context as something she said (it merges with her
        // last turn), so the chat does not contradict it; a session remark also appears in the chat
        // log with `free = true` so it does not count towards message_cap.
        if (session_remark && !reply.text.empty() && !companion_.live_chat && !companion_.chat.ended && !companion_.chat.waiting &&
            (!companion_.chat_look || *companion_.chat_look == companionLook())) {
            companion_ui::Message entry{false, reply.text, reply.emotions, false};
            entry.speaker = companionLook();
            entry.mentions = reply.mentions;
            entry.free = true;
            if (!companion_.chat.log.empty()) {
                companionFlush();
                companion_.chat.log.push_back(std::move(entry));
                companion_.chat.reveal = companion_.chat.log.back().text.size();
            }
        }
        if (answered && !reply.text.empty() && companion_.chat_look == companionLook() && !companion_.live_chat && !companion_.chat.ended &&
            !companion_.chat.log.empty() && !companion_.chat.waiting) {
            companion_.turns.push_back({false, reply.raw.empty() ? reply.text : reply.raw});
            companionSaveChat();
        }
        break;
    }
    case Purpose::test:
        if (answered) showNotification(NotificationKind::launch, tr(L"Połączono z modelem"), model + L" · " + std::to_wstring(message->elapsed_ms) + L" ms");
        else showNotification(NotificationKind::error, tr(L"Brak połączenia z modelem"), companion_.status +
                              (message->reply.error.empty() ? std::wstring{} : L"\n" + message->reply.error));
        react(true);
        if (!companionOpen()) {
            companion_.bubble = {};
            companion_.bubble.text = reply.text; companion_.bubble.emotions = reply.emotions; companion_.bubble.alpha = 1;
            companion_.bubble.mentions = reply.mentions;
        }
        break;
    default: {
        if (companion_.live_chat) companionCloseArchive(); // Her answer belongs to the live conversation.
        auto& chat = companion_.chat;
        // Her words already streaming in: the whole reply takes their place, and what is said stays said.
        const size_t streamed = companion_.streamed;
        const size_t in_log = std::min(chat.log.size(), streamed > chat.queue.size() ? streamed - chat.queue.size() : 0);
        companion_.streamed = 0; companion_.heard.clear();
        if (!streamed) companionFlush(); // Finish the previous line.
        chat.waiting = false;
        if (answered && !companion_.mood.empty()) companion_.mood_spent = true; // How she felt at the start: said once, in this reply.
        chat.hesitating = false;
        chat.hesitation_clock = 0;
        // Her reply as chat messages: two short ones now and then, both companions with a guest.
        auto& guest = companion_.guest;
        const auto host = companionLook();
        const auto with = message->guest && guest.look == message->guest ? message->guest : std::nullopt;
        // Alone, a question she ends with after something else is a message of its own (as in a messenger).
        auto said = companion::lines(reply.raw.empty() ? reply.text : reply.raw, host, with,
            with ? duo_messages : companion_.reply_length == companion::ReplyLength::brief ? 1 : solo_messages, !with);
        if (said.empty()) said.push_back({host, reply.text, reply.emotions, reply.mentions});
        const auto spoken_lines = said;
        const bool error = !answered && message->connected;
        const bool free_reply = std::exchange(companion_.next_reply_free, false);
        companion_.chat_quiet = 0;
        bool queued_first = false; // "Pisze…": even her first message waits behind the typing dots.
        if (streamed) {
            chat.queue.clear();
            const size_t keep = std::min(in_log, said.size());
            chat.log.erase(chat.log.end() - static_cast<std::ptrdiff_t>(in_log - keep), chat.log.end());
            for (size_t i = 0; i < said.size(); ++i) {
                companion_ui::Message entry{false, std::move(said[i].text), std::move(said[i].emotions), error};
                entry.speaker = said[i].speaker;
                entry.mentions = std::move(said[i].mentions);
                entry.free = free_reply;
                if (i < keep) chat.log[chat.log.size() - keep + i] = std::move(entry);
                else if (i == 0) { chat.log.push_back(std::move(entry)); chat.reveal = 0; chat.reveal_clock = 0; }
                else chat.queue.push_back(std::move(entry));
            }
            if (!chat.log.empty() && !chat.log.back().user) chat.reveal = std::min(chat.reveal, chat.log.back().text.size());
            chat.queue_clock = 0; chat.scroll = 0;
        } else {
            // "Pisze…": her whole reply waits behind the typing dots (the time the model took counts
            // as typing); with her voice speaking, the first message shows at once.
            const bool whole = settings_.companion_typing && !companionVoiceFor(true);
            for (auto& line : said) {
                companion_ui::Message entry{false, std::move(line.text), std::move(line.emotions), error};
                entry.speaker = line.speaker;
                entry.mentions = std::move(line.mentions);
                entry.free = free_reply;
                (!whole && &line == &said.front() ? chat.log : chat.queue).push_back(std::move(entry));
            }
            chat.reveal = 0; chat.reveal_clock = 0; chat.queue_clock = 0; chat.scroll = 0;
            if (whole && !chat.queue.empty()) {
                queued_first = true;
                const float waited = companion_.asked ? static_cast<float>(GetTickCount64() - companion_.asked) / 1000.0f : 0.0f;
                chat.queue_clock = std::clamp(waited, 0.0f, std::max(0.0f, wholeTypingSeconds(chat.queue.front().text) - 0.35f));
            }
        }
        // Her turn as she wrote it, hidden appraisal and thought too: she keeps her own format and her inner continuity.
        companion_.turns.push_back({false, !reply.full.empty() && !reply.appraisal.empty() ? reply.full : reply.raw.empty() ? reply.text : reply.raw});
        // Gamification: she set a boundary ([granica]); alone only — in a duo nobody knows whose it was.
        if (answered && reply.boundary && message->purpose == Purpose::chat && !with) companionBoundary();
        if (answered && message->purpose == Purpose::chat && !with) companionRelationshipShift(reply);
        if (answered && message->purpose == Purpose::chat && settings_.companion_notes &&
            companionMindState().of(message->reply_look).social.reflectionDue(companionMindContext(companionLook()).now)) requestCompanionSummary(true);
        companionChips(reply.games, reply.prompts);
        // A pad player lands on the first suggestion; a typist stays in the field.
        if (pad_input_ && chat.focus == companion_ui::focus_input && chat.input.empty() && !keyboard_.open && !chat.chips.empty()) chat.focus = 0;
        // A guest who dropped in steps in with her reply.
        if (with && !guest.leaving) guest.shown = true;
        // "Pisze…": her face changes when the message appears (the queue), not while she types.
        const auto& first = queued_first ? chat.queue.front() : chat.log.back();
        const auto feeling = first.emotions.empty() ? companion::calm : first.emotions.front().emotion;
        if (!queued_first) companionRig(first.speaker).emote(feeling);
        // Micro-expression: if her hidden subconscious feeling differs from what she shows out loud,
        // her face betrays it for a brief moment first.
        if (reply.subconscious_emotion && *reply.subconscious_emotion != feeling && !with)
            companionRig(first.speaker).flash(*reply.subconscious_emotion, 0.55f);
        const bool voiced = companionVoiceFor(true) && !spoken_lines.empty();
        if (voiced) companionSpeakLines(spoken_lines);
        if (!companion_.diagnostics.empty()) { // How long her typewriter (or "Pisze…" dots) takes, and whether her voice holds it.
            auto& noted = companion_.diagnostics.back();
            noted.messages = (queued_first ? 0 : 1) + chat.queue.size();
            noted.chars = queued_first ? 0 : first.text.size();
            noted.typing_seconds = queued_first ? 0 : typingSeconds(first.text);
            for (const auto& next : chat.queue) {
                noted.chars += next.text.size();
                noted.typing_seconds += settings_.companion_typing ? wholeTypingSeconds(next.text) : typingSeconds(next.text) + typingPause(next);
            }
            noted.voiced = voiced && companion_.voice.pending;
            noted.arrived = GetTickCount64();
        }
        // Her answer at the conversation's break threshold: she said goodbye; she rests now.
        if (companion_.closing) companionEnd(false);
        else companionSaveChat();
        break;
    }
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::openCompanion() {
    if (!companionAvailable()) return;
    rememberPlace(); cancelEnterHold();
    companion_.hover = false; companion_.hover_motion.snap(0);
    screen_ = ScreenMode::companion;
    auto& chat = companion_.chat;
    chat.age = 0; chat.pose_age = 0; chat.hover = companion_ui::no_hit; chat.scroll = 0; chat.scroll_motion.snap(0);
    chat.focus = pad_input_ && !chat.chips.empty() ? 0 : companion_ui::focus_input;
    chat.menu = false; chat.confirm = false; chat.viewer = false; chat.viewer_motion = 0; chat.feed = false;
    chat.mind = false; chat.mind_motion = 0; chat.mind_dragging = false;
    companion_.bubble.alpha = 0; companion_.bubble.waiting = false;
    companionLoadChat(); // Today's conversation with her, kept between starts; a new day, a new one.
    if (chat.log.empty() && !chat.waiting) companionBeginAwake();
    else if (chat.waiting && !companion_.busy) requestCompanion(CompanionMessage::Purpose::chat); // A remark took the worker.
    if (chat.ended) chat.focus = !companionResting() ? companion_ui::focus_send : pad_input_ && !chat.chips.empty() ? 0 : companion_ui::focus_back;
    audio_.quick(); InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::companionBeginAwake() {
    if (!companionAsleep(companionLook())) { companionBegin(); return; }
    // Her night: nobody opens the conversation; the chat waits for her morning (tickCompanionInitiative).
    auto& chat = companion_.chat;
    chat.ended = true;
    chat.focus = companion_ui::focus_back;
    companion_.closing = false;
    companion_.rig.emote(companion::Emotion::sleepy, false);
}

void Launcher::companionBegin() {
    companionPhotoChat(); // A new conversation: when (if at all) she sends a photo.
    const uint32_t seed = companion_.seed = companion_.seed * 1664525u + 1013904223u + static_cast<uint32_t>(GetTickCount64());
    companion_.typed = 0;
    companion_.chat_quiet = 0;
    companion_.chat_nudge_after = 75.0f + static_cast<float>((seed >> 12) % 50);
    companion_.chat_nudges = 0;
    companion_.next_reply_free = false;
    companion_.guest_after = 4 + static_cast<int>((seed >> 5) % 3); // A guest may drop in after the 4th–6th message.
    companion_.burst_in = 3 + static_cast<int>((seed >> 9) % 2);
    // A conversation of its own (companion_chats.hpp): named after when it started (never the same second twice).
    companion_.chat_look = companionLook();
    companion_.chat_started = companion_.chat_updated = std::max(companionNow(), companion_.chat_started + 1);
    companion_.closing = false; companion_.chat.ended = false;
    if (companion_.persist) // Her oldest conversations go.
        work_.start([config = userConfigDirectory(), look = companionLook(), io = companion_.chat_io] {
            std::lock_guard lock(io->mutex);
            companion::chats::prune(config, look, companion::chats::keep);
        });
    // How she feels as it starts (memory on): she missed him, is jealous (as far as their stage and her
    // stance allow), pleased with a played suggestion or worried after a late night — said once, in
    // her first reply. The week is looked at by its first request.
    companion_.mood.clear(); companion_.week.clear(); companion_.week_asked = false; companion_.mood_spent = false;
    if (settings_.companion_gamification) companionAffinity(); // Her level and stage, before she feels anything about him.
    if (settings_.companion_memory && companion_.persist) {
        const auto look = companionLook();
        const companion::mood::Closeness closeness{static_cast<int>(companionStage(look)), static_cast<int>(companionJealousy(look))};
        companion_.mood = companion::mood::begin(companionMoodState(), companion::lookKey(look), companion_.chat_started, english(), [](std::string_view key) {
            const auto other = companion::lookFromKey(key);
            return other ? std::wstring(companion::name(*other)) : text_encoding::wideLenient(key);
        }, closeness);
        saveCompanionMood();
    }
    companionMindBegin(); // Her mind catches up with the time apart; another companion may have let something slip.
    // She opens the conversation: a natural first-acquaintance greeting when meeting for the very first time, else her usual opening.
    const auto bond = companionBond();
    companion_.turns = {{true, companion::kickoff(companion::Kickoff::chat, english(), -1, bond.first_meet, bond.known_companion)}};
    companion_.summarized = 0;
    companion_.chat.waiting = true;
    requestCompanion(CompanionMessage::Purpose::chat);
    companion_.rig.emote(companion::Emotion::smile);
}

void Launcher::closeCompanion() {
    if (!companionOpen()) return;
    closeKeyboard();
    companion_.chat.feed = false;
    if (companion_.live_chat) companionCloseArchive(); // A history page closes back to the live conversation.
    companion_.chat.menu = false; companion_.chat.confirm = false; companion_.chat.mind = false; companion_.chat.mind_dragging = false;
    companionFlush();
    companionSaveChat();
    companion_.rig.silence();
    companionHush(); // Her chat line ends with the chat.
    companionGuestGone(true); // A guest leaves with the chat.
    if (companion_.typed > 0) companionMindEnd(); // A player who comes daily is expected back tomorrow.
    requestCompanionSummary(); // Notes on: the talk in a sentence for next time.
    showCategory(category_);
}

void Launcher::companionReset(bool keep_screen) {
    companionFeedReset();
    companion_.cancel->store(true);
    ++companion_.generation;
    companion_.busy = false;
    companion_.live_chat.reset(); companion_.live_photo.reset();
    companion_.picker = {}; companion_.link_pointer.reset();
    companion_.chat = {}; companion_.turns.clear(); companion_.bubble = {}; companion_.summarized = 0;
    companion_.streamed = 0; companion_.heard.clear();
    companion_.mood.clear(); companion_.week.clear(); companion_.week_asked = false; companion_.mood_spent = false;
    // Her conversation is read again from disk on the next opening.
    companion_.chat_look.reset(); companion_.chat_started = companion_.chat_updated = companion_.rest_until = 0; companion_.closing = false;
    companion_.photo_file.clear(); companion_.photo_file_pending.clear();
    companionGuestGone(false); companion_.guest = {}; companion_.typed = 0;
    companion_.chat_quiet = 0; companion_.chat_nudges = 0; companion_.next_reply_free = false;
    ++companion_.photo_chat; companion_.photo.reset(); companion_.photo_pending.reset(); companion_.photo_description.clear(); // A photo on its way belongs to the old chat.
    companion_.library = {}; companion_.held.reset();
    companion_.events.clear();
    companionHush();
    if (companionOpen() && !keep_screen) showCategory(category_);
}

void Launcher::companionMove(int dx, int dy) {
    companion_.link_pointer.reset();
    auto& chat = companion_.chat;
    if (chat.feed) { companionFeedMove(dx, dy); return; }
    if (chat.viewer) { navigationBoundary(); return; } // Her enlarged photo: only closing it.
    if (chat.mind) {
        if (chat.mind_confirm) {
            const int step = dx ? dx : dy;
            const int next = std::clamp(chat.mind_confirm_focus + step, companion_ui::confirm_reset, companion_ui::confirm_cancel);
            if (!step || next == chat.mind_confirm_focus) navigationBoundary();
            else { chat.mind_confirm_focus = next; audio_.navigate(); }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        const auto nodes = companionMindNodes();
        const int count = static_cast<int>(nodes.size());
        const int step = dx ? dx : dy;
        if (!step || count <= 0) { navigationBoundary(); return; }
        int next = chat.mind_selected;
        bool found = false;
        for (int i = 0; i < count; ++i) {
            next = (next + (step > 0 ? 1 : -1) + count) % count;
            if (nodes[static_cast<size_t>(next)].matches_filter) { found = true; break; }
        }
        if (!found) { navigationBoundary(); return; }
        chat.mind_selected = next;
        const auto& node = nodes[static_cast<size_t>(chat.mind_selected)];
        if (std::abs(node.x) > 0.01f || std::abs(node.z) > 0.01f) chat.mind_yaw = -std::atan2(node.x, -node.z);
        chat.mind_pitch = std::clamp(std::asin(std::clamp(node.y, -0.95f, 0.95f)) * 0.55f, -0.65f, 0.65f);
        static constexpr std::array<companion::Emotion, 6> node_emotions{
            companion::Emotion::smug, companion::Emotion::playful, companion::Emotion::thinking,
            companion::Emotion::smile, companion::Emotion::excited, companion::Emotion::gaming};
        companion_.rig.emote(node.emotion.value_or(node_emotions[static_cast<size_t>(node.kind) % node_emotions.size()]));
        audio_.navigate();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    chat.hover = companion_ui::no_hit;
    if (chat.confirm) { // The reset dialog: Left/Right (or Up/Down) between its two buttons.
        const int step = dx ? dx : dy;
        const int next = std::clamp(chat.confirm_focus + step, companion_ui::confirm_reset, companion_ui::confirm_cancel);
        if (!step || next == chat.confirm_focus) navigationBoundary();
        else { chat.confirm_focus = next; audio_.navigate(); }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (chat.menu) { // Up/Down through the menu's rows; her costume tiles also Left/Right, a row of tiles per Up/Down.
        const int count = static_cast<int>(chat.menu_items.size());
        int next = chat.menu_focus;
        if (companion_ui::Layout::wardrobe(chat)) {
            const int columns = companion_ui::Layout::wardrobeColumns(chat);
            const int moved = chat.menu_focus + (dx ? dx : dy * columns);
            const bool same_row = !dx || moved / columns == chat.menu_focus / columns;
            if (moved >= 0 && moved < count && same_row) next = moved;
        } else next = std::clamp(chat.menu_focus + dy, 0, std::max(0, count - 1));
        if ((!dx && !dy) || next == chat.menu_focus) navigationBoundary();
        else { chat.menu_focus = next; audio_.navigate(); }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (chat.archive) { navigationBoundary(); return; } // A history page: only Back (and scrolling).
    const auto view = renderer_.viewport();
    const companion_ui::Layout layout{view.width, view.height};
    const int count = static_cast<int>(chat.chips.size());
    const int old = chat.focus;
    if (dy) {
        // Rows from the top: Back with "…", the chip rows, the field with Send.
        const auto rects = layout.chips(chat);
        const int rows = count ? layout.chipRow(chat, count - 1) + 1 : 0;
        const int row = old == companion_ui::focus_back || old == companion_ui::focus_more || old == companion_ui::focus_feed ? -1 : old >= 0 ? layout.chipRow(chat, old) : rows;
        const int next = std::clamp(row + dy, -1, rows);
        if (next != row) {
            if (next == -1) {
                // The top row's button on that side.
                const float x = old >= 0 && old < count ? (rects[old].left + rects[old].right) * 0.5f : old == companion_ui::focus_send ? layout.send().left : layout.input().left;
                chat.focus = x > (layout.left() + layout.right()) * 0.5f ? companion_ui::focus_more : companion_ui::focus_back;
            }
            else if (next == rows) chat.focus = companion_ui::focus_input;
            else {
                // The chip of that row nearest to the current horizontal position.
                const float x = old >= 0 && old < count ? (rects[old].left + rects[old].right) * 0.5f :
                                old == companion_ui::focus_send ? layout.send().left : layout.input().left + 60;
                int best = -1; float distance = 1e9f;
                for (int i = 0; i < count; ++i) if (layout.chipRow(chat, i) == next) {
                    const float d = std::abs((rects[i].left + rects[i].right) * 0.5f - x);
                    if (d < distance) { distance = d; best = i; }
                }
                if (best >= 0) chat.focus = best;
            }
        }
    } else if (dx) {
        if (old >= 0) {
            const int next = old + dx;
            if (next >= 0 && next < count && layout.chipRow(chat, next) == layout.chipRow(chat, old)) chat.focus = next;
        } else if (old == companion_ui::focus_input && dx > 0) chat.focus = companion_ui::focus_send;
        else if (old == companion_ui::focus_send && dx < 0) chat.focus = companion_ui::focus_input;
        else if (old == companion_ui::focus_back && dx > 0) chat.focus = companion_ui::focus_feed;
        else if (old == companion_ui::focus_feed) chat.focus = dx > 0 ? companion_ui::focus_more : companion_ui::focus_back;
        else if (old == companion_ui::focus_more && dx < 0) chat.focus = companion_ui::focus_feed;
    }
    if (old == chat.focus) navigationBoundary(); else audio_.navigate();
    if (keyboard_.open && chat.focus != companion_ui::focus_input) closeKeyboard();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::companionAccept() {
    auto& chat = companion_.chat;
    if (chat.feed) {
        auto& feed = companion_.feed.view;
        if (!feed.ready()) return;
        if (!feed.thread.empty()) companionFeedComment();
        else {
            const int index = companion_ui::FeedLayout::visible(feed.archive, feed.motion.value + 160, feed.thread).first;
            companionFeedAction(index * 3 + feed.focus);
        }
        return;
    }
    if (chat.viewer) { companionViewer(false); return; } // Enter or A closes her enlarged photo.
    if (chat.mind && chat.mind_confirm) { companionMindDeletePick(chat.mind_confirm_focus == companion_ui::confirm_reset); return; }
    if (chat.mind) { // Enter or A shows (or masks) a selected hidden thought, else closes the 3D mind screen.
        const auto nodes = companionMindNodes();
        const int idx = std::clamp(chat.mind_selected, 0, std::max(0, static_cast<int>(nodes.size()) - 1));
        if (!nodes.empty() && nodes[static_cast<size_t>(idx)].revealable) companionMindReveal();
        else companionMind(false);
        return;
    }
    if (chat.confirm) { companionResetPick(chat.confirm_focus == companion_ui::confirm_reset); return; }
    if (chat.menu) { companionMenuPick(chat.menu_focus); return; }
    if (chat.focus == companion_ui::focus_back) { back(); return; }
    if (chat.archive) { navigationBoundary(); return; }
    if (chat.focus == companion_ui::focus_more) { companionMenu(true); return; }
    if (chat.focus == companion_ui::focus_feed) { companionFeed(true); return; }
    if (chat.ended && chat.focus == companion_ui::focus_send) { companionRestart(); return; } // "Nowa rozmowa".
    if (chat.ended && chat.focus == companion_ui::focus_input) { navigationBoundary(); return; }
    if (chat.focus == companion_ui::focus_input) {
        if (!chat.input.empty()) { companionSend(chat.input); return; }
        if (pad_input_ && !keyboard_.open) openKeyboard(); // A on the empty field: the on-screen keyboard.
        return;
    }
    if (chat.focus == companion_ui::focus_send) { companionSend(chat.input); return; }
    if (chat.focus < 0 || chat.focus >= static_cast<int>(chat.chips.size())) return;
    const auto chip = chat.chips[static_cast<size_t>(chat.focus)];
    switch (chip.kind) {
    case companion_ui::Chip::Kind::game: {
        const auto found = std::find_if(public_library_.begin(), public_library_.end(), [&](const Game& game) { return gameKey(game) == chip.game; });
        if (found == public_library_.end()) { navigationBoundary(); return; }
        const Game game = *found;
        companion_.rig.emote(companion::Emotion::excited);
        closeCompanion();
        launchGame(game);
        return;
    }
    case companion_ui::Chip::Kind::roulette: companionRoulette(); return;
    case companion_ui::Chip::Kind::prompt: companionSend(chip.prompt); return;
    }
}

void Launcher::companionSend(std::wstring text) {
    auto& chat = companion_.chat;
    text = trimmed(text);
    if (text.empty() || chat.waiting || chat.confirm || chat.mind || chat.feed || chat.ended || chat.archive || companionRouletteSpinning()) { navigationBoundary(); return; }
    if (text.size() > 600) text.resize(600);
    companionFlush();
    const bool en = english();
    // Back after a while (the chat was kept): she hears how long the player was away.
    const auto now = companionNow();
    if (companion_.chat_updated && now > companion_.chat_updated + companion::chats::return_gap)
        companion_.turns.push_back({true, companion::chats::returnNote(now - companion_.chat_updated, en)});
    companion_.chat_updated = now;
    chat.log.push_back({true, text, {}, false});
    if (!companion_.feed.reference.empty()) {
        companion_.turns.push_back({true, std::exchange(companion_.feed.reference, std::wstring{})});
        // Persist the reference in the visible transcript too, so an archived question is unambiguous.
        companion_ui::Message reference;
        reference.text = std::exchange(companion_.feed.reference_quote, std::wstring{});
        reference.user = true; reference.free = true;
        chat.log.insert(chat.log.end() - 1, std::move(reference));
    }
    companion_.turns.push_back({true, text});
    // Every message counts towards the conversation's cap: on the last one she answers and says
    // goodbye. Past her bedtime (by her clock) this answer is her goodnight.
    const bool bedtime = companionBedtime();
    const bool last = companionMessages() + 1 >= companionMessageLimit() || companionAsleep(companionLook());
    if (last) {
        companion_.closing = true;
        companion_.turns.push_back({true, companion::chats::farewellKickoff(en, bedtime)});
        companion_.guest_after = -1; // Nobody drops in on a goodbye.
    }
    // His or her gender from how he/she writes (Settings → "Twoja płeć" still wins when set).
    if (settings_.companion) {
        auto& state = companionAffinity();
        const auto before = state.player_gender;
        const auto folded = game_index::fold(text);
        std::optional<companion::Gender> explicit_gender;
        for (const auto* w : {L"jestem facetem", L"jestem chlopakiem", L"jestem mezczyzna", L"i am a guy", L"im a guy", L"i am a man", L"im a man"})
            if (folded.find(w) != std::wstring::npos) explicit_gender = companion::Gender::male;
        for (const auto* w : {L"jestem dziewczyna", L"jestem kobieta", L"i am a girl", L"im a girl", L"i am a woman", L"im a woman"})
            if (folded.find(w) != std::wstring::npos) explicit_gender = companion::Gender::female;
        if (explicit_gender && state.player_gender != *explicit_gender) {
            state.player_gender = *explicit_gender;
            saveCompanionAffinity();
        } else if (state.player_gender == companion::Gender::unknown) {
            int male = 0, female = 0;
            for (const auto* w : {L"gralem", L"zrobilem", L"bylem", L"widzialem", L"chcialem", L"kupilem", L"wrocilem", L"myslalem", L"skonczylem", L"poszedlem"})
                if (folded.find(w) != std::wstring::npos) ++male;
            for (const auto* w : {L"gralam", L"zrobilam", L"bylam", L"widzialam", L"chcialam", L"kupilam", L"wrocilam", L"myslalam", L"skonczylam", L"poszlam"})
                if (folded.find(w) != std::wstring::npos) ++female;
            if (male > female) { state.player_gender = companion::Gender::male; saveCompanionAffinity(); }
            else if (female > male) { state.player_gender = companion::Gender::female; saveCompanionAffinity(); }
        }
        if (state.player_gender != before) companionSyncPerson();
    }
    // Gamification on: talking to her raises her sympathy, the day's first message more (a streak
    // of days in a row more still); a guest present in the chat gets a small sympathy bonus too.
    if (settings_.companion && settings_.companion_gamification) {
        auto& state = companionAffinity();
        const int64_t daily = state.visit(dateKey(companion_.clock()));
        if (daily) {
            saveCompanionAffinity();
            showNotification(NotificationKind::launch, companion::name(companionLook()) + std::wstring(L" · +") + std::to_wstring(daily) + L" " + tr(L"pkt sympatii"),
                             state.streak >= 2 ? std::wstring(tr(L"Seria: ")) + std::to_wstring(state.streak) + tr(L" dni z rzędu") : std::wstring(tr(L"Pierwsza rozmowa dziś")));
        }
        companionAffinityGain(companion::affinity::per_message + daily);
        if (companion_.guest.look && *companion_.guest.look != companionLook())
            companionAffinityGainFor(*companion_.guest.look, std::max<int64_t>(1, companion::affinity::per_message / 2));
    }
    if (settings_.companion && settings_.companion_memory && companion_.persist && companion_.guest.look && *companion_.guest.look != companionLook()) {
        companionMoodState().note(std::string(companion::lookKey(*companion_.guest.look)), companion::mood::Kind::duo, now, companion::name(companionLook()));
        saveCompanionMood();
        // The others hear of the three-way chat (gossip among the companions).
        const auto unix_now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        companionCognition().postGossip(companion::lookKey(*companion_.guest.look), "duo", L"", unix_now, companion::lookKey(companionLook()));
        saveCompanionCognition();
    }
    chat.input.clear(); chat.waiting = true; chat.scroll = 0; chat.chips.clear();
    companion_.chat_quiet = 0; companion_.next_reply_free = false;
    chat.focus = companion_ui::focus_input; chat.menu = false;
    closeKeyboard();
    companionMindHeard(text); // System 1: her face reacts at once, then she thinks about her answer.
    companion_.rig.silence(); companion_.guest.rig.silence();
    std::wstring fallback = companionGuestTyped(); // A guest may drop in now, or run off.
    if (last && fallback.empty()) fallback = companion::chats::farewellLine(companionPersona(), en, companion_.seed, bedtime); // Offline, she says goodbye too.
    requestCompanion(CompanionMessage::Purpose::chat, {}, std::move(fallback), text);
    if (!last) companionPhotoTyped(); // Her photo, if this is the message after which she sends one.
    companionSaveChat();
    audio_.context(); InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::companionQuick() {
    if (companion_.chat.mind && !companion_.chat.mind_confirm) { companionMindNextFilter(); return; }
    if (companion_.chat.confirm || companion_.chat.viewer || companion_.chat.mind || companion_.chat.feed) { navigationBoundary(); return; } // The reset dialog or overlay waits.
    companionSend(tr(L"W co mam zagrać?"));
}

void Launcher::companionType(wchar_t character) {
    auto& chat = companion_.chat;
    if (chat.feed) {
        auto& feed = companion_.feed.view;
        if (feed.typing() && character >= L' ' && character != 127 && feed.input.size() < 300 &&
            !(character >= 0xd800 && character <= 0xdbff && feed.input.size() == 299)) feed.input += character;
        InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    if (character < L' ' || character == 127 || chat.input.size() >= 600 || chat.viewer || chat.mind || chat.feed ||
        (character >= 0xd800 && character <= 0xdbff && chat.input.size() == 599)) return;
    chat.input += character;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool Launcher::companionPasteKey(WPARAM key, bool repeated) {
    if (!companionTyping() || topModal() != Modal::none || companion_.chat.menu || companion_.chat.confirm ||
        (browser_ && browser_->isOpen()) || (GetKeyState(VK_MENU) & 0x8000)) return false;
    const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0, shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    if (!((key == 'V' && control) || (key == VK_INSERT && shift && !control))) return false;
    if (!repeated) companionPaste();
    return true;
}

void Launcher::companionPaste() {
    if (!companionTyping() || topModal() != Modal::none || companion_.chat.menu || companion_.chat.confirm ||
        (browser_ && browser_->isOpen())) return;
    auto& input = companion_.chat.feed ? companion_.feed.view.input : companion_.chat.input;
    const size_t limit = companion_.chat.feed ? 300 : 600;
    if (input.size() >= limit) return;
    // Clipboard history/managers can briefly lock it just after a copy; retry for at most 40 ms.
    bool opened = false;
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (OpenClipboard(hwnd_)) { opened = true; break; }
        if (attempt < 4) Sleep(10);
    }
    if (!opened) return;
    std::wstring pasted;
    const size_t room = limit - input.size();
    if (const auto handle = GetClipboardData(CF_UNICODETEXT)) {
        if (const auto* text = static_cast<const wchar_t*>(GlobalLock(handle))) {
            // Bound even malformed/non-terminated clipboard data; flatten lines without sending
            // the message and never split a UTF-16 pair at the input's character limit.
            const size_t capacity = std::min<size_t>(GlobalSize(handle) / sizeof(wchar_t), 65536);
            for (size_t i = 0; i < capacity && text[i] && pasted.size() < room; ++i) {
                wchar_t ch = text[i];
                if (ch == L'\r') { if (i + 1 < capacity && text[i + 1] == L'\n') ++i; ch = L' '; }
                else if (ch == L'\n' || ch == L'\t') ch = L' ';
                if (ch < L' ' || ch == 127) continue;
                if (ch >= 0xd800 && ch <= 0xdbff) {
                    if (i + 1 >= capacity || text[i + 1] < 0xdc00 || text[i + 1] > 0xdfff) continue;
                    if (room - pasted.size() < 2) break;
                    pasted += ch; pasted += text[++i];
                } else if (ch < 0xdc00 || ch > 0xdfff) pasted += ch;
            }
            GlobalUnlock(handle);
        }
    }
    CloseClipboard();
    if (pasted.empty()) return;
    input += pasted;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::companionErase() {
    auto& chat = companion_.chat;
    if (chat.feed) {
        auto& feed = companion_.feed.view;
        if (feed.typing() && !feed.input.empty()) {
            const wchar_t last = feed.input.back(); feed.input.pop_back();
            if (last >= 0xdc00 && last <= 0xdfff && !feed.input.empty() && feed.input.back() >= 0xd800 && feed.input.back() <= 0xdbff) feed.input.pop_back();
        }
        InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    if (!chat.input.empty() && !chat.viewer && !chat.mind && !chat.feed) {
        const wchar_t last = chat.input.back(); chat.input.pop_back();
        if (last >= 0xdc00 && last <= 0xdfff && !chat.input.empty() && chat.input.back() >= 0xd800 && chat.input.back() <= 0xdbff) chat.input.pop_back();
    } else navigationBoundary();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::companionScroll(int amount) {
    auto& chat = companion_.chat;
    if (chat.feed) { companionFeedScroll(-amount * 150.0f); return; }
    if (chat.mind) {
        chat.mind_yaw += amount * 0.22f;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    chat.scroll = std::clamp(chat.scroll + amount * 120.0f, 0.0f, renderer_.companionScrollLimit());
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::companionWheel(int delta) {
    auto& chat = companion_.chat;
    if (chat.feed) { companionFeedScroll(-static_cast<float>(delta) / WHEEL_DELTA * 130.0f); return; }
    if (!chat.mind) { if (delta) companionScroll(delta < 0 ? -1 : 1); return; }
    if (chat.mind_confirm || !delta) return;
    // Up zooms in, about 12% a notch (a touchpad's finer steps in proportion), from fitted to 200%.
    const float zoomed = std::clamp(chat.mind_zoom * std::pow(1.12f, static_cast<float>(delta) / WHEEL_DELTA), 1.0f, companion_ui::Layout::mind_zoom_max);
    if (zoomed == chat.mind_zoom) return;
    chat.mind_zoom = zoomed;
    if (!settings_.screen_transitions) chat.mind_zoom_spring.snap(zoomed);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool Launcher::companionPhotoShown() const {
    const auto& log = companion_.chat.log;
    return companion_.photo && std::any_of(log.begin(), log.end(), [](const companion_ui::Message& message) { return message.photo; });
}

void Launcher::companionViewer(bool open) {
    auto& chat = companion_.chat;
    if (chat.viewer == open) return;
    if (open && !companionPhotoShown()) { navigationBoundary(); return; }
    chat.viewer = open;
    chat.hover = companion_ui::no_hit;
    if (open) { closeKeyboard(); chat.menu = false; audio_.context(); }
    else audio_.navigate();
    if (!settings_.screen_transitions) chat.viewer_motion = open ? 1.0f : 0.0f;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::companionMind(bool open) {
    auto& chat = companion_.chat;
    if (chat.mind == open) return;
    const bool return_to_sidebar = !open && chat.mind_from_sidebar;
    chat.mind = open;
    chat.mind_from_sidebar = false;
    chat.mind_confirm = false;
    chat.hover = companion_ui::no_hit;
    chat.mind_hover = companion_ui::no_hit;
    chat.mind_dragging = false;
    if (return_to_sidebar) {
        chat.mind_motion = 0.0f;
        closeCompanion();
        return;
    }
    if (open) {
        closeKeyboard();
        chat.menu = false;
        chat.viewer = false;
        chat.mind_selected = 0;
        // Her hidden thoughts are masked again, and the sphere builds itself anew (at once without
        // screen transitions).
        chat.mind_revealed.clear();
        chat.mind_live_revealed = false;
        chat.mind_age = settings_.screen_transitions ? 0.0f : companion_ui::Layout::mind_build_seconds;
        chat.mind_yaw = 0.35f;
        chat.mind_pitch = 0.18f;
        chat.mind_yaw_spring.snap(chat.mind_yaw);
        chat.mind_pitch_spring.snap(chat.mind_pitch);
        chat.mind_zoom = 1;
        chat.mind_zoom_spring.snap(1);
        if (!companion_.memory) companionMemory();
        if (!companion_.cognition) companionCognition();
        if (!companion_.affinity) companionAffinity();
        audio_.context();
    } else audio_.navigate();
    if (!settings_.screen_transitions) chat.mind_motion = open ? 1.0f : 0.0f;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::companionMindDeleteConfirm(bool open) {
    auto& chat = companion_.chat;
    if (!chat.mind || chat.mind_confirm == open) return;
    if (open) {
        const auto nodes = companionMindNodes();
        const int idx = std::clamp(chat.mind_selected, 0, std::max(0, static_cast<int>(nodes.size()) - 1));
        if (nodes.empty() || !(nodes[static_cast<size_t>(idx)].deletable || nodes[static_cast<size_t>(idx)].locked)) { navigationBoundary(); return; }
        chat.mind_confirm = true;
        chat.mind_confirm_focus = companion_ui::confirm_cancel; // Default on Anuluj so accidental Enter keeps the memory.
        chat.mind_hover = companion_ui::no_hit;
        chat.mind_dragging = false;
        audio_.context();
    } else {
        chat.mind_confirm = false;
        audio_.navigate();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::companionMindDeletePick(bool remove) {
    auto& chat = companion_.chat;
    if (!chat.mind_confirm) return;
    chat.mind_confirm = false;
    if (remove) {
        const auto nodes = companionMindNodes();
        const int idx = std::clamp(chat.mind_selected, 0, std::max(0, static_cast<int>(nodes.size()) - 1));
        // Odkryj: her private matter opens for the player (she does not know he looked).
        if (!nodes.empty() && nodes[static_cast<size_t>(idx)].locked) {
            if (companionMindPeek(nodes[static_cast<size_t>(idx)].entry_key))
                showNotification(NotificationKind::launch, tr(L"Umysł i pamięć"), tr(L"Odkryte — ona nie wie, że zajrzałeś"));
            audio_.context();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        if (!nodes.empty() && nodes[static_cast<size_t>(idx)].deletable) {
            const auto& node = nodes[static_cast<size_t>(idx)];
            auto& mem = companionMemory();
            bool removed = false;
            using Kind = CompanionView::MindNode::Kind;
            if (node.kind == Kind::note) removed = mem.removeNote(node.entry_at, node.detail);
            else if (node.kind == Kind::summary) removed = mem.removeSummary(node.entry_at, node.detail);
            else if (node.kind == Kind::pick) removed = mem.removePick(node.entry_key, node.entry_at);
            else if (node.kind == Kind::reflection) {
                removed = companionCognition().removeReflection(node.entry_at, node.detail) ||
                          companionCognition().removeSchema(node.entry_at, node.detail);
                if (removed) saveCompanionCognition();
            } else if (node.kind == Kind::thought) {
                removed = companionCognition().removeThought(node.entry_at, node.detail);
                if (removed) saveCompanionCognition();
            } else if (node.kind == Kind::reading) {
                removed = companionCognition().removeReading(node.entry_at, node.detail);
                if (removed) saveCompanionCognition();
            }
            if (removed) {
                if (node.kind == Kind::note || node.kind == Kind::summary || node.kind == Kind::pick) saveCompanionMemory();
                const auto remaining = companionMindNodes();
                chat.mind_selected = std::clamp(chat.mind_selected, 0, std::max(0, static_cast<int>(remaining.size()) - 1));
                showNotification(NotificationKind::launch, tr(L"Umysł i pamięć"), tr(L"Wspomnienie usunięte"));
            }
        }
        audio_.context();
    } else audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

namespace {
std::pair<uint64_t, size_t> thoughtKey(uint64_t at, std::wstring_view text) { return {at, std::hash<std::wstring_view>{}(text)}; }
}

bool Launcher::companionThoughtShown(uint64_t at, std::wstring_view text) const {
    const auto& shown = companion_.chat.mind_revealed;
    return std::find(shown.begin(), shown.end(), thoughtKey(at, text)) != shown.end();
}

void Launcher::companionMindReveal() {
    auto& chat = companion_.chat;
    if (!chat.mind || chat.mind_confirm) return;
    const auto nodes = companionMindNodes();
    const int idx = std::clamp(chat.mind_selected, 0, std::max(0, static_cast<int>(nodes.size()) - 1));
    if (nodes.empty() || !nodes[static_cast<size_t>(idx)].revealable) { navigationBoundary(); return; }
    const auto& node = nodes[static_cast<size_t>(idx)];
    const auto key = thoughtKey(node.entry_at, node.detail);
    auto& shown = chat.mind_revealed;
    if (const auto found = std::find(shown.begin(), shown.end(), key); found != shown.end()) { shown.erase(found); audio_.navigate(); }
    else {
        shown.push_back(key);
        // Caught: a flustered moment, then what she really felt shows on her face.
        companion_.rig.flash(companion::Emotion::shy, 0.8f, node.emotion.value_or(companion::Emotion::shy));
        audio_.context();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::companionMindLiveReveal() {
    auto& chat = companion_.chat;
    if (!chat.mind || chat.mind_confirm) return;
    chat.mind_live_revealed = !chat.mind_live_revealed;
    chat.mind_live_revealed ? audio_.context() : audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

std::vector<CompanionView::MindNode> Launcher::companionMindNodes() const {
    using Node = CompanionView::MindNode;
    using Kind = Node::Kind;
    std::vector<Node> nodes;
    const auto look = companionLook();
    const auto look_key = std::string(companion::lookKey(look));
    const bool en = english();
    const auto* c = companion::character(look);
    const auto now_sec = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    const auto ago = [&](uint64_t at) -> std::wstring {
        if (!at || now_sec < at) return en ? L"today" : L"dziś";
        const uint64_t seconds = now_sec - at, d = seconds / 86400;
        if (seconds < 3600) return en ? L"just now" : L"przed chwilą";
        if (seconds < 86400) return std::to_wstring(seconds / 3600) + (en ? L" h ago" : L" h temu");
        if (en) return d == 1 ? L"yesterday" : std::to_wstring(d) + L" days ago";
        return d == 1 ? L"wczoraj" : std::to_wstring(d) + L" dni temu";
    };

    // 0: Core node at the center of the sphere (includes her long-term emotional disposition towards the player).
    {
        Node core;
        core.kind = Kind::core;
        core.label = std::wstring(companion::name(look)) + L" · " + companion::personaName(companionPersonaOf(look));
        core.when = core.category = en ? L"Personality core" : L"Rdzeń osobowości";
        core.detail = c ? (en ? (c->card_en.empty() ? c->card_pl : c->card_en) : c->card_pl) : core.label;
        if (companion_.cognition) {
            if (const auto disp = companion_.cognition->dispositionOf(look_key); !disp.empty())
                core.detail += (en ? L" · Attitude towards you: " : L" · Nastawienie do ciebie: ") + disp;
        }
        if (!companion_.mood.empty()) core.detail += L" · " + trimmed(companion_.mood);
        core.x = 0; core.y = 0; core.z = 0; core.weight = 1.6f; core.parent = 0;
        nodes.push_back(std::move(core));
    }

    const auto placeOnShell = [](size_t index, size_t count, float radius, float phase, float& x, float& y, float& z) {
        const float n = static_cast<float>( std::max<size_t>(1, count));
        const float k = static_cast<float>(index) + 0.5f;
        const float phi = std::acos(1.0f - 2.0f * k / n);
        const float theta = phase + 2.3999632f * static_cast<float>(index);
        x = radius * std::sin(phi) * std::cos(theta);
        y = radius * std::cos(phi);
        z = radius * std::sin(phi) * std::sin(theta);
    };

    const auto shortLabel = [](std::wstring_view text) {
        auto clean = trimmed(text);
        if (clean.size() <= 30) return clean;
        const auto cut = clean.rfind(L' ', 28);
        clean.resize(cut == std::wstring::npos || cut < 12 ? 28 : cut);
        return clean + L"…";
    };

    const std::wstring stm_tier = en ? L"Short-term" : L"Pamięć krótkotrwała";
    const std::wstring ltm_tier = en ? L"Long-term" : L"Pamięć długotrwała";

    // Shell 1 (radius 0.45): Active session, her inner thoughts (<mysl>) and recent online readings (Short-term / Working memory).
    std::vector<Node> context_nodes;
    if (companion_.mind) if (const auto* self = companion_.mind->find(look_key)) {
        int events_shown = 0;
        for (auto it = self->social.events.rbegin(); it != self->social.events.rend() && events_shown < 6; ++it) {
            if (it->kind == "turn" || it->kind == "message") continue;
            Node item; item.kind = Kind::context; item.label = shortLabel(it->text);
            item.category = tr(L"Zdarzenie w relacji"); item.detail = it->text;
            if (it->delta.trust || it->delta.respect || it->delta.security)
                item.detail += L"\n" + std::wstring(tr(L"Zmiana więzi (zaufanie / szacunek / bezpieczeństwo): ")) +
                    std::to_wstring(it->delta.trust) + L" / " + std::to_wstring(it->delta.respect) + L" / " + std::to_wstring(it->delta.security);
            item.when = ago(it->at); item.entry_at = it->at; item.memory_tier = ltm_tier; item.weight = 1.1f; item.parent = 0;
            context_nodes.push_back(std::move(item)); ++events_shown;
        }
        int plans_shown = 0;
        for (auto it = self->social.commitments.rbegin(); it != self->social.commitments.rend() && plans_shown < 4; ++it) {
            if (it->status == companion::social::CommitmentStatus::done || it->status == companion::social::CommitmentStatus::cancelled || it->status == companion::social::CommitmentStatus::declined) continue;
            Node item; item.kind = Kind::context; item.label = shortLabel(it->text); item.detail = it->text;
            item.category = it->status == companion::social::CommitmentStatus::proposed ? tr(L"Propozycja do uzgodnienia") :
                it->status == companion::social::CommitmentStatus::deferred ? tr(L"Przełożony plan") : tr(L"Uzgodniony plan");
            item.when = ago(it->updated); item.entry_at = it->updated; item.memory_tier = stm_tier; item.weight = 1.1f; item.parent = 0;
            context_nodes.push_back(std::move(item)); ++plans_shown;
        }
        int shown = 0;
        for (auto it = self->feelings.rbegin(); it != self->feelings.rend() && shown < 16; ++it, ++shown) {
            const auto& info = companion::feelingInfo(it->kind);
            Node item; item.kind = Kind::feeling; item.feeling = it->kind; item.emotion = info.sprite;
            item.label = item.feeling_label = en ? info.en : info.pl;
            item.category = en ? L"Named feeling" : L"Nazwane uczucie";
            item.detail = item.label;
            if (it->intensity) item.detail += (en ? L" · Model-rated intensity: " : L" · Natężenie według modelu: ") + std::to_wstring(static_cast<int>(*it->intensity * 100)) + L"%";
            else item.detail += en ? L" · Intensity not specified" : L" · Natężenie niepodane";
            if (!it->reason.empty()) item.detail += L" — " + it->reason;
            if (it->inferred) item.detail += en ? L" (recognized from an older text label)" : L" (rozpoznane ze starszego opisu tekstowego)";
            item.memory_tier = now_sec <= it->at + 45 * 60 ? stm_tier : ltm_tier;
            item.when = ago(it->at); item.entry_at = it->at; item.parent = 0; item.weight = 1.3f;
            context_nodes.push_back(std::move(item));
        }
        if (!self->felt.empty() && (self->feelings.empty() || self->felt_at > self->feelings.back().at)) {
            Node item; item.kind = Kind::feeling; item.label = item.detail = self->felt;
            item.category = en ? L"Feeling (text description)" : L"Uczucie (opis tekstowy)";
            item.when = ago(self->felt_at); item.entry_at = self->felt_at; item.memory_tier = stm_tier;
            item.feeling = companion::feelingFromText(self->felt); item.weight = 1.3f;
            context_nodes.push_back(std::move(item));
        }
    }
    if (companion_.last_session && !companion_.last_session->titles.empty()) {
        Node item;
        item.kind = Kind::context;
        item.label = companion_.last_session->titles.front();
        item.when = item.category = en ? L"Last session" : L"Ostatnia sesja";
        item.memory_tier = stm_tier;
        item.detail = (en ? L"Played for " : L"Sesja gry: ") + std::to_wstring(companion_.last_session->minutes) + L" min — " + item.label;
        item.weight = 1.25f;
        item.parent = 0;
        context_nodes.push_back(std::move(item));
    }
    if (companion_.cognition) {
        // Her hidden subconscious thoughts (<mysl>) and what she looked up online (on her own or
        // for the player in a chat) on the inner shell, each with room of its own. A thought stays
        // masked until the player shows it (Pokaż), and again in his next visit.
        int thoughts = 0, searches = 0;
        for (size_t i = companion_.cognition->thoughts.size(); i-- > 0 && thoughts < 5;) {
            const auto& th = companion_.cognition->thoughts[i];
            if (!th.look.empty() && th.look != look_key) continue;
            ++thoughts;
            Node item;
            item.kind = Kind::thought;
            item.label = shortLabel(th.text);
            item.category = en ? L"Hidden thought" : L"Ukryta myśl";
            item.memory_tier = stm_tier;
            item.emotion = th.emotion;
            item.feeling_label = companion::cognition::emotionFeelingLabel(th.emotion, en);
            item.when = ago(th.at);
            item.detail = th.text;
            item.weight = 1.25f;
            item.parent = 0;
            item.deletable = true;
            item.revealable = true;
            item.masked = !companionThoughtShown(th.at, th.text);
            item.entry_at = th.at;
            context_nodes.push_back(std::move(item));
        }
        for (size_t i = companion_.cognition->readings.size(); i-- > 0 && searches < 6;) {
            const auto& rd = companion_.cognition->readings[i];
            if (!rd.look.empty() && rd.look != look_key) continue;
            ++searches;
            Node item;
            item.kind = Kind::reading;
            item.label = rd.topic.empty() ? shortLabel(rd.summary) : shortLabel(rd.topic);
            item.category = rd.asked ? (en ? L"Looked up for you" : L"Sprawdziła dla ciebie") : (en ? L"Searched online" : L"Szukała w sieci");
            item.memory_tier = stm_tier;
            item.when = ago(rd.at);
            item.detail = rd.topic.empty() ? rd.summary : (en ? L"Searched: " : L"Szukała: ") + rd.topic + L" — " + rd.summary;
            item.weight = 1.15f;
            item.parent = 0;
            item.deletable = true;
            item.entry_at = rd.at;
            context_nodes.push_back(std::move(item));
        }
    }
    // Her mind (companion_mind.cpp): her mood on the inner shell; her life — goals, promises,
    // milestones, what she expects, what she did and plans (a private one closed) — on its own.
    std::vector<Node> life_nodes;
    for (const auto& mind_item : companionMindItems(look)) {
        Node item;
        item.kind = mind_item.thought ? Kind::thought : Kind::context;
        item.label = shortLabel(mind_item.label);
        item.category = mind_item.category;
        item.memory_tier = mind_item.short_term ? stm_tier : ltm_tier;
        item.when = !mind_item.when.empty() ? mind_item.when : mind_item.at ? ago(mind_item.at) : mind_item.category;
        item.detail = mind_item.detail;
        item.weight = mind_item.locked ? 1.0f : mind_item.active ? 1.4f : 1.2f;
        item.parent = 0;
        item.entry_at = mind_item.at;
        item.entry_key = mind_item.key;
        item.locked = mind_item.locked;
        item.excited = mind_item.active;
        (mind_item.inner ? context_nodes : life_nodes).push_back(std::move(item));
    }
    for (size_t i = 0; i < context_nodes.size(); ++i) {
        placeOnShell(i, context_nodes.size(), 0.45f, 0.4f, context_nodes[i].x, context_nodes[i].y, context_nodes[i].z);
        nodes.push_back(std::move(context_nodes[i]));
    }
    for (size_t i = 0; i < life_nodes.size(); ++i) {
        placeOnShell(i, life_nodes.size(), 0.6f, 0.9f, life_nodes[i].x, life_nodes[i].y, life_nodes[i].z);
        nodes.push_back(std::move(life_nodes[i]));
    }

    // Shell 2 (radius 0.72): Higher-order Reflections ("Sleep" beliefs), Causal Schemas + Player notes (Long-term memory).
    std::vector<Node> note_nodes;
    if (companion_.cognition) {
        for (size_t i = companion_.cognition->schemas.size(); i-- > 0 && note_nodes.size() < 4;) {
            const auto& sc = companion_.cognition->schemas[i];
            if (!sc.look.empty() && sc.look != look_key) continue;
            Node item;
            item.kind = Kind::reflection;
            item.label = shortLabel(sc.text);
            item.category = en ? L"Causal schema" : L"Schemat przyczynowy";
            item.memory_tier = ltm_tier;
            item.when = ago(sc.at);
            item.detail = sc.text;
            item.weight = 1.35f;
            item.parent = 0;
            item.deletable = true;
            item.entry_at = sc.at;
            note_nodes.push_back(std::move(item));
        }
        for (size_t i = companion_.cognition->reflections.size(); i-- > 0 && note_nodes.size() < 12;) {
            const auto& ref = companion_.cognition->reflections[i];
            if (!ref.look.empty() && ref.look != look_key) continue;
            Node item;
            item.kind = Kind::reflection;
            item.label = shortLabel(ref.text);
            item.category = en ? L"Her intuition" : L"Wniosek o tobie";
            item.memory_tier = ltm_tier;
            item.when = ago(ref.at);
            item.detail = ref.text + (ref.evolved_from.empty() ? L"" : (en ? L" (earlier: " : L" (wcześniej: ") + ref.evolved_from + L")");
            item.weight = 1.3f;
            item.parent = 0;
            item.deletable = true;
            item.entry_at = ref.at;
            note_nodes.push_back(std::move(item));
        }
    }
    if (companion_.memory) {
        const auto& notes = companion_.memory->notes;
        for (size_t i = notes.size(); i-- > 0 && note_nodes.size() < 34;) {
            if (!companion::Memory::knows(notes[i], look_key)) continue; // Another companion's: not hers to show.
            const auto* knower = companion::Memory::knower(notes[i], look_key);
            Node item;
            item.kind = Kind::note;
            item.label = shortLabel(notes[i].text);
            item.category = notes[i].important ? (en ? L"Key memory" : L"Ważne wspomnienie")
                          : knower && knower->hearsay ? (en ? L"Gossip about you" : L"Plotka o tobie")
                                                      : (en ? L"Note about you" : L"Notatka o tobie");
            item.memory_tier = ltm_tier;
            if (const auto em = companion::emotionFromKey(notes[i].emotion)) {
                item.emotion = *em;
                item.feeling_label = companion::cognition::emotionFeelingLabel(*em, en);
            } else if (const auto feeling = companion::feelingFromKey(notes[i].emotion)) {
                item.feeling = *feeling;
                const auto& info = companion::feelingInfo(*feeling);
                item.emotion = info.sprite; item.feeling_label = en ? info.en : info.pl;
            }
            item.when = ago(notes[i].at);
            item.detail = notes[i].text;
            if (!notes[i].feeling.empty() && (notes[i].look.empty() || notes[i].look == look_key))
                item.detail += (en ? L" — How she felt: " : L" — Jak się poczuła: ") + notes[i].feeling;
            item.weight = notes[i].important ? 1.35f : 1.1f;
            item.parent = 0;
            item.deletable = true;
            item.entry_at = notes[i].at;
            note_nodes.push_back(std::move(item));
        }
    }
    for (size_t i = 0; i < note_nodes.size(); ++i) {
        placeOnShell(i, note_nodes.size(), 0.72f, 1.3f, note_nodes[i].x, note_nodes[i].y, note_nodes[i].z);
        nodes.push_back(std::move(note_nodes[i]));
    }

    // Shell 3 (radius 0.95): Chat summaries (newest first!), suggested games (picks), and taste vectors.
    std::vector<Node> outer_nodes;
    if (companion_.memory) {
        for (size_t i = companion_.memory->summaries.size(); i-- > 0 && outer_nodes.size() < 18;) {
            const auto& sum = companion_.memory->summaries[i];
            if (!sum.look.empty() && sum.look != look_key) continue;
            Node item;
            item.kind = Kind::summary;
            item.label = shortLabel(sum.text);
            item.category = sum.important ? (en ? L"Key memory" : L"Ważne wspomnienie") : (en ? L"Chat memory" : L"Wspomnienie rozmowy");
            item.memory_tier = ltm_tier;
            item.when = ago(sum.at);
            item.detail = sum.text;
            item.weight = sum.important ? 1.35f : 1.0f;
            item.parent = 0;
            item.deletable = true;
            item.entry_at = sum.at;
            outer_nodes.push_back(std::move(item));
        }
        for (size_t i = companion_.memory->picks.size(); i-- > 0 && outer_nodes.size() < 28;) {
            const auto& pick = companion_.memory->picks[i];
            if (!pick.look.empty() && pick.look != look_key) continue;
            Node item;
            item.kind = Kind::pick;
            item.label = pick.title;
            item.category = en ? L"Suggested game" : L"Polecona gra";
            item.memory_tier = now_sec <= pick.at + 3 * 86400 ? stm_tier : ltm_tier;
            item.when = ago(pick.at);
            item.detail = (en ? L"Suggested game: " : L"Polecona gra: ") + pick.title;
            item.weight = 0.95f;
            item.parent = 0;
            item.deletable = true;
            item.entry_at = pick.at;
            item.entry_key = pick.key;
            outer_nodes.push_back(std::move(item));
        }
    }
    if (c) {
        for (size_t i = 0; i < c->likes.size() && i < 5; ++i) {
            Node item;
            item.kind = Kind::taste;
            item.label = L"+ " + c->likes[i];
            item.when = item.category = en ? L"Loved tag" : L"Ulubiony tag";
            item.memory_tier = ltm_tier;
            item.detail = (en ? L"She loves games tagged with: " : L"Uwielbia gry z tagiem: ") + c->likes[i];
            item.weight = 0.9f;
            item.parent = 0;
            outer_nodes.push_back(std::move(item));
        }
        for (size_t i = 0; i < c->dislikes.size() && i < 3; ++i) {
            Node item;
            item.kind = Kind::taste;
            item.label = L"− " + c->dislikes[i];
            item.when = item.category = en ? L"Disliked tag" : L"Nielubiany tag";
            item.memory_tier = ltm_tier;
            item.detail = (en ? L"She avoids games tagged with: " : L"Nie przepada za tagiem: ") + c->dislikes[i];
            item.weight = 0.85f;
            item.parent = 0;
            outer_nodes.push_back(std::move(item));
        }
    }
    for (size_t i = 0; i < outer_nodes.size(); ++i) {
        placeOnShell(i, outer_nodes.size(), 0.95f, 2.2f, outer_nodes[i].x, outer_nodes[i].y, outer_nodes[i].z);
        nodes.push_back(std::move(outer_nodes[i]));
    }

    // Reverse hierarchy for the UI list: newest dated memories at the very top (index 0..),
    // followed by current session/topics, tastes, and the foundational personality core at the end.
    // Every node keeps its 3D (x, y, z) orbital position, and `parent` is remapped to the core's new index.
    std::stable_sort(nodes.begin(), nodes.end(), [](const Node& a, const Node& b) {
        const bool a_dated = a.entry_at > 0;
        const bool b_dated = b.entry_at > 0;
        if (a_dated != b_dated) return a_dated;
        if (a_dated && a.entry_at != b.entry_at) return a.entry_at > b.entry_at;
        const auto rank = [](Kind k) {
            switch (k) {
            case Kind::feeling:    return -1;
            case Kind::thought:    return 0;
            case Kind::reflection: return 1;
            case Kind::reading:    return 2;
            case Kind::note:       return 3;
            case Kind::summary:    return 4;
            case Kind::pick:       return 5;
            case Kind::context:    return 6;
            case Kind::taste:      return 7;
            case Kind::core:       return 8;
            }
            return 9;
        };
        return rank(a.kind) < rank(b.kind);
    });
    int core_index = 0;
    for (size_t i = 0; i < nodes.size(); ++i)
        if (nodes[i].kind == Kind::core) { core_index = static_cast<int>(i); break; }
    int prev_sum = core_index;
    for (size_t i = nodes.size(); i-- > 0;) {
        if (static_cast<int>(i) == core_index) { nodes[i].parent = core_index; continue; }
        if (nodes[i].kind == Kind::summary) {
            nodes[i].parent = prev_sum;
            prev_sum = static_cast<int>(i);
        } else {
            nodes[i].parent = core_index;
        }
    }

    // Highlight nodes excited by Spreading Activation from the player's last message.
    if (companion_.cognition && companion_.memory && !companion_.last_query.empty()) {
        const auto hits = companion_.cognition->spreadActivation(*companion_.memory, look_key, companion_.last_query, now_sec);
        for (auto& node : nodes) {
            for (const auto& h : hits) {
                if (!h.text.empty() && node.detail.find(h.text) != std::wstring::npos) {
                    node.excited = true;
                    break;
                }
            }
        }
    }
    using Filter = companion_ui::MindFilter;
    for (auto& node : nodes) {
        switch (companion_.chat.mind_filter) {
        case Filter::all: node.matches_filter = true; break;
        case Filter::feelings: node.matches_filter = node.kind == Kind::feeling || node.feeling.has_value() || !node.feeling_label.empty(); break;
        case Filter::memories: node.matches_filter = node.kind == Kind::note || node.kind == Kind::summary; break;
        case Filter::thoughts: node.matches_filter = node.kind == Kind::thought; break;
        case Filter::reflections: node.matches_filter = node.kind == Kind::reflection; break;
        case Filter::life: node.matches_filter = node.kind == Kind::context || node.kind == Kind::reading || node.kind == Kind::core; break;
        case Filter::games: node.matches_filter = node.kind == Kind::pick || node.kind == Kind::taste; break;
        case Filter::active: node.matches_filter = node.excited; break;
        }
    }
    return nodes;
}

void Launcher::companionMindFilter(companion_ui::MindFilter filter) {
    auto& chat = companion_.chat;
    if (!chat.mind || chat.mind_confirm) return;
    chat.mind_filter = filter;
    const auto nodes = companionMindNodes();
    const auto first = std::find_if(nodes.begin(), nodes.end(), [](const auto& n) { return n.matches_filter; });
    if (first != nodes.end()) chat.mind_selected = static_cast<int>(first - nodes.begin());
    chat.mind_hover = companion_ui::no_hit;
    audio_.context(); InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::companionMindNextFilter() {
    companionMindFilter(static_cast<companion_ui::MindFilter>((static_cast<int>(companion_.chat.mind_filter) + 1) % companion_ui::mind_filter_count));
}

CompanionView::MindTelemetry Launcher::companionMindTelemetry() const {
    CompanionView::MindTelemetry tel;
    const auto look = companionLook();
    const auto look_key = std::string(companion::lookKey(look));
    const bool en = english();
    if (const auto* c = companion::character(look)) {
        const auto card = en ? (c->card_en.empty() ? c->card_pl : c->card_en) : c->card_pl;
        const auto about = c->about(en, false);
        tel.bio = about.empty() ? card : card + L" — " + about;
        tel.likes = c->likes;
        tel.dislikes = c->dislikes;
    }
    const int64_t pts = companion_.affinity ? companion::affinity::pointsOf(companion_.affinity->points, look_key) : 0;
    const int lvl = companion::affinity::level(pts);
    tel.points = pts;
    tel.next_points = lvl >= companion::affinity::max_level ? pts : companion::affinity::threshold(lvl + 1);
    tel.costumes_unlocked = 0;
    tel.costumes_total = 0;
    for (const auto& costume : companionCostumes()) {
        if (costume.look != look) continue;
        ++tel.costumes_total;
        if (companionCostumeUnlocked(costume)) ++tel.costumes_unlocked;
    }
    if (tel.costumes_total == 0) { tel.costumes_total = 1; tel.costumes_unlocked = 1; }
    if (companion_.memory) {
        tel.notes_count = static_cast<int>(companion_.memory->notes.size());
        for (const auto& s : companion_.memory->summaries) if (s.look.empty() || s.look == look_key) ++tel.summaries_count;
        for (const auto& p : companion_.memory->picks) if (p.look.empty() || p.look == look_key) ++tel.picks_count;
    }
    if (!companion_.diagnostics.empty()) {
        uint64_t sum = 0;
        for (const auto& d : companion_.diagnostics) sum += d.total_ms;
        tel.avg_latency_ms = static_cast<uint32_t>(sum / companion_.diagnostics.size());
        tel.model_name = companion_.diagnostics.back().model;
    }
    if (tel.model_name.empty()) {
        companion::Connection shown;
        shown.provider = companionAccess().provider;
        shown.model = companionAccess().model;
        tel.model_name = shown.label();
    }
    // Her mood now (her settled affect), beside her profile.
    if (companion_.mind)
        if (const auto* self = companion_.mind->find(look_key)) tel.mood = companion::mind::describe(companion::mind::settled(self->affect, companionMindContext(look)), en);
    // Live thought indicator: only shown while she is genuinely thinking in flight (`companion_.busy`).
    // Her hidden thought there is masked too until the player clicks the bar.
    tel.live_busy = companion_.busy;
    if (companion_.busy) {
        tel.live_thought_kind = en ? L"Thinking now…" : L"Teraz myśli…";
        if (!companion_.heard.empty()) {
            const auto parsed = companion::parseText(companion_.heard);
            tel.live_hidden = !parsed.subconscious.empty();
            tel.live_thought = tel.live_hidden ? parsed.subconscious : parsed.text;
        }
        // Her curiosity for this reply, decided as he wrote: she is about to ask back or carry the talk on.
        if (tel.live_thought.empty() && companion_.mind)
            if (const auto* self = companion_.mind->find(look_key); self && self->urge.move != companion::CuriousMove::none)
                tel.live_thought = self->urge.move == companion::CuriousMove::clarify ? (en ? L"Wants to ask what you meant…" : L"Chce dopytać, co ‹miałeś|miałaś› na myśli…")
                                                                                     : (en ? L"Wants to carry the talk on…" : L"Chce sama pociągnąć rozmowę…");
        if (tel.live_thought.empty())
            tel.live_thought = en ? L"Formulating a reply…" : L"Formułuje odpowiedź…";
        tel.live_masked = tel.live_hidden && !companion_.chat.mind_live_revealed;
    }
    return tel;
}

void Launcher::companionClick(float x, float y) {
    if (companion_.chat.feed) {
        const auto size = renderer_.viewport();
        const int hit = companion_ui::FeedLayout{size.width, size.height}.hit(x, y, companion_.feed.view);
        companionFeedAction(hit);
        return;
    }
    if (companion_.chat.viewer) { companionViewer(false); return; } // Her enlarged photo: a click anywhere closes it.
    const auto view = renderer_.viewport();
    if (companion_.chat.mind) {
        const companion_ui::Layout layout{view.width, view.height};
        if (companion_.chat.mind_confirm) {
            for (int button : {companion_ui::confirm_reset, companion_ui::confirm_cancel})
                if (layout.mindConfirmButton(button).contains(x, y)) { companionMindDeletePick(button == companion_ui::confirm_reset); return; }
            if (!layout.mindConfirm().contains(x, y)) companionMindDeleteConfirm(false);
            return;
        }
        if (layout.mindClose().contains(x, y)) { companionMind(false); return; }
        for (int i = 0; i < companion_ui::mind_filter_count; ++i)
            if (layout.mindFilter(i).contains(x, y)) { companionMindFilter(static_cast<companion_ui::MindFilter>(i)); return; }
        if (companion_.chat.mind_hover == companion_ui::mind_delete_hit) { companionMindDeleteConfirm(true); return; }
        if (companion_.chat.mind_hover == companion_ui::mind_reveal_hit) { companionMindReveal(); return; }
        if (companion_.chat.mind_hover == companion_ui::mind_live_hit) { companionMindLiveReveal(); return; }
        if (companion_.chat.mind_hover >= 0) {
            companion_.chat.mind_selected = companion_.chat.mind_hover;
            const auto nodes = companionMindNodes();
            if (companion_.chat.mind_selected < static_cast<int>(nodes.size())) {
                const auto& node = nodes[static_cast<size_t>(companion_.chat.mind_selected)];
                if (std::abs(node.x) > 0.01f || std::abs(node.z) > 0.01f)
                    companion_.chat.mind_yaw = -std::atan2(node.x, -node.z);
                companion_.chat.mind_pitch = std::clamp(std::asin(std::clamp(node.y, -0.85f, 0.85f)) * 0.6f, -0.7f, 0.7f);
                static constexpr std::array<companion::Emotion, 6> node_emotions{
                    companion::Emotion::smug, companion::Emotion::playful, companion::Emotion::thinking,
                    companion::Emotion::smile, companion::Emotion::excited, companion::Emotion::gaming};
                companion_.rig.emote(node.emotion.value_or(node_emotions[static_cast<size_t>(node.kind) % node_emotions.size()]));
            }
            audio_.navigate();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        if (layout.mindSphere().contains(x, y)) {
            companion_.chat.mind_dragging = true;
            companion_.chat.mind_drag_x = x;
            companion_.chat.mind_drag_y = y;
            return;
        }
        return;
    }
    const int hit = companion_ui::Layout{view.width, view.height}.hit(x, y, companion_.chat);
    if (companion_.chat.confirm) {
        // The reset dialog: a button picks; a click beside the dialog cancels (as Anuluj).
        const int button = companion_ui::confirmButtonOf(hit);
        const auto dialog = companion_ui::Layout{view.width, view.height}.confirm();
        if (button >= 0) companionResetPick(button == companion_ui::confirm_reset);
        else if (!dialog.contains(x, y)) companionResetConfirm(false);
        return;
    }
    if (companion_.chat.menu) {
        // Open menu: a row picks, "…" or anywhere else closes it.
        if (const int row = companion_ui::menuRowOf(hit); row >= 0) companionMenuPick(row);
        else companionMenu(false);
        return;
    }
    // Her photo in the log (live or a history page): enlarged.
    if (hit == companion_ui::no_hit && renderer_.companionPhoto().contains(x, y) && companionPhotoShown()) { companionViewer(true); return; }
    if (hit == companion_ui::no_hit && companionLinkClick(x, y)) return;
    if (hit == companion_ui::no_hit) return;
    companion_.chat.focus = hit;
    if (hit == companion_ui::focus_input) { InvalidateRect(hwnd_, nullptr, FALSE); return; } // Click into the field: type.
    companionAccept();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

std::wstring Launcher::companionGameUrl(const std::wstring& title) const {
    if (title.empty()) return {};
    // Prefer an exact Steam library entry, including when the player owns another store's copy too.
    const auto wanted = game_index::fold(title);
    for (const auto& game : public_library_)
        if (steamArtwork(game) && game.app_id && game_index::fold(game.name) == wanted)
            return L"https://store.steampowered.com/app/" + std::to_wstring(game.app_id) + L"/";
    // The index also knows aliases; only a real Steam key supplies an app id, never model text.
    const auto key = companionGameKey(title);
    if (key.starts_with("steam:")) {
        uint32_t id = 0;
        const auto parsed = std::from_chars(key.data() + 6, key.data() + key.size(), id);
        if (parsed.ec == std::errc{} && parsed.ptr == key.data() + key.size() && id)
            return L"https://store.steampowered.com/app/" + std::to_wstring(id) + L"/";
    }
    const auto query = url_encoding::component(title + L" game");
    return query.empty() ? std::wstring{} : L"https://www.google.com/search?q=" + query;
}

void Launcher::companionVideo(const std::wstring& url) {
    const auto id = companion::links::youtubeId(url);
    if (id.empty()) return;
    const auto destination = desktop_mode_ ? L"https://www.youtube.com/watch?v=" + text_encoding::wideLenient(id) : companion::links::playerUrl(id);
    openGlanceBrowser(destination, true);
}

bool Launcher::companionLinkClick(float x, float y) {
    if (const auto url = renderer_.companionVideoAt(x, y); !url.empty()) { companionVideo(url); return true; }
    const auto title = renderer_.companionLinkAt(x, y);
    if (title.empty()) return false;
    // An installed game in the player's library opens its native Szczegóły screen; uninstalled or
    // external games (or offline URL tests with browse_ set) open the store/search page as before.
    if (!browse_) {
        const auto key = companionGameKey(title);
        if (!key.empty()) {
            const auto found = std::find_if(public_library_.begin(), public_library_.end(),
                                            [&](const Game& game) { return gameKey(game) == key && game.installed; });
            if (found != public_library_.end()) {
                const Game game = *found;
                companionFlush();
                companionHush();
                companion_.rig.silence();
                openDetails(game);
                return true;
            }
        }
    }
    openGlanceBrowser(companionGameUrl(title));
    return true;
}

void Launcher::companionPointer(float mouse_x, float mouse_y) {
    auto& chat = companion_.chat;
    if (companionOpen() && chat.feed) {
        const float scale = desktopActive() ? desktopScale() : renderer_.viewport().scale;
        const auto size = renderer_.viewport();
        const int hit = companion_ui::FeedLayout{size.width, size.height}.hit(mouse_x / scale, mouse_y / scale, companion_.feed.view);
        if (companion_.feed.view.hover != hit) { companion_.feed.view.hover = hit; InvalidateRect(hwnd_, nullptr, FALSE); }
        return;
    }
    if (companionOpen() && chat.mind) {
        const float scale = desktopActive() ? desktopScale() : renderer_.viewport().scale;
        const float x = mouse_x / scale, y = mouse_y / scale;
        if (chat.mind_dragging) {
            if ((GetKeyState(VK_LBUTTON) & 0x8000) == 0) {
                chat.mind_dragging = false;
            } else {
                chat.mind_yaw += (x - chat.mind_drag_x) * 0.0075f;
                chat.mind_pitch = std::clamp(chat.mind_pitch + (y - chat.mind_drag_y) * 0.006f, -0.85f, 0.85f);
                chat.mind_drag_x = x;
                chat.mind_drag_y = y;
                InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
        }
        const auto view = renderer_.viewport();
        const companion_ui::Layout layout{view.width, view.height};
        if (chat.mind_confirm) {
            int hit_btn = companion_ui::no_hit;
            for (int button : {companion_ui::confirm_reset, companion_ui::confirm_cancel})
                if (layout.mindConfirmButton(button).contains(x, y)) hit_btn = companion_ui::confirmFocus(button);
            if (chat.mind_hover != hit_btn) { chat.mind_hover = hit_btn; InvalidateRect(hwnd_, nullptr, FALSE); }
            return;
        }
        const auto sphere = layout.mindSphere();
        const auto nodes = companionMindNodes();
        const int sel = std::clamp(chat.mind_selected, 0, std::max(0, static_cast<int>(nodes.size()) - 1));
        int best_node = companion_ui::no_hit;
        for (int i = 0; i < companion_ui::mind_filter_count; ++i) if (layout.mindFilter(i).contains(x, y)) best_node = companion_ui::mind_filter_hit_base - i;
        const bool revealable = !nodes.empty() && nodes[static_cast<size_t>(sel)].revealable;
        if (best_node <= companion_ui::mind_filter_hit_base) {
            // The toolbar is outside the sphere and list; retain its own hover identity.
        } else if (!nodes.empty() && (nodes[static_cast<size_t>(sel)].deletable || nodes[static_cast<size_t>(sel)].locked) && layout.mindDelete().contains(x, y)) {
            best_node = companion_ui::mind_delete_hit;
        } else if (revealable && (layout.mindReveal().contains(x, y) || (nodes[static_cast<size_t>(sel)].masked && layout.mindDetail().contains(x, y)))) {
            best_node = companion_ui::mind_reveal_hit; // Pokaż/Ukryj, or a click on the masked thought itself.
        } else if (layout.mindLiveBar().contains(x, y) && companionMindTelemetry().live_hidden) {
            best_node = companion_ui::mind_live_hit;
        } else if (sphere.contains(x, y)) {
            const float cx = (sphere.left + sphere.right) * 0.5f, cy = (sphere.top + sphere.bottom) * 0.5f;
            const float radius = layout.mindRadius(chat.mind_zoom_spring.value);
            const float cyaw = std::cos(chat.mind_yaw_spring.value), syaw = std::sin(chat.mind_yaw_spring.value);
            const float cpitch = std::cos(chat.mind_pitch_spring.value), spitch = std::sin(chat.mind_pitch_spring.value);
            float best_dist = 26.0f * 26.0f;
            for (size_t i = 0; i < nodes.size(); ++i) {
                const float rx = nodes[i].x * cyaw - nodes[i].z * syaw;
                const float rz0 = nodes[i].x * syaw + nodes[i].z * cyaw;
                const float ry = nodes[i].y * cpitch - rz0 * spitch;
                const float rz = nodes[i].y * spitch + rz0 * cpitch;
                const float persp = 2.4f / (2.4f + rz * 0.65f);
                const float sx = cx + rx * radius * persp;
                const float sy = cy - ry * radius * persp;
                const float d2 = (x - sx) * (x - sx) + (y - sy) * (y - sy);
                if (d2 < best_dist) { best_dist = d2; best_node = static_cast<int>(i); }
            }
        } else if (layout.mindClose().contains(x, y)) {
            best_node = companion_ui::focus_back;
        } else {
            const int total = static_cast<int>(nodes.size());
            const int first = layout.mindListFirst(std::clamp(chat.mind_selected, 0, std::max(0, total - 1)), total);
            for (int slot = 0; slot < layout.mindListRows() && first + slot < total; ++slot) {
                if (layout.mindListRow(slot).contains(x, y)) { best_node = first + slot; break; }
            }
        }
        if (chat.mind_hover != best_node) {
            chat.mind_hover = best_node;
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
    }
    const bool home = companionAvailable() && !desktop_menu_ && !desktop_quick_.open && !desktop_meta_.open &&
                      !desktop_widgets_open_ && !desktop_downloads_open_;
    const bool chatting = companionOpen() && topModal() == Modal::none && !friends_open_ && !chat.menu && !chat.confirm && !chat.viewer && !chat.mind && !chat.feed && !keyboard_.open;
    const auto before = companion_.link_pointer;
    if (home || chatting) {
        const float scale = desktopActive() ? desktopScale() : renderer_.viewport().scale;
        companion_.link_pointer = std::pair{mouse_x / scale, mouse_y / scale};
    } else companion_.link_pointer.reset();
    if (before != companion_.link_pointer) {
        const auto overLink = [&](const auto& point) { return point && (!renderer_.companionLinkAt(point->first, point->second).empty() || !renderer_.companionVideoAt(point->first, point->second).empty()); };
        if (overLink(before) || overLink(companion_.link_pointer)) InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

std::vector<companion_ui::MenuItem> Launcher::companionMenuItems() const {
    using Kind = companion_ui::MenuItem::Kind;
    const auto& chat = companion_.chat;
    const auto& guest = companion_.guest;
    // A guest comes and goes between replies, never over one on its way.
    const bool free = !chat.waiting && !companionRouletteSpinning() && !chat.ended;
    std::vector<companion_ui::MenuItem> items;
    if (guest.look) items.push_back({Kind::farewell, std::wstring(tr(L"Pożegnaj ")) + companion::name(*guest.look), free && !guest.leaving});
    else if (settings_.companion_guests) {
        const auto others = companionGuestCandidates();
        if (others.size() == 1) items.push_back({Kind::invite, std::wstring(tr(L"Zawołaj ")) + companion::name(others.front()), free});
        else if (!others.empty()) items.push_back({Kind::invite, tr(L"Zawołaj kogoś"), free});
    }
    if (companionPhotoShown()) items.push_back({Kind::photo, tr(L"Powiększ zdjęcie"), true}); // Her photo, for the pad and keyboard.
    // The latest shared videos also have a pad/keyboard route through the menu.
    for (size_t i = chat.log.size(); i-- > 0;) {
        if (!chat.log[i].user) continue;
        bool found = false;
        const auto links = companion::links::find(chat.log[i].text);
        for (size_t j = 0; j < links.size(); ++j) if (!links[j].video_id.empty()) {
            items.push_back({Kind::video, L"YouTube · " + std::wstring(tr(L"Odtwórz film")), true, static_cast<int>(i * companion::links::max_links + j), text_encoding::wideLenient(links[j].video_id)});
            found = true;
        }
        if (found) break;
    }
    items.push_back({Kind::diagnostics, settings_.companion_debug ? tr(L"Ukryj diagnostykę") : tr(L"Pokaż diagnostykę"), true});
    items.push_back({Kind::costumes, tr(L"Zmień strój"), true});
    items.push_back({Kind::change, tr(L"Zmień towarzyszkę"), companion::availableLooks().size() > 1});
    items.push_back({Kind::mind, tr(L"Umysł i pamięć"), true});
    if (settings_.companion_gamification) {
        items.push_back({Kind::reset_affinity, tr(L"Zresetuj sympatię"), true});
    }
    // Her older conversations, read only (never while a reply or a guest is in this one).
    if (companion_.persist) {
        const auto all = companion::chats::list(userConfigDirectory(), companionLook());
        const bool older = std::any_of(all.begin(), all.end(), [&](uint64_t started) { return started != companion_.chat_started; });
        items.push_back({Kind::history, tr(L"Historia czatu"), older && !chat.waiting && !chat.talking() && !companionRouletteSpinning() && !guest.look});
    }
    items.push_back({Kind::backup, tr(L"Pełna kopia postaci"), !companion_.backup_busy && !companion_.busy});
    // The conversation: ended by the player (her goodbye, then her rest), or a new one once she is back.
    if (chat.ended) {
        companion_ui::MenuItem item{Kind::clear, tr(L"Nowa rozmowa"), !companionResting()};
        if (!item.enabled) item.detail = std::wstring(tr(L"za ")) + companion::chats::duration(companionAway().seconds, english());
        items.push_back(std::move(item));
    } else items.push_back({Kind::clear, tr(L"Zakończ rozmowę"), free && !chat.talking() && companion_.typed > 0});
    return items;
}

void Launcher::companionMenu(bool open) {
    auto& chat = companion_.chat;
    if (chat.menu == open) return;
    if (open && chat.archive) { navigationBoundary(); return; } // A history page has no menu.
    chat.menu = open;
    if (open) {
        closeKeyboard();
        chat.menu_items = companionMenuItems();
        // The first row that can be picked.
        const auto usable = std::find_if(chat.menu_items.begin(), chat.menu_items.end(), [](const auto& item) { return item.enabled; });
        chat.menu_focus = usable == chat.menu_items.end() ? 0 : static_cast<int>(usable - chat.menu_items.begin());
        chat.focus = companion_ui::focus_more;
        audio_.context();
    } else audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::companionMenuPick(int row) {
    auto& chat = companion_.chat;
    if (row < 0 || row >= static_cast<int>(chat.menu_items.size()) || !chat.menu_items[static_cast<size_t>(row)].enabled) { navigationBoundary(); return; }
    const auto kind = chat.menu_items[static_cast<size_t>(row)].kind;
    if (kind == companion_ui::MenuItem::Kind::costumes) { companionCostumeMenu(); return; } // The menu lists her costumes.
    if (kind == companion_ui::MenuItem::Kind::history) { companionHistoryMenu(); return; } // …or her older conversations.
    if (kind == companion_ui::MenuItem::Kind::relationship) { companionRelationMenu(); return; } // …or her relationship track/distance.
    const int value = chat.menu_items[static_cast<size_t>(row)].value;
    chat.menu = false;
    chat.focus = chat.ended ? companion_ui::focus_send : companion_ui::focus_input;
    switch (kind) {
    case companion_ui::MenuItem::Kind::invite: companionInvite(); break;
    case companion_ui::MenuItem::Kind::farewell: companionDismiss(); break;
    case companion_ui::MenuItem::Kind::clear: if (chat.ended) companionRestart(); else companionEnd(true); break;
    case companion_ui::MenuItem::Kind::history: break; // Handled above.
    case companion_ui::MenuItem::Kind::archive: companionOpenArchive(value); return;
    case companion_ui::MenuItem::Kind::reset_affinity: companionResetConfirm(true); return;
    case companion_ui::MenuItem::Kind::costumes: break; // Handled above.
    case companion_ui::MenuItem::Kind::wear: companionWear(value); break;
    case companion_ui::MenuItem::Kind::relationship: break; // Handled above.
    case companion_ui::MenuItem::Kind::relation_pick: companionRelationPick(value); break;
    case companion_ui::MenuItem::Kind::photo: companionViewer(true); return;
    case companion_ui::MenuItem::Kind::video: {
        const size_t index = static_cast<size_t>(value) / companion::links::max_links, slot = static_cast<size_t>(value) % companion::links::max_links;
        if (index < chat.log.size()) {
            const auto links = companion::links::find(chat.log[index].text);
            if (slot < links.size()) companionVideo(links[slot].url);
        }
        return;
    }
    case companion_ui::MenuItem::Kind::change: companionPicker(true); return;
    case companion_ui::MenuItem::Kind::mind: companionMind(true); return;
    case companion_ui::MenuItem::Kind::backup: companionBackup(); return;
    case companion_ui::MenuItem::Kind::diagnostics: // Kept in settings.ini, like a Settings switch.
        settings_.companion_debug = !settings_.companion_debug;
        settings_dirty_ = true; settings_save_failed_ = false; settings_edited_ = std::chrono::steady_clock::now();
        audio_.context();
        break;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::companionClear() {
    requestCompanionSummary(); // Notes on: what the ending chat said about the player.
    companion_.cancel->store(true);
    ++companion_.generation;
    companion_.busy = false;
    companion_.held.reset();
    companionHush();
    companion_.guest = {};
    auto& chat = companion_.chat;
    const float age = chat.age, pose_age = chat.pose_age;
    chat = {};
    chat.age = age; chat.pose_age = pose_age; // The screen stays; only the conversation starts over.
    chat.focus = companion_ui::focus_input;
    companion_.turns.clear();
    companion_.rig.silence();
    companionBegin();
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::companionTest() {
    if (!settings_.companion) return;
    requestCompanion(CompanionMessage::Purpose::test);
    companion::Connection shown;
    shown.provider = companionAccess().provider; shown.model = companionAccess().model;
    showNotification(NotificationKind::launch, tr(L"Sprawdzanie połączenia…"), shown.label());
}

Launcher::CompanionAccess Launcher::companionAccess() const {
    CompanionAccess access;
    access.provider = static_cast<companion::Provider>(settings_.companion_provider);
    switch (access.provider) {
    case companion::Provider::claude: access.model = settings_.companion_claude_model; break;
    case companion::Provider::ollama: access.model = settings_.companion_ollama_model; access.host = settings_.companion_ollama_host; break;
    case companion::Provider::gemini: access.model = settings_.companion_model; break;
    case companion::Provider::xai: access.model = settings_.companion_xai_model; break;
    default: access.model = settings_.companion_model; access.host = settings_.companion_host; break;
    }
    access.config = userConfigDirectory();
    access.probe = companion_.probe;
    return access;
}

companion::Connection Launcher::CompanionAccess::resolve() const {
    return companion::resolve(provider, host, companion::storedToken(config, provider), model, probe ? probe() : companion::Probe{});
}

bool Launcher::tickCompanion(float dt) {
    if (!settings_.companion) { companion_.tile_progress = 0; return false; }
    pollCompanionFeed();
    if (settings_.companion_gamification && !companion_.affinity) companionAffinity(); // Her level, before it is drawn.
    const bool pulsing = companion_.affinity_pulse > 0 || companion_.affinity_card_pulse > 0;
    if (companion_.affinity_pulse > 0) companion_.affinity_pulse = std::max(0.0f, companion_.affinity_pulse - dt / 1.6f); // A new level: the badge glows.
    if (companion_.affinity_card_pulse > 0) companion_.affinity_card_pulse = std::max(0.0f, companion_.affinity_card_pulse - dt / 1.6f); // Her card: it flashes.
    // Her voice: check the ElevenLabs key right away, so even her greeting can be spoken.
    if (companion_.age == 0 && settings_.companion_voice != AppSettings::CompanionVoice::off) requestCompanionVoiceStatus();
    companion_.age += dt;
    const bool home = companionAvailable(true);
    // Her greeting and remarks need her bubble: every home but the Flix pills (they wait for another layout).
    const bool speaks = home && home_widgets::speaks(homeWidgets());
    const bool chat_open = companionOpen();
    const float before = companion_.tile_progress;
    if (!home) companion_.tile_progress = 0;
    else if (!settings_.screen_transitions) companion_.tile_progress = 1;
    else companion_.tile_progress = std::min(1.0f, companion_.tile_progress + dt / 0.30f);
    const float hover = companionAvailable() && companion_.hover ? 1.0f : 0.0f;
    if (settings_.screen_transitions) companion_.hover_motion.step(hover, dt, spring::glide.omega, spring::glide.zeta);
    else companion_.hover_motion.snap(hover);
    if (companion_.hover_motion.settled(hover, 0.002f)) companion_.hover_motion.snap(hover);
    bool moving = companion_.tile_progress != before || companion_.hover_motion.value != hover || pulsing;
    // Once per start she jumps up out of her tile as it appears, landing with a bounce.
    if (home && companion_.entrance < 1) {
        if (!settings_.screen_transitions) companion_.entrance = 1;
        else if (companion_.tile_progress >= 1) {
            companion_.entrance = std::min(1.0f, companion_.entrance + dt / 0.9f);
            if (companion_.entrance >= 1) companion_.rig.emote(companion_.rig.emotion()); // The landing hop.
        }
        moving = true;
    }
    // Settled once the home has its games (or after a few seconds): her remarks may start. She no
    // longer greets in a bubble of her own — the chat keeps the conversation, and a greeting there
    // would not know it. Asleep by her clock, she waits for her morning.
    const bool asleep = home && companionAsleep(companionLook());
    if (speaks && !companion_.greeted && !asleep && companion_.age > 1.2f && (!scanning_ || companion_.age > 6.0f)) {
        companion_.greeted = true;
        companion_.rig.emote(companion::Emotion::smile);
        requestCompanionPremieres();
    }
    // A long quiet on the home makes her doze off (and so does her rest after a conversation); the
    // next input wakes her with a start. Her night keeps her asleep whatever the player does.
    const bool quiet = std::chrono::steady_clock::now() - last_input_ > std::chrono::seconds(45) || (home && (asleep || companionResting()));
    if (home && quiet && !companion_.dozing && !companion_.busy && companion_.bubble.alpha <= 0) {
        companion_.dozing = true;
        companion_.rig.emote(companion::Emotion::sleepy, false);
    } else if (companion_.dozing && (!quiet || chat_open)) {
        companion_.dozing = false;
        if (companion_.rig.emotion() == companion::Emotion::sleepy) companion_.rig.flash(companion::Emotion::shock, 1.4f);
    }
    // Speech: characters appear at her pace, each one moving her mouth (the guest's for her lines).
    const auto speak = [&](avatar::Rig& rig, const std::wstring& text, const std::vector<companion::Segment>& emotions, size_t& shown, float& clock) {
        if (shown >= text.size()) return false;
        if (companionVoiceHolding()) return true; // Her text starts with her voice.
        const bool voiced = companion_.voice.pending || audio_.speaking(); // The audio moves her mouth instead.
        clock += dt * speech_rate;
        while (shown < text.size()) {
            const float cost = shown ? pause(text[shown - 1]) : 1.0f;
            if (clock < cost) break;
            clock -= cost;
            for (const auto& segment : emotions)
                if (segment.at == shown && segment.at && segment.emotion != rig.emotion()) rig.emote(segment.emotion);
            if (!voiced) rig.speak(text[shown]);
            ++shown;
        }
        if (shown >= text.size() && !voiced) rig.silence();
        return true;
    };
    auto& chat = companion_.chat;
    bool speaking = false, guest_speaking = false;
    if (chat_open) {
        if (tickCompanionFeed(dt)) moving = true;
        chat.age += dt; chat.pose_age += dt;
        // Her enlarged photo fades and grows in, and out again.
        if (const float target = chat.viewer ? 1.0f : 0.0f; chat.viewer_motion != target) {
            chat.viewer_motion = !settings_.screen_transitions ? target : target > chat.viewer_motion ? std::min(target, chat.viewer_motion + dt / 0.18f)
                                                                                                    : std::max(target, chat.viewer_motion - dt / 0.14f);
            moving = true;
        }
        // Her 3D Mind & Memory screen fades in and keeps the holographic sphere gently rotating.
        if (const float target = chat.mind ? 1.0f : 0.0f; chat.mind_motion != target) {
            chat.mind_motion = !settings_.screen_transitions ? target : target > chat.mind_motion ? std::min(target, chat.mind_motion + dt / 0.22f)
                                                                                                  : std::max(target, chat.mind_motion - dt / 0.16f);
            moving = true;
        }
        if (chat.mind || chat.mind_motion > 0.003f) {
            if (chat.mind) chat.mind_age = settings_.screen_transitions ? chat.mind_age + dt : companion_ui::Layout::mind_build_seconds;
            const float prev_yaw = chat.mind_yaw;
            if (!chat.mind_dragging) chat.mind_yaw += dt * 0.18f;
            // Every few seconds in the mind view she strikes a new expression/pose hop in her portrait.
            if (static_cast<int>(prev_yaw * 1.3f) != static_cast<int>(chat.mind_yaw * 1.3f)) {
                static constexpr std::array<companion::Emotion, 8> lively_emotions{
                    companion::Emotion::playful, companion::Emotion::smug, companion::Emotion::smile, companion::Emotion::thinking,
                    companion::Emotion::excited, companion::Emotion::gaming, companion::Emotion::drink, companion::Emotion::wave};
                const size_t pick = static_cast<size_t>(std::abs(static_cast<int>(chat.mind_yaw * 10.0f))) % lively_emotions.size();
                companion_.rig.emote(lively_emotions[pick]);
            }
            if (settings_.screen_transitions) {
                chat.mind_yaw_spring.step(chat.mind_yaw, dt, spring::glide.omega, spring::glide.zeta);
                chat.mind_pitch_spring.step(chat.mind_pitch, dt, spring::glide.omega, spring::glide.zeta);
                chat.mind_zoom_spring.step(chat.mind_zoom, dt, 18.0f, 1.0f); // A softer glide for the zoom.
            } else {
                chat.mind_yaw_spring.snap(chat.mind_yaw);
                chat.mind_pitch_spring.snap(chat.mind_pitch);
                chat.mind_zoom_spring.snap(chat.mind_zoom);
            }
            moving = true;
        }
        if (tickCompanionRoulette(dt)) moving = true;
        if (tickCompanionPhoto(dt)) moving = true;
        if (chat.waiting && chat.hesitating) {
            const float prev = chat.hesitation_clock;
            chat.hesitation_clock += dt;
            if (prev < 0.9f && chat.hesitation_clock >= 0.9f)
                companion_.rig.flash(companion::Emotion::shy, 0.55f, companion::Emotion::thinking);
            moving = true;
        }
        // "Pisze…" (whole messages): whatever she says appears at once, never letter by letter.
        if (settings_.companion_typing && chat.revealing()) chat.reveal = chat.log.back().text.size();
        if (!chat.log.empty() && !chat.log.back().user) {
            auto& message = chat.log.back();
            auto& rig = companionRig(message.speaker);
            (&rig == &companion_.rig ? speaking : guest_speaking) = speak(rig, message.text, message.emotions, chat.reveal, chat.reveal_clock);
        }
        // The next message of her reply, after a moment of typing (with "Pisze…", as long as typing it takes).
        if (!chat.revealing() && !chat.queue.empty() && !companionVoiceHolding()) {
            chat.queue_clock += dt;
            if (chat.queue_clock >= (settings_.companion_typing ? wholeTypingSeconds(chat.queue.front().text) : typingPause(chat.queue.front()))) {
                chat.log.push_back(std::move(chat.queue.front()));
                chat.queue.erase(chat.queue.begin());
                chat.reveal = settings_.companion_typing ? chat.log.back().text.size() : 0;
                chat.reveal_clock = 0; chat.queue_clock = 0; chat.scroll = 0;
                const auto& next = chat.log.back();
                companionRig(next.speaker).emote(next.emotions.empty() ? companion::calm : next.emotions.front().emotion);
            }
            moving = true;
        }
        if (tickCompanionGuest(dt)) moving = true;
        if (tickCompanionInitiative(dt)) moving = true;
        chat.scroll = std::clamp(chat.scroll, 0.0f, std::max(0.0f, renderer_.companionScrollLimit()));
        if (settings_.screen_transitions) chat.scroll_motion.step(chat.scroll, dt, spring::glide.omega, spring::glide.zeta);
        else chat.scroll_motion.snap(chat.scroll);
        if (!chat.scroll_motion.settled(chat.scroll, 0.2f)) moving = true; else chat.scroll_motion.snap(chat.scroll);
        // Her entrance: the full-figure pose, then her close-up (companion_ui::poseAlpha).
        moving = moving || chat.age < 0.5f || chat.pose_age < companion_ui::pose_hold + companion_ui::pose_out + 0.05f || chat.waiting;
    } else if (speaks) {
        auto& bubble = companion_.bubble;
        speaking = speak(companion_.rig, bubble.text, bubble.emotions, bubble.reveal, bubble.reveal_clock);
        if (!speaking && !bubble.text.empty()) bubble.shown += dt;
        const float target = bubble.waiting || (!bubble.text.empty() && bubble.shown < 14.0f) ? 1.0f : 0.0f;
        const float previous = bubble.alpha;
        bubble.alpha = target > bubble.alpha ? std::min(1.0f, bubble.alpha + dt / 0.2f) : std::max(0.0f, bubble.alpha - dt / 0.6f);
        if (bubble.alpha != previous) moving = true;
        if (bubble.waiting && !companion_.busy && bubble.text.empty()) bubble.waiting = false;
        pumpCompanionEvents();
    }
    // Only the selected preview moves; the other cards keep a neutral, still pose.
    const bool previews = settings_.companion && companion::lookCount() > 0 && (companion_.picker.open ||
        (settingsOpen() && settings_state_.category == static_cast<int>(settings_ui::Category::companion)));
    if (previews) {
        while (companion_.previews.size() < static_cast<size_t>(companion::lookCount()))
            companion_.previews.emplace_back(23 + 6 * static_cast<int>(companion_.previews.size()));
        const auto& picker = companion_.picker;
        const int selected = static_cast<int>(picker.open && !picker.choices.empty() ? picker.choices[static_cast<size_t>(picker.focus)] : companionLook());
        if (companion_.preview_look != selected) {
            companion_.preview_look = selected;
            companion_.preview_clock = 0;
            companion_.previews[static_cast<size_t>(selected)] = avatar::Rig{23 + 6u * static_cast<uint32_t>(selected)};
        }
        companion_.preview_clock += dt;
        if (companion_.preview_clock > 2.6f) {
            companion_.preview_clock = 0;
            companion_.preview_emotion = (companion_.preview_emotion + 1) % companion::emotion_count;
            companion_.previews[static_cast<size_t>(selected)].emote(static_cast<companion::Emotion>(companion_.preview_emotion));
        }
        companion_.previews[static_cast<size_t>(selected)].step(dt);
    }
    if (tickCompanionVoice(dt)) speaking = true;
    const bool visible = (home && companion_.tile_progress > 0) || chat_open;
    if (visible) companion_.rig.step(dt, !speaking);
    if (chat_open && (companion_.guest.look || companion_.guest.motion > 0)) companion_.guest.rig.step(dt, !guest_speaking);
    speaking = speaking || guest_speaking;
    // Idle animation only while someone is watching and recently active; speech always plays.
    const bool lively = speaking || chat_open || companion_.bubble.alpha > 0 || std::chrono::steady_clock::now() - last_input_ < std::chrono::seconds(60);
    return moving || speaking || (windowWatched() && ((visible && lively) || previews));
}

std::vector<std::wstring> Launcher::companionDiagnosticsLines() const {
    const bool en = english();
    const auto seconds = [en](uint64_t ms) {
        if (ms < 1000) return std::to_wstring(ms) + L" ms";
        const auto tenths = (ms + 50) / 100;
        return std::to_wstring(tenths / 10) + (en ? L"." : L",") + std::to_wstring(tenths % 10) + L" s";
    };
    std::vector<std::wstring> out;
    out.push_back(tr(L"Diagnostyka"));
    const auto now = GetTickCount64();
    if (companion_.busy && companion_.asked) out.push_back(std::wstring(tr(L"Czekam na model: ")) + seconds(now - companion_.asked) + L"…");
    const auto& recent = companion_.diagnostics;
    if (recent.empty()) {
        if (!companion_.busy) out.push_back(tr(L"Jeszcze nic nie zmierzono."));
        return out;
    }
    const auto& last = recent.back();
    const auto& timing = last.timing;
    out.push_back(std::wstring(tr(L"Ostatnia odpowiedź: ")) + seconds(last.total_ms) + (last.model.empty() ? std::wstring(L" · ") + tr(L"bez modelu (offline)") : L" · " + last.model));
    out.push_back(std::wstring(tr(L"kolejka ")) + seconds(timing.queued_ms) + tr(L" · przygotowanie ") + seconds(timing.prepare_ms) + tr(L" · model ") + seconds(timing.model_ms));
    for (size_t i = 0; i < timing.trace.calls.size(); ++i) {
        const auto& call = timing.trace.calls[i];
        std::wstring line = std::wstring(tr(L"zapytanie ")) + std::to_wstring(i + 1) + L": " + seconds(call.ms);
        if (call.status != 200) line += L" · HTTP " + std::to_wstring(call.status);
        if (call.prompt_tokens >= 0) line += L" · " + std::to_wstring(call.prompt_tokens) + L" → " + std::to_wstring(std::max(call.output_tokens, 0)) + tr(L" tokenów");
        if (call.thought_tokens > 0) line += tr(L" · myśli ") + std::to_wstring(call.thought_tokens);
        if (call.copies) line += tr(L" · kopie: ") + std::to_wstring(call.copies) + (call.answered_by ? tr(L", odpowiedziała kopia ") + std::to_wstring(call.answered_by)
                                                                                                   : std::wstring(tr(L", odpowiedział oryginał")));
        out.push_back(std::move(line));
    }
    if (!timing.trace.tools.empty()) {
        std::wstring line = tr(L"narzędzia: ");
        for (size_t i = 0; i < timing.trace.tools.size(); ++i)
            line += (i ? L", " : L"") + text_encoding::wideLenient(timing.trace.tools[i].name) + L" " + seconds(timing.trace.tools[i].ms);
        out.push_back(std::move(line));
    }
    if (last.voiced) out.push_back(last.voice_ms ? std::wstring(tr(L"głos: pierwsze zdanie po ")) + seconds(*last.voice_ms) :
                                   std::wstring(tr(L"głos: czeka… ")) + seconds(now - last.arrived));
    if (last.chars)
        out.push_back(std::wstring(tr(L"pisanie: ok. ")) + seconds(static_cast<uint64_t>(last.typing_seconds * 1000)) + L" · " + std::to_wstring(last.chars) + tr(L" znaków") +
                      (last.messages > 1 ? L" · " + std::to_wstring(last.messages) + tr(L" wiadomości") : L""));
    std::wstring prompt = std::wstring(tr(L"prompt: ")) + std::to_wstring(timing.system_chars + timing.turn_chars) + tr(L" znaków") + L" · " + std::to_wstring(timing.turns) + tr(L" tur");
    if (timing.tools) prompt += tr(L" · narzędzia");
    if (last.timing.burst) prompt += tr(L" · krótkie wiadomości");
    out.push_back(std::move(prompt));
    if (recent.size() > 1) {
        std::wstring earlier = tr(L"wcześniej: ");
        for (size_t i = recent.size() - 1; i-- > 0;) earlier += seconds(recent[i].total_ms) + (i ? L" · " : L"");
        out.push_back(std::move(earlier));
    }
    // This conversation, measured: how often she asks, her length against his, repeated openings and
    // stories, her curiosity's three paths and what was on her mind most.
    {
        int replies = 0, questions = 0, repeats = 0, retold = 0, her = 0, his = 0, clarify = 0, lead = 0, reflect = 0;
        std::map<std::string, int> sources;
        for (const auto& m : companion_.turn_metrics) {
            if (m.conversation != companion_.chat_started) continue;
            ++replies; questions += m.question; repeats += m.repeated_opening; retold += m.retold;
            her += m.her_words; his += m.his_words;
            clarify += m.curiosity == companion::CuriousMove::clarify;
            lead += m.curiosity == companion::CuriousMove::lead;
            reflect += m.curiosity == companion::CuriousMove::reflect;
            for (const auto& source : m.workspace) ++sources[source];
        }
        if (replies) {
            const int ratio_tenths = his ? (her * 10 + his / 2) / his : 0;
            out.push_back(std::wstring(tr(L"rozmowa: ")) + std::to_wstring(replies) + tr(L" odpowiedzi · pytania ") + std::to_wstring(questions * 100 / replies) + L"%" +
                          (his ? std::wstring(tr(L" · jej/jego długość ")) + std::to_wstring(ratio_tenths / 10) + (en ? L"." : L",") + std::to_wstring(ratio_tenths % 10) + L"×" : std::wstring{}));
            out.push_back(std::wstring(tr(L"ciekawość: dopytania ")) + std::to_wstring(clarify) + tr(L" · pociągnięcia ") + std::to_wstring(lead) + tr(L" · odbicia ") +
                          std::to_wstring(reflect) + tr(L" · powtórzone początki ") + std::to_wstring(repeats) + tr(L" · powtórzone historie ") + std::to_wstring(retold));
            std::vector<std::pair<std::string, int>> ranked(sources.begin(), sources.end());
            std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
            if (!ranked.empty()) {
                std::wstring line = tr(L"w głowie: ");
                for (size_t i = 0; i < ranked.size() && i < 5; ++i)
                    line += (i ? L", " : L"") + text_encoding::wideLenient(ranked[i].first) + L" ×" + std::to_wstring(ranked[i].second);
                out.push_back(std::move(line));
            }
        }
    }
    return out;
}

void Launcher::companionView(CompanionView& view) const {
    view.feed = &companion_.feed.view;
    view.look = lookOf(settings_);
    view.name = companion::name(view.look);
    view.persona = companion::personaName(companionPersona());
    const auto device = hintDevice();
    view.key = device == HintDevice::keyboard ? L"C" : device == HintDevice::playstation ? L"L2" : device == HintDevice::nintendo ? L"ZL" : L"LT";
    // The model only when asked for (Settings → "Pokaż model w czacie"); problems always.
    if (!companion_.status.empty()) view.status = companion_.status;
    else if (settings_.companion_model_info) {
        companion::Connection shown;
        shown.provider = companionAccess().provider; shown.model = companionAccess().model;
        view.status = companion_.model_label.empty() ? shown.label() : companion_.model_label;
    } else view.status.clear();
    // Her voice is credited to ElevenLabs (their free plan asks for it), with the voice she speaks with;
    // when it fails, why.
    if (companionVoiceFor(true) && companion_.voice.engine == CompanionVoice::Engine::ready) {
        const auto name = companionVoiceName(companionLook());
        view.status += (view.status.empty() ? L"" : L" · ") + std::wstring(L"ElevenLabs") + (name.empty() ? L"" : L": " + name);
        if (const auto problem = companionVoiceProblem(); !problem.empty()) view.status += L" (" + problem + L")";
    } else if (companionVoiceFor(true) && companion_.voice.engine == CompanionVoice::Engine::failed)
        view.status += (view.status.empty() ? L"" : L" · ") + std::wstring(tr(L"Głos: ")) + companionVoiceProblem();
    view.pose = companion_.rig.pose();
    const auto spots = homeWidgetSpots();
    const bool shown = settings_.companion && companionAvailable(true) && companion_.tile_progress > 0 && spots.companion;
    view.tile = shown && home_widgets::floating(spots.placement);
    view.pill = shown && spots.placement == home_widgets::Placement::pills;
    if (spots.companion) view.spot = *spots.companion;
    view.tile_alpha = settings_.screen_transitions ? glance_ui::ease(companion_.tile_progress) : 1;
    view.tile_hover = companion_.hover_motion.value;
    view.entrance = companion_.entrance;
    const auto& bubble = companion_.bubble;
    view.bubble = bubble.text.substr(0, std::min(bubble.reveal, bubble.text.size()));
    view.bubble_mentions = bubble.mentions;
    view.bubble_alpha = bubble.alpha;
    view.bubble_waiting = bubble.waiting;
    view.chat = companionOpen() ? &companion_.chat : nullptr;
    view.picker = companion_.picker.open ? &companion_.picker : nullptr;
    view.link_pointer = topModal() == Modal::none && !companion_.chat.menu && !companion_.chat.confirm && !companion_.chat.viewer && !companion_.chat.mind && !companion_.chat.feed &&
        !friends_open_ && !desktop_menu_ && !desktop_quick_.open && !desktop_meta_.open && !desktop_widgets_open_ && !desktop_downloads_open_ &&
        !keyboard_.open ? companion_.link_pointer : std::nullopt;
    view.photo = companion_.photo;
    view.chat_note = companionChatNote();
    view.can_restart = companion_.chat.ended && !companionResting();
    if (companion_.chat.archive) // A history page: when it was, in the header.
        view.status = std::wstring(tr(L"Historia")) + L" · " + companion::chats::label(companion_.archive_started, english());
    view.diagnostics = settings_.companion_debug && companionOpen() ? companionDiagnosticsLines() : std::vector<std::wstring>{};
    if (companionOpen() && (companion_.chat.mind || companion_.chat.mind_motion > 0.003f)) {
        view.mind_nodes = companionMindNodes();
        view.mind_telemetry = companionMindTelemetry();
        // Her mind's own words speak of you in both genders (‹on|ona›): as you are.
        const auto gender = companionPerson().gender;
        for (auto& node : view.mind_nodes)
            for (auto* text : {&node.label, &node.category, &node.detail, &node.when, &node.feeling_label})
                if (text->find(companion::person_open) != std::wstring::npos) *text = companion::personalize(*text, gender);
        auto& tel = view.mind_telemetry;
        for (auto* text : {&tel.bio, &tel.live_thought, &tel.live_thought_kind, &tel.mood})
            if (text->find(companion::person_open) != std::wstring::npos) *text = companion::personalize(*text, gender);
    } else {
        view.mind_nodes.clear();
        view.mind_telemetry = {};
    }
    // Gamification: the host's level and sympathy (loaded by tickCompanion).
    view.affinity = settings_.companion_gamification && companion_.affinity.has_value();
    view.affinity_pulse = companion_.affinity_pulse;
    view.affinity_card_pulse = companion_.affinity_card_pulse;
    view.streak = 0; view.affinity_card = 0;
    const auto count = static_cast<size_t>(companion::lookCount());
    view.preview_levels.assign(count, 0);
    view.preview_costumes.assign(count, 0);
    if (view.affinity) {
        const auto standing = companion::affinity::standing(companion::affinity::pointsOf(companion_.affinity->points, companion::lookKey(view.look)));
        view.affinity_level = standing.level; view.affinity_progress = standing.progress;
        const int today = dateKey(companion_.clock());
        view.streak = companion_.affinity->currentStreak(today);
        view.affinity_card = static_cast<int>(companion_.affinity->card(companion::lookKey(view.look), today));
        for (size_t i = 0; i < count; ++i) view.preview_levels[i] = companionLevel(static_cast<companion::Look>(i));
    }
    view.costume = companionCostume(view.look);
    for (size_t i = 0; i < count; ++i) view.preview_costumes[i] = companionCostume(static_cast<companion::Look>(i));
    // A guest in the chat: beside her, smaller, coming in or running off.
    view.guest.reset(); view.duo = 0; view.guest_leaving = false; view.guest_costume = 0;
    if (const auto& guest = companion_.guest; guest.look && *guest.look != view.look) {
        view.guest = guest.look;
        view.guest_costume = companionCostume(*guest.look);
        view.guest_name = companion::name(*guest.look);
        view.guest_persona = companion::personaName(companionPersonaOf(*guest.look));
        view.guest_pose = guest.rig.pose();
        view.duo = guest.motion;
        view.guest_leaving = !guest.shown;
    }
    view.previews.assign(count, avatar::Pose{});
    const auto& picker = companion_.picker;
    const auto preview = static_cast<size_t>(picker.open && !picker.choices.empty() ? picker.choices[static_cast<size_t>(picker.focus)] : companionLook());
    if (preview < count && preview < companion_.previews.size()) view.previews[preview] = companion_.previews[preview].pose();
}
