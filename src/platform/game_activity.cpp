#include "game_activity.hpp"
#include "../library/emulation_platforms.hpp"
#ifdef _WIN32
#include <Windows.h>
#include <tlhelp32.h>
#else
#include "proc_fs.hpp"
#include "system.hpp"
#include "text_encoding.hpp"
#include "../library/steam_registry.hpp"
#include <unistd.h>
#endif
#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <optional>
#include <string_view>
#include <utility>

namespace {
#ifdef _WIN32
constexpr wchar_t separator = L'\\', list_separator = L';';
std::wstring normalized(const std::filesystem::path& path) {
    auto result = path.lexically_normal().wstring();
    if (result.starts_with(L"\\\\?\\UNC\\")) result = L"\\\\" + result.substr(8);
    else if (result.starts_with(L"\\\\?\\")) result.erase(0, 4);
    std::replace(result.begin(), result.end(), L'/', L'\\');
    std::transform(result.begin(), result.end(), result.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return result;
}
#else
constexpr wchar_t separator = L'/', list_separator = L':';
// Linux file systems tell case apart.
std::wstring normalized(const std::filesystem::path& path) { return path.lexically_normal().wstring(); }
std::wstring lower(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return text;
}
#endif
bool helper(std::wstring name) {
    std::transform(name.begin(), name.end(), name.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    for (const auto* ignored : {L"steam.exe", L"steamwebhelper.exe", L"galaxyclient.exe", L"battle.net.exe", L"epicgameslauncher.exe",
         L"ubisoftconnect.exe", L"upc.exe", L"eadesktop.exe", L"xboxpcapp.exe", L"xboxpcappce.exe", L"crashreportclient.exe", L"unitycrashhandler64.exe"})
        if (name == ignored) return true;
#ifndef _WIN32
    // The Linux clients (Steam, Heroic, Lutris and their helpers) and crash handlers.
    for (const auto* ignored : {L"steam", L"steamwebhelper", L"steam-runtime-launcher-service", L"heroic", L"legendary", L"gogdl", L"nile",
         L"lutris", L"crashreportclient", L"unitycrashhandler64", L"unitycrashhandler"})
        if (name == ignored) return true;
#endif
    return false;
}
#ifdef _WIN32
struct Handle { HANDLE value; ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); } };
using Held = HANDLE;
#else
using Held = GameProcess;
// A process of this user, as a Toolhelp snapshot entry: identity, and its program when readable.
struct Entry {
    GameProcess process;
    std::optional<std::wstring> image; // normalized()
    std::wstring name;                 // The program's file name (else the kernel's name for it).
};
// This user's live processes (the "session" of the Windows build).
std::vector<Entry> snapshot() {
    std::vector<Entry> result;
    const auto user = static_cast<uint32_t>(::getuid());
    for (const auto pid : proc_fs::processes()) {
        const auto stat = proc_fs::stat(pid);
        if (!stat || stat->ended() || !stat->start || proc_fs::owner(pid) != user) continue;
        Entry entry{{pid, stat->start}, {}, {}};
        if (const auto image = proc_fs::image(pid)) {
            entry.image = normalized(*image);
            // A Wine program's name keeps its Windows form (C:\...\game.exe).
            const auto text = image->wstring();
            const auto slash = text.find_last_of(L"/\\");
            entry.name = slash == std::wstring::npos ? text : text.substr(slash + 1);
        } else entry.name = text_encoding::wideLenient(proc_fs::name(pid));
        result.push_back(std::move(entry));
    }
    return result;
}
bool same(const GameProcess& process) {
    const auto stat = proc_fs::stat(process.pid);
    return stat && !stat->ended() && stat->start == process.created;
}
#endif
bool sharedFolder(const std::wstring& directory);
// How a game's processes are recognized; empty when a shared shell/launcher would be ambiguous.
struct ProcessMatch {
    std::wstring executable, directory;
    bool matches(const std::wstring& full) const {
        return (!executable.empty() && full == executable) || (!directory.empty() && full.starts_with(directory));
    }
};
std::optional<ProcessMatch> processMatch(const Game& game) {
    ProcessMatch match;
    match.executable = game.executable.is_absolute() ? normalized(game.executable) : std::wstring{};
    match.directory = game.install_directory.is_absolute() && game.install_directory != game.install_directory.root_path() ? normalized(game.install_directory) : std::wstring{};
    if (!match.directory.empty() && match.directory.back() != separator) match.directory += separator;
    // A custom game's directory is only its working folder. Apply these rules
    // to watches and destructive Quick Resume actions as well as discovery.
    if (game.source == GameSource::custom || (!match.directory.empty() && sharedFolder(match.directory))) match.directory.clear();
    if (match.executable.empty() && match.directory.empty()) return {};
    if (!match.executable.empty() && match.directory.empty()) {
        const auto name = normalized(game.executable.filename());
        if (helper(name) || name == L"cmd.exe" || name == L"powershell.exe" || name == L"pwsh.exe" || name == L"explorer.exe") return {};
#ifndef _WIN32
        for (const auto* shared : {L"sh", L"bash", L"env", L"pwsh", L"flatpak", L"xdg-open", L"gio"}) if (name == shared) return {};
#endif
    }
    return match;
}
#ifdef _WIN32
std::optional<std::wstring> imagePath(HANDLE process) {
    // Not zero-filled: this runs for every process of each snapshot (64 KiB apiece); the
    // call reports the length it wrote.
    wchar_t path[32768]; DWORD count = static_cast<DWORD>(std::size(path));
    if (!process || !QueryFullProcessImageNameW(process, 0, path, &count) || count >= std::size(path)) return {};
    return normalized(std::filesystem::path(std::wstring_view(path, count)));
}
#endif
}
namespace {
#ifdef _WIN32
std::optional<GameActivity> steamActivity(const Game& game) {
    if (game.source != GameSource::steam || !isGame(game) || !game.app_id) return {};
    const auto key = L"Software\\Valve\\Steam\\Apps\\" + std::to_wstring(game.app_id);
    DWORD running = 0, size = sizeof(running);
    if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), L"Running", RRF_RT_REG_DWORD, nullptr, &running, &size) != ERROR_SUCCESS) return {};
    return running ? GameActivity::running : GameActivity::stopped;
}
#else
std::optional<GameActivity> steamActivity(const Game& game) {
    if (game.source != GameSource::steam || !isGame(game) || !game.app_id) return {};
    const auto running = steam_registry::number({"Apps", std::to_string(game.app_id)}, "Running");
    if (!running) return {};
    return *running ? GameActivity::running : GameActivity::stopped;
}
// As the Windows scan below; a held process is kept by its identity (PID and start time).
GameActivity scan(const Game& game, const ProcessMatch& match, std::vector<Held>* hold) {
    bool uncertain = false, running = false;
    const auto wanted = text_encoding::utf8Lenient(game.executable.filename().wstring()).substr(0, 15);
    for (const auto& entry : snapshot()) {
        if (helper(entry.name)) continue;
        if (!entry.image) {
            // Not inspectable (not dumpable): only its kernel name, cut to 15 bytes, is known.
            if (!game.executable.empty() && text_encoding::utf8Lenient(entry.name) == wanted) uncertain = true;
            continue;
        }
        if (!match.matches(*entry.image)) continue;
        running = true;
        if (!hold) break;
        if (hold->size() < 64) hold->push_back(entry.process);
    }
    return running ? GameActivity::running : uncertain ? GameActivity::unknown : GameActivity::stopped;
}
#endif
// Full process snapshot. With `hold`, every matching process is kept open with
// SYNCHRONIZE so later polls can wait on it instead of scanning the system again.
#ifdef _WIN32
GameActivity scan(const Game& game, const ProcessMatch& match, std::vector<Held>* hold) {
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return GameActivity::unknown;
    Handle snapshot_handle{snapshot};
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot, &entry)) return GameActivity::unknown;
    bool uncertain = false, running = false;
    do {
        // A terminating/protected process object can outlive every thread and
        // deny image queries. It is stopped, not permanently unknown/running.
        if (!entry.cntThreads || helper(entry.szExeFile)) continue;
        const DWORD access = PROCESS_QUERY_LIMITED_INFORMATION | (hold ? SYNCHRONIZE : 0);
        HANDLE process = OpenProcess(access, FALSE, entry.th32ProcessID);
        if (!process && hold) process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
        Handle owned{process};
        const auto full = imagePath(process);
        if (!full) {
            if (!game.executable.empty() && CompareStringOrdinal(game.executable.filename().c_str(), -1, entry.szExeFile, -1, TRUE) == CSTR_EQUAL) uncertain = true;
            continue;
        }
        if (!match.matches(*full)) continue;
        running = true;
        if (!hold) break;
        // A handle without SYNCHRONIZE cannot be waited on; that game keeps rescanning.
        if (WaitForSingleObject(process, 0) == WAIT_TIMEOUT && hold->size() < 64) hold->push_back(std::exchange(owned.value, nullptr));
    } while (Process32NextW(snapshot, &entry));
    return running ? GameActivity::running : uncertain ? GameActivity::unknown : GameActivity::stopped;
}
#endif

