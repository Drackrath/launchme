#pragma once
#include "../i18n/i18n.hpp"
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <array>
#include <deque>

namespace devices {
// sound: Settings → Dźwięk, the audio devices of `audio` plus the Bluetooth watcher of `bluetooth`
// (its devices flagged `audio` are headphones and speakers).
enum class Panel { controllers, audio, bluetooth, sound };
// Controller family shown in the devices panel. The first three values name the
// cached vendor PNGs (<value>.alpha-v2.png), so they must keep their numbers;
// every family is also drawn as vector art (ui/controller_art.cpp).
enum class Picture {
    none, dualsense, ultimate2, xbox,
    dualshock4, dualsense_edge, xbox360, xbox_elite, xbox_adaptive,
    switch_pro, joycon_left, joycon_right, joycon_pair,
    eightbitdo_pro, eightbitdo_ultimate, eightbitdo_sn,
    steam_controller, steam_deck, logitech, generic,
    steam_controller2, // 2026 / Triton: separate from the first generation; keep cache IDs stable.
    eightbitdo_zero2, // Yellow Zero 2; append new families to preserve downloaded image IDs.
    count
};
struct Controller {
    std::wstring id, name, connection;
    Picture picture = Picture::none;
    std::optional<int> battery;
    bool approximate = false, charging = false;
    // Panel-only connection management. The connected-device census stays live-only.
    std::wstring bluetooth_id;
    bool connected = true;
};
struct Endpoint { std::wstring id, name; bool input = false, active = false; };
struct BluetoothDevice { std::wstring id, name; bool paired = false, connected = false, audio = false; };
// Bluetooth class of device / LE appearance of headphones, speakers and headsets.
inline bool audioDevice(int cod_major, bool cod_audio, int le_category) {
    // Classic major class 4 = Audio/Video or the Audio service bit; LE appearance categories 0x21
    // audio sink, 0x22 audio source, 0x25 wearable audio (earbuds, headsets, headphones).
    return cod_major == 4 || cod_audio || le_category == 0x21 || le_category == 0x22 || le_category == 0x25;
}
struct AudioChannel {
    float volume = 0;
    bool available = false, muted = false;
    std::wstring endpoint;
    uint64_t acknowledged = 0;
};
struct Snapshot {
    std::vector<Controller> controllers;
    std::vector<Endpoint> endpoints;
    std::vector<BluetoothDevice> bluetooth;
    std::map<Picture, std::vector<uint8_t>> images;
    std::array<AudioChannel, 2> audio;
    bool busy = false;
    std::wstring status;
    uint64_t revision = 0;
};
enum class Action { refresh, volume, mute, endpoint, pair, cancel, connect, disconnect };
struct Command { Action action = Action::refresh; std::wstring id; float value = 0; bool input = false; uint64_t sequence = 0; };
// Slider motion is latest-wins per endpoint/direction, but cannot cross an
// endpoint switch or another command whose order matters (e.g. mute).
inline void queueCommand(std::deque<Command>& queue, Command command) {
    if (command.action == Action::volume) {
        for (auto it = queue.rbegin(); it != queue.rend() && it->action == Action::volume; ++it)
            if (it->input == command.input && it->id == command.id) { *it = std::move(command); return; }
    }
    queue.push_back(std::move(command));
}
// The same transaction is exercised with a fake writer in tests. Rollback covers
// every successful role, and reports when Windows rejects the rollback itself.
enum class SwitchResult { success, failed, rollback_failed };
inline SwitchResult switchRoles(const std::vector<std::wstring>& previous, const std::wstring& next,
                                const std::function<bool(int, const std::wstring&)>& write) {
    const auto saved = previous;
    for (int role = 0; role < static_cast<int>(previous.size()); ++role) {
        if (write(role, next)) continue;
        bool restored = true;
        for (int undo = role - 1; undo >= 0; --undo) restored = write(undo, saved[undo]) && restored;
        return restored ? SwitchResult::failed : SwitchResult::rollback_failed;
    }
    return SwitchResult::success;
}
inline std::wstring batteryLabel(const Controller& pad) {
    if (!pad.battery) return tr(L"Bateria: brak danych");
    return std::wstring(pad.approximate ? tr(L"Około ") : L"") + std::to_wstring(*pad.battery) + L"%" +
           (pad.charging ? tr(L" · ładowanie") : L"");
}
}
