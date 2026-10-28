#pragma once
#ifdef _WIN32
#include <Windows.h>
#include <tlhelp32.h>
#else
#include "proc_fs.hpp"
#include <cstdint>
#endif

namespace process_presence {
#ifdef _WIN32
// Windows may retain a terminated process object while another program holds a
// handle. Opening that object can return ACCESS_DENIED, just like a live protected
// game. A successful snapshot proves exit only if its PID is absent or has zero
// threads. A failed snapshot proves nothing; a suspended game still has threads.
inline bool exited(DWORD pid) {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    struct Close { HANDLE handle; ~Close() { CloseHandle(handle); } } close{snapshot};
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot, &entry)) return false;
    do {
        if (entry.th32ProcessID == pid) return entry.cntThreads == 0;
    } while (Process32NextW(snapshot, &entry));
    return GetLastError() == ERROR_NO_MORE_FILES;
}
#else
// Gone from /proc, or a zombie its parent has not waited for yet. A stopped game still exists.
inline bool exited(uint32_t pid) {
    const auto stat = proc_fs::stat(pid);
    return !stat || stat->ended();
}
#endif
}
