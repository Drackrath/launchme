// Her conversations between starts (the files: src/net/companion_chats.cpp). Opening the chat resumes
// today's conversation with the chosen companion (a new day starts a new one); every message is
// written on a worker. companionMessageLimit() is the length-dependent break threshold: on the player's
// last one her answer says goodbye, then she rests (her level and character decide how long) and
// "Nowa rozmowa" waits until she is back. Her night (companionAway, by her own clock) outlasts it:
// late, her goodbye is goodnight and she is back in her morning. "…" → "Historia czatu" shows the
// older ones read-only.
#include "launcher.hpp"
#include "../net/companion_person.hpp"
#include "../platform/user_config.hpp"

namespace {
namespace chats = companion::chats;
bool english() { return i18n::language() == i18n::Language::en; }

// A conversation's messages as the chat shows them; her photo is read from the photo folder (a
// missing one leaves no empty frame).
std::vector<companion_ui::Message> messagesOf(const chats::Conversation& conversation, const std::filesystem::path& pictures,
                                              std::shared_ptr<const std::vector<uint8_t>>& photo) {
    photo.reset();
    if (!conversation.photo.empty() && !pictures.empty()) {
        const auto path = pictures / conversation.photo;
        std::error_code ec;
        // Only inside the photo folder.
        const auto relative = std::filesystem::relative(path, pictures, ec);
        if (!ec && !relative.empty() && relative.native().rfind(L"..", 0) != 0)
            if (auto bytes = companion::photos::read(path)) photo = std::make_shared<const std::vector<uint8_t>>(std::move(*bytes));
    }
    std::vector<companion_ui::Message> out;
    for (const auto& line : conversation.lines) {
        if (line.photo && !photo) continue;
        companion_ui::Message message{line.user, line.text, line.emotions, line.error};
        message.speaker = line.speaker;
        message.mentions = line.mentions;
        message.photo = line.photo;
        message.free = line.free;
        if (line.roulette) {
            companion_ui::Roulette roulette;
            roulette.reel = {*line.roulette};
            roulette.key = line.roulette_key;
            roulette.age = companion_ui::Roulette::duration; // Landed long ago.
            message.roulette = std::move(roulette);
            message.text.clear();
        }
        out.push_back(std::move(message));
    }
    return out;
}
}

uint64_t Launcher::companionNow() const {
    const SYSTEMTIME local = companion_.clock();
    return chats::localSeconds(local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute, local.wSecond);
}

int Launcher::companionMessages() const {
    int count = 0;
    for (const auto* list : {&companion_.chat.log, &companion_.chat.queue})
        for (const auto& message : *list) if (!message.free) ++count;
    return count;
}

int Launcher::companionMessageLimit() const {
    return companion::replyStyle(settings_.companion_reply_length).messageLimit(companion_.message_cap);
}

void Launcher::companionTrimTurns() {
    auto& turns = companion_.turns;
    constexpr size_t keep = chats::context_turn_limit - 1; // The answer appended on completion still fits on reload.
    if (turns.size() <= keep) return;
    const size_t removed = turns.size() - keep;
    turns.erase(turns.begin() + 1, turns.begin() + 1 + static_cast<std::ptrdiff_t>(removed));
    const size_t already_summarized = companion_.summarized > 1 ? companion_.summarized - 1 : 0;
    companion_.summarized -= std::min(removed, already_summarized);
    companion_.summarized = std::min(companion_.summarized, turns.size());
}

bool Launcher::companionResting() const {
    if (!companion_.chat.ended) return false;
    return companionAway().seconds > 0; // Her rest, or her night (it outlasts the day of chats).
}

