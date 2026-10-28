#include "../src/model/game_session.hpp"
#include "../src/model/frozen_games.hpp"
#include "../src/platform/game_activity.hpp"
#include "../src/platform/process_freeze.hpp"
#include "../src/platform/atomic_file.hpp"
#include <Windows.h>
#ifndef _WIN32
#include "../src/platform/proc_fs.hpp"
#include "../src/platform/process_spawn.hpp"
#include "../src/platform/text_encoding.hpp"
#include <csignal>
#include <fcntl.h>
#include <map>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>

namespace {
#ifdef _WIN32
// Redirect HKCU in this test process only; never read/write the user's Steam state.
class SteamRegistry {
public:
    SteamRegistry() {
        path_ = L"Software\\Launchme-test-steam-" + std::to_wstring(GetCurrentProcessId()) + L'-' + std::to_wstring(GetTickCount64());
        if (RegCreateKeyExW(HKEY_CURRENT_USER, path_.c_str(), 0, nullptr, REG_OPTION_VOLATILE,
                           KEY_ALL_ACCESS, nullptr, &root_, nullptr) == ERROR_SUCCESS)
            ready_ = RegOverridePredefKey(HKEY_CURRENT_USER, root_) == ERROR_SUCCESS;
    }
    ~SteamRegistry() {
        if (ready_) RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
        if (root_) { RegCloseKey(root_); RegDeleteTreeW(HKEY_CURRENT_USER, path_.c_str()); }
    }
    bool ready() const { return ready_; }
    bool report(DWORD app, DWORD running, DWORD foreground) {
        const auto path = L"Software\\Valve\\Steam\\Apps\\" + std::to_wstring(app);
        HKEY key = nullptr;
        if (!ready_ || RegCreateKeyExW(root_, path.c_str(), 0, nullptr, REG_OPTION_VOLATILE,
                                      KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
        const auto result = RegSetValueExW(key, L"Running", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&running), sizeof(running));
        RegCloseKey(key);
        return result == ERROR_SUCCESS && RegSetKeyValueW(root_, L"Software\\Valve\\Steam", L"RunningAppID", REG_DWORD,
                                                         &foreground, sizeof(foreground)) == ERROR_SUCCESS;
    }
private:
    std::wstring path_;
    HKEY root_ = nullptr;
    bool ready_ = false;
};

// Child mode: a busy counter in shared memory proves whether its threads run.
int child(const wchar_t* mapping_name) {
    const auto mapping = OpenFileMappingW(FILE_MAP_WRITE, FALSE, mapping_name);
    auto* counter = mapping ? static_cast<volatile LONG*>(MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, sizeof(LONG))) : nullptr;
    if (!counter) return 1;
    for (const auto started = GetTickCount64(); GetTickCount64() - started < 20000;) { InterlockedIncrement(counter); Sleep(2); }
    return 0;
}

// Games started outside the launcher: a copy of this test in a folder of its own is the
// "game" (other tests may run next to this executable at the same time). It ends itself
// after 20 s at the latest, and is ended here earlier.
int runningDiscovery(const wchar_t* executable, const std::wstring& mapping_name) {
    SteamRegistry registry;
    if (!registry.ready()) return 58;
    const auto folder = std::filesystem::temp_directory_path() / (L"launchme-running-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    const auto copy = folder / L"game.exe";
    struct Cleanup { std::filesystem::path folder; PROCESS_INFORMATION process{};
        ~Cleanup() {
            if (process.hProcess) { TerminateProcess(process.hProcess, 0); WaitForSingleObject(process.hProcess, 5000); CloseHandle(process.hProcess); CloseHandle(process.hThread); }
            std::error_code ignored; std::filesystem::remove_all(folder, ignored);
        } } cleanup{folder};
    if (!std::filesystem::copy_file(executable, copy, std::filesystem::copy_options::overwrite_existing, ec)) return 40;
    Game by_folder; by_folder.source = GameSource::gog; by_folder.kind = GameKind::store; by_folder.install_directory = folder;
    Game by_exe; by_exe.source = GameSource::custom; by_exe.kind = GameKind::store; by_exe.executable = copy;
    Game custom_folder = by_exe; custom_folder.executable = folder / L"other.exe"; custom_folder.install_directory = folder;
    Game shortcut = by_exe; shortcut.source = GameSource::steam; shortcut.kind = GameKind::nonsteam;
    Game not_installed = by_folder; not_installed.installed = false;
    wchar_t system[MAX_PATH]{};
    GetSystemDirectoryW(system, MAX_PATH);
    Game system_folder = by_folder; system_folder.install_directory = system; // conhost, RuntimeBroker... run there.
    Game steam; steam.app_id = 0xFFFFFFF0u; steam.name = L"Not running";
    const std::vector<Game> candidates{by_folder, custom_folder, shortcut, not_installed, system_folder, steam};
    if (!findRunningGames(candidates).empty()) return 41; // The game is not started yet.
    auto command = L"\"" + copy.wstring() + L"\" --child " + mapping_name;
    STARTUPINFOW startup{sizeof(startup)};
    if (!CreateProcessW(copy.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &cleanup.process)) return 42;
    // A freshly copied executable may start slowly (virus scan, a loaded machine).
    std::vector<size_t> running;
    for (int i = 0; i < 400 && running.empty(); ++i) { Sleep(25); running = findRunningGames(candidates); }
    // Its folder only: not a user-added game's folder, a shortcut, an uninstalled game or System32.
    if (running != std::vector<size_t>{0}) return 43;
    // The exact executable is the more specific claim on the same process.
    if (findRunningGames({by_folder, by_exe}) != std::vector<size_t>{1}) return 44;
    // Quick Resume and session watches must use the same ownership rules as
    // discovery. A sibling program in a custom game's working folder is not it.
    if (!findGameProcesses(custom_folder).empty() || readGameActivity(custom_folder) != GameActivity::stopped) return 46;
    ProcessWatch unrelated;
    if (unrelated.read(custom_folder) != GameActivity::stopped || unrelated.holding()) return 47;
    const auto exact = findGameProcesses(by_exe);
    if (exact.size() != 1 || exact[0].pid != cleanup.process.dwProcessId) return 48;
    if (!findGameProcesses(system_folder).empty()) return 49;
    auto script = custom_folder; script.executable.clear();
    if (!findGameProcesses(script).empty() || readGameActivity(script) != GameActivity::unknown) return 50;

    Game direct = steam; direct.install_directory = folder;
    // A Steam title started directly is discoverable even without any Steam registry state.
    if (findRunningGames({direct}) != std::vector<size_t>{0}) return 59;
    // A persisted Running=0 must not veto a live executable, nor a watch holding it.
    if (!registry.report(direct.app_id, 0, 0)) return 60;
    ProcessWatch watch;
    if (findRunningGames({direct}) != std::vector<size_t>{0} || readGameActivity(direct) != GameActivity::running ||
        watch.read(direct) != GameActivity::running || !watch.holding() || watch.read(direct) != GameActivity::running) return 61;
    if (findRunningGames({direct, by_exe}) != std::vector<size_t>{1}) return 62;
    // Steam may be tracking another title at the same time. Keep its cheap registry path.
    Game reported = steam; reported.app_id -= 1;
    if (!registry.report(reported.app_id, 1, reported.app_id) ||
        findRunningGames({reported, direct}) != std::vector<size_t>({0, 1})) return 63;
    ProcessWatch steam_watch;
    if (readGameActivity(reported) != GameActivity::running || steam_watch.read(reported) != GameActivity::running ||
        steam_watch.holding()) return 64;
    // The registry and the process must not report the same title twice.
    if (!registry.report(direct.app_id, 1, direct.app_id) || findRunningGames({direct, custom_folder}) != std::vector<size_t>{0} ||
        watch.read(direct) != GameActivity::running || !watch.holding()) return 65;
    if (!registry.report(direct.app_id, 0, 0) || watch.read(direct) != GameActivity::running || !watch.holding()) return 66;
    // A protected game's exiting process can remain in Windows with no threads
    // while OpenProcess denies access. Denial alone must not keep "In game" forever.
    // Only this child fixture gets an empty DACL; the already-open owner handle
    // still permits cleanup. A live inaccessible process must remain alive.
    ACL acl{}; SECURITY_DESCRIPTOR descriptor{};
    if (!InitializeAcl(&acl, sizeof(acl), ACL_REVISION) ||
        !InitializeSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION) ||
        !SetSecurityDescriptorDacl(&descriptor, TRUE, &acl, FALSE) ||
        !SetKernelObjectSecurity(cleanup.process.hProcess, DACL_SECURITY_INFORMATION, &descriptor)) return 75;
    const auto denied = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, cleanup.process.dwProcessId);
    if (denied) { CloseHandle(denied); return 76; }
    if (GetLastError() != ERROR_ACCESS_DENIED || !process_freeze::alive(exact) ||
        readGameActivity(by_exe) != GameActivity::unknown || watch.read(direct) != GameActivity::running) return 77;
    TerminateProcess(cleanup.process.hProcess, 0);
    WaitForSingleObject(cleanup.process.hProcess, 5000);
    if (process_freeze::alive(exact)) return 78;
    if (!findRunningGames(candidates).empty()) return 45;
    if (!findRunningGames({direct}).empty() || readGameActivity(direct) != GameActivity::stopped ||
        watch.read(direct) != GameActivity::stopped || watch.holding()) return 67;
    if (readGameActivity(steam) != GameActivity::stopped || ProcessWatch{}.read(steam) != GameActivity::stopped) return 68;
    return 0;
}

