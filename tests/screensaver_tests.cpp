#include "../src/ui/screensaver.hpp"

#include <iostream>
#include <set>

using namespace screensaver;

int main() {
    using namespace std::chrono_literals;
    // Idle timer and start condition.
    Conditions ready{true, true, false, false, 60s, 1};
    if (!shouldStart(ready)) return 1;
    auto early = ready; early.idle = 59s;
    auto off = ready; off.enabled = false;
    auto background = ready; background.watched = false;
    auto playing = ready; playing.session = true;
    auto typing = ready; typing.text_entry = true;
    if (shouldStart(early) || shouldStart(off) || shouldStart(background) || shouldStart(playing) || shouldStart(typing)) return 2;
    auto ten = ready; ten.minutes = 10; ten.idle = 9min + 59s;
    if (shouldStart(ten)) return 3;
    ten.idle = 10min;
    if (!shouldStart(ten)) return 4;
    auto odd = ready; odd.minutes = 7; odd.idle = 61s; // Unknown values fall back to one minute.
    if (!shouldStart(odd) || normalizedMinutes(3) != 3 || normalizedMinutes(0) != 1 || normalizedMinutes(10) != 10) return 5;

    // Which games qualify for each style, from the artwork on disk.
    const std::vector<Candidate> candidates{
        {"steam:1", {true, false, true}},  // Hero and cover.
        {"steam:2", {false, true, false}}, // Poster only.
        {"steam:3", {false, false, true}}, // Cover only.
        {"steam:4", {false, false, false}},
        {"steam:1", {true, true, true}},   // A duplicate never plays twice as often.
        {"", {true, true, true}}};
    if (selection(candidates, Style::backgrounds) != std::vector<std::string>{"steam:1"} ||
        selection(candidates, Style::boxes) != std::vector<std::string>{"steam:1", "steam:2", "steam:3"} ||
        selection(candidates, Style::discs) != std::vector<std::string>{"steam:1", "steam:3"}) return 6;
    if (imageFor(Style::boxes, {false, true, true}) != Image::poster || imageFor(Style::boxes, {true, false, true}) != Image::cover ||
        imageFor(Style::backgrounds, {false, true, true}) != Image::none || imageFor(Style::discs, {true, true, false}) != Image::none) return 7;
    if (needed(Style::backgrounds).poster || needed(Style::backgrounds).cover || !needed(Style::boxes).poster ||
        !needed(Style::boxes).cover || needed(Style::discs).hero || needed(Style::discs).poster) return 8;

    // Shuffle: every pass is a permutation, deterministic per seed, and never repeats back to back.
    for (size_t size = 2; size <= 9; ++size) {
        std::vector<std::string> keys;
        for (size_t i = 0; i < size; ++i) keys.push_back("game:" + std::to_string(i));
        for (uint32_t seed = 1; seed <= 40; ++seed) {
            Playlist playlist;
            playlist.reset(keys, seed);
            std::string previous;
            for (int pass = 0; pass < 12; ++pass) {
                std::set<std::string> seen;
                for (size_t i = 0; i < size; ++i) {
                    const auto key = playlist.next();
                    if (key == previous) return 9;
                    if (!seen.insert(key).second) return 10;
                    previous = key;
                }
                if (seen.size() != size) return 11;
            }
        }
    }
    {
        Playlist first, second;
        first.reset({"a", "b", "c", "d", "e"}, 77);
        second.reset({"a", "b", "c", "d", "e"}, 77);
        for (int i = 0; i < 20; ++i) if (first.next() != second.next()) return 12;
        Playlist other;
        other.reset({"a", "b", "c", "d", "e"}, 78);
        first.reset({"a", "b", "c", "d", "e"}, 77);
        bool differs = false;
        for (int i = 0; i < 20; ++i) differs = differs || first.next() != other.next();
        if (!differs) return 13;
    }
    {
        Playlist empty;
        empty.reset({}, 5);
        if (!empty.next().empty() || !empty.empty()) return 14;
        Playlist single;
        single.reset({"only"}, 5);
        if (single.next() != "only" || single.next() != "only") return 15;
        // A game that cannot be shown leaves the rotation, also from the running pass.
        Playlist shrinking;
        shrinking.reset({"a", "b", "c"}, 9);
        const auto first = shrinking.next();
        const auto gone = first == "a" ? std::string("b") : std::string("a");
        shrinking.remove(gone);
        if (shrinking.size() != 2) return 16;
        for (int i = 0; i < 30; ++i) if (shrinking.next() == gone) return 17;
        shrinking.remove("a"); shrinking.remove("b"); shrinking.remove("c");
        if (!shrinking.empty() || !shrinking.next().empty()) return 18;
    }
    if (fade(0) != 0 || fade(fade_seconds * 0.5f) < 0.49f || fade(fade_seconds * 0.5f) > 0.51f || fade(100) != 1) return 19;

    // Input: a key wakes it; its auto-repeat, character and release never reach the launcher.
    InputGate gate;
    if (gate.keyDown('A', false) != Verdict::pass || gate.character() != Verdict::pass || gate.keyUp('A') != Verdict::pass) return 20;
    gate.activate(std::pair{100.0f, 100.0f});
    if (gate.keyUp(0x0D) != Verdict::swallow) return 21; // Released after "Start now": not a wake.
    if (gate.keyDown(0x0D, true) != Verdict::swallow || !gate.active()) return 22; // Held since before: not a wake.
    if (gate.character() != Verdict::swallow || !gate.active()) return 23;
    if (gate.keyDown(0x27, false) != Verdict::wake || gate.active()) return 24;
    if (gate.keyDown(0x27, true) != Verdict::swallow || gate.character() != Verdict::swallow) return 25;
    if (gate.keyUp(0x27) != Verdict::swallow || gate.keyUp(0x27) != Verdict::pass) return 26;
    if (gate.keyDown(0x27, false) != Verdict::pass || gate.character() != Verdict::pass) return 27;
    // Another key right after the waking one acts normally; the waking key's character does not linger.
    gate.activate(std::nullopt);
    if (gate.keyDown('W', false) != Verdict::wake || gate.keyDown(0x28, false) != Verdict::pass || gate.character() != Verdict::pass) return 28;

    // Mouse: small movement stays, more than 8 px ends it; the first move without an anchor sets one.
    gate.activate(std::pair{100.0f, 100.0f});
    if (gate.mouseMove(105, 104) != Verdict::swallow || !gate.active()) return 29;
    if (gate.mouseMove(109, 100) != Verdict::wake || gate.active()) return 30;
    if (gate.mouseMove(300, 300) != Verdict::pass) return 31;
    gate.activate(std::nullopt);
    if (gate.mouseMove(10, 10) != Verdict::swallow || gate.mouseMove(16, 10) != Verdict::swallow || !gate.active() ||
        gate.mouseMove(10, 19) != Verdict::wake) return 32;
    // A click wakes it and its double-click partner is swallowed; later ones pass.
    gate.activate(std::nullopt);
    if (gate.button(Button::left, 1000, 500) != Verdict::wake) return 33;
    if (gate.button(Button::left_double, 1300, 500) != Verdict::swallow || gate.button(Button::left_double, 1400, 500) != Verdict::pass) return 34;
    gate.activate(std::nullopt);
    if (gate.button(Button::left, 1000, 500) != Verdict::wake || gate.button(Button::left_double, 1600, 500) != Verdict::pass) return 35;
    // A right-click wakes it without opening the context menu on release.
    gate.activate(std::nullopt);
    if (gate.buttonUp(Button::left) != Verdict::pass || gate.button(Button::right, 0, 500) != Verdict::wake ||
        gate.buttonUp(Button::right) != Verdict::swallow || gate.buttonUp(Button::right) != Verdict::pass) return 36;
    gate.activate(std::nullopt);
    if (gate.wheel() != Verdict::wake || gate.wheel() != Verdict::pass) return 37;
    gate.activate(std::nullopt);
    if (gate.button(Button::middle, 0, 500) != Verdict::wake) return 38;

    // Pad: buttons/stick held when it starts ("Start now" with A) are ignored until released.
    constexpr uint16_t a_button = 0x1000, b_button = 0x2000;
    gate.activate(std::nullopt);
    if (gate.pad(a_button, false) != Verdict::swallow || gate.pad(a_button, false) != Verdict::swallow || !gate.active()) return 39;
    if (gate.pad(0, false) != Verdict::swallow || !gate.active()) return 40;
    if (gate.pad(a_button, false) != Verdict::wake || gate.active()) return 41; // Pressed again: a new press.
    // The waking button stays swallowed while held, then everything passes again.
    if (gate.pad(a_button, false) != Verdict::swallow || gate.pad(a_button | b_button, false) != Verdict::swallow) return 42;
    if (gate.pad(0, false) != Verdict::swallow || gate.pad(b_button, false) != Verdict::pass) return 43;
    gate.activate(std::nullopt);
    if (gate.pad(0, true) != Verdict::swallow || gate.pad(0, true) != Verdict::swallow || !gate.active()) return 44; // Stick held since before.
    if (gate.pad(0, false) != Verdict::swallow || gate.pad(0, true) != Verdict::wake) return 45;
    if (gate.pad(0, true) != Verdict::swallow || gate.pad(0, false) != Verdict::swallow || gate.pad(0, true) != Verdict::pass) return 46;
    gate.activate(std::nullopt);
    if (gate.pad(0, false) != Verdict::swallow || gate.pad(b_button, false) != Verdict::wake) return 47;
    // Ended by losing focus: nothing is left to swallow.
    gate.activate(std::nullopt);
    gate.pad(0, false);
    gate.deactivate();
    if (gate.active() || gate.pad(a_button, false) != Verdict::pass || gate.keyUp('A') != Verdict::pass ||
        gate.character() != Verdict::pass) return 48;
    std::cout << "Screensaver idle timer, playlist, shuffle and input gate passed\n";
    return 0;
}
