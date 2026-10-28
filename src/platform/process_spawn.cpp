#include "process_spawn.hpp"

#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char** environ;

namespace process_spawn {
std::vector<std::string> splitArguments(std::string_view line) {
    std::vector<std::string> result;
    std::string argument;
    bool quoted = false, started = false;
    for (size_t i = 0; i < line.size();) {
        const char ch = line[i];
        if (ch == '\\') {
            size_t slashes = 0;
            while (i < line.size() && line[i] == '\\') { ++slashes; ++i; }
            if (i < line.size() && line[i] == '"') {
                argument.append(slashes / 2, '\\');
                if (slashes % 2) { argument += '"'; ++i; }
            } else argument.append(slashes, '\\');
            started = true;
            continue;
        }
        if (ch == '"') {
            if (quoted && i + 1 < line.size() && line[i + 1] == '"') { argument += '"'; i += 2; continue; }
            quoted = !quoted; started = true; ++i;
            continue;
        }
        if (!quoted && (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r')) {
            if (started) { result.push_back(std::move(argument)); argument.clear(); started = false; }
            ++i;
            continue;
        }
        argument += ch; started = true; ++i;
    }
    if (started) result.push_back(std::move(argument));
    return result;
}

int spawn(const std::vector<std::string>& arguments, const Options& options, pid_t& pid) {
    if (arguments.empty() || arguments[0].empty()) return ENOENT;
    std::vector<char*> argv;
    for (const auto& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    std::vector<char*> envp;
    if (options.environment) {
        for (const auto& entry : *options.environment) envp.push_back(const_cast<char*>(entry.c_str()));
        envp.push_back(nullptr);
    }
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    if (posix_spawn_file_actions_init(&actions) != 0) return ENOMEM;
    if (posix_spawnattr_init(&attributes) != 0) { posix_spawn_file_actions_destroy(&actions); return ENOMEM; }
    int error = posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    if (!error && options.output >= 0) error = posix_spawn_file_actions_adddup2(&actions, options.output, 1);
    if (!error && options.error >= 0) error = posix_spawn_file_actions_adddup2(&actions, options.error, 2);
#if defined(__GLIBC__) && (__GLIBC__ > 2 || __GLIBC_MINOR__ >= 34)
    // Sockets, locks and pipes of the launcher are not the game's.
    if (!error) error = posix_spawn_file_actions_addclosefrom_np(&actions, 3);
#endif
    if (!error && !options.directory.empty()) error = posix_spawn_file_actions_addchdir_np(&actions, options.directory.c_str());
    // Ignored signals (SIGPIPE) and the launcher threads' blocked ones are not inherited.
    sigset_t none, all;
    sigemptyset(&none); sigfillset(&all);
    short flags = POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
    if (options.session) flags |= POSIX_SPAWN_SETSID;
    if (!error) error = posix_spawnattr_setsigmask(&attributes, &none);
    if (!error) error = posix_spawnattr_setsigdefault(&attributes, &all);
    if (!error) error = posix_spawnattr_setflags(&attributes, flags);
    if (!error) {
        char** environment = options.environment ? envp.data() : environ;
        error = options.search ? posix_spawnp(&pid, argv[0], &actions, &attributes, argv.data(), environment)
                               : posix_spawn(&pid, argv[0], &actions, &attributes, argv.data(), environment);
    }
    posix_spawnattr_destroy(&attributes);
    posix_spawn_file_actions_destroy(&actions);
    return error;
}

void reapLater(pid_t pid) {
    if (pid <= 0) return;
    try {
        std::thread([pid] {
            int status = 0;
            while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
        }).detach();
    } catch (...) {} // No thread: the child stays a zombie until the launcher exits.
}

std::optional<int> waitExit(pid_t pid, std::chrono::milliseconds timeout) {
    const auto until = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        int status = 0;
        const pid_t done = ::waitpid(pid, &status, WNOHANG);
        if (done == pid) return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
        if (done < 0 && errno != EINTR) return -1; // Not our child (any more).
        if (std::chrono::steady_clock::now() >= until) return std::nullopt;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void killGroup(pid_t pid) {
    if (pid <= 0) return;
    ::kill(-pid, SIGKILL);
    ::kill(pid, SIGKILL);
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
}
}
