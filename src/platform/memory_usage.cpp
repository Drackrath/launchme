#include "memory_usage.hpp"

#ifdef _WIN32
#include <Windows.h>
#include <Psapi.h>
#endif
#include <dxgi1_4.h>
#ifdef _WIN32
#include <wrl/client.h>
#endif

#include <vector>

MemoryUsage currentMemoryUsage() {
    MemoryUsage usage;
    PROCESS_MEMORY_COUNTERS_EX counters{}; counters.cb = sizeof(counters);
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) {
        usage.private_bytes = counters.PrivateUsage;
        usage.working_set = counters.WorkingSetSize;
    }
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())))) return usage;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory->EnumAdapters1(i, adapter.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; ++i) {
        Microsoft::WRL::ComPtr<IDXGIAdapter3> modern;
        if (FAILED(adapter.As(&modern))) continue;
        DXGI_QUERY_VIDEO_MEMORY_INFO info{};
        if (SUCCEEDED(modern->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) usage.gpu_local += static_cast<size_t>(info.CurrentUsage);
        if (SUCCEEDED(modern->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &info))) usage.gpu_shared += static_cast<size_t>(info.CurrentUsage);
    }
    return usage;
}

void trimProcessMemory() {
    std::vector<HANDLE> heaps(64);
    DWORD count = GetProcessHeaps(static_cast<DWORD>(heaps.size()), heaps.data());
    if (count > heaps.size()) { heaps.resize(count); count = GetProcessHeaps(count, heaps.data()); }
    for (DWORD i = 0; i < count && i < heaps.size(); ++i) HeapCompact(heaps[i], 0);
    SetProcessWorkingSetSizeEx(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1), 0);
}

void setBackgroundPriority(bool background) {
    const HANDLE self = GetCurrentProcess();
    SetPriorityClass(self, background ? BELOW_NORMAL_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS);
    // EcoQoS; leaving it, the system decides again (an empty control mask). The timer flag is
    // Windows 11's: Windows 10 refuses the pair and takes EcoQoS alone.
    const auto throttle = [self](ULONG flags) {
        PROCESS_POWER_THROTTLING_STATE throttling{};
        throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        throttling.ControlMask = flags;
        throttling.StateMask = flags;
        return SetProcessInformation(self, ProcessPowerThrottling, &throttling, sizeof(throttling)) != FALSE;
    };
    if (!background) throttle(0);
    else if (!throttle(PROCESS_POWER_THROTTLING_EXECUTION_SPEED | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION))
        throttle(PROCESS_POWER_THROTTLING_EXECUTION_SPEED);
    MEMORY_PRIORITY_INFORMATION memory{background ? static_cast<ULONG>(MEMORY_PRIORITY_LOW) : static_cast<ULONG>(MEMORY_PRIORITY_NORMAL)};
    SetProcessInformation(self, ProcessMemoryPriority, &memory, sizeof(memory));
}