bool emulatorStem(std::wstring_view stem) {
    static const wchar_t* names[]{L"cemu", L"retroarch", L"dolphin", L"rpcs3", L"pcsx2", L"yuzu", L"ryujinx", L"suyu", L"sudachi", L"eden",
        L"citra", L"azahar", L"lime3ds", L"xenia", L"ppsspp", L"duckstation", L"epsxe", L"pcsxr", L"vita3k", L"melonds", L"mgba",
        L"snes9x", L"mesen", L"bsnes", L"project64", L"flycast", L"redream", L"xemu", L"mame", L"shadps4", L"ares", L"winuae", L"fs-uae"};
    for (const std::wstring_view item : names) {
        if (!stem.starts_with(item)) continue;
        if (stem.size() == item.size()) return true;
        if (!std::iswalpha(stem[item.size()])) return true;
    }
    return false;
}
bool platformEmulator(const Game& game, std::wstring_view stem) {
    if (!emulatorStem(stem)) return false;
    const auto* platform = findEmulationPlatform(game.platform);
    if (!platform || platform->name == L"PC") return false;
    if (platform->name == L"Retro" || platform->name == L"Amiga")
        return stem.starts_with(L"retroarch") || stem.starts_with(L"winuae") || stem.starts_with(L"fs-uae");
    for (const auto marker : platform->markers) {
        if (marker.size() < 3 || marker.find(L' ') != std::wstring_view::npos || marker.find(L'\\') != std::wstring_view::npos) continue;
        if (stem.starts_with(marker) && (stem.size() == marker.size() || !std::iswalpha(stem[marker.size()]))) return true;
    }
    return platform->name == L"Switch" && (stem.starts_with(L"eden") || stem.starts_with(L"suyu"));
}
std::wstring folded(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
#ifdef _WIN32
    for (auto& ch : text) if (ch == L'/') ch = L'\\';
#endif
    return text;
}
bool mentionsRom(std::wstring_view text, const Game& game, bool title) {
    if (text.empty()) return false;
    const auto hay = folded(std::wstring(text));
    if (!game.rom.empty()) {
        const auto full = folded(game.rom.wstring());
        const auto file = folded(game.rom.filename().wstring());
        if (full.size() >= 5 && hay.find(full) != std::wstring::npos) return true;
        if (file.size() >= 5 && hay.find(file) != std::wstring::npos) return true;
    }
    return title && game.name.size() >= 4 && hay.find(folded(game.name)) != std::wstring::npos;
}
struct SeenEmulator {
    GameProcess process;
    std::wstring stem;
    bool names_rom = false;
};
// Minecraft (MultiMC / Prism instances) runs as javaw.exe from a shared Java folder: the instance
// is recognised by its folder on the Java command line (natives, game directory).
struct JavaProcess {
    GameProcess process;
    std::wstring command; // folded()
};
#ifndef _WIN32
std::optional<std::wstring> commandLine(uint32_t pid) {
    const auto arguments = proc_fs::commandLine(pid);
    if (!arguments) return {};
    std::wstring line;
    for (const auto& argument : *arguments) {
        if (!line.empty()) line += L' ';
        line += text_encoding::wideLenient(argument);
    }
    return line;
}
// Window titles belong to the display server: on Linux the command line alone names the ROM.
std::vector<SeenEmulator> emulatorSnapshot(const Game& game) {
    std::vector<SeenEmulator> seen;
    if (game.rom.empty() && game.name.size() < 4) return seen;
    const auto self = system_info::processId();
    for (const auto& entry : snapshot()) {
        if (entry.process.pid == self || helper(entry.name)) continue;
        const auto stem = lower(std::filesystem::path(entry.name).stem().wstring());
        if (!emulatorStem(stem)) continue;
        bool named = false;
        if (const auto line = commandLine(entry.process.pid)) named = mentionsRom(*line, game, false);
        seen.push_back({entry.process, stem, named});
        if (seen.size() >= 64) break;
    }
    return seen;
}
bool javaName(std::wstring_view name) {
    return name == L"java" || name == L"javaw" || lower(std::wstring(name)) == L"javaw.exe" || lower(std::wstring(name)) == L"java.exe";
}
std::vector<JavaProcess> javaProcesses(uint32_t only_pid = 0) {
    std::vector<JavaProcess> result;
    for (const auto& entry : snapshot()) {
        if (!javaName(entry.name) || (only_pid && entry.process.pid != only_pid)) continue;
        const auto line = commandLine(entry.process.pid);
        if (!line) continue;
        result.push_back({entry.process, folded(*line)});
        if (result.size() >= 64) break;
    }
    return result;
}
#else
std::optional<std::wstring> commandLine(HANDLE process) {
    const auto ntdll = GetModuleHandleW(L"ntdll.dll");
    using Query = long (NTAPI *)(HANDLE, unsigned long, void*, unsigned long, unsigned long*);
    const auto query = ntdll ? reinterpret_cast<Query>(GetProcAddress(ntdll, "NtQueryInformationProcess")) : nullptr;
    if (!query) return {};
    struct Info { PVOID reserved1; PVOID peb; PVOID reserved2[2]; ULONG_PTR pid; PVOID reserved3; } info{};
    if (query(process, 0, &info, sizeof(info), nullptr) < 0) return {};
    ULONG_PTR wow = 0;
    const bool wow64 = query(process, 26, &wow, sizeof(wow), nullptr) >= 0 && wow;
    const auto peb = wow64 ? reinterpret_cast<PVOID>(wow) : info.peb;
    if (!peb) return {};
    auto readText = [&](PVOID address, uint16_t bytes) -> std::optional<std::wstring> {
        if (!address || bytes < sizeof(wchar_t)) return {};
        std::wstring text(std::min<size_t>(bytes / sizeof(wchar_t), 32768), L'\0');
        SIZE_T got = 0;
        if (!ReadProcessMemory(process, address, text.data(), text.size() * sizeof(wchar_t), &got) || got < sizeof(wchar_t)) return {};
        text.resize(got / sizeof(wchar_t));
        return text;
    };
    if (wow64) {
        uint32_t params = 0;
        if (!ReadProcessMemory(process, static_cast<BYTE*>(peb) + 0x10, &params, sizeof(params), nullptr) || !params) return {};
        struct Line { uint16_t len, max; uint32_t buf; } cmd{};
        if (!ReadProcessMemory(process, reinterpret_cast<void*>(static_cast<uintptr_t>(params) + 0x40), &cmd, sizeof(cmd), nullptr)) return {};
        return readText(reinterpret_cast<void*>(static_cast<uintptr_t>(cmd.buf)), cmd.len);
    }
    PVOID params = nullptr;
    if (!ReadProcessMemory(process, static_cast<BYTE*>(peb) + 0x20, &params, sizeof(params), nullptr) || !params) return {};
    struct Line { uint16_t len, max; uint32_t pad; PVOID buf; } cmd{};
    if (!ReadProcessMemory(process, static_cast<BYTE*>(params) + 0x70, &cmd, sizeof(cmd), nullptr)) return {};
    return readText(cmd.buf, cmd.len);
}
std::vector<SeenEmulator> emulatorSnapshot(const Game& game) {
    std::vector<SeenEmulator> seen;
    if (game.rom.empty() && game.name.size() < 4) return seen;
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return seen;
    Handle snapshot_handle{snapshot};
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    const DWORD self = GetCurrentProcessId();
    DWORD session = 0;
    if (!Process32FirstW(snapshot, &entry) || !ProcessIdToSessionId(self, &session)) return seen;
    std::vector<std::pair<uint32_t, std::wstring>> titles;
    EnumWindows([](HWND window, LPARAM param) -> BOOL {
        auto* out = reinterpret_cast<std::vector<std::pair<uint32_t, std::wstring>>*>(param);
        if (!IsWindowVisible(window) || out->size() >= 256) return TRUE;
        wchar_t text[512]{};
        DWORD pid = 0;
        if (GetWindowTextW(window, text, static_cast<int>(std::size(text))) > 0 && GetWindowThreadProcessId(window, &pid) && pid)
            out->push_back({pid, text});
        return TRUE;
    }, reinterpret_cast<LPARAM>(&titles));
    do {
        DWORD owner = 0;
        if (!entry.cntThreads || entry.th32ProcessID == self || helper(entry.szExeFile) ||
            !ProcessIdToSessionId(entry.th32ProcessID, &owner) || owner != session) continue;
        const auto stem = normalized(std::filesystem::path(entry.szExeFile).stem().wstring());
        if (!emulatorStem(stem)) continue;
        Handle process{OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE, FALSE, entry.th32ProcessID)};
        if (!process.value) process.value = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, entry.th32ProcessID);
        FILETIME created{}, exited{}, kernel{}, user{};
        if (!process.value || !GetProcessTimes(process.value, &created, &exited, &kernel, &user)) continue;
        const auto ticks = (static_cast<uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
        if (!ticks) continue;
        bool named = false;
        if (const auto line = commandLine(process.value)) named = mentionsRom(*line, game, false);
        if (!named) for (const auto& [pid, title] : titles) if (pid == entry.th32ProcessID && mentionsRom(title, game, true)) named = true;
        seen.push_back({{entry.th32ProcessID, ticks}, stem, named});
    } while (Process32NextW(snapshot, &entry) && seen.size() < 64);
    return seen;
}
bool javaName(std::wstring_view name) {
    return CompareStringOrdinal(name.data(), static_cast<int>(name.size()), L"javaw.exe", -1, TRUE) == CSTR_EQUAL ||
           CompareStringOrdinal(name.data(), static_cast<int>(name.size()), L"java.exe", -1, TRUE) == CSTR_EQUAL;
}
std::vector<JavaProcess> javaProcesses(DWORD only_pid = 0) {
    std::vector<JavaProcess> result;
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return result;
    Handle snapshot_handle{snapshot};
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    DWORD session = 0;
    if (!Process32FirstW(snapshot, &entry) || !ProcessIdToSessionId(GetCurrentProcessId(), &session)) return result;
    do {
        DWORD owner = 0;
        if (!entry.cntThreads || !javaName(entry.szExeFile) || (only_pid && entry.th32ProcessID != only_pid) ||
            !ProcessIdToSessionId(entry.th32ProcessID, &owner) || owner != session) continue;
        Handle process{OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, entry.th32ProcessID)};
        FILETIME created{}, exited{}, kernel{}, user{};
        if (!process.value || !GetProcessTimes(process.value, &created, &exited, &kernel, &user)) continue;
        const auto line = commandLine(process.value);
        if (!line) continue;
        result.push_back({{entry.th32ProcessID, (static_cast<uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime}, folded(*line)});
    } while (Process32NextW(snapshot, &entry) && result.size() < 64);
    return result;
}
#endif
// The instance folder named as a whole path (not as the start of a longer folder name).
bool namesInstance(const std::wstring& command, const Game& game) {
    if (game.source != GameSource::multimc || !game.install_directory.is_absolute()) return false;
    auto folder = folded(game.install_directory.lexically_normal().wstring());
    while (folder.size() > 3 && folder.back() == separator) folder.pop_back();
    if (folder.size() < 4) return false;
    for (size_t at = command.find(folder); at != std::wstring::npos; at = command.find(folder, at + 1)) {
        const size_t end = at + folder.size();
        if (end == command.size() || command[end] == separator || command[end] == L'"' || command[end] == L' ' || command[end] == list_separator) return true;
    }
    return false;
}
std::vector<GameProcess> minecraftProcesses(const Game& game) {
    std::vector<GameProcess> found;
    if (game.source != GameSource::multimc) return found;
    for (const auto& java : javaProcesses()) if (namesInstance(java.command, game)) found.push_back(java.process);
    return found;
}
#ifdef _WIN32
bool holdProcess(uint32_t pid, std::vector<HANDLE>* hold) {
    if (!hold || hold->size() >= 64) return false;
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!process) return false;
    if (WaitForSingleObject(process, 0) != WAIT_TIMEOUT) { CloseHandle(process); return false; }
    hold->push_back(process);
    return true;
}
#else
bool holdProcess(uint32_t pid, std::vector<Held>* hold) {
    if (!hold || hold->size() >= 64) return false;
    const auto stat = proc_fs::stat(pid);
    if (!stat || stat->ended()) return false;
    hold->push_back({pid, stat->start});
    return true;
}
#endif
}