// Suspends and resumes only a child copy of this test executable. No game or
// other program is ever touched; the child ends itself after 20 s at the latest.
int realSuspension() {
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
    const auto name = L"Local\\launchme-freeze-test-" + std::to_wstring(GetCurrentProcessId());
    const auto mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(LONG), name.c_str());
    auto* counter = mapping ? static_cast<volatile LONG*>(MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, sizeof(LONG))) : nullptr;
    if (!counter) return 20;
    struct Cleanup { HANDLE mapping; volatile LONG* view; PROCESS_INFORMATION process{};
        ~Cleanup() {
            if (process.hProcess) { TerminateProcess(process.hProcess, 0); WaitForSingleObject(process.hProcess, 5000); CloseHandle(process.hProcess); CloseHandle(process.hThread); }
            UnmapViewOfFile(const_cast<LONG*>(view)); CloseHandle(mapping);
        } } cleanup{mapping, counter};
    auto command = L"\"" + std::wstring(executable) + L"\" --child " + name;
    STARTUPINFOW startup{sizeof(startup)};
    if (!CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &cleanup.process)) return 21;
    const auto counted = [&] { return InterlockedCompareExchange(const_cast<LONG*>(counter), 0, 0); };
    for (int i = 0; i < 500 && counted() < 5; ++i) Sleep(10);
    if (counted() < 5) return 22;
    Game fixture; fixture.source = GameSource::gog; fixture.executable = executable;
    auto processes = findGameProcesses(fixture);
    // Exactly the child: the launcher itself (here: this test) is never a game process.
    if (processes.size() != 1 || processes[0].pid != cleanup.process.dwProcessId || !processes[0].created) return 23;

    if (process_freeze::suspend(processes) != process_freeze::Result::ok || processes.size() != 1) return 24;
    Sleep(120);
    const auto frozen = counted();
    Sleep(250);
    if (counted() != frozen || !process_freeze::alive(processes)) return 25;
    if (readGameActivity(fixture) != GameActivity::running) return 26; // A suspended game still runs.
    if (process_freeze::resume(processes) != process_freeze::Result::ok) return 27;
    for (int i = 0; i < 100 && counted() == frozen; ++i) Sleep(10);
    if (counted() == frozen) return 28;
    // A stale creation time is a different process: never suspended, reported gone.
    auto reused = processes; reused[0].created += 1;
    if (process_freeze::suspend(reused) != process_freeze::Result::gone || process_freeze::alive(reused)) return 29;
    // "Zamknij grę" also ends a suspended process.
    if (process_freeze::suspend(processes) != process_freeze::Result::ok ||
        process_freeze::terminate(processes) != process_freeze::Result::ok ||
        WaitForSingleObject(cleanup.process.hProcess, 5000) != WAIT_OBJECT_0) return 31;
    if (process_freeze::alive(processes) || process_freeze::resume(processes) != process_freeze::Result::gone ||
        process_freeze::terminate(processes) != process_freeze::Result::gone) return 30;
    return runningDiscovery(executable, name);
}

