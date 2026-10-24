// A guest in her chat: another companion for a while (CompanionGuest). The player calls one from
// the chat's "…" menu — she stays until sent off with "Pożegnaj" — or, once a day, one drops in by
// herself after the player's 4th–6th message and runs off again two or three messages later (or
// after a long quiet). Meanwhile the model writes both of them, each message signed with a name
// (companion::lines); the guest stands behind the host, smaller, and both shrink to fit
// (companion_ui::Layout::pair). Settings → "Goście w czacie" switches visits off.
#include "launcher.hpp"
#include "../platform/atomic_file.hpp"
#include "../net/companion_characters.hpp"

#include <fstream>

namespace {
bool english() { return i18n::language() == i18n::Language::en; }
int dateKey(const SYSTEMTIME& time) { return time.wYear * 10000 + time.wMonth * 100 + time.wDay; }
// She steps in and makes the host shrink aside, or runs off.
constexpr float entrance_seconds = 0.55f, exit_seconds = 0.4f;
// A guest who dropped in runs off after this long without a word from the player.
constexpr float quiet_seconds = 45;
}

std::vector<companion::Look> Launcher::companionGuestCandidates() const {
    // Every companion that can be chosen ("soon" ones only have a card), but the host and the ones
    // asleep now by their own clocks.
    auto others = companion::availableLooks();
    std::erase(others, companionLook());
    std::erase_if(others, [&](companion::Look look) { return companionAsleep(look); });
    return others;
}

std::optional<companion::Guest> Launcher::companionGuestPrompt() const {
    const auto& guest = companion_.guest;
    if (!guest.look || *guest.look == companionLook()) return std::nullopt;
    return companion::Guest{*guest.look, companionPersonaOf(*guest.look)};
}

int Launcher::companionGuestDay() {
    // companion_guest.txt: the day one last dropped in by herself (once a day, across restarts).
    if (companion_.guest_day < 0) {
        std::string line;
        std::getline(std::ifstream(userConfigDirectory() / L"companion_guest.txt"), line);
        companion_.guest_day = std::atoi(line.c_str());
    }
    return companion_.guest_day;
}

std::wstring Launcher::companionGuestTyped() {
    auto& guest = companion_.guest;
    const bool en = english();
    const auto host = companionLook();
    ++companion_.typed;
    guest.quiet = 0;
    if (guest.look) {
        // Dropped in by herself: a couple of messages later she runs off.
        if (guest.invited || guest.leaving || --guest.stay > 0) return {};
        guest.leaving = true;
        companion_.turns.push_back({true, companion::guestKickoff(companion::GuestMoment::leave, host, *guest.look, en)});
        return companion::guestLine(companion::GuestMoment::leave, host, companionPersona(), *guest.look, companionPersonaOf(*guest.look), en, companion_.seed);
    }
    // Once a day, after a few messages, while her model answers (offline lines cannot hold a duet).
    if (!settings_.companion_guests || companion_.offline || companion_.typed != companion_.guest_after) return {};
    const int today = dateKey(companion_.clock());
    const auto others = companionGuestCandidates();
    if (others.empty() || companionGuestDay() == today) return {};
    companion_.guest_day = today;
    work_.start([file = userConfigDirectory() / L"companion_guest.txt", day = std::to_string(today)] { atomic_file::write(file, day + "\n"); });
    const uint32_t seed = companion_.seed = companion_.seed * 1664525u + 1013904223u;
    guest = {};
    guest.look = others[(seed >> 8) % others.size()];
    guest.stay = 2 + static_cast<int>((seed >> 4) % 2); // She stays for this many more messages.
    companion_.turns.push_back({true, companion::guestKickoff(companion::GuestMoment::drop_in, host, *guest.look, en)});
    return companion::guestLine(companion::GuestMoment::drop_in, host, companionPersona(), *guest.look, companionPersonaOf(*guest.look), en, seed);
}

