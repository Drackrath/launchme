#pragma once
#include <cstddef>

// This process's memory as Windows accounts it: committed private bytes, the resident working set,
// and the GPU memory its devices hold (DXGI: dedicated VRAM and shared system memory, summed over
// every adapter; 0 where the system cannot say).
struct MemoryUsage {
    size_t private_bytes = 0, working_set = 0, gpu_local = 0, gpu_shared = 0;
};
MemoryUsage currentMemoryUsage();

// Gives memory nobody uses back to the system: the process heaps are compacted and the working set
// is emptied (pages come back from the standby list when touched again). For a launcher waiting
// in the background while a game runs.
void trimProcessMemory();