// Opt-in, read-only process workload. Reports cost instead of asserting machine-dependent timings.
int discoveryBenchmark() {
    SteamRegistry registry;
    if (!registry.ready()) return 70;
    const auto cpu = [] {
        FILETIME created{}, exited{}, kernel{}, user{};
        GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user);
        return ((static_cast<uint64_t>(kernel.dwHighDateTime) << 32) | kernel.dwLowDateTime) +
               ((static_cast<uint64_t>(user.dwHighDateTime) << 32) | user.dwLowDateTime);
    };
    const auto measure = [&](const char* label, int iterations, const auto& work) {
        const auto started = std::chrono::steady_clock::now();
        const auto cpu_started = cpu();
        for (int i = 0; i < iterations; ++i) if (!work()) return false;
        const double cpu_ms = static_cast<double>(cpu() - cpu_started) / 10000.0 / iterations;
        const double wall_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count() / iterations;
        std::cout << label << ": wall_ms=" << wall_ms << " cpu_ms=" << cpu_ms << '\n';
        return true;
    };
    std::cout << std::fixed << std::setprecision(6);
    const auto folder = std::filesystem::temp_directory_path() / (L"launchme-discovery-benchmark-" + std::to_wstring(GetCurrentProcessId()));
    for (const int count : {100, 1000}) {
        std::vector<Game> candidates(count);
        for (int i = 0; i < count; ++i) {
            candidates[i].app_id = 0xFFF00000u + i;
            candidates[i].install_directory = folder / std::to_wstring(i);
        }
        std::cout << count << " installed Steam games, one shared scan: ";
        if (!measure("discovery", 100, [&] { return findRunningGames(candidates).empty(); })) return 71;
    }
    wchar_t executable[32768]; GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
    Game game; game.app_id = 0xFFFFFFF0u; game.executable = executable;
    if (!registry.report(game.app_id, 0, 0)) return 72;
    ProcessWatch watch;
    if (watch.read(game) != GameActivity::running || !watch.holding()) return 73;
    return measure("held Steam process", 100000, [&] { return watch.read(game) == GameActivity::running; }) ? 0 : 74;
}

