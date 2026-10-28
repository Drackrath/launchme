#include "bluetooth_controllers.hpp"
#include "bluez.hpp"
#include "controller_overrides.hpp"
#include "game_controllers.hpp"
#include "linux_hid.hpp"
#include "text_encoding.hpp"
#include "user_config.hpp"
#include "../i18n/i18n.hpp"
#include <algorithm>
#include <thread>

// BlueZ's paired gamepads (bluez.hpp); a connected pad is tied to its row by the Bluetooth address
// the kernel reports for its HID device (HID_UNIQ), never by a shared VID/PID or name.
namespace {
std::wstring controllerId(uint64_t value) {
    wchar_t id[32]{};
    swprintf_s(id, L"bluetooth:%012llx", static_cast<unsigned long long>(value));
    return id;
}
std::optional<uint64_t> address(std::wstring_view text) {
    if (text.size() != 12) return {};
    uint64_t result = 0;
    for (auto c : text) {
        c = static_cast<wchar_t>(std::towlower(c));
        if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'))) return {};
        result = result * 16 + static_cast<uint64_t>(c <= L'9' ? c - L'0' : c - L'a' + 10);
    }
    return result ? std::optional<uint64_t>{result} : std::nullopt;
}
const bluez::Device* find(const bluez::Objects& objects, uint64_t target) {
    for (const auto& device : objects.devices)
        if (bluez::gamepad(device) && (device.paired || device.bonded) && linux_hid::macAddress(device.address) == target) return &device;
    return nullptr;
}
}

std::vector<devices::Controller> controllerPanelControllers() {
    auto pads = enumerateControllers();
    for (auto& pad : pads) {
        const auto node = linux_hid::node(pad.id);
        if (!node || node->bus != linux_hid::bus_bluetooth) continue;
        pad.connection = L"Bluetooth";
        if (const auto mac = linux_hid::macAddress(node->uniq)) pad.bluetooth_id = controllerId(*mac);
    }
    bluez::Session session;
    const auto objects = session.objects();
    // Bluetooth off (or no adapter): the remembered pads stay listed, greyed.
    const bool radio = std::any_of(objects.adapters.begin(), objects.adapters.end(), [](const auto& adapter) { return adapter.powered; });
    for (const auto& device : objects.devices) {
        const auto mac = linux_hid::macAddress(device.address);
        if (!mac || !bluez::gamepad(device) || !(device.paired || device.bonded)) continue;
        const auto id = controllerId(*mac);
        const auto listed = std::find_if(pads.begin(), pads.end(), [&](const auto& pad) { return pad.bluetooth_id == id || pad.id == id; });
        if (listed != pads.end()) {
            if (!listed->battery && device.battery) listed->battery = device.battery;
            continue;
        }
        devices::Controller pad;
        pad.id = id;
        pad.name = text_encoding::wideLenient(device.name.empty() ? std::string("Bluetooth") : device.name);
        pad.connection = L"Bluetooth";
        pad.picture = devices::classifyController(device.vendor, device.product, pad.name);
        pad.battery = device.battery;
        if (radio) { pad.bluetooth_id = id; pad.connected = device.connected; }
        else { pad.connected = false; pad.radio_off = true; } // Not bluetooth_id: nothing can connect it until the radio is on.
        pads.push_back(std::move(pad));
    }
    loadControllerOverrides(pads, userConfigDirectory() / L"controller_overrides.json");
    return pads;
}

std::wstring setBluetoothControllerConnection(const std::wstring& id, bool connect,
                                             const std::function<bool()>& cancelled) {
    const auto target = id.starts_with(L"bluetooth:") ? address(std::wstring_view(id).substr(10)) : std::nullopt;
    if (!target) return tr(L"Nie znaleziono sparowanego pada Bluetooth.");
    bluez::Session session;
    const auto objects = session.objects();
    const auto* device = find(objects, *target);
    if (!device) return tr(L"Nie znaleziono sparowanego pada Bluetooth.");
    if (cancelled()) return tr(L"Anulowano operację Bluetooth.");
    if (device->connected == connect) return connect ? tr(L"Połączono") : tr(L"Rozłączono");
    const auto path = device->path;
    const auto error = session.call(path, "org.bluez.Device1", connect ? "Connect" : "Disconnect", cancelled, 30000);
    if (error == "cancelled") return tr(L"Anulowano operację Bluetooth.");
    if (!error.empty()) return tr(L"Błąd połączenia Bluetooth (") + text_encoding::wideLenient(bluez::shortError(error)) + L").";
    // A reply means BlueZ took the request; the property says whether the pad followed.
    const auto deadline = GetTickCount64() + 6000;
    do {
        if (cancelled()) return tr(L"Anulowano operację Bluetooth.");
        const auto now = session.objects();
        const auto* current = find(now, *target);
        if (current && current->connected == connect) return connect ? tr(L"Połączono") : tr(L"Rozłączono");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    } while (GetTickCount64() < deadline);
    return connect ? tr(L"Nie połączono. Włącz pada i ponów próbę.") : tr(L"Pad nadal jest połączony. Spróbuj w ustawieniach Bluetooth Windows.");
}
