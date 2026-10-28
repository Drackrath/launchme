#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// This process's memory as Windows accounts it: committed private bytes, the resident working set,
// and the GPU memory its devices hold (DXGI: dedicated VRAM and shared system memory, summed over
// every adapter; 0 where the system cannot say). On Linux: anonymous resident + swapped pages, the
// resident set, and the DRM fdinfo of this process's GPU clients (VRAM; GTT/system memory as shared).
struct MemoryUsage {
    size_t private_bytes = 0, working_set = 0, gpu_local = 0, gpu_shared = 0;
};
MemoryUsage currentMemoryUsage();

// Linux parsing, pure for tests: /proc/<pid>/status (private_bytes and working_set) and one
// /proc/<pid>/fdinfo/<fd> of a DRM client (empty for any other descriptor).
MemoryUsage memoryFromProcStatus(std::string_view status);
struct DrmClientMemory {
    std::string pdev; uint64_t client = 0;
    size_t local = 0, shared = 0;
};
std::optional<DrmClientMemory> drmClientMemory(std::string_view fdinfo);

// Gives memory nobody uses back to the system: the process heaps are compacted and the working set
// is emptied (pages come back from the standby list when touched again). For a launcher waiting
// in the background while a game runs. On Linux the allocator's free pages are returned (malloc_trim).
void trimProcessMemory();
