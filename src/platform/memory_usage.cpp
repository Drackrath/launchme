#include "memory_usage.hpp"

#include <Windows.h>
#include <Psapi.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

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