GameActivity readGameActivity(const Game& game) {
    if (game.source == GameSource::multimc) return minecraftProcesses(game).empty() ? GameActivity::stopped : GameActivity::running;
    const auto steam = steamActivity(game);
    if (steam == GameActivity::running) return *steam;
    // Running=0 (or no client state) says nothing about a directly started EXE.
    // A launcher script exiting is not the emulator exiting.
    const bool emulated = game.kind == GameKind::nonsteam && !game.rom.empty();
    const auto match = processMatch(game);
    if (match) {
        const auto activity = scan(game, *match, nullptr);
        if (activity == GameActivity::running || !emulated) return activity;
    } else if (!emulated) return steam.value_or(GameActivity::unknown);
    if (!findEmulatorRomProcesses(game).empty()) return GameActivity::running;
    return GameActivity::unknown;
}

ProcessWatch::~ProcessWatch() { release(); }
ProcessWatch::ProcessWatch(ProcessWatch&& other) noexcept
    : handles_(std::move(other.handles_)), baseline_(std::move(other.baseline_)), primed_(other.primed_), confirmed_(other.confirmed_) {
    other.handles_.clear(); other.primed_ = other.confirmed_ = false;
}
ProcessWatch& ProcessWatch::operator=(ProcessWatch&& other) noexcept {
    if (this != &other) {
        release();
        handles_ = std::move(other.handles_); baseline_ = std::move(other.baseline_);
        primed_ = other.primed_; confirmed_ = other.confirmed_;
        other.handles_.clear(); other.primed_ = other.confirmed_ = false;
    }
    return *this;
}
void ProcessWatch::release() {
#ifdef _WIN32
    for (const auto handle : handles_) CloseHandle(handle);
#endif
    handles_.clear();
}
bool ProcessWatch::holding() const { return !handles_.empty(); }
GameActivity ProcessWatch::read(const Game& game) {
#ifdef _WIN32
    // Steady state: one kernel wait per known process, no system-wide snapshot.
    std::erase_if(handles_, [](HANDLE handle) {
        if (WaitForSingleObject(handle, 0) == WAIT_TIMEOUT) return false;
        CloseHandle(handle); return true;
    });
#else
    // Steady state: one /proc/<pid>/stat read per known process, no scan of /proc.
    std::erase_if(handles_, [](const GameProcess& process) { return !same(process); });
#endif
    if (!handles_.empty()) { confirmed_ = true; return GameActivity::running; }
    if (game.source == GameSource::multimc) {
        // Its Java process, held from then on; until it appears the launch job (the launcher
        // started with --launch) speaks for the session.
        for (const auto& process : minecraftProcesses(game)) holdProcess(process.pid, &handles_);
        if (!handles_.empty()) { confirmed_ = true; return GameActivity::running; }
        return GameActivity::stopped;
    }
    const bool emulated = game.kind == GameKind::nonsteam && !game.rom.empty();
    const auto steam = steamActivity(game);
    // Steam's Running bit follows the shortcut script, not Cemu. A stuck bit must
    // not keep the session (and the menu music) up after the emulator has exited.
    if (steam == GameActivity::running && !emulated) { confirmed_ = true; return *steam; }
    const auto match = processMatch(game);
    if (match) {
        const auto activity = scan(game, *match, &handles_);
        if (activity == GameActivity::running) { confirmed_ = true; return activity; }
        if (!emulated) return activity;
    }
    if (emulated && claimEmulator(game)) return GameActivity::running;
    if (confirmed_) return GameActivity::stopped;
    if (emulated) return primed_ || steam == GameActivity::stopped ? GameActivity::stopped : GameActivity::unknown;
    return steam.value_or(GameActivity::unknown);
}
bool ProcessWatch::claimEmulator(const Game& game) {
    const auto seen = emulatorSnapshot(game);
    for (const auto& item : seen) if (item.names_rom) holdProcess(item.process.pid, &handles_);
    // A process that appears after launch is the handoff. Once the emulator we
    // already watched has exited, a leftover helper must not keep the session open.
    if (handles_.empty() && primed_ && !confirmed_)
        for (const auto& item : seen)
            if (platformEmulator(game, item.stem) &&
                std::find(baseline_.begin(), baseline_.end(), item.process.pid) == baseline_.end())
                holdProcess(item.process.pid, &handles_);
    if (!handles_.empty()) { confirmed_ = true; return true; }
    if (!confirmed_) {
        baseline_.clear();
        for (const auto& item : seen) if (platformEmulator(game, item.stem)) baseline_.push_back(item.process.pid);
        primed_ = true;
    }
    return false;
}
std::vector<GameProcess> findEmulatorRomProcesses(const Game& game) {
    std::vector<GameProcess> found;
    if (game.kind != GameKind::nonsteam || game.rom.empty()) return found;
    for (const auto& item : emulatorSnapshot(game)) if (item.names_rom) found.push_back(item.process);
    return found;
}

