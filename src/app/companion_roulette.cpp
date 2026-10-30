// The game roulette in her chat ("Wylosuj grę"): a reel of the installed games (never played ones
// twice as likely) spins in a card in the log, slows down and stops on the pick
// (companion_ui::Roulette, drawn by companion_chat_renderer.cpp). She is asked for her verdict at once;
// the answer waits in companion_.held until the reel stops, then a ▶ chip starts the game.
#include "launcher.hpp"

namespace {
bool english() { return i18n::english(); }
struct Candidate { std::wstring title; std::string key; };
}

bool Launcher::companionRouletteSpinning() const {
    const auto& log = companion_.chat.log;
    return !log.empty() && log.back().roulette && !log.back().roulette->landed();
}

void Launcher::companionRoulette() {
    auto& chat = companion_.chat;
    if (chat.waiting || chat.ended || chat.archive || companionRouletteSpinning()) { navigationBoundary(); return; }
    std::vector<Candidate> pool;
    for (const auto& game : public_library_) {
        if (!isGame(game) || !game.installed || game.kind == GameKind::catalog || game.visibility != GameVisibility::public_game ||
            locallyHidden(game) || game.name.empty() || placeholderGameName(game.name)) continue;
        Candidate candidate{game.name, gameKey(game)};
        if (candidate.key.empty()) continue;
        if (!game.last_played) pool.push_back(candidate);
        pool.push_back(std::move(candidate));
    }
    if (pool.empty()) { companionSend(tr(L"W co mam zagrać?")); return; } // Nothing installed to spin: ask her instead.
    companion_.seed = companion_.seed * 1664525u + 1013904223u + static_cast<uint32_t>(GetTickCount64());
    const auto reel = companion::spinReel(pool.size(), companion_.seed);
    companion_ui::Roulette roulette;
    for (const auto index : reel) roulette.reel.push_back(pool[index].title);
    roulette.key = pool[reel.back()].key;
    const auto pick = roulette.reel.back();
    companionFlush(); // Her previous lines, finished.
    chat.log.push_back({false, {}, {}, false, std::move(roulette), false, companionLook()});
    chat.chips.clear(); chat.scroll = 0; chat.focus = companion_ui::focus_input;
    chat.waiting = true;
    closeKeyboard();
    const bool en = english();
    companion_.turns.push_back({true, companion::rouletteKickoff(pick, en)});
    companion_.rig.emote(companion::Emotion::excited);
    requestCompanion(CompanionMessage::Purpose::chat, {}, companion::rouletteLine(companionPersona(), pick, en, companion_.seed));
    audio_.context(); InvalidateRect(hwnd_, nullptr, FALSE);
}

bool Launcher::tickCompanionRoulette(float dt) {
    auto& log = companion_.chat.log;
    if (log.empty() || !log.back().roulette || log.back().roulette->landed()) return false;
    auto& roulette = *log.back().roulette;
    const auto passed = static_cast<int>(roulette.position());
    roulette.age += dt;
    if (static_cast<int>(roulette.position()) != passed) audio_.navigate(); // A click per title.
    if (!roulette.landed()) return true;
    // Stopped: her verdict, if it came meanwhile (else the dots until it does).
    companion_.rig.emote(companion::Emotion::excited);
    audio_.quick();
    if (companion_.held) onCompanion(std::move(companion_.held));
    return true;
}
