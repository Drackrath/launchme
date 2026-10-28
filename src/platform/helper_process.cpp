#include "helper_process.hpp"
#ifdef _WIN32
#include "command_line.hpp"

#include <Windows.h>

#include <algorithm>
#include <cwctype>
#include <map>

namespace helper_process {
namespace {
struct Handle {
    HANDLE h = nullptr;
    explicit Handle(HANDLE value = nullptr) : h(value) {}
    ~Handle() { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    explicit operator bool() const { return h && h != INVALID_HANDLE_VALUE; }
};
std::wstring upper(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towupper(c)); });
    return text;
}
}

Result run(const std::filesystem::path& executable, const std::filesystem::path& working_directory,
           const std::vector<std::wstring>& arguments, const Environment& environment,
           const std::function<bool()>& cancel, const std::function<void(std::string_view)>& error_line) {
    Result result;
    if (cancel && cancel()) return result;
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    Handle out_read, out_write, err_read, err_write;
    if (!CreatePipe(&out_read.h, &out_write.h, &sa, 0) || !CreatePipe(&err_read.h, &err_write.h, &sa, 0)) return result;
    SetHandleInformation(out_read.h, HANDLE_FLAG_INHERIT, 0); SetHandleInformation(err_read.h, HANDLE_FLAG_INHERIT, 0);
    Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr));
    if (!input) return result;
    // The environment block: Launchme's, with the helper's own variables on top.
    std::map<std::wstring, std::pair<std::wstring, std::wstring>> variables; // UPPER name → (name, value)
    if (auto* original = GetEnvironmentStringsW()) {
        for (const wchar_t* p = original; *p; p += wcslen(p) + 1) {
            const std::wstring entry(p); const auto pos = entry.find(L'=', 1);
            if (pos == std::wstring::npos) continue;
            const auto name = entry.substr(0, pos);
            variables[upper(name)] = {name, entry.substr(pos + 1)};
        }
        FreeEnvironmentStringsW(original);
    } else return result;
    for (const auto& [name, value] : environment)
        if (!name.empty() && name.find(L'=') == std::wstring::npos) variables[upper(name)] = {name, value};
    std::vector<wchar_t> block;
    for (const auto& [key, entry] : variables) {
        (void)key;
        const auto line = entry.first + L'=' + entry.second;
        block.insert(block.end(), line.begin(), line.end()); block.push_back(0);
    }
    block.push_back(0);
    std::wstring command = command_line::quoteArgument(executable.native());
    for (const auto& argument : arguments) command += L' ' + command_line::quoteArgument(argument);
    STARTUPINFOEXW start{}; start.StartupInfo.cb = sizeof(start); start.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    start.StartupInfo.hStdInput = input.h; start.StartupInfo.hStdOutput = out_write.h; start.StartupInfo.hStdError = err_write.h;
    SIZE_T size = 0; InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    std::vector<uint8_t> attributes(size);
    start.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    if (!InitializeProcThreadAttributeList(start.lpAttributeList, 1, 0, &size)) return result;
    HANDLE handles[]{input.h, out_write.h, err_write.h};
    const bool attributes_ok = UpdateProcThreadAttribute(start.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, sizeof(handles), nullptr, nullptr) != FALSE;
    Handle job(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    PROCESS_INFORMATION pi{};
    const auto directory = working_directory.native();
    const bool created = attributes_ok && job && SetInformationJobObject(job.h, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) &&
        CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
                       CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT,
                       block.data(), directory.empty() ? nullptr : directory.c_str(), &start.StartupInfo, &pi);
    DeleteProcThreadAttributeList(start.lpAttributeList);
    if (!created) return result;
    Handle process(pi.hProcess), thread(pi.hThread);
    if (!AssignProcessToJobObject(job.h, process.h)) { TerminateProcess(process.h, 1); WaitForSingleObject(process.h, INFINITE); return result; }
    result.started = true;
    CloseHandle(out_write.h); out_write.h = nullptr; CloseHandle(err_write.h); err_write.h = nullptr;
    ResumeThread(thread.h);
    std::string pending; bool overflow = false;
    const auto drain = [&](HANDLE pipe, bool output) {
        for (;;) {
            DWORD available = 0, received = 0;
            if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) || !available) break;
            char buffer[16384];
            if (!ReadFile(pipe, buffer, std::min<DWORD>(available, sizeof(buffer)), &received, nullptr) || !received) break;
            if (output) {
                if (result.output.size() + received > 8 * 1024 * 1024) overflow = true;
                else result.output.append(buffer, received);
            } else if (error_line) {
                pending.append(buffer, received);
                for (size_t end; (end = pending.find_first_of("\r\n")) != std::string::npos;) {
                    if (end) error_line(std::string_view(pending).substr(0, end));
                    pending.erase(0, end + 1);
                }
                if (pending.size() > 65536) pending.clear();
            }
        }
    };
    const auto cancelled = [&] { return cancel && cancel(); };
    for (;;) {
        drain(out_read.h, true); drain(err_read.h, false);
        if (cancelled() || overflow) { TerminateJobObject(job.h, 1); WaitForSingleObject(process.h, 5000); result.output.clear(); return result; }
        if (WaitForSingleObject(process.h, 40) == WAIT_OBJECT_0) break;
    }
    drain(out_read.h, true); drain(err_read.h, false);
    if (error_line && !pending.empty()) error_line(pending);
    DWORD exit = 1; GetExitCodeProcess(process.h, &exit);
    result.exit_code = exit;
    result.ok = !overflow && exit == 0 && !cancelled();
    return result;
}
}
#else
// Linux: posix_spawn (process_spawn) in a session of its own, so cancelling ends the helper's whole
// process group; stdout and stderr through non-blocking pipes read with poll(2).
#include "process_spawn.hpp"
#include "text_encoding.hpp"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace helper_process {
namespace {
struct Descriptor {
    int fd = -1;
    Descriptor() = default;
    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
    ~Descriptor() { reset(); }
    void reset() { if (fd >= 0) ::close(fd); fd = -1; }
};
// Close-on-exec on both ends: no other child of Launchme may hold the helper's pipes open.
bool pipeOf(Descriptor& read_end, Descriptor& write_end) {
    int fds[2];
    if (::pipe2(fds, O_CLOEXEC) != 0) return false;
    read_end.fd = fds[0]; write_end.fd = fds[1];
    return ::fcntl(read_end.fd, F_SETFL, ::fcntl(read_end.fd, F_GETFL) | O_NONBLOCK) == 0;
}
std::vector<std::string> environmentOf(const Environment& overrides) {
    std::vector<std::pair<std::string, std::string>> extra;
    for (const auto& [name, value] : overrides) {
        if (name.empty() || name.find(L'=') != std::wstring::npos) continue;
        extra.emplace_back(text_encoding::utf8Lenient(name), text_encoding::utf8Lenient(value));
    }
    std::vector<std::string> block;
    for (char** entry = environ; entry && *entry; ++entry) {
        const std::string_view line(*entry);
        const auto at = line.find('=');
        if (at == 0 || at == std::string_view::npos) continue;
        const auto name = line.substr(0, at);
        if (std::none_of(extra.begin(), extra.end(), [&](const auto& e) { return e.first == name; })) block.emplace_back(line);
    }
    for (const auto& [name, value] : extra) {
        if (value.find('\0') != std::string::npos) continue;
        block.push_back(name + '=' + value);
    }
    return block;
}
}