std::vector<GameProcess> findGameProcesses(const Game& game) {
    if (game.source == GameSource::multimc) return minecraftProcesses(game);
    const auto match = processMatch(game);
    if (!match) return findEmulatorRomProcesses(game);
#ifndef _WIN32
    std::vector<GameProcess> found;
    const auto self = system_info::processId();
    for (const auto& entry : snapshot()) {
        if (entry.process.pid == self || helper(entry.name) || !entry.image || !match->matches(*entry.image)) continue;
        found.push_back(entry.process);
        if (found.size() >= 64) break;
    }
    return found;
#else
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return {};
    Handle snapshot_handle{snapshot};
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot, &entry)) return {};
    const DWORD self = GetCurrentProcessId();
    DWORD session = 0;
    if (!ProcessIdToSessionId(self, &session)) return {};
    std::vector<GameProcess> found;
    do {
        DWORD owner = 0;
        // Services of other sessions (anti-cheat, updaters) are not part of the game window.
        if (!entry.cntThreads || entry.th32ProcessID == self || helper(entry.szExeFile) ||
            !ProcessIdToSessionId(entry.th32ProcessID, &owner) || owner != session) continue;
        Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID)};
        const auto full = imagePath(process.value);
        FILETIME created{}, exited{}, kernel{}, user{};
        if (!full || !match->matches(*full) || !GetProcessTimes(process.value, &created, &exited, &kernel, &user)) continue;
        const auto ticks = (static_cast<uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
        if (ticks) found.push_back({entry.th32ProcessID, ticks});
    } while (Process32NextW(snapshot, &entry) && found.size() < 64);
    return found;
