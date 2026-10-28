#include "bluez.hpp"
#include <charconv>
#include <cstring>
#include <systemd/sd-bus.h>

namespace bluez {
namespace {
constexpr const char* service = "org.bluez";
struct Message {
    sd_bus_message* value = nullptr;
    ~Message() { sd_bus_message_unref(value); }
};
// One property value; anything else (arrays, dictionaries) is skipped.
void property(sd_bus_message* reply, std::string_view interface, std::string_view key, Device& device, Adapter& adapter) {
    char type = 0; const char* contents = nullptr;
    if (sd_bus_message_peek_type(reply, &type, &contents) <= 0 || type != 'v' || !contents) return;
    if (sd_bus_message_enter_container(reply, 'v', contents) <= 0) return;
    const std::string_view kind(contents);
    const char* text = nullptr; int flag = 0; uint32_t u32 = 0; uint16_t u16 = 0; uint8_t u8 = 0;
    if ((kind == "s" || kind == "o") && sd_bus_message_read_basic(reply, kind[0], &text) > 0 && text) {
        if (interface == "org.bluez.Device1") {
            if (key == "Address") device.address = text;
            else if (key == "Name") device.name = text;
            else if (key == "Icon") device.icon = text;
            else if (key == "Adapter") device.adapter = text;
            else if (key == "Modalias") if (const auto ids = modalias(text)) { device.vendor = ids->first; device.product = ids->second; }
        }
    } else if (kind == "b" && sd_bus_message_read_basic(reply, 'b', &flag) > 0) {
        if (interface == "org.bluez.Device1") {
            if (key == "Paired") device.paired = flag;
            else if (key == "Bonded") device.bonded = flag;
            else if (key == "Connected") device.connected = flag;
        } else if (interface == "org.bluez.Adapter1" && key == "Powered") adapter.powered = flag;
    } else if (kind == "u" && sd_bus_message_read_basic(reply, 'u', &u32) > 0) {
        if (interface == "org.bluez.Device1" && key == "Class") device.device_class = u32;
    } else if (kind == "q" && sd_bus_message_read_basic(reply, 'q', &u16) > 0) {
        if (interface == "org.bluez.Device1" && key == "Appearance") device.appearance = u16;
    } else if (kind == "y" && sd_bus_message_read_basic(reply, 'y', &u8) > 0) {
        if (interface == "org.bluez.Battery1" && key == "Percentage" && u8 <= 100) device.battery = u8;
    } else sd_bus_message_skip(reply, contents);
    sd_bus_message_exit_container(reply);
}
}

bool gamepad(const Device& device) {
    // Class of Device: peripheral major (5), joystick (1) or gamepad (2) minor; LE: 0x03c3 / 0x03c4.
    const auto minor = device.device_class & 0x3c;
    return device.icon == "input-gaming" || ((device.device_class & 0x1f00) == 0x500 && (minor == 0x04 || minor == 0x08)) ||
           device.appearance == 0x03c3 || device.appearance == 0x03c4;
}
bool audio(const Device& device) {
    return device.icon.starts_with("audio-") || ((device.device_class >> 8) & 0x1f) == 4 || (device.device_class & (1u << 21)) ||
           (device.appearance >> 6) == 0x21 || (device.appearance >> 6) == 0x22 || (device.appearance >> 6) == 0x25;
}
std::optional<std::pair<uint16_t, uint16_t>> modalias(std::string_view text) {
    const auto colon = text.find(':');
    if (colon == text.npos || text.size() < colon + 11 || text[colon + 1] != 'v' || text[colon + 6] != 'p') return {};
    uint16_t vendor = 0, product = 0;
    const auto* v = text.data() + colon + 2;
    if (std::from_chars(v, v + 4, vendor, 16).ptr != v + 4 || std::from_chars(v + 5, v + 9, product, 16).ptr != v + 9) return {};
    return std::pair{vendor, product};
}
std::string shortError(std::string_view name) {
    const auto dot = name.rfind('.');
    return std::string(dot == name.npos ? name : name.substr(dot + 1));
}

Session::Session() { if (sd_bus_open_system(&bus_) < 0) bus_ = nullptr; }
Session::~Session() { if (bus_) sd_bus_flush_close_unref(bus_); }

Objects Session::objects() {
    Objects result;
    if (!bus_) return result;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    Message reply;
    const int status = sd_bus_call_method(bus_, service, "/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects", &error, &reply.value, "");
    sd_bus_error_free(&error);
    if (status < 0 || sd_bus_message_enter_container(reply.value, 'a', "{oa{sa{sv}}}") <= 0) return result;
    result.available = true;
    auto* m = reply.value;
    while (sd_bus_message_enter_container(m, 'e', "oa{sa{sv}}") > 0) {
        const char* path = nullptr;
        if (sd_bus_message_read_basic(m, 'o', &path) <= 0 || sd_bus_message_enter_container(m, 'a', "{sa{sv}}") <= 0) break;
        Device device; Adapter adapter;
        bool is_device = false, is_adapter = false;
        while (sd_bus_message_enter_container(m, 'e', "sa{sv}") > 0) {
            const char* interface = nullptr;
            if (sd_bus_message_read_basic(m, 's', &interface) <= 0 || sd_bus_message_enter_container(m, 'a', "{sv}") <= 0) return result;
            const std::string_view name(interface);
            is_device |= name == "org.bluez.Device1"; is_adapter |= name == "org.bluez.Adapter1";
            while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
                const char* key = nullptr;
                if (sd_bus_message_read_basic(m, 's', &key) > 0) property(m, name, key, device, adapter);
                sd_bus_message_exit_container(m);
            }
            sd_bus_message_exit_container(m); sd_bus_message_exit_container(m);
        }
        sd_bus_message_exit_container(m); sd_bus_message_exit_container(m);
        if (is_device) { device.path = path; result.devices.push_back(std::move(device)); }
        if (is_adapter) { adapter.path = path; result.adapters.push_back(std::move(adapter)); }
    }
    return result;
}

