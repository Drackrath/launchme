#pragma once
// Linux only: processes as /proc shows them (the Toolhelp/OpenProcess queries of the Windows build).
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace proc_fs {
struct Stat {
    uint32_t pid = 0, parent = 0, group = 0, session = 0;
    char state = 0;     // R, S, D, T (stopped), Z (zombie), X (dead)...
    uint64_t start = 0; // Clock ticks after boot: with the PID, the process's identity.
    bool ended() const { return state == 'Z' || state == 'X' || state == 'x'; }
};
// /proc/<pid>/stat; nullopt when the process is gone (or never existed).
std::optional<Stat> stat(uint32_t pid);
// Every process ID in /proc, threads excluded.
std::vector<uint32_t> processes();
// The user that owns the process (its /proc entry); nullopt when it is gone.
std::optional<uint32_t> owner(uint32_t pid);
// The process's program: /proc/<pid>/exe, or for a Wine process the Windows program on its
// command line (Z:\... becomes the Linux path; other drives stay Windows paths). nullopt when the
// process denies the query (another user's, or not dumpable).
std::optional<std::filesystem::path> image(uint32_t pid);
// The arguments (/proc/<pid>/cmdline); nullopt when unreadable. Empty for kernel threads.
std::optional<std::vector<std::string>> commandLine(uint32_t pid);
// The kernel's short name for the process (/proc/<pid>/comm, at most 15 bytes).
std::string name(uint32_t pid);
}