#endif
}

bool sharesProcesses(const Game& a, const Game& b) {
    const auto first = processMatch(a), second = processMatch(b);
    if (!first || !second) return false;
    if (!first->executable.empty() && second->matches(first->executable)) return true;
    if (!second->executable.empty() && first->matches(second->executable)) return true;
    return !first->directory.empty() && first->directory == second->directory;
}

#ifndef _WIN32
// The foreground window and its process are the display server's (X11, Wayland), so the UI's.
std::optional<GameWindow> findWindowGame(HWND, const std::vector<Game>&) { return {}; }
#else
std::optional<GameWindow> findWindowGame(HWND window, const std::vector<Game>& candidates) {
    DWORD pid = 0;
    if (!window || !IsWindowVisible(window) || IsIconic(window) || !GetWindowThreadProcessId(window, &pid) ||
        !pid || pid == GetCurrentProcessId()) return {};
    Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)};
    const auto path = imagePath(process.value);
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!path || helper(std::filesystem::path(*path).filename().wstring()) ||
        !GetProcessTimes(process.value, &created, &exited, &kernel, &user)) return {};
    const GameProcess identity{pid, (static_cast<uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime};
    if (javaName(std::filesystem::path(*path).filename().wstring())) {
        // Minecraft's window: the instance its command line names.
        const auto java = javaProcesses(pid);
        if (!java.empty())
            for (const auto& game : candidates)
                if (game.source == GameSource::multimc && isGame(game) && namesInstance(java.front().command, game)) return GameWindow{window, identity, gameKey(game)};
    }
    const Game* best = nullptr;
    size_t score = 0;
    for (const auto& game : candidates) {
        if (!isGame(game) || !game.installed || game.installation.active()) continue;
        const auto match = processMatch(game);
        if (!match) continue;
        const size_t next = !match->executable.empty() && *path == match->executable ? SIZE_MAX :
            !match->directory.empty() && path->starts_with(match->directory) ? match->directory.size() : 0;
        if (next > score) { best = &game; score = next; }
    }
    if (!best) return {};
    return GameWindow{window, {pid, (static_cast<uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime}, gameKey(*best)};
}
#endif

namespace {
#ifndef _WIN32
// `directory` (normalized, ending in '/') is, or lies above, the home folder or a folder many
// programs run from, or lies inside the system: a process there proves nothing about one game.
bool sharedFolder(const std::wstring& directory) {
    std::vector<std::wstring> above{L"/usr/", L"/opt/", L"/tmp/", L"/var/", L"/home/", L"/run/", L"/srv/", L"/mnt/", L"/media/"};
    if (const auto home = system_info::environment(L"HOME"); home && !home->empty()) {
        auto folder = normalized(std::filesystem::path(*home));
        if (folder.back() != separator) folder += separator;
        above.push_back(std::move(folder));
    }
    for (const auto& folder : above) if (folder.starts_with(directory)) return true;
    for (const auto* system : {L"/usr/", L"/bin/", L"/sbin/", L"/lib/", L"/lib64/", L"/etc/", L"/proc/", L"/sys/", L"/dev/"})
        if (directory.starts_with(system)) return true;
    return false;
}
#else
// `directory` (normalized, ending in '\') is, or lies above, a folder many programs run from,
// or lies inside Windows: a process there proves nothing about one game.
bool sharedFolder(const std::wstring& directory) {
    for (const auto* variable : {L"SystemRoot", L"ProgramFiles", L"ProgramFiles(x86)", L"ProgramW6432", L"ProgramData",
                                 L"USERPROFILE", L"PUBLIC", L"APPDATA", L"LOCALAPPDATA"}) {
        wchar_t value[1024];
        const DWORD length = GetEnvironmentVariableW(variable, value, static_cast<DWORD>(std::size(value)));
        if (!length || length >= std::size(value)) continue;
        auto folder = normalized(std::filesystem::path(std::wstring_view(value, length)));
        if (folder.empty()) continue;
        if (folder.back() != L'\\') folder += L'\\';
        if (folder.starts_with(directory)) return true;
        if (std::wstring_view(variable) == L"SystemRoot" && directory.starts_with(folder)) return true;
    }
    return false;
}
#endif
}

#ifndef _WIN32
HANDLE launchJob(HANDLE process) { return process > 0 ? process : 0; }

GameActivity readLaunchJob(HANDLE job) {
    if (job <= 0) return GameActivity::unknown;
    const auto session = static_cast<uint32_t>(job);
    const auto self = system_info::processId();
    for (const auto pid : proc_fs::processes()) {
        const auto stat = proc_fs::stat(pid);
        if (!stat || stat->ended() || stat->session != session) continue;
        // The ID is free again once the session is empty: a new leader of that number that is not
        // the launcher's child is another program's session.
        if (pid == session && stat->parent != self) continue;
        const auto image = proc_fs::image(pid);
        if (!image) return GameActivity::running; // Not inspectable: still a live process of the game.
        // A store client the script or link started (and that stays open) is not the game.
        const auto text = image->wstring();
        const auto slash = text.find_last_of(L"/\\");
        if (helper(slash == std::wstring::npos ? text : text.substr(slash + 1))) continue;
        return GameActivity::running;
    }
    return GameActivity::stopped;
}
#else
HANDLE launchJob(HANDLE process) {
    if (!process) return nullptr;
    const HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job) return nullptr;
    // No limits and no kill-on-close: the job only lets the launcher see the process tree.
    if (!AssignProcessToJobObject(job, process)) { CloseHandle(job); return nullptr; }
    return job;
}