// A suspended copy renamed cemu.exe stands in for the emulator. Nothing is resumed, so the
// test binary never runs as that process; it is killed here.
int emulatorPlaytime() {
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
    const auto folder = std::filesystem::temp_directory_path() / (L"launchme-emu-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    const auto cemu = folder / L"cemu.exe";
    const auto rom = folder / L"Zelda.wua";
    struct Cleanup {
        std::filesystem::path folder;
        PROCESS_INFORMATION named{}, fresh{};
        ~Cleanup() {
            const auto end = [](PROCESS_INFORMATION& process) {
                if (!process.hProcess) return;
                TerminateProcess(process.hProcess, 0);
                WaitForSingleObject(process.hProcess, 5000);
                CloseHandle(process.hProcess); CloseHandle(process.hThread);
            };
            end(named); end(fresh);
            std::error_code ignored;
            std::filesystem::remove_all(folder, ignored);
        }
    } cleanup{folder};
    if (!std::filesystem::copy_file(executable, cemu, std::filesystem::copy_options::overwrite_existing, ec)) return 80;
    Game game;
    game.kind = GameKind::nonsteam;
    game.platform = L"Wii U";
    game.rom = rom;
    game.name = L"Zelda";
    ProcessWatch waiting;
    if (waiting.read(game) != GameActivity::stopped || waiting.holding()) return 81;
    auto named = L"\"" + cemu.wstring() + L"\" -g \"" + rom.wstring() + L"\"";
    STARTUPINFOW startup{sizeof(startup)};
    if (!CreateProcessW(cemu.c_str(), named.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &startup, &cleanup.named)) return 82;
    const auto found = findEmulatorRomProcesses(game);
    if (found.size() != 1 || found[0].pid != cleanup.named.dwProcessId) return 83;
    ProcessWatch by_rom;
    if (by_rom.read(game) != GameActivity::running || !by_rom.holding() || by_rom.read(game) != GameActivity::running) return 84;
    TerminateProcess(cleanup.named.hProcess, 0);
    WaitForSingleObject(cleanup.named.hProcess, 5000);
    CloseHandle(cleanup.named.hProcess); CloseHandle(cleanup.named.hThread);
    cleanup.named = {};
    if (by_rom.read(game) != GameActivity::stopped || by_rom.holding()) return 85;
    ProcessWatch spawned;
    if (spawned.read(game) != GameActivity::stopped) return 86;
    auto bare = L"\"" + cemu.wstring() + L"\"";
    if (!CreateProcessW(cemu.c_str(), bare.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &startup, &cleanup.fresh)) return 87;
    if (spawned.read(game) != GameActivity::running || !spawned.holding()) return 88;
    return 0;
}
#else
// Redirect ~/.steam/registry.vdf (HOME) in this test process only; never read/write the user's Steam state.
class SteamRegistry {
public:
    SteamRegistry() {
        if (const char* home = std::getenv("HOME")) previous_ = home;
        home_ = std::filesystem::temp_directory_path() / ("launchme-test-steam-" + std::to_string(getpid()) + '-' + std::to_string(GetTickCount64()));
        std::error_code ec;
        std::filesystem::create_directories(home_ / ".steam", ec);
        ready_ = !ec && ::setenv("HOME", home_.c_str(), 1) == 0 && write();
    }
    ~SteamRegistry() {
        if (previous_) ::setenv("HOME", previous_->c_str(), 1); else ::unsetenv("HOME");
        std::error_code ignored; std::filesystem::remove_all(home_, ignored);
    }
    bool ready() const { return ready_; }
    bool report(DWORD app, DWORD running, DWORD foreground) {
        if (!ready_) return false;
        apps_[app] = running; foreground_ = foreground;
        return write();
    }
private:
    bool write() {
        std::string apps;
        for (const auto& [app, running] : apps_)
            apps += "\t\t\t\t\t\t\"" + std::to_string(app) + "\"\n\t\t\t\t\t\t{\n\t\t\t\t\t\t\t\"Running\"\t\t\"" + std::to_string(running) + "\"\n\t\t\t\t\t\t}\n";
        return atomic_file::write(home_ / ".steam" / "registry.vdf", "\"Registry\"\n{\n\t\"HKCU\"\n\t{\n\t\t\"Software\"\n\t\t{\n\t\t\t\"Valve\"\n\t\t\t{\n\t\t\t\t\"Steam\"\n\t\t\t\t{\n"
            "\t\t\t\t\t\"RunningAppID\"\t\t\"" + std::to_string(foreground_) + "\"\n\t\t\t\t\t\"Apps\"\n\t\t\t\t\t{\n" + apps + "\t\t\t\t\t}\n\t\t\t\t}\n\t\t\t}\n\t\t}\n\t}\n}\n");
    }
    std::filesystem::path home_;
    std::optional<std::string> previous_;
    std::map<DWORD, DWORD> apps_;
    DWORD foreground_ = 0;
    bool ready_ = false;
};

// Child mode: a busy counter in a shared file mapping proves whether it runs. SIGUSR1 makes it
// not dumpable (its /proc/<pid>/exe then denied, as a protected game's image on Windows).
int child(const wchar_t* mapping_name) {
    std::signal(SIGUSR1, [](int) { ::prctl(PR_SET_DUMPABLE, 0, 0, 0, 0); });
    const int fd = ::open(text_encoding::utf8(mapping_name).c_str(), O_RDWR | O_CLOEXEC);
    auto* counter = fd >= 0 ? static_cast<volatile long*>(::mmap(nullptr, sizeof(long), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)) : nullptr;
    if (!counter || counter == MAP_FAILED) return 1;
    for (const auto started = GetTickCount64(); GetTickCount64() - started < 20000;) { __atomic_add_fetch(counter, 1, __ATOMIC_SEQ_CST); Sleep(2); }
    return 0;
}
// Idle mode: stands in for an emulator, never touches anything, ends itself after 20 s.
int idle() { Sleep(20000); return 0; }

struct Child {
    pid_t pid = 0;
    ~Child() { end(); }
    void end() { if (pid > 0) { ::kill(pid, SIGKILL); ::waitpid(pid, nullptr, 0); pid = 0; } }
};
bool start(Child& child, const std::vector<std::string>& arguments) {
    process_spawn::Options options;
    return process_spawn::spawn(arguments, options, child.pid) == 0;
}
struct Counter {
    std::filesystem::path file;
    int fd = -1;
    volatile long* value = nullptr;
    Counter() {
        file = std::filesystem::temp_directory_path() / ("launchme-freeze-test-" + std::to_string(getpid()));
        fd = ::open(file.c_str(), O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        if (fd < 0 || ::ftruncate(fd, sizeof(long)) != 0) return;
        void* view = ::mmap(nullptr, sizeof(long), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (view != MAP_FAILED) value = static_cast<volatile long*>(view);
    }
    ~Counter() {
        if (value) ::munmap(const_cast<long*>(value), sizeof(long));
        if (fd >= 0) ::close(fd);
        std::error_code ignored; std::filesystem::remove(file, ignored);
    }
    long read() const { return __atomic_load_n(value, __ATOMIC_SEQ_CST); }
};

// Games started outside the launcher: a copy of this test in a folder of its own is the "game".
int runningDiscovery(const std::filesystem::path& executable, const Counter& counter) {
    SteamRegistry registry;
    if (!registry.ready()) return 58;
    const auto folder = std::filesystem::temp_directory_path() / ("launchme-running-" + std::to_string(getpid()));
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    const auto copy = folder / "game";
    struct Cleanup { std::filesystem::path folder; Child process;
        ~Cleanup() { process.end(); std::error_code ignored; std::filesystem::remove_all(folder, ignored); } } cleanup{folder};
    if (!std::filesystem::copy_file(executable, copy, std::filesystem::copy_options::overwrite_existing, ec)) return 40;
    Game by_folder; by_folder.source = GameSource::gog; by_folder.kind = GameKind::store; by_folder.install_directory = folder;
    Game by_exe; by_exe.source = GameSource::custom; by_exe.kind = GameKind::store; by_exe.executable = copy;
    Game custom_folder = by_exe; custom_folder.executable = folder / "other"; custom_folder.install_directory = folder;
    Game shortcut = by_exe; shortcut.source = GameSource::steam; shortcut.kind = GameKind::nonsteam;
    Game not_installed = by_folder; not_installed.installed = false;
    Game system_folder = by_folder; system_folder.install_directory = "/usr/bin"; // Shells, coreutils... run there.
    Game steam; steam.app_id = 0xFFFFFFF0u; steam.name = L"Not running";
    const std::vector<Game> candidates{by_folder, custom_folder, shortcut, not_installed, system_folder, steam};
    if (!findRunningGames(candidates).empty()) return 41; // The game is not started yet.
    if (!start(cleanup.process, {copy.string(), "--child", counter.file.string()})) return 42;
    std::vector<size_t> running;
    for (int i = 0; i < 400 && running.empty(); ++i) { Sleep(25); running = findRunningGames(candidates); }
    if (running != std::vector<size_t>{0}) return 43;
    if (findRunningGames({by_folder, by_exe}) != std::vector<size_t>{1}) return 44;
    if (!findGameProcesses(custom_folder).empty() || readGameActivity(custom_folder) != GameActivity::stopped) return 46;
    ProcessWatch unrelated;
    if (unrelated.read(custom_folder) != GameActivity::stopped || unrelated.holding()) return 47;
    const auto exact = findGameProcesses(by_exe);
    if (exact.size() != 1 || exact[0].pid != static_cast<uint32_t>(cleanup.process.pid)) return 48;
    if (!findGameProcesses(system_folder).empty()) return 49;
    auto script = custom_folder; script.executable.clear();
    if (!findGameProcesses(script).empty() || readGameActivity(script) != GameActivity::unknown) return 50;

    Game direct = steam; direct.install_directory = folder;
    if (findRunningGames({direct}) != std::vector<size_t>{0}) return 59;
    if (!registry.report(direct.app_id, 0, 0)) return 60;
    ProcessWatch watch;
    if (findRunningGames({direct}) != std::vector<size_t>{0} || readGameActivity(direct) != GameActivity::running ||
        watch.read(direct) != GameActivity::running || !watch.holding() || watch.read(direct) != GameActivity::running) return 61;
    if (findRunningGames({direct, by_exe}) != std::vector<size_t>{1}) return 62;
    Game reported = steam; reported.app_id -= 1;
    if (!registry.report(reported.app_id, 1, reported.app_id) ||
        findRunningGames({reported, direct}) != std::vector<size_t>({0, 1})) return 63;
    ProcessWatch steam_watch;
    if (readGameActivity(reported) != GameActivity::running || steam_watch.read(reported) != GameActivity::running ||
        steam_watch.holding()) return 64;
    if (!registry.report(direct.app_id, 1, direct.app_id) || findRunningGames({direct, custom_folder}) != std::vector<size_t>{0} ||
        watch.read(direct) != GameActivity::running || !watch.holding()) return 65;
    if (!registry.report(direct.app_id, 0, 0) || watch.read(direct) != GameActivity::running || !watch.holding()) return 66;
    // A process that denies inspection (not dumpable, like a protected game) stays alive and
    // makes its executable's activity unknown, never stopped.
    ::kill(cleanup.process.pid, SIGUSR1);
    std::error_code denied;
    for (int i = 0; i < 200; ++i) {
        (void)std::filesystem::read_symlink("/proc/" + std::to_string(cleanup.process.pid) + "/exe", denied);
        if (denied) break;
        Sleep(10);
    }
    if (!denied) return 76;
    if (!process_freeze::alive(exact) || readGameActivity(by_exe) != GameActivity::unknown || watch.read(direct) != GameActivity::running) return 77;
    cleanup.process.end();
    if (process_freeze::alive(exact)) return 78;
    if (!findRunningGames(candidates).empty()) return 45;
    if (!findRunningGames({direct}).empty() || readGameActivity(direct) != GameActivity::stopped ||
        watch.read(direct) != GameActivity::stopped || watch.holding()) return 67;
    if (readGameActivity(steam) != GameActivity::stopped || ProcessWatch{}.read(steam) != GameActivity::stopped) return 68;
    return 0;
}

// Suspends and resumes only a child copy of this test executable (SIGSTOP/SIGCONT).
int realSuspension() {
    const auto executable = std::filesystem::read_symlink("/proc/self/exe");
    Counter counter;
    if (!counter.value) return 20;
    Child process;
    if (!start(process, {executable.string(), "--child", counter.file.string()})) return 21;
    for (int i = 0; i < 500 && counter.read() < 5; ++i) Sleep(10);
    if (counter.read() < 5) return 22;
    Game fixture; fixture.source = GameSource::gog; fixture.executable = executable;
    auto processes = findGameProcesses(fixture);
    if (processes.size() != 1 || processes[0].pid != static_cast<uint32_t>(process.pid) || !processes[0].created) return 23;

    if (process_freeze::suspend(processes) != process_freeze::Result::ok || processes.size() != 1) return 24;
    Sleep(120);
    const auto frozen = counter.read();
    Sleep(250);
    if (counter.read() != frozen || !process_freeze::alive(processes)) return 25;
    if (readGameActivity(fixture) != GameActivity::running) return 26; // A suspended game still runs.
    if (process_freeze::resume(processes) != process_freeze::Result::ok) return 27;
    for (int i = 0; i < 100 && counter.read() == frozen; ++i) Sleep(10);
    if (counter.read() == frozen) return 28;
    auto reused = processes; reused[0].created += 1;
    if (process_freeze::suspend(reused) != process_freeze::Result::gone || process_freeze::alive(reused)) return 29;
    if (process_freeze::suspend(processes) != process_freeze::Result::ok ||
        process_freeze::terminate(processes) != process_freeze::Result::ok) return 31;
    int status = 0;
    if (::waitpid(process.pid, &status, 0) != process.pid || !WIFSIGNALED(status)) return 31;
    process.pid = 0;
    if (process_freeze::alive(processes) || process_freeze::resume(processes) != process_freeze::Result::gone ||
        process_freeze::terminate(processes) != process_freeze::Result::gone) return 30;
    return runningDiscovery(executable, counter);
}

// A copy named cemu stands in for the emulator; it only sleeps (--idle) and is killed here.
int emulatorPlaytime() {
    const auto executable = std::filesystem::read_symlink("/proc/self/exe");
    const auto folder = std::filesystem::temp_directory_path() / ("launchme-emu-" + std::to_string(getpid()));
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    const auto cemu = folder / "cemu";
    const auto rom = folder / "Zelda.wua";
    struct Cleanup { std::filesystem::path folder; Child named, fresh;
        ~Cleanup() { named.end(); fresh.end(); std::error_code ignored; std::filesystem::remove_all(folder, ignored); } } cleanup{folder};
    if (!std::filesystem::copy_file(executable, cemu, std::filesystem::copy_options::overwrite_existing, ec)) return 80;
    Game game;
    game.kind = GameKind::nonsteam;
    game.platform = L"Wii U";
    game.rom = rom;
    game.name = L"Zelda";
    ProcessWatch waiting;
    if (waiting.read(game) != GameActivity::stopped || waiting.holding()) return 81;
    if (!start(cleanup.named, {cemu.string(), "--idle", "-g", rom.string()})) return 82;
    std::vector<GameProcess> found;
    for (int i = 0; i < 200 && found.empty(); ++i) { found = findEmulatorRomProcesses(game); if (found.empty()) Sleep(10); }
    if (found.size() != 1 || found[0].pid != static_cast<uint32_t>(cleanup.named.pid)) return 83;
    ProcessWatch by_rom;
    if (by_rom.read(game) != GameActivity::running || !by_rom.holding() || by_rom.read(game) != GameActivity::running) return 84;
    cleanup.named.end();
    if (by_rom.read(game) != GameActivity::stopped || by_rom.holding()) return 85;
    ProcessWatch spawned;
    if (spawned.read(game) != GameActivity::stopped) return 86;
    if (!start(cleanup.fresh, {cemu.string(), "--idle"})) return 87;
    for (int i = 0; i < 200 && proc_fs::image(static_cast<uint32_t>(cleanup.fresh.pid)) != cemu; ++i) Sleep(10);
    if (spawned.read(game) != GameActivity::running || !spawned.holding()) return 88;
    return 0;
}
#endif
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    if (argc == 3 && std::wstring(argv[1]) == L"--child") return child(argv[2]);
    if (argc == 2 && std::wstring(argv[1]) == L"--discovery-benchmark") return discoveryBenchmark();
#else
int main(int argc, char** arguments) {
    std::vector<std::wstring> wide;
    for (int i = 0; i < argc; ++i) wide.push_back(text_encoding::wideLenient(arguments[i]));
    std::vector<wchar_t*> pointers;
    for (auto& argument : wide) pointers.push_back(argument.data());
    wchar_t** argv = pointers.data();
    if (argc == 3 && std::wstring(argv[1]) == L"--child") return child(argv[2]);
    if (argc >= 2 && std::wstring(argv[1]) == L"--idle") return idle();
#endif
    GameSession pending(100);
    {
        GameSession clock;
        clock.observe(GameActivity::stopped, 1000);
        clock.observe(GameActivity::running, 5000);
        clock.observe(GameActivity::running, 5500);
        clock.observe(GameActivity::running, 6000);
        if (clock.takePlaytime() != 1000 || clock.takePlaytime()) return 80;
        clock.freeze({{1, 1}}); clock.observe(GameActivity::running, 10000); clock.thaw();
        clock.observe(GameActivity::running, 12000); clock.observe(GameActivity::running, 12500);
        if (clock.takePlaytime() != 500) return 81;
        clock.observe(GameActivity::unknown, 13000); clock.observe(GameActivity::running, 15000);
        clock.observe(GameActivity::running, 15500);
        clock.observe(GameActivity::running, 900000); // Sleeping computer, not gameplay.
        clock.observe(GameActivity::running, 900500);
        if (clock.takePlaytime() != 1000) return 82;
        clock.observe(GameActivity::stopped, 901000); clock.observe(GameActivity::stopped, 903000);
        if (clock.takePlaytime()) return 83;
    }
    if (pending.observe(GameActivity::stopped, 200) != SessionResult::waiting ||
        pending.observe(GameActivity::unknown, 120100) != SessionResult::waiting ||
        pending.observe(GameActivity::stopped, 120101) != SessionResult::start_failed) return 1;
    GameSession active(100);
    if (active.observe(GameActivity::running, 500) != SessionResult::waiting ||
        active.observe(GameActivity::stopped, 1000) != SessionResult::waiting ||
        active.observe(GameActivity::running, 1200) != SessionResult::waiting ||
        active.observe(GameActivity::stopped, 1500) != SessionResult::waiting ||
        active.observe(GameActivity::unknown, 2000) != SessionResult::waiting ||
        active.observe(GameActivity::stopped, 2500) != SessionResult::waiting ||
        active.observe(GameActivity::stopped, 4001) != SessionResult::ended) return 2;
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
    Game fixture; fixture.source = GameSource::gog; fixture.executable = executable;
    // Observe this test process only. No game, client or child process is started.
    if (readGameActivity(fixture) != GameActivity::running) return 3;
    {
        // The watch keeps the proving process open and answers without a new snapshot.
        ProcessWatch watch;
        if (watch.read(fixture) != GameActivity::running || !watch.holding() || watch.read(fixture) != GameActivity::running) return 6;
        ProcessWatch moved = std::move(watch);
        if (watch.holding() || !moved.holding()) return 7;
    }
    fixture.executable = fixture.executable.parent_path() / L"launchme-nonexistent-fixture.exe";
    if (readGameActivity(fixture) != GameActivity::stopped) return 4;
    if (ProcessWatch missing; missing.read(fixture) != GameActivity::stopped || missing.holding()) return 8;
    fixture.executable.clear();
    if (readGameActivity(fixture) != GameActivity::unknown || !findGameProcesses(fixture).empty()) return 5;

    // Quick resume session state: "W grze" only after confirmation, then suspended.
    std::map<std::string, GameSession> sessions;
    sessions.emplace("steam:10", GameSession(0));
    if (sessionState(sessions, "steam:10") != SessionState::none || sessionState(sessions, "steam:11") != SessionState::none) return 10;
    sessions.at("steam:10").observe(GameActivity::running, 10);
    if (sessionState(sessions, "steam:10") != SessionState::running) return 11;
    sessions.at("steam:10").freeze({{42, 99}});
    if (sessionState(sessions, "steam:10") != SessionState::frozen) return 12;
    sessions.at("steam:10").thaw();
    if (sessionState(sessions, "steam:10") != SessionState::running) return 13;

    const std::vector<FrozenGame> frozen{{"steam:10", {{42, 133000000000000000ULL}, {43, 7}}}, {"gog:1 two\nlines", {{9, 1}}}};
    if (parseFrozenGames(formatFrozenGames(frozen)) != frozen) return 14;
    if (!parseFrozenGames("zz 1:1\n6161 0:5\n6161 1:0\n6161 1:x\n6161\n").empty()) return 15;
    const auto file = std::filesystem::temp_directory_path() / (L"launchme-quick-resume-" + std::to_wstring(GetCurrentProcessId()) + L".txt");
    if (!process_freeze::save(file, frozen) || process_freeze::load(file) != frozen) return 16;
    // The journal's writer must not acknowledge records beyond its reader's
    // bounds, and a damaged/locked file is distinct from an empty/missing one.
    auto oversized = frozen;
    oversized[0].processes.resize(65, {42, 7});
    if (process_freeze::save(file, oversized) || process_freeze::load(file) != frozen) return 51;
    oversized.assign(65, frozen[0]);
    if (process_freeze::save(file, oversized) || process_freeze::load(file) != frozen) return 52;
    oversized = frozen; oversized[0].key.assign(40000, 'x');
    if (process_freeze::save(file, oversized) || process_freeze::load(file) != frozen) return 53;
#ifdef _WIN32
    // Windows only: a file opened without sharing (no such lock on Linux).
    const HANDLE locked = CreateFileW(file.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (locked == INVALID_HANDLE_VALUE) return 54;
    const bool read_failed = !process_freeze::load(file);
    const bool write_failed = !process_freeze::save(file, frozen);
    CloseHandle(locked);
    if (!read_failed || !write_failed || process_freeze::load(file) != frozen) return 55;
#endif
    if (!atomic_file::write(file, formatFrozenGames(frozen) + "invalid record\n") || process_freeze::load(file)) return 56;
    if (!process_freeze::save(file, {}) || std::filesystem::exists(file)) return 17;
    const auto missing = process_freeze::load(file);
    if (!missing || !missing->empty()) return 57;

    if (const int code = emulatorPlaytime()) { std::cerr << "Emulator playtime check failed: " << code << '\n'; return code; }
    if (const int code = realSuspension()) { std::cerr << "Real process check failed: " << code << '\n'; return code; }
    std::cout << "Game start, exit debounce, unavailable telemetry, process identity and real suspend/resume passed\n";
    return 0;
}