void Launcher::companionInvite() {
    auto& chat = companion_.chat;
    const auto others = companionGuestCandidates();
    if (!settings_.companion_guests || companion_.guest.look || others.empty() || chat.waiting || companionRouletteSpinning()) { navigationBoundary(); return; }
    companionFlush();
    const bool en = english();
    const auto host = companionLook();
    const uint32_t seed = companion_.seed = companion_.seed * 1664525u + 1013904223u + static_cast<uint32_t>(GetTickCount64());
    auto& guest = companion_.guest;
    guest = {};
    guest.look = others[(seed >> 8) % others.size()];
    guest.invited = true;
    guest.shown = true; // She comes in at once; her hello follows.
    guest.rig.emote(companion::Emotion::smile);
    companion_.rig.emote(companion::Emotion::shock);
    companion_.turns.push_back({true, companion::guestKickoff(companion::GuestMoment::called, host, *guest.look, en)});
    chat.waiting = true; chat.chips.clear(); chat.scroll = 0; chat.focus = companion_ui::focus_input;
    closeKeyboard();
    companionHush();
    requestCompanion(CompanionMessage::Purpose::chat, {},
                     companion::guestLine(companion::GuestMoment::called, host, companionPersona(), *guest.look, companionPersonaOf(*guest.look), en, seed));
    audio_.quick();
}

void Launcher::companionDismiss() {
    const auto& guest = companion_.guest;
    if (!guest.look || guest.leaving || companion_.chat.waiting || companionRouletteSpinning()) { navigationBoundary(); return; }
    companionGuestLeave(companion::GuestMoment::dismissed);
}

void Launcher::companionGuestLeave(companion::GuestMoment moment) {
    auto& guest = companion_.guest;
    auto& chat = companion_.chat;
    if (!guest.look) return;
    companionFlush();
    const bool en = english();
    const auto host = companionLook();
    guest.leaving = true;
    companion_.turns.push_back({true, companion::guestKickoff(moment, host, *guest.look, en)});
    chat.waiting = true; chat.chips.clear(); chat.scroll = 0;
    companionHush();
    requestCompanion(CompanionMessage::Purpose::chat, {},
                     companion::guestLine(moment, host, companionPersona(), *guest.look, companionPersonaOf(*guest.look), en, companion_.seed));
    audio_.context();
}

void Launcher::companionGuestGone(bool told) {
    auto& guest = companion_.guest;
    if (!guest.look) return;
    const auto look = *guest.look;
    guest.look.reset();
    guest.invited = guest.leaving = guest.shown = false;
    guest.motion = 0; guest.quiet = 0; guest.stay = 0;
    guest.rig.silence();
    if (told && look != companionLook()) companion_.turns.push_back({true, companion::guestKickoff(companion::GuestMoment::gone, companionLook(), look, english())});
    const auto& items = companion_.chat.menu_items;
    const bool costumes = !items.empty() && items.front().kind == companion_ui::MenuItem::Kind::wear; // Her costume list stays.
    if (companion_.chat.menu && !costumes) companion_.chat.menu_items = companionMenuItems();
}

bool Launcher::tickCompanionGuest(float dt) {
    auto& guest = companion_.guest;
    auto& chat = companion_.chat;
    if (!guest.look) return false;
    // The host took her look in Settings, or visits were switched off: she is simply gone.
    if (*guest.look == companionLook() || (!settings_.companion_guests && !guest.invited && !guest.shown)) { companionGuestGone(false); return true; }
    const bool quiet = !chat.waiting && !chat.talking() && !companion_.busy;
    // A guest who dropped in and hears nothing for long runs off.
    if (guest.shown && !guest.invited && !guest.leaving && quiet) {
        guest.quiet += dt;
        if (guest.quiet >= quiet_seconds && !companionRouletteSpinning()) { companionGuestLeave(companion::GuestMoment::leave); return true; }
    }
    // Her goodbye said: off she goes.
    if (guest.leaving && guest.shown && quiet) guest.shown = false;
    const float target = guest.shown ? 1.0f : 0.0f, before = guest.motion;
    if (!settings_.screen_transitions) guest.motion = target;
    else if (target > guest.motion) guest.motion = std::min(target, guest.motion + dt / entrance_seconds);
    else if (target < guest.motion) guest.motion = std::max(target, guest.motion - dt / exit_seconds);
    // Away again after leaving: the host has the chat to herself.
    if (guest.leaving && !guest.shown && guest.motion <= 0) { companionGuestGone(true); return true; }
    return guest.motion != before;
}