GameActivity readLaunchJob(HANDLE job) {
    if (!job) return GameActivity::unknown;
    struct List { JOBOBJECT_BASIC_PROCESS_ID_LIST header; ULONG_PTR more[255]; } list{};
    list.header.NumberOfAssignedProcesses = 256;
    if (!QueryInformationJobObject(job, JobObjectBasicProcessIdList, &list, sizeof(list), nullptr)) {
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
        if (!QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &accounting, sizeof(accounting), nullptr)) return GameActivity::unknown;
        return accounting.ActiveProcesses ? GameActivity::running : GameActivity::stopped;
    }
    const auto* ids = list.header.ProcessIdList;
    for (DWORD i = 0; i < list.header.NumberOfProcessIdsInList; ++i) {
        Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(ids[i]))};
        const auto path = imagePath(process.value);
        if (!path) return GameActivity::running; // Protected (anti-cheat): still a live process of the game.
        // A store client the script or link started (and that stays open) is not the game.
        const auto name = std::filesystem::path(*path).filename().wstring();
        if (helper(name) || name == L"conhost.exe") continue;
        return GameActivity::running;
    }
    return GameActivity::stopped;
}
#endif

#ifdef _WIN32
LaunchJob ownLaunchJob(HANDLE job) { return job ? LaunchJob(job, CloseHandle) : LaunchJob{}; }
GameActivity readLaunchJob(const LaunchJob& job) { return job ? readLaunchJob(static_cast<HANDLE>(job.get())) : GameActivity::unknown; }
void closeLaunchedProcess(HANDLE process) { if (process) CloseHandle(process); }
#else
LaunchJob ownLaunchJob(HANDLE job) { return job > 0 ? std::make_shared<HANDLE>(job) : LaunchJob{}; }
GameActivity readLaunchJob(const LaunchJob& job) { return job ? readLaunchJob(*static_cast<const HANDLE*>(job.get())) : GameActivity::unknown; }
void closeLaunchedProcess(HANDLE) {}
#endif

