#pragma once

#ifdef _WIN32
#include <Windows.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#endif
#include <array>
#include <memory>
#include <optional>
#include <string>
#include "../model/devices.hpp"
#include "../model/headphones.hpp"

struct VolumeLevel {
    float level = 0;
    bool muted = false;
};

// Reads the real system default output, including changes from hardware keys and the system
// mixer: WASAPI on Windows (system_audio_windows.cpp), the PulseAudio API elsewhere, which
// PipeWire serves too (system_audio_pulse.cpp). This does not change the application's own volume.
class SystemAudio {
public:
    SystemAudio();
    ~SystemAudio();
    SystemAudio(const SystemAudio&) = delete;
    SystemAudio& operator=(const SystemAudio&) = delete;
    bool read(VolumeLevel& value, bool input = false);
    void refreshDevice();
    devices::Snapshot enumerate();
    std::optional<bool> headphonesConnected(); // Active output endpoints, independent of the default output.
    std::optional<headphones::State> headphonesState(); // Connection + battery of the same physical device, when Windows reports it.
    std::wstring setVolume(float level, bool input = false, const std::wstring& expected = {});
    std::wstring setMuted(bool muted, bool input = false, const std::wstring& expected = {});
    std::wstring setDefault(const std::wstring& id, bool input);

private:
    struct Impl; // The platform's connection and the two default endpoints.
    std::unique_ptr<Impl> impl_;
    std::array<std::wstring, 2> endpoint_ids_;
};