void Launcher::companionLoadChat() {
    const auto look = companionLook();
    auto& chat = companion_.chat;
    const auto now = companionNow();
    const auto fresh = [&] {
        // Her conversation goes (on disk it stays); the next one begins on opening.
        if (!chat.log.empty()) requestCompanionSummary();
        companion_.cancel->store(true);
        ++companion_.generation;
        companion_.busy = false;
        companion_.held.reset();
        companionHush();
        companion_.guest = {};
        const float age = chat.age, pose_age = chat.pose_age;
        chat = {};
        chat.age = age; chat.pose_age = pose_age;
        companion_.turns.clear(); companion_.summarized = 0; companion_.closing = false;
        companion_.photo.reset(); companion_.photo_file.clear();
    };
    if (companion_.chat_look != look) {
        if (companion_.chat_look) fresh(); // Another companion: hers stays saved.
        companion_.chat_look = look;
        companion_.chat_started = companion_.chat_updated = companion_.rest_until = 0;
        auto conversation = companion_.persist ? chats::latest(userConfigDirectory(), look) : std::nullopt;
        // Only today's: a new day, a new conversation.
        if (conversation && chats::dayOf(conversation->started) == chats::dayOf(now) && !conversation->lines.empty()) {
            const float age = chat.age, pose_age = chat.pose_age;
            chat = {};
            chat.age = age; chat.pose_age = pose_age;
            companionPhotoChat();
            chat.log = messagesOf(*conversation, settings_.companion_photo_folder, companion_.photo);
            if (!chat.log.empty() && !chat.log.back().user) chat.reveal = chat.log.back().text.size();
            chat.ended = conversation->ended;
            companion_.turns = conversation->turns;
            if (companion_.turns.empty()) companion_.turns.push_back({true, companion::kickoff(companion::Kickoff::chat, english())});
            companion_.summarized = std::min(conversation->summarized, companion_.turns.size());
            companion_.chat_started = conversation->started;
            companion_.chat_updated = conversation->updated;
            companion_.rest_until = conversation->rest_until;
            companion_.photo_file = companion_.photo ? conversation->photo : std::wstring{};
            companion_.typed = static_cast<int>(std::count_if(chat.log.begin(), chat.log.end(), [](const auto& message) { return message.user; }));
            companion_.photo_typed = companion_.typed;
            if (std::any_of(conversation->lines.begin(), conversation->lines.end(), [](const auto& line) { return line.photo; })) companion_.photo_asked = true;
            const uint32_t seed = companion_.seed = companion_.seed * 1664525u + 1013904223u + static_cast<uint32_t>(GetTickCount64());
            companion_.guest_after = companion_.typed + 2 + static_cast<int>((seed >> 5) % 3);
            companion_.burst_in = 3 + static_cast<int>((seed >> 9) % 2);
            companion_.closing = false;
        }
        return;
    }
    // The same companion, the launcher left open overnight: a new day starts a new conversation.
    if (!chat.log.empty() && !chat.waiting && !chat.talking() && companion_.chat_started && chats::dayOf(companion_.chat_started) != chats::dayOf(now)) fresh();
}

std::optional<companion::chats::Conversation> Launcher::companionChatSnapshot() const {
    const auto& chat = companion_.live_chat ? *companion_.live_chat : companion_.chat;
    if (!companion_.chat_look || !companion_.chat_started || (chat.log.empty() && chat.queue.empty())) return {};
    chats::Conversation conversation;
    conversation.look = *companion_.chat_look;
    conversation.started = companion_.chat_started;
    conversation.updated = std::max(companion_.chat_updated, companion_.chat_started);
    conversation.turns = companion_.turns;
    conversation.summarized = companion_.summarized;
    conversation.photo = companion_.photo_file;
    conversation.ended = chat.ended;
    conversation.rest_until = companion_.rest_until;
    for (const auto* list : {&chat.log, &chat.queue})
        for (const auto& message : *list) {
            chats::Line line;
            line.user = message.user; line.text = message.text; line.emotions = message.emotions; line.error = message.error; line.speaker = message.speaker;
            line.mentions = message.mentions;
            line.free = message.free;
            if (message.photo) {
                if (conversation.photo.empty()) continue; // Where it lies is unknown: not kept.
                line.photo = true;
            }
            if (message.roulette) {
                if (message.roulette->reel.empty()) continue;
                line.roulette = message.roulette->reel.back();
                line.roulette_key = message.roulette->key;
            }
            if (line.text.empty() && !line.photo && !line.roulette) continue;
            conversation.lines.push_back(std::move(line));
        }
    return conversation;
}

void Launcher::companionSaveChat() {
    if (!companion_.persist || companion_.chat.archive || companion_.live_chat) return;
    auto snapshot = companionChatSnapshot();
    if (!snapshot) return;
    auto conversation = std::move(*snapshot);
    conversation.updated = std::max(companionNow(), companion_.chat_started);
    companion_.chat_updated = conversation.updated;
    const auto io = companion_.chat_io;
    uint64_t number = 0;
    {
        std::lock_guard lock(io->mutex);
        number = io->newest[conversation.started] = ++io->counter;
    }
    work_.start([conversation = std::move(conversation), config = userConfigDirectory(), io, number] {
        std::lock_guard lock(io->mutex);
        if (io->newest[conversation.started] == number) chats::write(config, conversation);
    });
}

