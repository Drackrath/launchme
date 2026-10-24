#include "process_freeze.hpp"
#include "atomic_file.hpp"
#include "process_presence.hpp"

#include <Windows.h>

#include <algorithm>
#include <fstream>
#include <utility>

namespace {
using ProcessCall = LONG(NTAPI*)(HANDLE);
// Undocumented but stable since Windows XP; used by Resource Monitor and PsSuspend.
ProcessCall ntdll(const char* name) {
    const HMODULE module = GetModuleHandleW(L"ntdll.dll");
    return module ? reinterpret_cast<ProcessCall>(GetProcAddress(module, name)) : nullptr;
}
struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value) CloseHandle(value); }
};
enum class Open { ok, gone, denied };
// Opens the exact process identity: a PID reused by another program counts as gone.
Open open(const GameProcess& process, DWORD access, Handle& handle) {
    handle.value = OpenProcess(access | SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process.pid);
    if (!handle.value) return GetLastError() == ERROR_ACCESS_DENIED && !process_presence::exited(process.pid) ? Open::denied : Open::gone;
    const auto wait = WaitForSingleObject(handle.value, 0);
    if (wait == WAIT_OBJECT_0) return Open::gone;
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(handle.value, &created, &exited, &kernel, &user))
        return process_presence::exited(process.pid) ? Open::gone : Open::denied;
    const auto ticks = (static_cast<uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    if (ticks != process.created) return Open::gone;
    return wait == WAIT_TIMEOUT ? Open::ok : Open::denied;
}
}

namespace process_freeze {
Result suspend(std::vector<GameProcess>& processes) {
    const auto call = ntdll("NtSuspendProcess"), undo = ntdll("NtResumeProcess");
    if (!call || !undo) return Result::denied;
    struct Held { std::vector<HANDLE> values; ~Held() { for (auto value : values) CloseHandle(value); } } held;
    std::vector<GameProcess> suspended;
    for (const auto& process : processes) {
        Handle handle;
        const auto state = open(process, PROCESS_SUSPEND_RESUME, handle);
        if (state == Open::gone) continue;
        // Anti-cheat drivers may strip PROCESS_SUSPEND_RESUME from an opened handle.
        if (state == Open::denied || call(handle.value) < 0) {
            for (auto value : held.values) undo(value);
            return Result::denied;
        }
        held.values.push_back(std::exchange(handle.value, nullptr));
        suspended.push_back(process);
    }
    if (suspended.empty()) return Result::gone;
    processes = std::move(suspended);
    return Result::ok;
}

Result resume(const std::vector<GameProcess>& processes) {
    const auto call = ntdll("NtResumeProcess");
    if (!call) return Result::denied;
    bool resumed = false, denied = false;
    for (const auto& process : processes) {
        Handle handle;
        const auto state = open(process, PROCESS_SUSPEND_RESUME, handle);
        if (state == Open::gone) continue;
        if (state == Open::denied || call(handle.value) < 0) denied = true;
        else resumed = true;
    }
    return denied ? Result::denied : resumed ? Result::ok : Result::gone;
}

Result terminate(const std::vector<GameProcess>& processes) {
    bool ended = false, denied = false;
    for (const auto& process : processes) {
        Handle handle;
        const auto state = open(process, PROCESS_TERMINATE, handle);
        if (state == Open::gone) continue;
        if (state == Open::denied || !TerminateProcess(handle.value, 0)) denied = true;
        else ended = true;
    }
    return denied ? Result::denied : ended ? Result::ok : Result::gone;
}

bool alive(const std::vector<GameProcess>& processes) {
    return std::any_of(processes.begin(), processes.end(), [](const GameProcess& process) {
        Handle handle;
        return open(process, 0, handle) != Open::gone;
    });
}

bool focus(const std::vector<GameProcess>& processes) {
    std::vector<GameProcess> live;
    for (const auto& process : processes) {
        Handle handle;
        if (open(process, 0, handle) == Open::ok && WaitForSingleObject(handle.value, 0) == WAIT_TIMEOUT) live.push_back(process);
    }
    if (live.empty()) return false;
    struct Search { const std::vector<GameProcess>* processes; HWND best = nullptr; long long area = 0; } search{&live};
    // Only non-blocking window queries: a window of a still-suspended process must not hang the UI.
    EnumWindows([](HWND window, LPARAM parameter) -> BOOL {
        auto& search = *reinterpret_cast<Search*>(parameter);
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER) || (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) ||
            std::none_of(search.processes->begin(), search.processes->end(), [&](const GameProcess& process) { return process.pid == pid; }))
            return TRUE;
        RECT bounds{};
        WINDOWPLACEMENT placement{sizeof(placement)};
        if (IsIconic(window) && GetWindowPlacement(window, &placement)) bounds = placement.rcNormalPosition;
        else GetWindowRect(window, &bounds);
        const long long area = static_cast<long long>(bounds.right - bounds.left) * (bounds.bottom - bounds.top);
        if (area > search.area || !search.best) { search.area = area; search.best = window; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    if (!search.best) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(search.best, &pid);
    AllowSetForegroundWindow(pid);
    if (IsIconic(search.best)) ShowWindowAsync(search.best, SW_RESTORE);
    SetForegroundWindow(search.best);
    const auto foreground = GetForegroundWindow();
    return foreground == search.best || (foreground && GetAncestor(foreground, GA_ROOTOWNER) == search.best);
}

std::optional<std::vector<FrozenGame>> load(const std::filesystem::path& file) {
    std::error_code error;
    if (!std::filesystem::exists(file, error)) {
        if (error) return {};
        return std::vector<FrozenGame>{};
    }
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() < 0 || input.tellg() > 64 * 1024) return {};
    std::string text(static_cast<size_t>(input.tellg()), '\0');
    input.seekg(0);
    if (!input.read(text.data(), static_cast<std::streamsize>(text.size()))) return {};
    bool complete = false;
    auto games = parseFrozenGames(text, &complete);
    if (!complete) return {};
    return games;
}

bool save(const std::filesystem::path& file, const std::vector<FrozenGame>& games) {
    if (std::none_of(games.begin(), games.end(), [](const FrozenGame& game) { return !game.processes.empty(); })) {
        std::error_code error;
        std::filesystem::remove(file, error);
        return !error;
    }
    const auto text = formatFrozenGames(games);
    // Never acknowledge a journal that the bounded reader cannot restore fully.
    if (text.size() > 64 * 1024 || parseFrozenGames(text) != games) return false;
    return atomic_file::write(file, text);
}
}
