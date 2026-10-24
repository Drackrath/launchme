#include "../src/model/devices.hpp"
#include "../src/model/headphones.hpp"
#include "../src/platform/game_controllers.hpp"
#include "../src/platform/pad_rumble.hpp"
#include "../src/platform/steam_controller.hpp"
#include "../src/ui/device_panel.hpp"
#include "../src/platform/frame_clock.hpp"
#include <xinput.h>
#include <iostream>

int main() {
    for (const auto name : {L"ATH-M50xBT2", L"LE_ATH-M50xBT2", L"Słuchawki (LE_ATH-M50xBT2)",
                            L"Zestaw słuchawkowy (LE_ATH-M50xBT2 Hands-Free)", L"Headphones (ath-m50xbt2 Stereo)"})
        if (!headphones::supported(name)) return 80;
    for (const auto name : {L"", L"JBL Xtreme 3", L"ATH-M50x", L"ATH-M50xBT", L"ATH-M50xBT20", L"OtherATH-M50xBT2"})
        if (headphones::supported(name)) return 81;
    const auto rumble_usb = dualSenseRumbleReport(false, true);
    const auto rumble_bt = dualSenseRumbleReport(true, true, 7);
    if (rumble_usb.size() != 48 || rumble_usb[0] != 2 || rumble_usb[1] != 3 || rumble_usb[3] != 24 || rumble_usb[4] != 12 ||
        rumble_bt.size() != 78 || rumble_bt[0] != 0x31 || rumble_bt[1] != 0x70 || rumble_bt[2] != 0x10 ||
        rumble_bt[3] != 3 || rumble_bt[5] != 24 || rumble_bt[6] != 12 ||
        rumble_bt[74] != 0x66 || rumble_bt[75] != 0x03 || rumble_bt[76] != 0xc3 || rumble_bt[77] != 0xca) return 24; // Independent CRC32 fixture.
    for (size_t i = 5; i < rumble_usb.size(); ++i) if (rumble_usb[i]) return 25; // Never change triggers, lights or volume.
    for (size_t i = 1; i < 48; ++i) if (dualSenseRumbleReport(false, false)[i]) return 26;
    // The worker stops on its own without a UI tick; cancellation/destruction stop the original pad.
    std::mutex rumble_mutex;
    std::condition_variable rumble_wake;
    std::vector<std::pair<std::wstring, bool>> rumble_events;
    const auto output = [&](const PadRumble::Target& target, bool running) {
        { std::lock_guard lock(rumble_mutex); rumble_events.emplace_back(target.id, running); }
        rumble_wake.notify_one();
    };
    const auto wait_events = [&](size_t count) {
        std::unique_lock lock(rumble_mutex);
        return rumble_wake.wait_for(lock, std::chrono::seconds(2), [&] { return rumble_events.size() >= count; });
    };
    {
        PadRumble rumble(output); rumble.pulse({L"test-pad"});
        for (int i = 0; i < 30; ++i) rumble.pulse({L"wrong-pad"}); // Rate limit, no endless rumble / queued taps.
        if (!wait_events(2)) return 27;
    }
    if (rumble_events != std::vector<std::pair<std::wstring, bool>>{{L"test-pad", true}, {L"test-pad", false}}) return 28;
    {
        PadRumble rumble(output); rumble.pulse({L"cancel-pad"});
        if (!wait_events(3)) return 29;
        rumble.stop();
        if (!wait_events(4)) return 30;
    }
    {
        PadRumble rumble(output); rumble.pulse({L"closing-pad"});
        if (!wait_events(5)) return 31;
    }
    if (rumble_events.size() != 6 || rumble_events[3] != std::pair<std::wstring, bool>{L"cancel-pad", false} ||
        rumble_events[5] != std::pair<std::wstring, bool>{L"closing-pad", false}) return 32;
    for (int hz : {60, 120, 144, 240}) {
        FrameSchedule clock; clock.rate(hz, 10);
        for (int frame = 0; frame < 2000; ++frame) {
            const double now = 10 + static_cast<double>(frame) / hz + 0.00001;
            if (!clock.ready(now)) return 9;
            clock.consume(now);
            if (clock.ready(now) || clock.delay(now) > 1.0 / hz) return 10;
        }
        clock.consume(100); // A pause must not start an unbounded catch-up loop.
        if (clock.ready(100) || clock.delay(100) > 1.0 / hz + 0.00001) return 11;
        clock.rate(30, 101); if (!clock.ready(101) || clock.hz() != 30) return 12;
    }
    std::vector<uint8_t> usb(64); usb[0] = 1; usb[1] = usb[2] = 128; usb[8] = 0x22; usb[53] = 0x14;
    const auto reading = parseDualSense(usb);
    if (!reading.connected || reading.x || reading.y || reading.battery != 45 || !reading.charging ||
        !(reading.buttons & XINPUT_GAMEPAD_A) || !(reading.buttons & XINPUT_GAMEPAD_DPAD_RIGHT)) return 1;
    std::vector<uint8_t> bt(78); bt[0] = 0x31; bt[2] = bt[3] = 128; bt[9] = 0x48; bt[54] = 0x20;
    const auto wireless = parseDualSense(bt);
    if (wireless.battery != 100 || wireless.charging || wireless.buttons != XINPUT_GAMEPAD_B) return 2;
    std::vector<uint8_t> short_bt(10); short_bt[0] = 1; short_bt[5] = 8;
    if (!parseDualSense(short_bt).connected || parseDualSense(short_bt).battery) return 3;
    // Actual neutral DualSense Bluetooth report from Windows Raw Input: short
    // 0x01 payload padded to 78 bytes, not a USB report despite its buffer size.
    std::vector<uint8_t> padded_bt(78);
    const uint8_t neutral[] = {0x01, 0x80, 0x84, 0x82, 0x7c, 0x08, 0x00, 0x10, 0x00, 0x00};
    std::copy(std::begin(neutral), std::end(neutral), padded_bt.begin());
    const auto neutral_bt = parseDualSense(padded_bt);
    if (!neutral_bt.connected || neutral_bt.buttons || neutral_bt.x != 0 || neutral_bt.y != -1020 || neutral_bt.battery) return 33;
    padded_bt[5] = 0x22; // Cross + Right, at the simple report's button/hat offset.
    padded_bt[6] = 0x20; padded_bt[7] = 0x01; // Options + PS.
    padded_bt[8] = 0xff; padded_bt[9] = 0x7f; // Triggers never become navigation/buttons.
    if (parseDualSense(padded_bt).buttons != (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_DPAD_RIGHT | XINPUT_GAMEPAD_START | pad_guide) ||
        parseDualSense(padded_bt).battery) return 34;
    if (parseDualSense(std::span<const uint8_t>(usb).first(63)).connected) return 35;
    usb[53] = 0xf4;
    if (parseDualSense(usb).battery || parseDualSense({}).connected) return 4;
    // Square/triangle, Create/Options and PS reach the same actions as on an Xbox pad.
    usb[8] = 0x98; usb[9] = 0x30; usb[10] = 0x01;
    if (parseDualSense(usb).buttons != (XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_Y | XINPUT_GAMEPAD_BACK | XINPUT_GAMEPAD_START | pad_guide)) return 13;
    usb[8] = 0x08; usb[9] = usb[10] = 0;
    // R2 is a separate normalized shortcut, never mistaken for the hat / face buttons.
    usb[6] = 200;
    if (parseDualSense(usb).buttons != pad_right_trigger) return 94;
    usb[6] = 128;
    if (parseDualSense(usb).buttons) return 95;
    bt[7] = 255;
    if (parseDualSense(bt).buttons != (XINPUT_GAMEPAD_B | pad_right_trigger)) return 96;
    short_bt[9] = 255;
    if (parseDualSense(short_bt).buttons != pad_right_trigger) return 97;
    padded_bt[9] = 255;
    if (!(parseDualSense(padded_bt).buttons & pad_right_trigger)) return 98;
    // DualShock 4: USB 0x01 and Bluetooth 0x11 carry the same fields at different offsets.
    std::vector<uint8_t> ds4_usb(64, 0), ds4_bt(78, 0);
    ds4_usb[0] = 0x01; ds4_bt[0] = 0x11;
    for (auto* report : {&ds4_usb, &ds4_bt}) {
        const size_t at = report == &ds4_usb ? 1 : 3;
        (*report)[at + 0] = 128; (*report)[at + 1] = 0;        // Stick up.
        (*report)[at + 4] = 0x08 | 0x20;                       // Hat centred, Cross.
        (*report)[at + 5] = 0x20;                              // Options.
        (*report)[at + 6] = 0x01;                              // PS.
        (*report)[at + 8] = 200;                               // R2.
        const auto ds4 = parseDualShock4(*report);
        if (!ds4.connected || ds4.y < 30000 || ds4.buttons != (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_START | pad_guide | pad_right_trigger)) return 99;
    }
    if (parseDualShock4(std::vector<uint8_t>(64, 0)).connected) return 100; // Not an input report.
    std::vector<std::wstring> roles{L"speakers", L"headphones", L"communications"};
    int writes = 0;
    auto result = devices::switchRoles(roles, L"new", [&](int role, const std::wstring& id) {
        ++writes; if (role == 2 && id == L"new") return false; roles[role] = id; return true;
    });
    if (result != devices::SwitchResult::failed || writes != 5 || roles != std::vector<std::wstring>{L"speakers", L"headphones", L"communications"}) return 5;
    result = devices::switchRoles(roles, L"new", [&](int role, const std::wstring& id) { return role == 0 && id == L"new"; });
    if (result != devices::SwitchResult::rollback_failed) return 6;
    device_ui::State panel;
    panel.panel = devices::Panel::audio;
    for (int i = 0; i < 40; ++i) panel.data.endpoints.push_back({std::to_wstring(i), L"Endpoint", i > 20, false});
    panel.rebuild(); panel.selected = 30;
    device_ui::Layout{1600, 900, panel.panel}.ensure(panel);
    const auto selected_id = panel.rows[30].id;
    panel.data.endpoints.erase(panel.data.endpoints.begin()); panel.rebuild();
    if (panel.rows[panel.selected].id != selected_id || panel.audio_scroll[1] <= 0) return 7;
    device_ui::AudioLayout audio{1600, 900};
    if (audio.card(0).right >= audio.card(1).left || audio.maximum(panel, 0) <= 0 || audio.maximum(panel, 1) <= 0) return 13;
    // Up/Down follow the drawing: the mute button above the slider, Back above the mute button.
    panel.selected = 0; panel.audio_column = 0; panel.moveAudio(-1);
    if (panel.rows[panel.selected].kind != device_ui::Kind::mute || panel.rows[panel.selected].input) return 124;
    panel.switchAudioColumn(1);
    if (panel.rows[panel.selected].kind != device_ui::Kind::mute || !panel.rows[panel.selected].input) return 125;
    panel.moveAudio(-1);
    if (panel.selected != -1) return 126;
    panel.moveAudio(1); panel.moveAudio(1);
    if (panel.rows[panel.selected].kind != device_ui::Kind::volume || !panel.rows[panel.selected].input) return 127;
    panel.selected = 0; panel.audio_column = 0; panel.switchAudioColumn(1);
    if (panel.rows[panel.selected].kind != device_ui::Kind::volume || !panel.rows[panel.selected].input) return 14;
    panel.moveAudio(2);
    if (panel.rows[panel.selected].kind != device_ui::Kind::endpoint || !panel.rows[panel.selected].input) return 15;
    panel.audio_scroll = {}; audio.ensure(panel);
    const auto row = audio.row(panel, panel.selected);
    if (audio.hit(panel, row.left + 10, row.top + 10) != panel.selected) return 16;
    const auto mute = audio.mute(1);
    if (panel.rows[audio.hit(panel, mute.left + 10, mute.top + 10)].kind != device_ui::Kind::mute) return 17;
    const auto removed = panel.rows[panel.selected].id;
    std::erase_if(panel.data.endpoints, [&](const auto& endpoint) { return endpoint.id == removed; }); panel.rebuild();
    if (panel.rows[panel.selected].kind != device_ui::Kind::volume || !panel.rows[panel.selected].input) return 18;
    std::deque<devices::Command> queue;
    for (int i = 0; i < 100; ++i) {
        devices::queueCommand(queue, {devices::Action::volume, L"speakers", i / 100.0f, false, static_cast<uint64_t>(i + 1)});
        devices::queueCommand(queue, {devices::Action::volume, L"microphone", 1 - i / 100.0f, true, static_cast<uint64_t>(i + 1)});
    }
    if (queue.size() != 2 || queue[0].value != 0.99f || !queue[1].input || queue[1].sequence != 100) return 19;
    devices::queueCommand(queue, {devices::Action::endpoint, L"new-speakers"});
    devices::queueCommand(queue, {devices::Action::volume, L"new-speakers", 0.4f});
    if (queue.size() != 4 || queue[0].id != L"speakers" || queue[3].id != L"new-speakers") return 20;
    const device_ui::AudioLayout smaller{1280, 720};
    smaller.ensure(panel);
    if (smaller.card(0).bottom >= smaller.list_top || smaller.bottom() <= smaller.list_top) return 21;
    const auto audio_card = audio.card(0), audio_slider = audio.slider(0);
    if (audio.hit(panel, audio_card.left + 30, audio_card.top + 30) != -2 ||
        panel.rows[audio.hit(panel, audio_slider.left + 2, audio_slider.top + 2)].kind != device_ui::Kind::volume) return 22;
    device_ui::State controllers; controllers.panel = devices::Panel::controllers; controllers.rebuild();
    const device_ui::Layout controllers_layout{1600, 900, controllers.panel};
    const auto empty_button = controllers_layout.controllerEmptyButton();
    if (controllers_layout.hit(empty_button.left + 10, empty_button.top + 10, controllers.rows.size(), 0) != 0 ||
        controllers_layout.hit(controllers_layout.left() + 10, controllers_layout.list_top + 10, controllers.rows.size(), 0) != -2) return 23;
    devices::Controller pad;
    if (devices::batteryLabel(pad) != L"Bateria: brak danych") return 8;
    // Steam Controller (2026): neutral Puck state report captured on hardware (54 bytes, left stick Y = 896).
    std::vector<uint8_t> triton(54); triton[0] = 0x42; triton[1] = 0x7e; triton[12] = 0x80; triton[13] = 0x03;
    const auto neutral_sc = steam_controller::parseState(triton);
    if (!neutral_sc || !neutral_sc->connected || neutral_sc->buttons || neutral_sc->x || neutral_sc->y != 896 ||
        neutral_sc->name != L"Steam Controller" || neutral_sc->battery) return 40;
    // A, ≡ (0x40) = Start, RB, D-pad up, ⧉ (0x4000) = Back, Steam = Guide, LB; stick left/down.
    triton[2] = 0x41; triton[3] = 0x62; triton[4] = 0x09;
    triton[10] = 0x00; triton[11] = 0x80; triton[12] = 0x01; triton[13] = 0x80;
    auto pressed_sc = steam_controller::parseState(triton);
    if (!pressed_sc || pressed_sc->buttons != (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_START | XINPUT_GAMEPAD_RIGHT_SHOULDER |
        XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_BACK | pad_guide | XINPUT_GAMEPAD_LEFT_SHOULDER) ||
        pressed_sc->x != -32768 || pressed_sc->y != -32767) return 41;
    // Triggers, grips, paddles, QAM and trackpad touch never become navigation.
    std::vector<uint8_t> extras(54); extras[0] = 0x45; extras[2] = 0x90; extras[3] = 0x01; extras[4] = 0xf6; extras[5] = 0x3f;
    extras[6] = extras[8] = 0xff; extras[7] = extras[9] = 0x7f;
    if (!steam_controller::parseState(extras) || steam_controller::parseState(extras)->buttons) return 42;
    extras[0] = 0x47; if (!steam_controller::parseState(extras)) return 43;
    extras[0] = 0x43; if (steam_controller::parseState(extras) || steam_controller::parseState(std::span(triton).first(17))) return 44;
    const uint8_t battery_sc[] = {0x43, 0x01, 0x5f, 0x0e, 0x10}; // Captured: discharging, 95 %.
    const auto level = steam_controller::parseBattery(battery_sc);
    const uint8_t charging_sc[] = {0x43, 0x02, 0x40}, invalid_sc[] = {0x43, 0x02, 0xff};
    if (!level || level->level != 95 || level->charging || !steam_controller::parseBattery(charging_sc)->charging ||
        steam_controller::parseBattery(invalid_sc) || steam_controller::parseBattery(triton)) return 45;
    const uint8_t gone[] = {0x79, 0x01}, back[] = {0x46, 0x02}, other[] = {0x79, 0x03};
    if (steam_controller::parseWireless(gone) != false || steam_controller::parseWireless(back) != true ||
        steam_controller::parseWireless(other) || steam_controller::parseWireless(battery_sc)) return 46;
    const auto lizard_off = steam_controller::lizardReport(false), lizard_on = steam_controller::lizardReport(true);
    if (lizard_off.size() != 64 || lizard_off[0] != 1 || lizard_off[1] != 0x87 || lizard_off[2] != 3 || lizard_off[3] != 9 ||
        lizard_off[4] || lizard_on[4] != 1 || std::any_of(lizard_off.begin() + 5, lizard_off.end(), [](uint8_t b) { return b; })) return 47;
    const auto click = steam_controller::clickReport();
    if (click.size() != 64 || click[0] != 0x82 || click[1] != 2 || click[2] != 2 || click[3] ||
        std::any_of(click.begin() + 4, click.end(), [](uint8_t b) { return b; })) return 48;
    if (!steam_controller::supported(0x28de, 0x1302) || !steam_controller::supported(0x28de, 0x1305) ||
        steam_controller::supported(0x28de, 0x1102) || steam_controller::supported(0x28de, 0x11ff) ||
        steam_controller::supported(0x054c, 0x1304) || !steam_controller::inputCollection(0xff00, 1) ||
        steam_controller::inputCollection(0xff00, 2) || steam_controller::inputCollection(1, 5)) return 49;
    // No Reader: nothing listed, nothing claimed, no hardware touched.
    if (steam_controller::active() || !steam_controller::pads().empty() || steam_controller::known(L"x")) return 50;
    std::cout << "HID reports, honest battery, role rollback and hotplug focus passed\n";
}
