#pragma once
// Linux only: starting programs with posix_spawn (CreateProcessW on Windows).
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <vector>

namespace process_spawn {
// A Windows command line's arguments, by CommandLineToArgvW's rules (the form
// command_line::quoteArgument writes): 2n backslashes before a quote are n and the quote
// opens or closes, 2n+1 are n and a literal quote, "" inside quotes is a quote.
std::vector<std::string> splitArguments(std::string_view line);

struct Options {
    std::filesystem::path directory; // Empty: the launcher's own.
    bool search = false;             // A bare program name is looked up in PATH.
    bool session = false;            // Its own session (setsid): not ended with the launcher's terminal.
    int output = -1, error = -1;     // Descriptors for stdout/stderr; -1 keeps the launcher's.
    const std::vector<std::string>* environment = nullptr; // "KEY=value" entries; null inherits.
};
// Starts `arguments` (argv[0] is the program). stdin is /dev/null, every other descriptor of the
// launcher is closed in the child, and signal handling starts from the defaults. 0 or an errno.
int spawn(const std::vector<std::string>& arguments, const Options& options, pid_t& pid);
// The child is waited for on a thread of its own once it exits, so it never lingers as a zombie.
void reapLater(pid_t pid);
// The exit status (WEXITSTATUS, or 128 + signal) of a child that ended within `timeout`; nullopt
// while it still runs (it is then still the caller's to wait for).
std::optional<int> waitExit(pid_t pid, std::chrono::milliseconds timeout);
// Kills the child's whole process group (spawned with `session`) and waits for the child.
void killGroup(pid_t pid);
}
