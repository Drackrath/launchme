#include "system_audio.hpp"
#include "../i18n/i18n.hpp"
#include "../model/headphones.hpp"
#include <functiondiscoverykeys_devpkey.h>
#include <propsys.h>
#include <mmreg.h>
#include <cfgmgr32.h>
#include <devpkey.h>
#include <algorithm>
#include <cmath>
#include <cwchar>

namespace {
// Windows desktop PolicyConfig ABI, checked against SoundSwitch's
// SoundSwitch.Audio.Manager/Interop/Interface/Policy/IPolicyConfig.cs.
struct __declspec(uuid("F8679F50-850A-41CF-9C72-430F290290C8")) PolicyConfig : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(LPCWSTR, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(LPCWSTR, BOOL, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(LPCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(LPCWSTR, WAVEFORMATEX*, WAVEFORMATEX*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(LPCWSTR, BOOL, INT64*, INT64*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(LPCWSTR, INT64*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(LPCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(LPCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(LPCWSTR, BOOL, PROPERTYKEY, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(LPCWSTR, BOOL, PROPERTYKEY, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(LPCWSTR, ERole) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(LPCWSTR, BOOL) = 0;
};
constexpr CLSID policy_class{0x870af99c, 0x171d, 0x4f9e, {0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9}};
std::wstring endpointId(IMMDevice* device) {
    LPWSTR id = nullptr;
    if (!device || FAILED(device->GetId(&id))) return {};
    std::wstring result(id); CoTaskMemFree(id); return result;
}

// Cached Bluetooth battery property exposed by Windows' device-property store.
// Read only: no pairing, discovery, GATT connection or vendor-specific command.
constexpr DEVPROPKEY bluetooth_battery{{0x104ea319, 0x6ee2, 0x4701, {0xbd, 0x47, 0x8d, 0xdb, 0xf4, 0x25, 0xbb, 0xe5}}, 2};
std::optional<int> nodeBattery(DEVINST node) {
    DEVPROPTYPE type{}; ULONG size = sizeof(DWORD); DWORD value = 0;
    if (CM_Get_DevNode_PropertyW(node, &bluetooth_battery, &type, reinterpret_cast<PBYTE>(&value), &size, 0) != CR_SUCCESS) return {};
    if (!((type == DEVPROP_TYPE_BYTE && size == 1) || (type == DEVPROP_TYPE_UINT32 && size == sizeof(DWORD))) || value > 100) return {};
    return static_cast<int>(value);
}
std::optional<GUID> nodeContainer(DEVINST node) {
    DEVPROPTYPE type{}; ULONG size = sizeof(GUID); GUID value{};
    if (CM_Get_DevNode_PropertyW(node, &DEVPKEY_Device_ContainerId, &type, reinterpret_cast<PBYTE>(&value), &size, 0) != CR_SUCCESS ||
        type != DEVPROP_TYPE_GUID || size != sizeof(GUID) || IsEqualGUID(value, GUID_NULL)) return {};
    return value;
}
std::optional<int> headphoneBattery(IPropertyStore* properties) {
    // Audio profiles can expose a function node or a software endpoint. Follow the former's
    // parents, then use the physical container to associate the latter with its Bluetooth node.
    std::optional<GUID> container;
    PROPVARIANT value{};
    const PROPERTYKEY container_key{DEVPKEY_Device_ContainerId.fmtid, DEVPKEY_Device_ContainerId.pid};
    if (SUCCEEDED(properties->GetValue(container_key, &value)) && value.vt == VT_CLSID && value.puuid && !IsEqualGUID(*value.puuid, GUID_NULL))
        container = *value.puuid;
    PropVariantClear(&value);
    const PROPERTYKEY instance_key{DEVPKEY_Device_InstanceId.fmtid, DEVPKEY_Device_InstanceId.pid};
    if (SUCCEEDED(properties->GetValue(instance_key, &value)) && value.vt == VT_LPWSTR && value.pwszVal) {
        DEVINST node{};
        if (CM_Locate_DevNodeW(&node, value.pwszVal, CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS) {
            for (int depth = 0; depth < 8; ++depth) {
                if (const auto battery = nodeBattery(node)) { PropVariantClear(&value); return battery; }
                if (!container) container = nodeContainer(node);
                DEVINST parent{};
                if (CM_Get_Parent(&parent, node, 0) != CR_SUCCESS) break;
                node = parent;
            }
        }
    }
    PropVariantClear(&value);
    if (!container) return {}; // Never use another headset's reading just because its name matches.
    constexpr ULONG flags = CM_GETIDLIST_FILTER_ENUMERATOR | CM_GETIDLIST_FILTER_PRESENT;
    for (const auto enumerator : {L"BTHENUM", L"BTHLEDEVICE", L"BTHLE"}) {
        for (int attempt = 0; attempt < 2; ++attempt) {
            ULONG count = 0;
            if (CM_Get_Device_ID_List_SizeW(&count, enumerator, flags) != CR_SUCCESS || count < 2) break;
            std::vector<wchar_t> ids(count, 0);
            const auto result = CM_Get_Device_ID_ListW(enumerator, ids.data(), count, flags);
            if (result == CR_BUFFER_SMALL) continue; // A device arrived between the two reads.
            if (result != CR_SUCCESS) break;
            for (auto* id = ids.data(); *id; id += std::wcslen(id) + 1) {
                DEVINST node{};
                if (CM_Locate_DevNodeW(&node, id, CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS) continue;
                const auto candidate = nodeContainer(node);
                if (candidate && IsEqualGUID(*candidate, *container))
                    if (const auto battery = nodeBattery(node)) return battery;
            }
            break;
        }
    }
    return {};
}
}

bool SystemAudio::read(VolumeLevel& value, bool input) {
    auto& endpoint = endpoints_[input];
    if (!endpoint) {
        if (!devices_ && FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                                  CLSCTX_INPROC_SERVER, IID_PPV_ARGS(devices_.GetAddressOf()))))
            return false;
        Microsoft::WRL::ComPtr<IMMDevice> device;
        if (FAILED(devices_->GetDefaultAudioEndpoint(input ? eCapture : eRender, eMultimedia, device.GetAddressOf())) ||
            FAILED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                                    reinterpret_cast<void**>(endpoint.GetAddressOf())))) { endpoint_ids_[input].clear(); return false; }
        endpoint_ids_[input] = endpointId(device.Get());
        if (endpoint_ids_[input].empty()) { endpoint.Reset(); return false; }
    }
    BOOL muted = FALSE;
    if (FAILED(endpoint->GetMasterVolumeLevelScalar(&value.level)) || FAILED(endpoint->GetMute(&muted))) {
        endpoint.Reset(); endpoint_ids_[input].clear();
        return false;
    }
    value.muted = muted != FALSE;
    return true;
}

void SystemAudio::refreshDevice() { for (auto& endpoint : endpoints_) endpoint.Reset(); endpoint_ids_ = {}; }

std::optional<bool> SystemAudio::headphonesConnected() {
    const auto state = headphonesState();
    return state ? std::optional<bool>{state->connected} : std::nullopt;
}

std::optional<headphones::State> SystemAudio::headphonesState() {
    if (!devices_ && FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                             CLSCTX_INPROC_SERVER, IID_PPV_ARGS(devices_.GetAddressOf())))) return {};
    Microsoft::WRL::ComPtr<IMMDeviceCollection> collection;
    if (FAILED(devices_->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, collection.GetAddressOf()))) return {};
    UINT count = 0;
    if (FAILED(collection->GetCount(&count))) return {};
    bool complete = true;
    headphones::State state;
    for (UINT i = 0; i < count; ++i) {
        Microsoft::WRL::ComPtr<IMMDevice> device;
        Microsoft::WRL::ComPtr<IPropertyStore> properties;
        if (FAILED(collection->Item(i, device.GetAddressOf())) ||
            FAILED(device->OpenPropertyStore(STGM_READ, properties.GetAddressOf()))) { complete = false; continue; }
        PROPVARIANT value{};
        const bool read = SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR && value.pwszVal;
        const bool match = read && headphones::supported(value.pwszVal);
        PropVariantClear(&value);
        if (match) {
            state.connected = true; // Multiple audio profiles produce one connection state.
            state.battery = headphoneBattery(properties.Get());
            if (state.battery) return state;
        }
        if (!read) complete = false;
    }
    return state.connected || complete ? std::optional<headphones::State>{state} : std::nullopt;
}

devices::Snapshot SystemAudio::enumerate() {
    devices::Snapshot result;
    refreshDevice();
    for (const bool input : {false, true}) {
        VolumeLevel volume;
        auto& channel = result.audio[input];
        channel.available = read(volume, input);
        channel.volume = volume.level; channel.muted = volume.muted; channel.endpoint = endpoint_ids_[input];
    }
    if (!devices_) { result.status = tr(L"Nie można odczytać urządzeń audio."); return result; }
    for (const auto flow : {eRender, eCapture}) {
        Microsoft::WRL::ComPtr<IMMDevice> current;
        devices_->GetDefaultAudioEndpoint(flow, eMultimedia, current.GetAddressOf());
        const auto selected = endpointId(current.Get());
        Microsoft::WRL::ComPtr<IMMDeviceCollection> collection;
        if (FAILED(devices_->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, collection.GetAddressOf()))) continue;
        UINT count = 0; collection->GetCount(&count);
        for (UINT i = 0; i < count; ++i) {
            Microsoft::WRL::ComPtr<IMMDevice> device;
            Microsoft::WRL::ComPtr<IPropertyStore> properties;
            if (FAILED(collection->Item(i, device.GetAddressOf()))) continue;
            const auto id = endpointId(device.Get());
            if (id.empty()) continue;
            std::wstring name = tr(L"Urządzenie audio");
            PROPVARIANT value{};
            if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, properties.GetAddressOf())) &&
                SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR &&
                value.pwszVal && *value.pwszVal)
                name = value.pwszVal;
            PropVariantClear(&value);
            result.endpoints.push_back({id, name, flow == eCapture, id == selected});
        }
    }
    return result;
}

