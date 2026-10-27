#pragma once
// Small process/environment helpers shared by Windows and Linux builds.
#include "text_encoding.hpp"

#ifdef _WIN32
#include <Windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#else
#include <cstdlib>
#include <sys/random.h>
#include <unistd.h>
#endif

#include <cerrno>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <optional>
#include <string>

namespace system_info {
// An environment variable's value; nullopt when unset or longer than `limit` characters.
inline std::optional<std::wstring> environment(const wchar_t* name, size_t limit = 32767) {
#ifdef _WIN32
    const DWORD length = GetEnvironmentVariableW(name, nullptr, 0);
    if (!length || length - 1 > limit) return std::nullopt;
    std::wstring value(length, L'\0');
    if (GetEnvironmentVariableW(name, value.data(), length) != length - 1) return std::nullopt;
    value.resize(length - 1);
    return value;
#else
    const char* value = std::getenv(text_encoding::utf8(name).c_str());
    if (!value) return std::nullopt;
    auto result = text_encoding::wideLenient(value);
    if (result.size() > limit) return std::nullopt;
    return result;
#endif
}

inline uint32_t processId() {
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return static_cast<uint32_t>(getpid());
#endif
}

// The running executable's full path (empty when unknown).
inline std::filesystem::path executablePath() {
#ifdef _WIN32
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) return {};
    path.resize(length);
    return path;
#else
    std::error_code error;
    auto path = std::filesystem::read_symlink("/proc/self/exe", error);
    return error ? std::filesystem::path{} : path;
#endif
}

// Cryptographically secure random bytes (the system's preferred RNG); false if it fails.
inline bool randomBytes(void* data, size_t size) {
#ifdef _WIN32
    return BCryptGenRandom(nullptr, static_cast<PUCHAR>(data), static_cast<ULONG>(size), BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0;
#else
    auto* bytes = static_cast<uint8_t*>(data);
    while (size) {
        const ssize_t got = getrandom(bytes, size, 0);
        if (got < 0) { if (errno == EINTR) continue; return false; }
        bytes += got; size -= static_cast<size_t>(got);
    }
    return true;
#endif
}

struct LocalTime { int year, month, day, hour, minute, second, millisecond; };
inline LocalTime localTime() {
#ifdef _WIN32
    SYSTEMTIME now{}; GetLocalTime(&now);
    return {now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds};
#else
    timespec now{}; clock_gettime(CLOCK_REALTIME, &now);
    tm parts{}; localtime_r(&now.tv_sec, &parts);
    return {parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday, parts.tm_hour, parts.tm_min, parts.tm_sec,
            static_cast<int>(now.tv_nsec / 1000000)};
#endif
}
}
