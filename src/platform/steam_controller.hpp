#pragma once
#include "game_controllers.hpp"
#ifdef _WIN32
#include <Windows.h>
#endif
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Steam Controller (2026, "Triton") read directly over HID: no Steam, no ViGEm, no driver.
// Wired 28DE:1302, Bluetooth LE 1303, Puck receivers 1304 / 1305 (four slots, MI_02..MI_05).
// Game input comes only from the vendor collection 0xFF00/0x0001; 0x0002 is the Puck's dock.
// Protocol after SDL (joystick/hidapi/SDL_hidapi_steam_triton.c, steam/controller_structs.h).
namespace steam_controller {
inline constexpr uint16_t valve_vendor = 0x28de;
bool supported(uint16_t vendor, uint16_t product);
bool inputCollection(uint16_t usage_page, uint16_t usage);

// Pure parsers. State reports 0x42 (USB / Puck), 0x45 (BLE) and 0x47 (timestamped trackpads)
// share the layout up to the sticks: [1] sequence, [2..5] buttons, [6..9] triggers,
// [10..17] sticks, Y up. Buttons map by position, like on an Xbox pad; Steam = pad_guide.
std::optional<PadReading> parseState(std::span<const uint8_t> report);
struct Battery { int level = 0; bool charging = false; };
std::optional<Battery> parseBattery(std::span<const uint8_t> report); // 0x43
// Puck wireless status 0x46 / 0x79: true = connected, false = disconnected.
std::optional<bool> parseWireless(std::span<const uint8_t> report);
// Feature report 0x01: ID_SET_SETTINGS_VALUES (0x87), SETTING_LIZARD_MODE (9) = on/off.
std::vector<uint8_t> lizardReport(bool lizard, size_t length = 64);
// Output report 0x82 (haptic command): a click on both trackpads, the list-boundary tap.
std::vector<uint8_t> clickReport(size_t length = 64);

struct Pad {
    std::wstring path;      // HID path of the slot's vendor collection; also PadReading::id.
    uint16_t product = 0;
    PadReading reading;
    bool receiver = false;  // Through the Puck.
    bool bluetooth = false;
    bool input = false;     // Drives the launcher: Steam is not running.
};

// One background worker per process with shared-access HID handles, so Steam can still open
// the controller. Rescans every 2 s (hotplug, Puck slots). Lizard mode (the firmware's
// mouse/keyboard emulation) is kept off with a 2 s heartbeat only while `window` is in the
// foreground and Steam is not running; otherwise it is restored once. If the process dies,
// the firmware watchdog restores it by itself. While Steam runs, pads are listed only.
// Windows: Raw Input and overlapped HID I/O. Linux: /dev/hidraw* (needs read/write access, as
// Valve's steam-devices udev rules grant), Steam found through /proc, focus from the SDL window.
class Reader {
public:
#ifdef _WIN32
    explicit Reader(HWND window);
#else
    explicit Reader(void* window); // SDL_Window*; null counts as always in front.
#endif
    ~Reader();
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;
private:
    struct Worker;
    std::unique_ptr<Worker> worker_;
};
// Connected pads of the running Reader; empty without one. Any thread.
std::vector<Pad> pads();
bool active();
bool known(const std::wstring& path);
// Boundary tap on a pad from pads(); own handle, overlapped (Linux: non-blocking) write cancelled after 40 ms.
void click(const std::wstring& path);
}
