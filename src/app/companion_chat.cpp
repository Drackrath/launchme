// Her chat's life cycle and typing: open, begin, close and reset, sending a message (the limit and
// her goodbye), the input field (typing, paste, erase), scrolling, the photo viewer and clearing.
#include "launcher.hpp"
#include "companion_app_internal.hpp"
#include "../platform/text_encoding.hpp"
#include "../platform/unix_time.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

using namespace companion_app;

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
        const auto unix_now = unix_time::now();
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