std::string Session::call(const std::string& path, const char* interface, const char* method,
                          const std::function<bool()>& cancelled, uint64_t timeout_ms) {
    if (!bus_) return "org.freedesktop.DBus.Error.NoServer";
    struct Result { bool done = false; std::string error; } result;
    Message request;
    if (sd_bus_message_new_method_call(bus_, &request.value, service, path.c_str(), interface, method) < 0)
        return "org.freedesktop.DBus.Error.Failed";
    sd_bus_slot* slot = nullptr;
    const auto handler = [](sd_bus_message* reply, void* data, sd_bus_error*) -> int {
        auto* out = static_cast<Result*>(data);
        if (const auto* error = sd_bus_message_is_method_error(reply, nullptr) ? sd_bus_message_get_error(reply) : nullptr)
            out->error = error->name ? error->name : "org.freedesktop.DBus.Error.Failed";
        out->done = true;
        return 0;
    };
    if (sd_bus_call_async(bus_, &slot, request.value, handler, &result, timeout_ms * 1000) < 0) return "org.freedesktop.DBus.Error.Failed";
    std::string outcome;
    while (!result.done) {
        if (cancelled()) { outcome = "cancelled"; break; }
        const int processed = sd_bus_process(bus_, nullptr);
        if (processed < 0) { outcome = "org.freedesktop.DBus.Error.Disconnected"; break; }
        if (processed == 0) sd_bus_wait(bus_, 100 * 1000);
    }
    sd_bus_slot_unref(slot); // Abandons a call still pending.
    return result.done ? result.error : outcome;
}

bool Session::startDiscovery(const std::string& adapter) {
    return call(adapter, "org.bluez.Adapter1", "StartDiscovery", [] { return false; }, 5000).empty();
}
void Session::stopDiscovery(const std::string& adapter) {
    call(adapter, "org.bluez.Adapter1", "StopDiscovery", [] { return false; }, 5000);
}
}
