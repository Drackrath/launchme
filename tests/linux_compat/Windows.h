#pragma once
// Tests only (cmake/Linux.cmake puts this folder on the test targets' include path): the few
// Win32 helpers the tests use for temp folder names and environment variables (Sleep and
// GetTickCount64 come from src/platform/crt_compat.hpp, force-included everywhere).
// Production code never includes this; it uses #ifdef _WIN32 ports instead.
#include "../../src/platform/text_encoding.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

using DWORD = uint32_t;
using UINT = unsigned int;
using BOOL = int;
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
#define MAX_PATH 260
#define CP_UTF8 65001

inline DWORD GetCurrentProcessId() { return static_cast<DWORD>(getpid()); }
inline BOOL SetConsoleOutputCP(UINT) { return TRUE; }

// Like Win32: the length without the terminator when it fits, the size needed with it when it
// does not, 0 when the variable is unset.
inline DWORD GetEnvironmentVariableW(const wchar_t* name, wchar_t* buffer, DWORD size) {
    const char* value = std::getenv(text_encoding::utf8(name).c_str());
    if (!value) return 0;
    const auto wide = text_encoding::wideLenient(value);
    if (wide.size() + 1 > size) return static_cast<DWORD>(wide.size() + 1);
    std::wmemcpy(buffer, wide.c_str(), wide.size() + 1);
    return static_cast<DWORD>(wide.size());
}
inline BOOL SetEnvironmentVariableW(const wchar_t* name, const wchar_t* value) {
    const auto key = text_encoding::utf8(name);
    return value ? setenv(key.c_str(), text_encoding::utf8(value).c_str(), 1) == 0 : unsetenv(key.c_str()) == 0;
}
// The running executable (nullptr module only).
inline DWORD GetModuleFileNameW(void*, wchar_t* buffer, DWORD size) {
    char path[4096];
    const ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (length <= 0) return 0;
    const auto wide = text_encoding::wideLenient(std::string_view(path, static_cast<size_t>(length)));
    if (wide.size() + 1 > size) return 0;
    std::wmemcpy(buffer, wide.c_str(), wide.size() + 1);
    return static_cast<DWORD>(wide.size());
}
