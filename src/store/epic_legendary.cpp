#include "epic_legendary.hpp"
#include "../platform/hashing.hpp"
#include "../platform/unicode_text.hpp"
#include "../net/http_download.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/command_line.hpp"
#include "../platform/text_encoding.hpp"
#ifdef _WIN32
#include <Windows.h>
#include <bcrypt.h>
#include <sddl.h>
#include <shellapi.h>
#else
#include "../platform/process_spawn.hpp"
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
extern char** environ;
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <map>

namespace epic {
namespace {
using text_encoding::wide;
using text_encoding::utf8;
#ifdef _WIN32
struct Handle {
    HANDLE h = nullptr;
    explicit Handle(HANDLE value = nullptr) : h(value) {}
    ~Handle() { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    explicit operator bool() const { return h && h != INVALID_HANDLE_VALUE; }
};
// The profile's launchme.lock, held while Legendary works in it.
Handle lockProfile(const std::filesystem::path& profile) {
    return Handle(CreateFileW((profile / L"launchme.lock").c_str(), GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
}
#else
struct ProfileLock {
    atomic_file::ExclusiveLock lock;
    explicit operator bool() const { return lock.locked(); }
};
ProfileLock lockProfile(const std::filesystem::path& profile) { return ProfileLock{atomic_file::ExclusiveLock(profile / L"launchme.lock")}; }
#endif
std::string field(const json::Value& v, const char* key) { const auto p = v.get(key); return p ? p->stringOr() : std::string{}; }
bool flag(const json::Value& v, const char* key) { const auto p = v.get(key); return p && p->type == json::Value::Type::boolean && p->flag; }
std::string readText(const std::filesystem::path& p, size_t limit = 8 * 1024 * 1024) {
    std::ifstream f(p, std::ios::binary | std::ios::ate); if (!f || f.tellg() < 0 || f.tellg() > static_cast<std::streamoff>(limit)) return {};
    std::string text(static_cast<size_t>(f.tellg()), '\0'); f.seekg(0);
    return f.read(text.data(), static_cast<std::streamsize>(text.size())) ? text : std::string{};
}
bool regular(const std::filesystem::path& p) { std::error_code ec; return std::filesystem::is_regular_file(p, ec); }
std::filesystem::path childPath(const std::filesystem::path& root, std::wstring_view relative) {
    const std::filesystem::path part(relative);
    if (!root.is_absolute() || part.empty() || part.is_absolute() || part.has_root_name()) return {};
    for (const auto& segment : part) if (segment == L".." || segment.wstring().find(L':') != std::wstring::npos) return {};
    const auto full = root / part;
    // A reparse point must not redirect an executable outside the selected installation.
    std::error_code ec;
    const auto canonical_root = std::filesystem::weakly_canonical(root, ec); if (ec) return {};
    const auto canonical = std::filesystem::weakly_canonical(full, ec); if (ec) return {};
    auto r = canonical_root.begin(), p = canonical.begin();
    for (; r != canonical_root.end(); ++r, ++p) if (p == canonical.end() || !unicode_text::equalIgnoreCase(r->wstring(), p->wstring())) return {};
    return full;
}
#ifndef _WIN32
// Only this user may enter the folder (0700); no ACLs needed.
bool privateDirectory(const std::filesystem::path& p) {
    std::error_code ec; std::filesystem::create_directories(p, ec); if (ec) return false;
    std::filesystem::permissions(p, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, ec);
    return !ec;
}
// As the Windows run below: stdout collected (8 MiB at most), stderr passed on line by line, the
// helper and everything it started killed on cancellation.
ProcessResult run(const std::filesystem::path& exe, const std::filesystem::path& profile, const std::vector<std::wstring>& args,
                  const Cancel& cancel, const std::function<void(std::string_view)>& line = {}) {
    if (cancel()) return {};
    int out[2]{-1, -1}, err[2]{-1, -1};
    struct Pipes { int* fds[2]; ~Pipes() { for (auto* pair : fds) for (int i = 0; i < 2; ++i) if (pair[i] >= 0) ::close(pair[i]); } } pipes{{out, err}};
    if (::pipe2(out, O_CLOEXEC) != 0 || ::pipe2(err, O_CLOEXEC) != 0) return {};
    std::vector<std::string> environment;
    for (char** entry = environ; *entry; ++entry) {
        const std::string_view text(*entry);
        const auto key = text.substr(0, text.find('='));
        if (key != "LEGENDARY_CONFIG_PATH" && key != "PYTHONIOENCODING" && key != "LEGENDARY_WRAPPER_EXE") environment.emplace_back(text);
    }
    environment.push_back("LEGENDARY_CONFIG_PATH=" + profile.string());
    environment.push_back("PYTHONIOENCODING=utf-8");
    std::vector<std::string> command{exe.string()};
    for (const auto& arg : args) command.push_back(text_encoding::utf8Lenient(arg));
    process_spawn::Options options;
    options.directory = profile; options.session = true; options.output = out[1]; options.error = err[1]; options.environment = &environment;
    pid_t pid = 0;
    if (process_spawn::spawn(command, options, pid) != 0) return {};
    ::close(out[1]); out[1] = -1; ::close(err[1]); err[1] = -1;
    ProcessResult result; std::string pending; bool overflow = false, data = false;
    const auto consume = [&](bool output, const char* data, size_t size) {
        if (output) {
            if (result.output.size() + size > 8 * 1024 * 1024) overflow = true;
            else result.output.append(data, size);
        } else if (line) {
            pending.append(data, size);
            for (size_t end; (end = pending.find_first_of("\r\n")) != pending.npos;) {
                line(std::string_view(pending).substr(0, end)); pending.erase(0, end + 1);
            }
            if (pending.size() > 65536) pending.clear();
        }
    };
    // Reads what is there now; false once both pipes are closed.
    const auto drain = [&](int timeout) {
        pollfd fds[2]{{out[0], POLLIN, 0}, {err[0], POLLIN, 0}};
        if (::poll(fds, 2, timeout) < 0) return errno == EINTR;
        bool open = false;
        for (int i = 0; i < 2; ++i) {
            int& fd = i ? err[0] : out[0];
            if (fd < 0) continue;
            if (fds[i].revents & (POLLIN | POLLHUP | POLLERR)) {
                char buffer[16384];
                const ssize_t got = ::read(fd, buffer, sizeof(buffer));
                if (got > 0) { data = true; consume(i == 0, buffer, static_cast<size_t>(got)); }
                else if (got == 0 || errno != EINTR) { ::close(fd); fd = -1; continue; }
            }
            open = true;
        }
        return open;
    };
    std::optional<int> code;
    for (bool open = true;;) {
        if (open) open = drain(40);
        if (cancel() || overflow) { process_spawn::killGroup(pid); return {}; }
        if (!code) code = process_spawn::waitExit(pid, std::chrono::milliseconds(open ? 0 : 40));
        if (code && !open) break;
        // A helper it left running may keep the pipes open: read what is left, then stop.
        if (code) { do { data = false; drain(0); } while (data); break; }
    }
    if (line && !pending.empty()) line(pending);
    result.ok = !overflow && *code == 0 && !cancel(); return result;
}
#else
bool privateDirectory(const std::filesystem::path& p) {
    std::error_code ec; std::filesystem::create_directories(p, ec); if (ec) return false;
    Handle token; if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.h)) return false;
    DWORD size = 0; GetTokenInformation(token.h, TokenUser, nullptr, 0, &size);
    std::vector<uint8_t> bytes(size); if (!GetTokenInformation(token.h, TokenUser, bytes.data(), size, &size)) return false;
    LPWSTR sid = nullptr; if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid, &sid)) return false;
    const auto sddl = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;" + std::wstring(sid) + L")"; LocalFree(sid);
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr)) return false;
    const bool ok = SetFileSecurityW(p.c_str(), DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, sd) != FALSE;
    LocalFree(sd); return ok;
}
#endif
bool sha256(const std::filesystem::path& path, std::string_view expected, const Cancel& cancel) {
    const auto digest = hashing::fileHex(hashing::Algorithm::sha256, path, cancel);
    return !digest.empty() && !cancel() && digest == expected;
}
#ifdef _WIN32
ProcessResult run(const std::filesystem::path& exe, const std::filesystem::path& profile, const std::vector<std::wstring>& args,
                  const Cancel& cancel, const std::function<void(std::string_view)>& line = {}) {
    if (cancel()) return {};
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    Handle out_read, out_write, err_read, err_write;
    if (!CreatePipe(&out_read.h, &out_write.h, &sa, 0) || !CreatePipe(&err_read.h, &err_write.h, &sa, 0)) return {};
    SetHandleInformation(out_read.h, HANDLE_FLAG_INHERIT, 0); SetHandleInformation(err_read.h, HANDLE_FLAG_INHERIT, 0);
    Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr)); if (!input) return {};
    std::map<std::wstring, std::wstring> env;
    auto original = GetEnvironmentStringsW(); if (!original) return {};
    for (const wchar_t* p = original; *p; p += wcslen(p) + 1) {
        std::wstring entry(p); const auto pos = entry.find(L'=', 1); if (pos == entry.npos) continue;
        auto key = entry.substr(0, pos); std::transform(key.begin(), key.end(), key.begin(), towupper);
        if (key != L"LEGENDARY_CONFIG_PATH" && key != L"PYTHONIOENCODING" && key != L"LEGENDARY_WRAPPER_EXE") env[key] = entry.substr(pos + 1);
    }
    FreeEnvironmentStringsW(original);
    env[L"LEGENDARY_CONFIG_PATH"] = profile.wstring(); env[L"PYTHONIOENCODING"] = L"utf-8";
    std::vector<wchar_t> environment;
    for (const auto& [key, value] : env) { const auto entry = key + L'=' + value; environment.insert(environment.end(), entry.begin(), entry.end()); environment.push_back(0); }
    environment.push_back(0);
    std::wstring command = quoteArgument(exe.wstring()); for (const auto& arg : args) command += L' ' + quoteArgument(arg);
    STARTUPINFOEXW start{}; start.StartupInfo.cb = sizeof(start); start.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    start.StartupInfo.hStdInput = input.h; start.StartupInfo.hStdOutput = out_write.h; start.StartupInfo.hStdError = err_write.h;
    SIZE_T size = 0; InitializeProcThreadAttributeList(nullptr, 1, 0, &size); std::vector<uint8_t> attributes(size);
    start.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    if (!InitializeProcThreadAttributeList(start.lpAttributeList, 1, 0, &size)) return {};
    HANDLE handles[]{input.h, out_write.h, err_write.h};
    const bool attr_ok = UpdateProcThreadAttribute(start.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, sizeof(handles), nullptr, nullptr) != FALSE;
    Handle job(CreateJobObjectW(nullptr, nullptr)); JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    PROCESS_INFORMATION pi{};
    const bool created = attr_ok && job && SetInformationJobObject(job.h, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) &&
        CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT,
                       environment.data(), profile.c_str(), &start.StartupInfo, &pi);
    DeleteProcThreadAttributeList(start.lpAttributeList);
    if (!created) return {};
    Handle process(pi.hProcess), thread(pi.hThread);
    if (!AssignProcessToJobObject(job.h, process.h)) { TerminateProcess(process.h, 1); WaitForSingleObject(process.h, INFINITE); return {}; }
    CloseHandle(out_write.h); out_write.h = nullptr; CloseHandle(err_write.h); err_write.h = nullptr;
    ResumeThread(thread.h);
    ProcessResult result; std::string pending; bool overflow = false;
    const auto drain = [&](HANDLE pipe, bool output) {
        for (;;) {
            DWORD available = 0, received = 0; if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) || !available) break;
            char buffer[16384]; if (!ReadFile(pipe, buffer, std::min<DWORD>(available, sizeof(buffer)), &received, nullptr) || !received) break;
            if (output) {
                if (result.output.size() + received > 8 * 1024 * 1024) overflow = true;
                else result.output.append(buffer, received);
            } else if (line) {
                pending.append(buffer, received);
                for (size_t end; (end = pending.find_first_of("\r\n")) != pending.npos;) {
                    line(std::string_view(pending).substr(0, end)); pending.erase(0, end + 1);
                }
                if (pending.size() > 65536) pending.clear();
            }
        }
    };
    while (true) {
        drain(out_read.h, true); drain(err_read.h, false);
        if (cancel() || overflow) { TerminateJobObject(job.h, 1); WaitForSingleObject(process.h, 5000); return {}; }
        if (WaitForSingleObject(process.h, 40) == WAIT_OBJECT_0) break;
    }
    drain(out_read.h, true); drain(err_read.h, false); if (line && !pending.empty()) line(pending);
    DWORD exit = 1; GetExitCodeProcess(process.h, &exit); result.ok = !overflow && exit == 0 && !cancel(); return result;
}
#endif
class Credentials {
    std::filesystem::path profile_;
public:
    explicit Credentials(std::filesystem::path profile) : profile_(std::move(profile)) {}
    bool write(const Tokens& t) { auto text = sessionJson(t, false); const auto ok = atomic_file::write(profile_ / L"user.json", text); SecureZeroMemory(text.data(), text.size()); return ok; }
    ~Credentials() {
        std::error_code ec;
        // The helper never receives a refresh token. Clear its short-lived bearer
        // and account-derived lists, including after timeout/cancellation/error.
        for (const auto* name : {L"user.json", L"user.json.lock", L"assets.json", L"entitlements.json", L"achievements.json", L"aliases.json"}) std::filesystem::remove(profile_ / name, ec);
    }
};
class LegendaryRunner final : public Runner {
    std::filesystem::path root_, profile_;
    LegendaryOperations operations_;
    Result setup(std::filesystem::path& exe, const Cancel& cancel) {
        if (!privateDirectory(profile_)) return {Failure::storage};
        // Private Launchme profile: never read/import a Legendary, Heroic or EGL login.
        if (!atomic_file::write(profile_ / L"config.ini", "[Legendary]\ndisable_update_check = true\ndisable_update_notice = true\ndisable_auto_aliasing = true\negl_sync = false\n")) return {Failure::storage};
        exe = operations_.prepare(root_, cancel); return exe.empty() ? Result{Failure::helper} : Result{};
    }
public:
    LegendaryRunner(std::filesystem::path root, LegendaryOperations operations) : root_(std::move(root)), profile_(root_ / L"epic" / L"legendary"), operations_(std::move(operations)) {}
    void cleanup() override {
        auto lock = lockProfile(profile_);
        if (lock) { Credentials credentials(profile_); }
    }
    Result install(const Tokens& t, const Game& game, const gog::InstallRequest& request, const Cancel& cancel, const gog::InstallProgress& progress) override {
        if (!game.epic_partner.empty()) return {Failure::unsupported};
        if (!identifier(game.source_id) || !request.install_directory.is_absolute()) return {Failure::invalid};
        std::filesystem::path exe; auto result = setup(exe, cancel); if (!result) return result;
        auto lock = lockProfile(profile_); if (!lock) return {Failure::storage};
        Credentials credentials(profile_); if (!credentials.write(t)) return {Failure::storage};
        const auto app = wide(game.source_id);
        const auto marker = root_ / L"installed" / L"epic" / (key_codec::encode(game.source_id) + ".path");
        std::error_code ec; std::filesystem::remove(marker, ec);
        const auto installed = json::parse(readText(profile_ / L"installed.json"));
        const bool repair = installed && installed->get(game.source_id);
        std::vector<std::wstring> args{L"-y", repair ? L"repair" : L"install", app, L"--base-path", request.install_directory.parent_path().wstring(),
            L"--game-folder", request.install_directory.filename().wstring(), L"--platform", L"Windows", L"--skip-dlcs", L"--disable-sdl"};
        if (repair) args.push_back(L"--repair-and-update");
        progress(gog::InstallState::downloading, 0, 0);
        auto proc = operations_.invoke(exe, profile_, args, cancel, [&](auto line) { if (const auto p = downloadPercent(line)) progress(gog::InstallState::downloading, *p, 10000); });
        if (!proc.ok) return {cancel() ? Failure::cancelled : Failure::helper};
        // Legendary may exit 0 after a failed child download. A full verification
        // and valid executable are required before our local installation marker.
        progress(gog::InstallState::verifying, 0, 0); bool verified = false;
        proc = operations_.invoke(exe, profile_, {L"-y", L"verify", app}, cancel, [&](auto line) { if (line.find("Verification finished successfully.") != line.npos) verified = true; });
        const auto data = json::parse(readText(profile_ / L"installed.json")); const auto item = data ? data->get(game.source_id) : nullptr;
        if (!proc.ok || !verified || !item || flag(*item, "needs_verification") || flag(*item, "is_preloaded")) return {Failure::invalid};
        const std::filesystem::path dir(wide(field(*item, "install_path")));
        const auto executable = childPath(dir, wide(field(*item, "executable")));
        if (!regular(executable)) return {Failure::invalid};
        if (const auto prereq = item->get("prereq_info"); prereq && !flag(*prereq, "installed")) {
            const auto path = childPath(dir, wide(field(*prereq, "path")));
            if (!path.empty() && regular(path)) {
                if (cancel()) return {Failure::cancelled};
                progress(gog::InstallState::installing, 0, 0);
                const auto arguments = wide(field(*prereq, "args"));
#ifdef _WIN32
                SHELLEXECUTEINFOW info{sizeof(info)}; info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
                info.hwnd = reinterpret_cast<HWND>(request.owner_window); info.lpFile = path.c_str(); info.lpParameters = arguments.c_str();
                const auto cwd = path.parent_path(); info.lpDirectory = cwd.c_str(); info.nShow = SW_SHOWNORMAL;
                if (!ShellExecuteExW(&info)) return {Failure::helper};
                Handle process(info.hProcess); if (!process) return {Failure::helper};
                while (WaitForSingleObject(process.h, 100) == WAIT_TIMEOUT) if (cancel()) return {Failure::cancelled};
                DWORD code = 1; GetExitCodeProcess(process.h, &code); if (code != 0 && code != 3010) return {Failure::helper};
#else
                // Windows redistributables: only Wine or Proton could run them, as they do the game.
                (void)arguments;
#endif
            } else if (!field(*prereq, "path").empty()) return {Failure::invalid};
        }
        if (cancel()) return {Failure::cancelled};
        return atomic_file::write(marker, utf8(dir.wstring())) ? Result{} : Result{Failure::storage};
    }
    Result launch(const Tokens& t, const Game& game, GameAction& action, const Cancel& cancel, bool offline) override {
        if (!game.epic_managed || !identifier(game.source_id)) return {Failure::unsupported};
        std::filesystem::path exe; auto result = setup(exe, cancel); if (!result) return result;
        auto lock = lockProfile(profile_); if (!lock) return {Failure::storage};
        Credentials credentials(profile_); if (!credentials.write(t)) return {Failure::storage};
        const auto installed = json::parse(readText(profile_ / L"installed.json")); const auto item = installed ? installed->get(game.source_id) : nullptr;
        if (!item || (offline && !flag(*item, "can_run_offline"))) return {Failure::offline};
        std::vector<std::wstring> args{L"launch", wide(game.source_id), L"--json", L"--skip-version-check"}; if (offline) args.push_back(L"--offline");
        auto proc = operations_.invoke(exe, profile_, args, cancel, {});
        const auto parsed = proc.ok ? json::parse(proc.output) : std::nullopt;
        const auto next = parsed ? launchAction(*parsed, game.install_directory) : std::nullopt;
        SecureZeroMemory(proc.output.data(), proc.output.size());
        if (!next) return {Failure::helper};
        action = *next; return {};
    }
};
}
std::wstring quoteArgument(std::wstring_view text) {
    return command_line::quoteArgument(text);
}
std::optional<unsigned> downloadPercent(std::string_view line) {
    const auto begin = line.find("Progress: "); if (begin == line.npos) return {};
    line.remove_prefix(begin + 10); const auto end = line.find('%'); if (end == line.npos || end > 7) return {};
    const auto value = std::string(line.substr(0, end)); char* tail = nullptr; const double percent = strtod(value.c_str(), &tail);
    if (tail != value.c_str() + value.size() || !std::isfinite(percent) || percent < 0 || percent > 100) return {};
    return static_cast<unsigned>(std::round(percent * 100));
}
std::optional<GameAction> launchAction(const json::Value& json, const std::filesystem::path& install) {
    if (wide(field(json, "game_directory")) != install.wstring() || !field(json, "pre_launch_command").empty()) return {};
    const auto executable = childPath(install, wide(field(json, "game_executable")));
    if (!regular(executable)) return {};
    if (const auto command = json.get("launch_command"); command && !command->elements.empty()) return {};
    if (const auto env = json.get("environment"); env && !env->properties.empty()) return {};
    GameAction action{executable.wstring(), {}, executable.parent_path(), ActionKind::play};
    for (const auto name : {"game_parameters", "user_parameters", "egl_parameters"}) {
        const auto args = json.get(name); if (!args || args->type != json::Value::Type::array) return {};
        for (const auto& arg : args->elements) {
            if (arg.type != json::Value::Type::string || arg.text.find('\0') != arg.text.npos) return {};
            if (!action.arguments.empty()) action.arguments += L' ';
            auto text = wide(arg.text);
            // shlex(posix=False) preserves enclosing quotes in manifest args.
            if (text.size() >= 2 && text.front() == L'"' && text.back() == L'"') text = text.substr(1, text.size() - 2);
            action.arguments += quoteArgument(text);
            if (action.arguments.size() > 30000) return {};
        }
    }
    return action;
}
std::filesystem::path prepareLegendary(const std::filesystem::path& root, const Cancel& cancel) {
#ifdef _WIN32
    constexpr auto digest = "6be77857dc0a6dd33ea7442ce8b5291944ec6da53757f81ca95b57ce794baf5b";
    const auto bin = root / L"tools" / L"legendary-0.21.1.exe";
    constexpr auto url = L"https://github.com/legendary-gl/legendary/releases/download/0.21.1/legendary_windows_x64.exe";
    constexpr uint64_t bytes = 17654404;
#elif defined(__aarch64__)
    constexpr auto digest = "e901cb52ada10d5cef6ffb2a0985099958124457bfb455a571023d1e042143d9";
    const auto bin = root / L"tools" / L"legendary-0.21.1";
    constexpr auto url = L"https://github.com/legendary-gl/legendary/releases/download/0.21.1/legendary_linux_arm64";
    constexpr uint64_t bytes = 2999455;
#else
    constexpr auto digest = "dbed33bbe96031e65858233e4badf0a1d401bb6cb0cc995d1237cf3a0c826a45";
    const auto bin = root / L"tools" / L"legendary-0.21.1";
    constexpr auto url = L"https://github.com/legendary-gl/legendary/releases/download/0.21.1/legendary_linux_x64";
    constexpr uint64_t bytes = 3093622;
#endif
    if (regular(bin) && sha256(bin, digest, cancel)) {
#ifndef _WIN32
        std::error_code mode; std::filesystem::permissions(bin, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, mode);
        if (mode) return {};
#endif
        return bin;
    }
    if (cancel()) return {};
    std::error_code ec; std::filesystem::create_directories(bin.parent_path(), ec); if (ec) return {};
    const auto part = bin.wstring() + L".part";
    const auto error = httpDownload(url, part, bytes, cancel, [](auto) {},
        [](std::wstring_view url) { return url.starts_with(L"https://github.com/legendary-gl/legendary/releases/download/0.21.1/") || url.starts_with(L"https://release-assets.githubusercontent.com/"); });
    if (error != DownloadError::none || cancel()) return {};
    if (!sha256(part, digest, cancel)) { if (!cancel()) std::filesystem::remove(part, ec); return {}; }
#ifdef _WIN32
    if (!MoveFileExW(part.c_str(), bin.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return {};
#else
    std::filesystem::permissions(part, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, ec);
    if (ec || !atomic_file::replace(part, bin)) return {};
#endif
    return bin;
}
std::vector<Game> installedGames(const std::filesystem::path& root) {
    const auto profile = root / L"epic" / L"legendary";
    std::vector<Game> games;
    const auto json = json::parse(readText(profile / L"installed.json")); if (!json || json->type != json::Value::Type::object) return games;
    for (const auto& [id, item] : json->properties) {
        if (!identifier(id) || field(item, "app_name") != id || flag(item, "is_dlc") || flag(item, "needs_verification") || flag(item, "is_preloaded") || field(item, "platform") != "Windows") continue;
        const auto marker = root / L"installed" / L"epic" / (key_codec::encode(id) + ".path");
        const std::filesystem::path dir(wide(field(item, "install_path")));
        if (readText(marker, 32768) != utf8(dir.wstring())) continue;
        const auto executable = childPath(dir, wide(field(item, "executable"))); if (!regular(executable)) continue;
        Game g; g.kind = GameKind::store; g.source = GameSource::epic; g.source_id = id; g.epic_managed = true;
        g.name = text_encoding::wideLenient(field(item, "title")); if (g.name.empty()) continue;
        g.install_directory = dir; g.executable = executable; g.manifest_path = marker;
        g.play_action = {executable.wstring(), {}, executable.parent_path(), ActionKind::play};
        games.push_back(std::move(g));
    }
    return games;
}
bool uninstallManaged(const std::filesystem::path& root, const Game& game, const Cancel& cancel) {
    if (!game.epic_managed || !identifier(game.source_id)) return false;
    const auto profile = root / L"epic" / L"legendary";
    const auto exe = prepareLegendary(root, cancel);
    if (exe.empty() || cancel()) return false;
    auto lock = lockProfile(profile);
    if (!lock) return false;
    // Legendary removes the files and its installed.json entry; no login is needed for that.
    const auto result = run(exe, profile, {L"-y", L"uninstall", wide(game.source_id)}, cancel);
    if (!result.ok) return false;
    std::error_code ec;
    std::filesystem::remove(root / L"installed" / L"epic" / (key_codec::encode(game.source_id) + ".path"), ec);
    return true;
}
std::unique_ptr<Runner> makeRunner(const std::filesystem::path& root, LegendaryOperations operations) { return std::make_unique<LegendaryRunner>(root, std::move(operations)); }
std::unique_ptr<Runner> makeRunner(const std::filesystem::path& root) { return makeRunner(root, {prepareLegendary, run}); }
}