std::wstring SystemAudio::setVolume(float level, bool input, const std::wstring& expected) {
    if (!std::isfinite(level)) return tr(L"Nieprawidłowy poziom głośności.");
    refreshDevice(); VolumeLevel old;
    if (!read(old, input)) return input ? tr(L"Brak dostępnego wejścia audio.") : tr(L"Brak dostępnego wyjścia audio.");
    if (!expected.empty() && expected != endpoint_ids_[input]) return tr(L"Urządzenie audio zmieniło się. Ponów zmianę.");
    if (FAILED(endpoints_[input]->SetMasterVolumeLevelScalar(std::clamp(level, 0.0f, 1.0f), nullptr)))
        return tr(L"Nie udało się zmienić głośności.");
    return {};
}
std::wstring SystemAudio::setMuted(bool muted, bool input, const std::wstring& expected) {
    refreshDevice(); VolumeLevel old;
    if (!read(old, input)) return input ? tr(L"Brak dostępnego wejścia audio.") : tr(L"Brak dostępnego wyjścia audio.");
    if (!expected.empty() && expected != endpoint_ids_[input]) return tr(L"Urządzenie audio zmieniło się. Ponów zmianę.");
    if (FAILED(endpoints_[input]->SetMute(muted, nullptr))) return tr(L"Nie udało się zmienić wyciszenia.");
    return {};
}
std::wstring SystemAudio::setDefault(const std::wstring& id, bool input) {
    const auto available = enumerate();
    if (std::none_of(available.endpoints.begin(), available.endpoints.end(), [&](const auto& e) { return e.id == id && e.input == input; }))
        return tr(L"Urządzenie nie jest już dostępne.");
    Microsoft::WRL::ComPtr<PolicyConfig> policy;
    if (FAILED(CoCreateInstance(policy_class, nullptr, CLSCTX_ALL, __uuidof(PolicyConfig),
                                reinterpret_cast<void**>(policy.GetAddressOf()))))
        return tr(L"Ta wersja Windows nie udostępnia zmiany wyjścia/wejścia. Otwórz ustawienia dźwięku Windows.");
    std::vector<std::wstring> previous;
    for (int role = 0; role < 3; ++role) {
        Microsoft::WRL::ComPtr<IMMDevice> device;
        if (FAILED(devices_->GetDefaultAudioEndpoint(input ? eCapture : eRender, static_cast<ERole>(role), device.GetAddressOf())))
            return tr(L"Nie można zachować poprzednich ustawień audio.");
        previous.push_back(endpointId(device.Get()));
        if (previous.back().empty()) return tr(L"Nie można zachować poprzednich ustawień audio.");
    }
    const auto result = devices::switchRoles(previous, id, [&](int role, const std::wstring& endpoint) {
        return SUCCEEDED(policy->SetDefaultEndpoint(endpoint.c_str(), static_cast<ERole>(role)));
    });
    refreshDevice();
    if (result == devices::SwitchResult::rollback_failed) return tr(L"Częściowa zmiana. Sprawdź role urządzeń w ustawieniach Windows.");
    return result == devices::SwitchResult::success ? L"" : tr(L"Zmiana nie powiodła się. Przywrócono poprzednie urządzenia.");
}
