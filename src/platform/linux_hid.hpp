#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Linux only: what sysfs and /proc say about input devices, for the controller readers
// (game_controllers_linux, steam_controller_linux, bluetooth_controllers_linux). Nothing here
// opens a device. The roots are parameters so tests can point them at a fake tree.
namespace linux_hid {
inline constexpr uint16_t bus_usb = 0x03, bus_bluetooth = 0x05;
struct Node {
    std::filesystem::path device; // The sysfs device (HID device, or the USB interface for xpad).
    uint16_t bus = 0, vendor = 0, product = 0;
    std::string name, uniq;       // uniq: the MAC address for Bluetooth pads.
};
// A HID device's uevent: HID_ID=0005:0000054C:00000CE6, HID_NAME=..., HID_UNIQ=....
std::optional<Node> parseUevent(std::string_view text);
// Usage page / usage of each top-level (application) collection in a report descriptor.
std::vector<std::pair<uint16_t, uint16_t>> topCollections(std::span<const uint8_t> descriptor);
// "aa:bb:cc:dd:ee:ff" → 0xaabbccddeeff.
std::optional<uint64_t> macAddress(std::string_view text);

struct Hidraw { std::filesystem::path node; Node hid; std::vector<uint8_t> descriptor; };
std::vector<Hidraw> hidraws(const std::filesystem::path& sys = "/sys", const std::filesystem::path& dev = "/dev");
// The device behind /dev/input/eventN or /dev/hidrawN (SDL's gamepad paths).
std::optional<Node> node(std::wstring_view path, const std::filesystem::path& sys = "/sys");
struct Battery { int level = 0; bool charging = false; };
// The kernel driver's power_supply (hid-playstation, hid-nintendo, xpad, hid-steam, ...).
std::optional<Battery> battery(const std::filesystem::path& device);
// A process of this user whose comm is `name` (Steam's client is "steam").
bool processRunning(std::string_view name, const std::filesystem::path& proc = "/proc");
}
