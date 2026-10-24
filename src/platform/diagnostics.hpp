#pragma once
// What a user can send when something goes wrong, all in the data folder:
//   launchme.log            errors and notable events (1 MiB, then launchme.old.log)
//   crashes\*.dmp           a minidump per crash, from the unhandled-exception filter
//   crashes\unseen.txt      the last crash, reported once at the next start (takeUnseenCrash)
// Header-only: the filter and the log are usable before the Launcher exists.
#include <Windows.h>
#include <DbgHelp.h>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "version.lib")

namespace diagnostics {
// ProductVersion from the EXE's VERSIONINFO (CMake's project() version), e.g. "0.1.0".
inline std::wstring version() {
    wchar_t path[MAX_PATH * 2]{};
    if (!GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)))) return {};
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(path, &ignored);
    if (!size) return {};
    std::wstring buffer(size / sizeof(wchar_t) + 1, L'\0');
    if (!GetFileVersionInfoW(path, 0, size, buffer.data())) return {};
    VS_FIXEDFILEINFO* info = nullptr; UINT length = 0;
    if (!VerQueryValueW(buffer.data(), L"\\", reinterpret_cast<void**>(&info), &length) || !info) return {};
    return std::to_wstring(HIWORD(info->dwProductVersionMS)) + L'.' + std::to_wstring(LOWORD(info->dwProductVersionMS)) + L'.' +
           std::to_wstring(HIWORD(info->dwProductVersionLS));
}
namespace detail {
inline std::filesystem::path& folder() { static std::filesystem::path value; return value; }
inline std::mutex& lock() { static std::mutex value; return value; }
inline std::string utf8(std::wstring_view text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size > 0 ? static_cast<size_t>(size) : 0, '\0');
    if (size > 0) WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}
inline std::wstring stamp(bool file_name) {
    SYSTEMTIME now{}; GetLocalTime(&now);
    wchar_t text[64]{};
    if (file_name) swprintf_s(text, L"%04u%02u%02u-%02u%02u%02u", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    else swprintf_s(text, L"%04u-%02u-%02u %02u:%02u:%02u.%03u", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
    return text;
}
// No allocation-heavy work is safe in a crashing process; this only appends to a file.
inline void append(const std::filesystem::path& file, std::string_view bytes) {
    const HANDLE handle = CreateFileW(file.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    CloseHandle(handle);
}
inline std::wstring moduleOf(const void* address) {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR>(address), &module) || !module) return {};
    wchar_t path[MAX_PATH * 2]{};
    return GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path))) ? path : L"";
}
inline LONG WINAPI unhandled(EXCEPTION_POINTERS* info) {
    static volatile LONG entered = 0;
    if (InterlockedExchange(&entered, 1)) return EXCEPTION_CONTINUE_SEARCH; // A second fault while dumping.
    const auto& base = folder();
    if (!base.empty()) {
        const auto crashes = base / L"crashes";
        CreateDirectoryW(base.c_str(), nullptr);
        CreateDirectoryW(crashes.c_str(), nullptr);
        const auto name = L"launchme-" + stamp(true) + L".dmp";
        const auto dump = crashes / name;
        const HANDLE file = CreateFileW(dump.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION exception{GetCurrentThreadId(), info, FALSE};
            MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules),
                info ? &exception : nullptr, nullptr, nullptr);
            CloseHandle(file);
        }
        const void* address = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionAddress : nullptr;
        const auto module = moduleOf(address);
        char code[160]{};
        sprintf_s(code, "code 0x%08lX at %p", info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0ul, address);
        append(base / L"launchme.log", utf8(stamp(false)) + " [crash] " + code + " in " + utf8(module) + " -> crashes\\" + utf8(name) + "\r\n");
        // Read (and removed) by the next start: the dump's name, then the faulting module.
        const auto unseen = crashes / L"unseen.txt";
        DeleteFileW(unseen.c_str());
        append(unseen, utf8(name) + "\n" + utf8(module) + "\n");
    }
    return EXCEPTION_CONTINUE_SEARCH; // Windows Error Reporting still sees it.
}
}

// Call once, early. The data folder must be known (LAUNCHME_CONFIG_DIR applied).
inline void install(const std::filesystem::path& data_folder) {
    detail::folder() = data_folder;
    SetUnhandledExceptionFilter(detail::unhandled);
    // CRT failures (invalid parameter, pure call, std::terminate) become the same crash report.
    _set_invalid_parameter_handler([](const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) {
        RaiseException(0xE0000001, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    });
    _set_purecall_handler([] { RaiseException(0xE0000002, EXCEPTION_NONCONTINUABLE, 0, nullptr); });
    std::set_terminate([] { RaiseException(0xE0000003, EXCEPTION_NONCONTINUABLE, 0, nullptr); std::abort(); });
}

// One line in launchme.log. Thread-safe; never throws.
inline void log(std::string_view level, std::wstring_view text) {
    try {
        const auto& base = detail::folder();
        if (base.empty()) return;
        std::lock_guard guard(detail::lock());
        const auto file = base / L"launchme.log";
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &data) && (data.nFileSizeHigh || data.nFileSizeLow > 1024 * 1024))
            MoveFileExW(file.c_str(), (base / L"launchme.old.log").c_str(), MOVEFILE_REPLACE_EXISTING);
        std::string line = detail::utf8(detail::stamp(false)) + " [" + std::string(level) + "] " + detail::utf8(text);
        for (auto& ch : line) if (ch == '\r' || ch == '\n') ch = ' ';
        detail::append(file, line + "\r\n");
    } catch (...) {}
}
inline void log(std::string_view level, std::string_view text) {
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(size > 0 ? static_cast<size_t>(size) : 0, L'\0');
    if (size > 0) MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
    log(level, std::wstring_view(wide));
}
// The text of an exception caught as exception_ptr (worker failures), for the log.
inline std::string describe(std::exception_ptr error) {
    try { if (error) std::rethrow_exception(error); }
    catch (const std::exception& e) { return e.what(); }
    catch (...) { return "unknown exception"; }
    return {};
}

struct Crash { std::wstring dump, module; };
// The crash of a previous run, once: the next start tells the user where its report is.
inline std::optional<Crash> takeUnseenCrash() {
    const auto& base = detail::folder();
    if (base.empty()) return {};
    const auto file = base / L"crashes" / L"unseen.txt";
    FILE* input = nullptr;
    if (_wfopen_s(&input, file.c_str(), L"rb") || !input) return {};
    char buffer[4096]{};
    const size_t length = fread(buffer, 1, sizeof(buffer) - 1, input);
    fclose(input);
    DeleteFileW(file.c_str());
    const std::string_view text(buffer, length);
    const auto line = text.find('\n');
    const auto wide = [](std::string_view part) {
        const int size = MultiByteToWideChar(CP_UTF8, 0, part.data(), static_cast<int>(part.size()), nullptr, 0);
        std::wstring result(size > 0 ? static_cast<size_t>(size) : 0, L'\0');
        if (size > 0) MultiByteToWideChar(CP_UTF8, 0, part.data(), static_cast<int>(part.size()), result.data(), size);
        return result;
    };
    Crash crash;
    crash.dump = wide(text.substr(0, line));
    if (line != std::string_view::npos) {
        auto module = text.substr(line + 1);
        if (const auto end = module.find('\n'); end != std::string_view::npos) module = module.substr(0, end);
        crash.module = wide(module);
    }
    return crash.dump.empty() ? std::nullopt : std::optional<Crash>(crash);
}
}