std::vector<size_t> findRunningGames(const std::vector<Game>& candidates) {
    std::vector<size_t> running;
#ifdef _WIN32
    DWORD steam_app = 0, size = sizeof(steam_app);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"RunningAppID", RRF_RT_REG_DWORD,
                     nullptr, &steam_app, &size) != ERROR_SUCCESS) steam_app = 0;
#else
    const auto reported = steam_registry::number({}, "RunningAppID");
    const uint32_t steam_app = reported && *reported <= UINT32_MAX ? static_cast<uint32_t>(*reported) : 0;
#endif
    std::vector<std::pair<size_t, ProcessMatch>> matchers;
    std::vector<char> found(candidates.size());
    bool needs_scan = false;
    // Minecraft instances: one look at the Java processes' command lines for all of them.
    std::vector<size_t> instances;
    for (size_t i = 0; i < candidates.size(); ++i)
        if (candidates[i].source == GameSource::multimc && isGame(candidates[i]) && candidates[i].installed) instances.push_back(i);
    if (!instances.empty()) {
        const auto java = javaProcesses();
        for (const auto index : instances)
            if (std::any_of(java.begin(), java.end(), [&](const JavaProcess& item) { return namesInstance(item.command, candidates[index]); })) {
                found[index] = 1; running.push_back(index);
            }
    }
    for (size_t i = 0; i < candidates.size(); ++i) {
        const auto& game = candidates[i];
        if (!isGame(game) || !game.installed || game.installation.active() || game.kind == GameKind::nonsteam || game.kind == GameKind::catalog) continue;
        if (game.source == GameSource::multimc) continue;
        if (game.source == GameSource::steam && steam_app && game.app_id == steam_app && steamActivity(game) == GameActivity::running) {
            found[i] = 1; running.push_back(i);
        }
        // One shared snapshot also finds Steam games started without the client.
        // Keep reported games in the matchers so their processes cannot be assigned to a broader folder.
        if (auto match = processMatch(game)) {
            matchers.emplace_back(i, std::move(*match));
            needs_scan = needs_scan || !found[i];
        }
    }
    if (!needs_scan) return running;
