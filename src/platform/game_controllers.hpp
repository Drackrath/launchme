#pragma once
#include "../model/devices.hpp"
#include <Windows.h>
#include <memory>
#include <span>

// Guide (Xbox logo) / PS button. XInput has no public flag for it; the value is
// what XInputGetStateEx reports, and the DualSense parser maps PS to the same bit.
constexpr WORD pad_guide = 0x0400;
// Reserved XInput bit, normalized from analog R2/RT (also participates in wake/refocus gates).
constexpr WORD pad_right_trigger = 0x0800;

struct PadReading {
    bool connected = false;
    std::wstring id;
    WORD buttons = 0; // XINPUT_GAMEPAD_* plus pad_guide and pad_right_trigger.
    SHORT x = 0, y = 0;
    bool left_trigger = false; // Analog L2/LT past half way (no free bit is left in `buttons`).
    std::optional<int> battery;
    bool charging = false;
    std::wstring name;
};
// Pure parser: Bluetooth 0x01 (10 bytes, padded to 78 by Raw Input) is distinct
// from USB 0x01 (64 bytes); simple reports never fabricate battery information.
PadReading parseDualSense(std::span<const uint8_t> report);
// DualShock 4: USB 0x01, Bluetooth 0x11 (and its simple 0x01). No battery or rumble.
PadReading parseDualShock4(std::span<const uint8_t> report);
// Rumble-only output: USB 0x02 / Bluetooth 0x31 with HIDP CRC; no LEDs or triggers.
std::vector<uint8_t> dualSenseRumbleReport(bool bluetooth, bool running, uint8_t sequence = 0);
std::vector<devices::Controller> enumerateControllers();
// Windows setting "Open Xbox Game Bar using this button on a controller" (on by default).
bool gameBarUsesGuideButton();
class ControllerInput {
public:
    ControllerInput();
    ~ControllerInput();
    void initialize(HWND window);
    // Experimental: 2026 Steam Controller straight from HID (steam_controller.hpp). After initialize().
    void steamController(bool enabled);
    void rawInput(HRAWINPUT input);
    void deviceChanged(HANDLE device);
    PadReading read();
    void pulseBoundary();
    void stopRumble();
    void enrich(std::vector<devices::Controller>& controllers) const;
private:
    struct State;
    std::unique_ptr<State> state_;
};
