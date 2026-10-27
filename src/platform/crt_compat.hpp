#pragma once
// The MSVC CRT extensions the shared code uses, with MSVC's signatures, for other compilers, and
// the few Win32 helpers that have an exact POSIX equivalent (SecureZeroMemory, GetTickCount64,
// Sleep). Anything with platform behaviour gets a real port instead. cmake/Linux.cmake
// force-includes this header (-include); the Windows build never sees it.
#ifndef _WIN32
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cwchar>
#include <cstring>
#include <time.h>
#include <unistd.h>

using errno_t = int;

inline errno_t localtime_s(std::tm* result, const std::time_t* time) { return localtime_r(time, result) ? 0 : EINVAL; }
inline errno_t gmtime_s(std::tm* result, const std::time_t* time) { return gmtime_r(time, result) ? 0 : EINVAL; }
inline std::time_t _mkgmtime(std::tm* time) { return timegm(time); }

template <size_t size>
int swprintf_s(wchar_t (&buffer)[size], const wchar_t* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    const int result = std::vswprintf(buffer, size, format, arguments);
    va_end(arguments);
    if (result < 0) buffer[0] = L'\0';
    return result;
}
template <size_t size>
int sprintf_s(char (&buffer)[size], const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    const int result = std::vsnprintf(buffer, size, format, arguments);
    va_end(arguments);
    return result;
}

inline int _wtoi(const wchar_t* text) { return static_cast<int>(std::wcstol(text, nullptr, 10)); }
inline unsigned long long _wcstoui64(const wchar_t* text, wchar_t** end, int base) { return std::wcstoull(text, end, base); }

using ULONGLONG = unsigned long long;
inline void* SecureZeroMemory(void* data, size_t size) { explicit_bzero(data, size); return data; }
inline ULONGLONG GetTickCount64() {
    timespec now{}; clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<ULONGLONG>(now.tv_sec) * 1000 + static_cast<ULONGLONG>(now.tv_nsec) / 1000000;
}
inline void Sleep(unsigned milliseconds) {
    timespec wait{static_cast<time_t>(milliseconds / 1000), static_cast<long>(milliseconds % 1000) * 1000000};
    while (nanosleep(&wait, &wait) != 0 && errno == EINTR) {}
}
#endif
