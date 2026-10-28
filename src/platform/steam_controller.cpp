#include "steam_controller.hpp"
#ifdef _WIN32
#include <xinput.h>
#endif
#include <algorithm>

// Reports shared by every system; the HID readers are per system
// (steam_controller_windows.cpp, steam_controller_linux.cpp).
namespace steam_controller {
namespace {
constexpr uint8_t report_state = 0x42, report_battery = 0x43, report_state_ble = 0x45, report_wireless_x = 0x46,
                  report_state_timestamp = 0x47, report_wireless = 0x79;
// SDL's TritonButtons. SDL names 0x40 "VIEW" and 0x4000 "MENU" but maps them to Start and
// Back; physically 0x40 is ≡ (right) and 0x4000 is ⧉ (left), as on an Xbox pad.
struct ButtonBit { uint32_t from; uint16_t to; };
constexpr ButtonBit button_map[]{
    {0x00000001, XINPUT_GAMEPAD_A}, {0x00000002, XINPUT_GAMEPAD_B}, {0x00000004, XINPUT_GAMEPAD_X},
    {0x00000008, XINPUT_GAMEPAD_Y}, {0x00000020, XINPUT_GAMEPAD_RIGHT_THUMB}, {0x00000040, XINPUT_GAMEPAD_START},
    {0x00000200, XINPUT_GAMEPAD_RIGHT_SHOULDER}, {0x00000400, XINPUT_GAMEPAD_DPAD_DOWN},
    {0x00000800, XINPUT_GAMEPAD_DPAD_RIGHT}, {0x00001000, XINPUT_GAMEPAD_DPAD_LEFT}, {0x00002000, XINPUT_GAMEPAD_DPAD_UP},
    {0x00004000, XINPUT_GAMEPAD_BACK}, {0x00008000, XINPUT_GAMEPAD_LEFT_THUMB}, {0x00010000, pad_guide},
    {0x00080000, XINPUT_GAMEPAD_LEFT_SHOULDER}};
int16_t le16(std::span<const uint8_t> bytes, size_t at) { return static_cast<int16_t>(bytes[at] | bytes[at + 1] << 8); }
}

bool supported(uint16_t vendor, uint16_t product) {
    return vendor == valve_vendor && product >= 0x1302 && product <= 0x1305;
}
bool inputCollection(uint16_t usage_page, uint16_t usage) { return usage_page == 0xff00 && usage == 0x0001; }

std::optional<PadReading> parseState(std::span<const uint8_t> report) {
    if (report.size() < 18 || (report[0] != report_state && report[0] != report_state_ble && report[0] != report_state_timestamp))
        return std::nullopt;
    const uint32_t bits = report[2] | report[3] << 8 | report[4] << 16 | static_cast<uint32_t>(report[5]) << 24;
    PadReading reading;
    reading.connected = true;
    reading.name = L"Steam Controller";
    for (const auto& [from, to] : button_map) if (bits & from) reading.buttons |= to;
    reading.x = le16(report, 10);
    reading.y = le16(report, 12);
    return reading;
}

std::optional<Battery> parseBattery(std::span<const uint8_t> report) {
    if (report.size() < 3 || report[0] != report_battery || report[2] > 100) return std::nullopt;
    return Battery{report[2], report[1] == 2}; // EChargeState: 1 discharging, 2 charging, 4 done.
}

std::optional<bool> parseWireless(std::span<const uint8_t> report) {
    if (report.size() < 2 || (report[0] != report_wireless && report[0] != report_wireless_x)) return std::nullopt;
    if (report[1] == 1) return false;
    if (report[1] == 2) return true;
    return std::nullopt;
}

std::vector<uint8_t> lizardReport(bool lizard, size_t length) {
    std::vector<uint8_t> bytes(std::max<size_t>(length, 6));
    bytes[0] = 0x01;            // Feature report ID.
    bytes[1] = 0x87;            // ID_SET_SETTINGS_VALUES
    bytes[2] = 3;               // One ControllerSetting: number + 16-bit value.
    bytes[3] = 9;               // SETTING_LIZARD_MODE
    bytes[4] = lizard ? 1 : 0;  // LIZARD_MODE_ON / LIZARD_MODE_OFF
    return bytes;
}

std::vector<uint8_t> clickReport(size_t length) {
    std::vector<uint8_t> bytes(std::max<size_t>(length, 4));
    bytes[0] = 0x82; // ID_OUT_REPORT_HAPTIC_COMMAND
    bytes[1] = 2;    // Both trackpads (0 = left, 1 = right).
    bytes[2] = 2;    // Click (1 = tick).
    bytes[3] = 0;    // Gain, dB.
    return bytes;
}

}
