#pragma once

#include <optional>
#include <string>
#ifdef _WIN32
#include <Windows.h>
#else
#include "launch.hpp" // HWND, HANDLE (a process ID) on Linux.
#endif
#include "../model/game.hpp"
#include "../model/game_session.hpp"

#include <vector>

// Read-only; intended for a worker. Unknown never means that a game has exited.
GameActivity readGameActivity(const Game& game);
// Same answer as readGameActivity, but remembers the processes that proved the
// game running and afterwards only checks whether they still exist. A new
// snapshot is taken only once all of them have exited (e.g. launcher handoff).
// Steam's Running=0 never overrides a live process started outside the client.
class ProcessWatch {
public:
    ProcessWatch() = default;
    ~ProcessWatch();
    ProcessWatch(ProcessWatch&& other) noexcept;
    ProcessWatch& operator=(ProcessWatch&& other) noexcept;
    ProcessWatch(const ProcessWatch&) = delete;
    ProcessWatch& operator=(const ProcessWatch&) = delete;
    GameActivity read(const Game& game);
    bool holding() const;
private:
    void release();
    // A non-Steam shortcut often starts through PowerShell/EmuDeck, then the
    // emulator. Latch that process (ROM on its command line or window, else one
    // that appeared after this watch began) so playtime is not lost when the script exits.
    bool claimEmulator(const Game& game);
#ifdef _WIN32
    std::vector<void*> handles_;
#else
    std::vector<GameProcess> handles_; // Checked by PID and start time.
#endif
    std::vector<uint32_t> baseline_;
    bool primed_ = false;
    bool confirmed_ = false;
};
// Emulator processes whose command line or a visible window title names this shortcut's ROM.
// Launcher scripts are ignored. Empty when the game has no ROM.
std::vector<GameProcess> findEmulatorRomProcesses(const Game& game);
// Processes of this user session whose image is the game's executable or lies in
// its own install directory. A custom game's working folder and shared system/user
// folders never establish ownership; custom games require the exact executable.
// Store clients, crash handlers and the launcher itself are never included.
std::vector<GameProcess> findGameProcesses(const Game& game);
// Two library entries for one installation (a user-added game pointing to a store
// game's .exe, a GOG and a Steam copy in one folder): the executable of one is the
// executable of the other or lies in its own folder, or both own the same folder.
// One process then proves only the game that was started, never both.
bool sharesProcesses(const Game& a, const Game& b);

struct GameWindow {
    HWND window = nullptr;
    GameProcess process{};
    std::string key;
    bool operator==(const GameWindow&) const = default;
};
// Worker-only: match the foreground process against the same ownership rules as
// game discovery. Never treats a store client or the launcher as a game.
// Linux: always empty (windows belong to the display server, which is the UI's).
std::optional<GameWindow> findWindowGame(HWND window, const std::vector<Game>& candidates);
// A program or script the launcher started (ShellExecuteEx's process): a job object that holds
// it and, by inheritance, every process it starts. Null when Windows refuses the assignment.
// Linux: the program was started as a session leader; the job is its session (the same ID),
// which every process it starts belongs to unless it starts a session of its own.
HANDLE launchJob(HANDLE process);
// Running while one of the job's processes is not a store client, crash handler or shell host
// (a script's cmd.exe counts: the script is still working); stopped once none is left.
GameActivity readLaunchJob(HANDLE job);

// Indices of the `candidates` running now, however they were started (Steam, a store
// client, a shortcut, before the launcher). Steam's RunningAppID, confirmed by
// Apps\<id>\Running, is a fast positive signal. All installed games, including Steam,
// also share one process snapshot: their executable or a process inside their own folder,
// restricted to this user session;
// for user-added games only their .exe counts, and a shared folder (Windows, Program Files,
// the user profile...) never does. Non-Steam shortcuts are skipped: many share one emulator.
// A session started here still watches that emulator by its ROM (findEmulatorRomProcesses).
// One process belongs to the most specific game (its executable, else the deepest folder).
// Linux: Steam's values come from ~/.steam/registry.vdf, "this user session" is this user's
// processes in /proc, paths compare with case, and a Wine process counts as the Windows
// program on its command line (Z:\ is the Linux root).
std::vector<size_t> findRunningGames(const std::vector<Game>& candidates);