void Launcher::companionEnd(bool farewell) {
    auto& chat = companion_.chat;
    if (chat.ended || chat.archive) return;
    companion_.closing = false;
    const bool en = english();
    const uint32_t seed = companion_.seed = companion_.seed * 1664525u + 1013904223u + static_cast<uint32_t>(GetTickCount64());
    const bool bedtime = companionBedtime(); // Late by her clock: she goes to bed until her morning.
    if (farewell) { // Ended by the player ("Zakończ rozmowę"): her goodbye, in her style.
        companionFlush();
        companionHush();
        const auto line = companion::parseText(companion::personalize(chats::farewellLine(companionPersona(), en, seed, bedtime), companionPerson()));
        companion_ui::Message message{false, line.text, line.emotions, false};
        message.speaker = companionLook();
        chat.log.push_back(std::move(message));
        chat.reveal = 0; chat.reveal_clock = 0; chat.scroll = 0;
        companion_.turns.push_back({false, line.raw});
        if (!line.emotions.empty()) companion_.rig.emote(line.emotions.front().emotion);
        audio_.context();
    }
    companionGuestGone(true); // A guest goes with the conversation.
    chat.ended = true;
    chat.input.clear();
    closeKeyboard();
    // Only her games stay suggested; nothing is left to ask.
    std::erase_if(chat.chips, [](const auto& chip) { return chip.kind != companion_ui::Chip::Kind::game; });
    chat.focus = companion_ui::focus_send;
    const int level = settings_.companion_gamification ? companionLevel(companionLook()) : 10; // Off: a fifth of the way.
    companion_.rest_until = companionNow() + chats::restSeconds(companionPersona(), level, seed);
    if (const auto night = bedtime ? companionNight(companionLook()) : std::nullopt) {
        const auto now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        if (night->to > now) companion_.rest_until = std::max(companion_.rest_until, companionNow() + (night->to - now));
    }
    companion_.photo_pending.reset(); companion_.photo_file_pending.clear(); companion_.photo_description.clear();
    companionSaveChat();
    requestCompanionSummary(); // Notes on: the talk in a sentence for next time.
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::companionRestart() {
    if (!companion_.chat.ended || companion_.chat.archive || companionResting()) { navigationBoundary(); return; }
    companionClear();
}

std::wstring Launcher::companionChatNote() const {
    const auto& chat = companion_.chat;
    if (chat.archive) return tr(L"Archiwalna rozmowa — tylko do odczytu");
    if (!chat.ended) return {};
    const auto away = companionAway();
    const auto name = std::wstring(companion::name(companionLook()));
    if (away.sleeping) // Her night: asleep (also before anything was said today).
        return (chat.log.empty() ? name + tr(L" śpi, wstanie za ") : std::wstring(tr(L"Rozmowa zakończona · ")) + name + tr(L" śpi, wstanie za ")) +
               chats::duration(away.seconds, english());
    if (!away.seconds) return std::wstring(tr(L"Rozmowa zakończona · ")) + name + tr(L" już odpoczęła");
    return std::wstring(tr(L"Rozmowa zakończona · ")) + name + tr(L" odpoczywa, wróci za ") + chats::duration(away.seconds, english());
}

void Launcher::companionHistoryMenu() {
    auto& chat = companion_.chat;
    const bool en = english();
    const auto now = companionNow();
    companion_.history = chats::list(userConfigDirectory(), companionLook());
    std::erase(companion_.history, companion_.chat_started); // The live one is open already.
    if (companion_.history.size() > 12) companion_.history.resize(12);
    if (companion_.history.empty()) { navigationBoundary(); return; }
    chat.menu_items.clear();
    for (size_t i = 0; i < companion_.history.size(); ++i) {
        const auto started = companion_.history[i];
        chat.menu_items.push_back({companion_ui::MenuItem::Kind::archive, chats::label(started, en), true, static_cast<int>(i), chats::ago(started, now, en)});
    }
    chat.menu_focus = 0;
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::companionOpenArchive(int index) {
    if (index < 0 || index >= static_cast<int>(companion_.history.size()) || companion_.live_chat) { navigationBoundary(); return; }
    const auto started = companion_.history[static_cast<size_t>(index)];
    const auto conversation = chats::read(chats::file(userConfigDirectory(), companionLook(), started));
    if (!conversation) {
        showNotification(NotificationKind::error, tr(L"Historia czatu"), tr(L"Nie udało się odczytać tej rozmowy."));
        return;
    }
    companionFlush();
    companionHush();
    companion_.rig.silence();
    closeKeyboard();
    companion_.live_photo = companion_.photo;
    const float age = companion_.chat.age;
    companion_.live_chat = std::move(companion_.chat);
    companion_ui::Chat archive;
    archive.archive = true;
    archive.age = age;
    archive.pose_age = companion_ui::pose_hold + companion_ui::pose_out; // Her close-up at once.
    archive.log = messagesOf(*conversation, settings_.companion_photo_folder, companion_.photo);
    if (!archive.log.empty() && !archive.log.back().user) archive.reveal = archive.log.back().text.size();
    archive.focus = companion_ui::focus_back;
    companion_.chat = std::move(archive);
    companion_.archive_started = started;
    audio_.quick();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::companionCloseArchive() {
    if (!companion_.live_chat) return;
    companion_.chat = std::move(*companion_.live_chat);
    companion_.live_chat.reset();
    companion_.photo = std::move(companion_.live_photo);
    companion_.live_photo.reset();
    companion_.chat.menu = false;
    companion_.chat.hover = companion_ui::no_hit;
    companion_.chat.focus = companion_.chat.ended ? companion_ui::focus_send : companion_ui::focus_input;
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}
