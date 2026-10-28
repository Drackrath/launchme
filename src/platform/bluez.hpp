#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct sd_bus;

// Linux only: BlueZ (org.bluez) on the system D-Bus through sd-bus, for the Bluetooth rows of
// the devices panel (bluetooth_controllers_linux, device_service_linux). Reads BlueZ's cache;
// nothing here searches unless startDiscovery() is called.
namespace bluez {
struct Device {
    std::string path, adapter, address, name, icon;
    bool paired = false, bonded = false, connected = false;
    uint32_t device_class = 0;   // Classic class of device.
    uint16_t appearance = 0;     // LE appearance.
    uint16_t vendor = 0, product = 0;
    std::optional<int> battery;  // org.bluez.Battery1.
};
struct Adapter { std::string path; bool powered = false; };
struct Objects { bool available = false; std::vector<Adapter> adapters; std::vector<Device> devices; };

// Gamepad or joystick: icon "input-gaming", Class of Device peripheral/gamepad, LE appearance 0x03c3/0x03c4.
bool gamepad(const Device& device);
bool audio(const Device& device);
// Modalias "usb:v054Cp0CE6d0100" or "bluetooth:v…p…d…" → vendor, product.
std::optional<std::pair<uint16_t, uint16_t>> modalias(std::string_view text);
// One D-Bus error name to a short reason (org.bluez.Error.Failed → "Failed").
std::string shortError(std::string_view name);

// One system bus connection; use it on one thread. BlueZ stops a discovery when its client leaves.
class Session {
public:
    Session();
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    bool valid() const { return bus_ != nullptr; }
    Objects objects();
    // A method without arguments; checks `cancelled` every 100 ms. Empty on success, else the D-Bus
    // error name (NoReply on timeout), or "cancelled" (the call is then abandoned).
    std::string call(const std::string& path, const char* interface, const char* method,
                     const std::function<bool()>& cancelled, uint64_t timeout_ms);
    bool startDiscovery(const std::string& adapter);
    void stopDiscovery(const std::string& adapter);
private:
    sd_bus* bus_ = nullptr;
};
}
