#include "bluetooth_controllers.hpp"
#include "controller_overrides.hpp"
#include "game_controllers.hpp"
#include "user_config.hpp"
#include <bluetoothapis.h>
#include <winioctl.h>
#include <bthioctl.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <devpkey.h>
#include <algorithm>
#include <array>

namespace {
std::optional<ULONGLONG> address(std::wstring_view text) {
    if (text.size() != 12) return {};
    ULONGLONG result = 0;
    for (auto c : text) {
        c = static_cast<wchar_t>(std::towlower(c));
        if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'))) return {};
        result = result * 16 + (c <= L'9' ? c - L'0' : c - L'a' + 10);
    }
    return result ? std::optional<ULONGLONG>{result} : std::nullopt;
}
std::wstring controllerId(ULONGLONG value) {
    wchar_t id[32]{};
    swprintf_s(id, L"bluetooth:%012llx", value);
    return id;
}
std::wstring bluetoothId(const std::wstring& path) {
    // Resolve the HID interface's parents: Classic HID paths themselves omit the
    // Bluetooth address. Never associate two pads by a shared VID/PID or name.
    std::array<wchar_t, MAX_DEVICE_ID_LEN> instance{};
    DEVPROPTYPE type{}; ULONG size = static_cast<ULONG>(sizeof(instance));
    if (CM_Get_Device_Interface_PropertyW(path.c_str(), &DEVPKEY_Device_InstanceId, &type,
        reinterpret_cast<PBYTE>(instance.data()), &size, 0) != CR_SUCCESS || type != DEVPROP_TYPE_STRING) return {};
    DEVINST node{};
    if (CM_Locate_DevNodeW(&node, instance.data(), CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS) return {};
    for (int depth = 0; depth < 8; ++depth) {
        if (CM_Get_Device_IDW(node, instance.data(), static_cast<ULONG>(instance.size()), 0) != CR_SUCCESS) break;
        std::wstring id(instance.data());
        for (auto& c : id) c = static_cast<wchar_t>(std::towlower(c));
        if (id.starts_with(L"bthenum\\")) {
            const auto start = id.find(L"dev_");
            if (start != id.npos && start + 16 < id.size() && id[start + 16] == L'\\')
                if (const auto found = address(std::wstring_view(id).substr(start + 4, 12))) return controllerId(*found);
            const auto last = id.rfind(L'&');
            if (last != id.npos && last + 13 < id.size() && id[last + 13] == L'_')
                if (const auto found = address(std::wstring_view(id).substr(last + 1, 12))) return controllerId(*found);
        }
        DEVINST parent{};
        if (CM_Get_Parent(&parent, node, 0) != CR_SUCCESS) break;
        node = parent;
    }
    return {};
}
bool gamepad(ULONG class_of_device) {
    // Bluetooth Class of Device: peripheral major, joystick/gamepad minor.
    const auto minor = class_of_device & 0x3c;
    return (class_of_device & 0x1f00) == 0x500 && (minor == 0x04 || minor == 0x08);
}
bool gamepad(const BLUETOOTH_DEVICE_INFO& info) { return gamepad(info.ulClassofDevice); }
// False when Windows has no Bluetooth radio switched on (off, airplane mode, no adapter).
template<class Visitor> bool visitPads(Visitor&& visit) {
    BLUETOOTH_FIND_RADIO_PARAMS params{sizeof(params)};
    HANDLE radio{};
    const auto radios = BluetoothFindFirstRadio(&params, &radio);
    if (!radios) return false;
    bool done = false;
    do {
        BLUETOOTH_DEVICE_SEARCH_PARAMS search{sizeof(search)};
        search.fReturnAuthenticated = TRUE; search.fReturnRemembered = TRUE; search.fReturnConnected = TRUE;
        search.hRadio = radio; // Cached devices only; no discovery/inquiry.
        BLUETOOTH_DEVICE_INFO info{sizeof(info)};
        const auto devices = BluetoothFindFirstDevice(&search, &info);
        if (devices) {
            do {
                if (gamepad(info) && (info.fAuthenticated || info.fRemembered) && visit(radio, info)) { done = true; break; }
            } while (BluetoothFindNextDevice(devices, &info));
            BluetoothFindDeviceClose(devices);
        }
        CloseHandle(radio);
    } while (!done && BluetoothFindNextRadio(radios, &radio));
    BluetoothFindRadioClose(radios);
    return true;
}
// The Bluetooth properties Windows keeps on a paired device's node ({2bd67d8b-…} 1 address,
// 7/8 VID/PID, 10 Class of Device); not every SDK names them.
constexpr GUID bluetooth_properties{0x2bd67d8b, 0x8beb, 0x48d5, {0x87, 0xe0, 0x6c, 0xda, 0x34, 0x28, 0x04, 0x0a}};
// With the radio off Windows removes the Bluetooth stack, so the API above lists nothing, but every
// paired Classic device keeps its node: BTHENUM\DEV_<address>\…, now not present.
std::vector<devices::Controller> pairedPadsWithoutRadio() {
    std::vector<devices::Controller> pads;
    const wchar_t* enumerator = L"BTHENUM";
    ULONG length = 0;
    if (CM_Get_Device_ID_List_SizeW(&length, enumerator, CM_GETIDLIST_FILTER_ENUMERATOR) != CR_SUCCESS || length < 2) return pads;
    std::vector<wchar_t> list(length);
    if (CM_Get_Device_ID_ListW(enumerator, list.data(), length, CM_GETIDLIST_FILTER_ENUMERATOR) != CR_SUCCESS) return pads;
    const auto property = [](DEVINST node, DEVPROPID id, DEVPROPTYPE expected, void* out, ULONG size) {
        const DEVPROPKEY key{bluetooth_properties, id};
        DEVPROPTYPE type{};
        return CM_Get_DevNode_PropertyW(node, &key, &type, static_cast<PBYTE>(out), &size, 0) == CR_SUCCESS && type == expected;
    };
    for (const wchar_t* id = list.data(); *id && id < list.data() + list.size(); id += wcslen(id) + 1) {
        std::wstring text(id);
        for (auto& c : text) c = static_cast<wchar_t>(std::towlower(c));
        // The device itself (bthenum\dev_<12 hex>\…), not one of its service nodes.
        if (!text.starts_with(L"bthenum\\dev_") || text.size() < 25 || text[24] != L'\\') continue;
        const auto found = address(std::wstring_view(text).substr(12, 12));
        DEVINST node{};
        if (!found || CM_Locate_DevNodeW(&node, const_cast<wchar_t*>(id), CM_LOCATE_DEVNODE_PHANTOM) != CR_SUCCESS) continue;
        ULONG class_of_device = 0;
        if (!property(node, 10, DEVPROP_TYPE_UINT32, &class_of_device, sizeof(class_of_device)) || !gamepad(class_of_device)) continue;
        USHORT vid = 0, pid = 0;
        property(node, 7, DEVPROP_TYPE_UINT16, &vid, sizeof(vid));
        property(node, 8, DEVPROP_TYPE_UINT16, &pid, sizeof(pid));
        std::array<wchar_t, 256> name{};
        DEVPROPTYPE type{}; ULONG size = static_cast<ULONG>(sizeof(name) - sizeof(wchar_t));
        if (CM_Get_DevNode_PropertyW(node, &DEVPKEY_Device_FriendlyName, &type, reinterpret_cast<PBYTE>(name.data()), &size, 0) != CR_SUCCESS ||
            type != DEVPROP_TYPE_STRING) name[0] = L'\0';
        devices::Controller pad;
        pad.id = controllerId(*found); // Not bluetooth_id: nothing can connect it until the radio is on.
        pad.name = name[0] ? name.data() : L"Bluetooth";
        pad.connection = L"Bluetooth";
        pad.picture = devices::classifyController(vid, pid, pad.name);
        pad.connected = false;
        pad.radio_off = true;
        if (std::none_of(pads.begin(), pads.end(), [&](const auto& other) { return other.id == pad.id; })) pads.push_back(std::move(pad));
    }
    return pads;
}
}

std::vector<devices::Controller> controllerPanelControllers() {
    auto pads = enumerateControllers();
    for (auto& pad : pads) {
        pad.bluetooth_id = bluetoothId(pad.id);
        if (!pad.bluetooth_id.empty()) pad.connection = L"Bluetooth";
        else if (pad.id.find(L"{00001812-") != pad.id.npos) pad.connection = L"Bluetooth LE";
    }
    const bool radio = visitPads([&](HANDLE, const BLUETOOTH_DEVICE_INFO& info) {
        const auto id = controllerId(info.Address.ullLong);
        if (std::any_of(pads.begin(), pads.end(), [&](const auto& pad) { return pad.bluetooth_id == id; })) return false;
        devices::Controller pad;
        pad.id = pad.bluetooth_id = id; pad.name = info.szName; pad.connection = L"Bluetooth";
        pad.picture = devices::classifyController(0, 0, pad.name);
        pad.connected = info.fConnected != FALSE;
        pads.push_back(std::move(pad));
        return false;
    });
    // Bluetooth off: the remembered pads stay listed, greyed, with the way to switch it on.
    if (!radio) for (auto& pad : pairedPadsWithoutRadio())
        if (std::none_of(pads.begin(), pads.end(), [&](const auto& other) { return other.id == pad.id || other.bluetooth_id == pad.id; }))
            pads.push_back(std::move(pad));
    loadControllerOverrides(pads, userConfigDirectory() / L"controller_overrides.json");
    return pads;
}

std::wstring setBluetoothControllerConnection(const std::wstring& id, bool connect,
                                             const std::function<bool()>& cancelled) {
    const auto target = id.starts_with(L"bluetooth:") ? address(std::wstring_view(id).substr(10)) : std::nullopt;
    if (!target) return tr(L"Nie znaleziono sparowanego pada Bluetooth.");
    std::wstring result = tr(L"Nie znaleziono sparowanego pada Bluetooth.");
    visitPads([&](HANDLE radio, BLUETOOTH_DEVICE_INFO info) {
        if (info.Address.ullLong != *target) return false;
        if (cancelled()) { result = tr(L"Anulowano operację Bluetooth."); return true; }
        if ((info.fConnected != FALSE) == connect) { result = connect ? tr(L"Połączono") : tr(L"Rozłączono"); return true; }
        DWORD error = ERROR_SUCCESS;
        if (connect) {
            constexpr GUID hid_service{0x00001124, 0x0000, 0x1000, {0x80, 0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb}};
            error = BluetoothSetServiceState(radio, &info, &hid_service, BLUETOOTH_SERVICE_ENABLE);
            if (error == static_cast<DWORD>(E_INVALIDARG)) {
                // Already enabled: rebind only this pad's HID service to request
                // reconnection. Pairing is retained. Always re-enable after disable.
                error = BluetoothSetServiceState(radio, &info, &hid_service, BLUETOOTH_SERVICE_DISABLE);
                if (error == ERROR_SUCCESS) error = BluetoothSetServiceState(radio, &info, &hid_service, BLUETOOTH_SERVICE_ENABLE);
            }
        } else {
            DWORD returned{};
            auto remote = info.Address.ullLong;
            if (!DeviceIoControl(radio, IOCTL_BTH_DISCONNECT_DEVICE, &remote, sizeof(remote), nullptr, 0, &returned, nullptr))
                error = GetLastError();
        }
        if (error != ERROR_SUCCESS) {
            result = tr(L"Błąd połączenia Bluetooth (") + std::to_wstring(error) + tr(L"). Otwórz Bluetooth w Windows.");
            return true;
        }
        // API success means the request was accepted, not that the pad connected.
        const auto deadline = GetTickCount64() + 6000;
        do {
            if (cancelled()) { result = tr(L"Anulowano operację Bluetooth."); return true; }
            if (BluetoothGetDeviceInfo(radio, &info) == ERROR_SUCCESS && (info.fConnected != FALSE) == connect) {
                result = connect ? tr(L"Połączono") : tr(L"Rozłączono"); return true;
            }
            Sleep(100);
        } while (GetTickCount64() < deadline);
        result = connect ? tr(L"Nie połączono. Włącz pada i ponów próbę.") : tr(L"Pad nadal jest połączony. Spróbuj w ustawieniach Bluetooth Windows.");
        return true;
    });
    return result;
}