Result run(const std::filesystem::path& executable, const std::filesystem::path& working_directory,
           const std::vector<std::wstring>& arguments, const Environment& environment,
           const std::function<bool()>& cancel, const std::function<void(std::string_view)>& error_line) {
    Result result;
    const auto cancelled = [&] { return cancel && cancel(); };
    if (cancelled() || !executable.is_absolute()) return result;
    Descriptor out_read, out_write, err_read, err_write;
    if (!pipeOf(out_read, out_write) || !pipeOf(err_read, err_write)) return result;
    std::vector<std::string> argv{executable.string()};
    for (const auto& argument : arguments) {
        auto text = text_encoding::utf8Lenient(argument);
        if (text.find('\0') != std::string::npos) return result;
        argv.push_back(std::move(text));
    }
    const auto block = environmentOf(environment);
    process_spawn::Options options;
    options.directory = working_directory;
    options.session = true; // Its own process group: cancellation reaches whatever it started.
    options.output = out_write.fd; options.error = err_write.fd;
    options.environment = &block;
    pid_t pid = 0;
    if (process_spawn::spawn(argv, options, pid) != 0) return result;
    result.started = true;
    out_write.reset(); err_write.reset();

    std::string pending; bool overflow = false;
    const auto line = [&](std::string_view chunk) {
        if (!error_line) return;
        pending.append(chunk);
        for (size_t end; (end = pending.find_first_of("\r\n")) != std::string::npos;) {
            if (end) error_line(std::string_view(pending).substr(0, end));
            pending.erase(0, end + 1);
        }
        if (pending.size() > 65536) pending.clear();
    };
    // Everything readable now; false once the pipe reached its end.
    const auto drain = [&](Descriptor& pipe, bool output) {
        while (pipe.fd >= 0) {
            char buffer[16384];
            const ssize_t received = ::read(pipe.fd, buffer, sizeof(buffer));
            if (received < 0 && errno == EINTR) continue;
            if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
            if (received <= 0) { pipe.reset(); return; }
            if (!output) line({buffer, static_cast<size_t>(received)});
            else if (result.output.size() + static_cast<size_t>(received) > 8 * 1024 * 1024) overflow = true;
            else result.output.append(buffer, static_cast<size_t>(received));
        }
    };
    for (;;) {
        pollfd fds[2]{{out_read.fd, POLLIN, 0}, {err_read.fd, POLLIN, 0}};
        if (out_read.fd >= 0 || err_read.fd >= 0) ::poll(fds, 2, 40);
        else ::usleep(40 * 1000);
        drain(out_read, true); drain(err_read, false);
        if (cancelled() || overflow) {
            process_spawn::killGroup(pid);
            result.output.clear();
            return result;
        }
        // Exited, but not reaped yet: its process group stays addressable for the cleanup below.
        siginfo_t info{};
        const int waited = ::waitid(P_PID, static_cast<id_t>(pid), &info, WEXITED | WNOHANG | WNOWAIT);
        if (waited == 0 && info.si_pid == pid) break;
        if (waited < 0 && errno != EINTR) break;
    }
    // The helper ended; what it wrote before is still in the pipes (a child it left running may
    // keep them open, so nothing waits for their end). Like the Windows job, what it left in its
    // process group ends with it.
    drain(out_read, true); drain(err_read, false);
    ::kill(-pid, SIGKILL);
    int status = 0;
    pid_t reaped;
    while ((reaped = ::waitpid(pid, &status, 0)) < 0 && errno == EINTR) {}
    if (error_line && !pending.empty()) error_line(pending);
    if (overflow) { result.output.clear(); return result; }
    result.exit_code = reaped != pid ? 1 : WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    result.ok = reaped == pid && result.exit_code == 0 && !cancelled();
    return result;
}
}
#endif
