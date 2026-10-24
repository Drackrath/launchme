#pragma once

#include <Windows.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <array>
#include "../model/devices.hpp"
#include "../model/headphones.hpp"

struct VolumeLevel {
    float level = 0;
    bool muted = false;
};

// Reads the real Windows default output, including changes from hardware keys
// and the system mixer. This does not change the application's XAudio2 volume.
class SystemAudio {
public:
    bool read(VolumeLevel& value, bool input = false);
    void refreshDevice();
    devices::Snapshot enumerate();
    std::optional<bool> headphonesConnected(); // Active output endpoints, independent of the default output.
    std::optional<headphones::State> headphonesState(); // Connection + battery of the same physical device, when Windows reports it.
    std::wstring setVolume(float level, bool input = false, const std::wstring& expected = {});
    std::wstring setMuted(bool muted, bool input = false, const std::wstring& expected = {});
    std::wstring setDefault(const std::wstring& id, bool input);

private:
    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> devices_;
    std::array<Microsoft::WRL::ComPtr<IAudioEndpointVolume>, 2> endpoints_;
    std::array<std::wstring, 2> endpoint_ids_;
};