#ifndef _WIN32
    const auto self = system_info::processId();
    for (const auto& entry : snapshot()) {
        if (entry.process.pid == self || helper(entry.name) || !entry.image) continue;
        const auto& full = entry.image;
#else
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return running;
    Handle snapshot_handle{snapshot};
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    const DWORD self = GetCurrentProcessId();
    DWORD session = 0;
    if (!Process32FirstW(snapshot, &entry) || !ProcessIdToSessionId(self, &session)) return running;
    do {
        DWORD owner = 0;
        // Services of other sessions (anti-cheat, updaters) are no sign of a game being played.
        if (!entry.cntThreads || entry.th32ProcessID == self || helper(entry.szExeFile) ||
            !ProcessIdToSessionId(entry.th32ProcessID, &owner) || owner != session) continue;
        Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID)};
        const auto full = imagePath(process.value);
        if (!full) continue;
#endif
        std::optional<size_t> best;
        size_t best_score = 0;
        for (const auto& [index, match] : matchers) {
            const size_t score = !match.executable.empty() && *full == match.executable ? SIZE_MAX :
                                 !match.directory.empty() && full->starts_with(match.directory) ? match.directory.size() : 0;
            if (score > best_score) { best_score = score; best = index; }
        }
        if (best && !found[*best]) { found[*best] = 1; running.push_back(*best); }
#ifdef _WIN32
    } while (Process32NextW(snapshot, &entry));
#else
    }
#endif
    return running;
}
