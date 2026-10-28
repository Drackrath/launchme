// Linux BrowserWindow: the window itself is the `launchme-web` helper (src/linux/web_helper.cpp,
// WebKitGTK 6.0 + GTK4), started per window and driven over a socket pair (browser_protocol.hpp).
//
// Why a process and not a GTK thread or a GLib context pumped from the SDL loop: GTK 4 cannot be
// loaded into a process that has GTK 3 in it, and SDL brings GTK 3 in by itself on common desktops
// (its tray icon dlopens libgtk-3 with libappindicator, libdecor's GTK plugin draws SDL's window
// frames on GNOME). A process of its own also keeps GTK's main loop, WebKit's memory and a web
// process crash away from the launcher, and the launcher never links GTK or WebKit.
#include "browser_window.hpp"
#include "browser_protocol.hpp"
#include "../i18n/i18n.hpp"
#include "../platform/process_spawn.hpp"
#include "../platform/system.hpp"
#include "../platform/text_encoding.hpp"
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <map>
#include <mutex>
#include <spawn.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

extern char** environ;

namespace {
constexpr char helper_name[] = "launchme-web";

std::filesystem::path helperPath() {
    const auto exe = system_info::executablePath();
    return exe.empty() ? std::filesystem::path{} : exe.parent_path() / helper_name;
}

std::string utf8(const std::wstring& text) { return text_encoding::utf8Lenient(text); }
std::wstring wide(const std::string& text) { return text_encoding::wideLenient(text); }

// The launcher's end of the socket pair, read on a thread of its own; pump() takes the lines.
struct Channel {
    int fd = -1;
    std::mutex mutex;
    std::vector<std::string> lines;
    bool ended = false;
    std::function<void()> wake; // Cleared (under the mutex) when the window is closed.
    ~Channel() { if (fd >= 0) ::close(fd); }
    void signal() { if (wake) wake(); }
};

void readLoop(std::shared_ptr<Channel> channel) {
    std::string buffer;
    char chunk[16384];
    while (true) {
        const auto got = ::recv(channel->fd, chunk, sizeof(chunk), 0);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) break;
        buffer.append(chunk, static_cast<size_t>(got));
        auto lines = browser_protocol::takeLines(buffer);
        if (buffer.size() > 64u * 1024 * 1024) break; // A line without an end: not the helper speaking.
        if (lines.empty()) continue;
        std::lock_guard lock(channel->mutex);
        for (auto& line : lines) channel->lines.push_back(std::move(line));
        channel->signal();
    }
    std::lock_guard lock(channel->mutex);
    channel->ended = true;
    channel->signal();
}

// Starts the helper with `socket` as its descriptor 3, in a session of its own (so its WebKit
// processes go with it), stdin from /dev/null, the launcher's stdout/stderr.
int spawnHelper(const std::vector<std::string>& arguments, int socket, const std::string& activation_token, pid_t& pid) {
    std::vector<std::string> environment;
    for (char** entry = environ; entry && *entry; ++entry) {
        const std::string_view value(*entry);
        // The launcher's own start-up token is not the reader window's.
        if (value.starts_with("XDG_ACTIVATION_TOKEN=") || value.starts_with("DESKTOP_STARTUP_ID=")) continue;
        environment.emplace_back(value);
    }
    if (!activation_token.empty()) environment.push_back("XDG_ACTIVATION_TOKEN=" + activation_token);
    std::vector<char*> argv, envp;
    for (const auto& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    for (const auto& entry : environment) envp.push_back(const_cast<char*>(entry.c_str()));
    envp.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    if (posix_spawn_file_actions_init(&actions) != 0) return ENOMEM;
    if (posix_spawnattr_init(&attributes) != 0) { posix_spawn_file_actions_destroy(&actions); return ENOMEM; }
    int error = posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    if (!error) error = posix_spawn_file_actions_adddup2(&actions, socket, 3);
    if (!error) error = posix_spawn_file_actions_addclosefrom_np(&actions, 4);
    sigset_t none, all;
    sigemptyset(&none);
    sigfillset(&all);
    if (!error) error = posix_spawnattr_setsigmask(&attributes, &none);
    if (!error) error = posix_spawnattr_setsigdefault(&attributes, &all);
    if (!error) error = posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSID);
    if (!error) error = posix_spawn(&pid, argv[0], &actions, &attributes, argv.data(), envp.data());
    posix_spawnattr_destroy(&attributes);
    posix_spawn_file_actions_destroy(&actions);
    return error;
}

// Waits a little for the helper to leave after `close` / end of file, then ends its session.
void retire(pid_t pid) {
    if (pid <= 0) return;
    std::thread([pid] {
        if (!process_spawn::waitExit(pid, std::chrono::milliseconds(3000))) process_spawn::killGroup(pid);
    }).detach();
}

bool flag(const std::string& value) { return value == "1"; }
}

struct BrowserWindow::State {
    std::shared_ptr<Channel> channel;
    pid_t pid = -1;
    Options options;
    Status status = Status::closed;
    std::wstring url, title, error;
    bool open = false, heard = false, loading = true, can_back = false, can_forward = false, video = false, active = false;
    uint64_t next_id = 1;
    std::map<uint64_t, std::function<void(std::wstring)>> scripts;
    std::map<uint64_t, std::function<void(bool)>> captures;

    void send(std::initializer_list<std::string_view> fields) const {
        if (!open || !channel) return;
        const auto message = browser_protocol::line(fields);
        size_t sent = 0;
        while (sent < message.size()) {
            const auto wrote = ::send(channel->fd, message.data() + sent, message.size() - sent, MSG_NOSIGNAL);
            if (wrote < 0 && errno == EINTR) continue;
            if (wrote <= 0) return; // The helper is gone; pump() sees the end of file.
            sent += static_cast<size_t>(wrote);
        }
    }
    // Stops talking to the helper: it sees end of file and closes its window.
    void end() {
        if (channel) {
            { std::lock_guard lock(channel->mutex); channel->wake = nullptr; }
            ::shutdown(channel->fd, SHUT_RDWR);
        }
        retire(pid);
        pid = -1;
        open = false;
        status = Status::closed;
        scripts.clear();
        captures.clear();
    }
    void apply(const std::vector<std::string>& f, std::vector<std::function<void()>>& later) {
        const auto& kind = f[0];
        heard = true;
        const auto number = [&](size_t i) { return i < f.size() ? std::strtoull(f[i].c_str(), nullptr, 10) : 0ull; };
        if (kind == "status" && f.size() >= 2) {
            const auto value = number(1);
            if (value <= static_cast<unsigned>(Status::failed)) status = static_cast<Status>(value);
            if (status == Status::missing_runtime) { // The helper found no display to show a window on.
                end();
                status = Status::missing_runtime;
                if (options.external) later.push_back([external = options.external, target = url] { external(target); });
                if (options.closed) later.push_back(options.closed);
            }
        } else if (kind == "page" && f.size() >= 7) {
            url = wide(f[1]); title = wide(f[2]);
            loading = flag(f[3]); can_back = flag(f[4]); can_forward = flag(f[5]); video = flag(f[6]);
        } else if (kind == "error" && f.size() >= 2) {
            error = wide(f[1]);
        } else if (kind == "active" && f.size() >= 2) {
            active = flag(f[1]);
        } else if (kind == "external" && f.size() >= 2) {
            if (options.external) later.push_back([external = options.external, target = wide(f[1])] { external(target); });
        } else if (kind == "script" && f.size() >= 3) {
            if (const auto found = scripts.find(number(1)); found != scripts.end()) {
                later.push_back([done = std::move(found->second), result = wide(f[2])] { done(result); });
                scripts.erase(found);
            }
        } else if (kind == "capture" && f.size() >= 3) {
            if (const auto found = captures.find(number(1)); found != captures.end()) {
                later.push_back([done = std::move(found->second), ok = flag(f[2])] { done(ok); });
                captures.erase(found);
            }
        } else if (kind == "closed") {
            end();
            if (options.closed) later.push_back(options.closed);
        }
    }
};

BrowserWindow::BrowserWindow() = default;
BrowserWindow::~BrowserWindow() { close(false); }

bool BrowserWindow::supportedUrl(const std::wstring& url) {
    if (url.empty() || url.size() > 8192) return false;
    return browser_protocol::supportedUrl(utf8(url));
}

bool BrowserWindow::runtimeAvailable() {
    const auto helper = helperPath();
    return !helper.empty() && ::access(helper.c_str(), X_OK) == 0;
}

bool BrowserWindow::open(NativeWindow, const std::wstring& url, const std::filesystem::path& profile, Options options) {
    if (!supportedUrl(url) || !profile.is_absolute()) return false;
    if (isOpen()) {
        state_->options.external = std::move(options.external);
        state_->options.closed = std::move(options.closed);
        {
            std::lock_guard lock(state_->channel->mutex);
            state_->channel->wake = std::move(options.wake);
        }
        state_->send({"navigate", utf8(url)});
        if (options.activate) state_->send({"present"});
        return true;
    }
    if (state_) close(false);
    state_ = std::make_shared<State>();
    auto& s = *state_;
    s.options = std::move(options);
    s.url = url;
    bool available = runtimeAvailable();
#ifdef LAUNCHME_UI_TEST
    if (test_missing_) available = false;
#endif
    if (!available) {
        // No WebKitGTK helper in this build or installation: the page opens in the user's browser.
        s.status = Status::missing_runtime;
        if (s.options.external) s.options.external(url);
        return true;
    }
    std::error_code ec;
    std::filesystem::create_directories(profile, ec);
    if (ec) return false;
    std::vector<std::string> arguments{helperPath().string(), "--url", utf8(url), "--profile", profile.string(),
        "--language", std::to_string(static_cast<int>(i18n::language()))};
    if (low_memory_) arguments.push_back("--low-memory");
    if (!s.options.activate) arguments.push_back("--no-activate");
#ifdef LAUNCHME_UI_TEST
    if (!test_html_.empty()) {
        const auto fixture = profile / "fixture.html";
        std::ofstream(fixture, std::ios::binary | std::ios::trunc) << utf8(test_html_);
        arguments.insert(arguments.end(), {"--fixture", fixture.string()});
    }
#endif
    int sockets[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) != 0) return false;
    // Above 3, so the child's dup2 onto 3 always clears close-on-exec.
    const int child = ::fcntl(sockets[1], F_DUPFD_CLOEXEC, 10);
    ::close(sockets[1]);
    if (child < 0) { ::close(sockets[0]); return false; }
    pid_t pid = -1;
    const int failed = spawnHelper(arguments, child, s.options.activation_token, pid);
    ::close(child);
    if (failed) { ::close(sockets[0]); state_.reset(); return false; }
    s.channel = std::make_shared<Channel>();
    s.channel->fd = sockets[0];
    s.channel->wake = std::move(s.options.wake);
    s.pid = pid;
    s.open = true;
    s.status = Status::starting;
    std::thread(readLoop, s.channel).detach();
    return true;
}

void BrowserWindow::close(bool restore) {
    if (!state_) return;
    auto state = std::move(state_);
    if (!state->open) return;
    state->send({"close"});
    state->end();
    if (restore && state->options.closed) state->options.closed();
}

void BrowserWindow::pump() {
    if (!state_ || !state_->open || !state_->channel) return;
    const auto state = state_; // Callbacks may close or replace this window.
    std::vector<std::string> lines;
    bool ended = false;
    {
        std::lock_guard lock(state->channel->mutex);
        lines.swap(state->channel->lines);
        ended = state->channel->ended;
    }
    std::vector<std::function<void()>> later;
    for (const auto& line : lines) {
        if (!state->open) break;
        const auto f = browser_protocol::fields(line);
        if (!f.empty()) state->apply(f, later);
    }
    if (ended && state->open) { // The helper ended without saying so (it crashed, or was killed).
        const bool started = state->heard;
        state->end();
        if (!started) {
            // It never came up (WebKitGTK missing or incompatible, as in an AppImage on a system without
            // it): the page opens in the user's browser instead.
            state->status = Status::missing_runtime;
            if (state->options.external) later.push_back([external = state->options.external, url = state->url] { external(url); });
        }
        if (state->options.closed) later.push_back(state->options.closed);
    }
    for (const auto& callback : later) callback();
}

bool BrowserWindow::isOpen() const { return state_ && state_->open; }
bool BrowserWindow::owns(NativeWindow window) const { return isOpen() && window && window == handle(); }
BrowserWindow::NativeWindow BrowserWindow::handle() const { return isOpen() ? static_cast<NativeWindow>(state_.get()) : nullptr; }
bool BrowserWindow::focused() const { return isOpen() && state_->active; }
BrowserWindow::Status BrowserWindow::status() const { return state_ ? state_->status : Status::closed; }
void BrowserWindow::command(Command command) {
    if (isOpen()) state_->send({"command", std::to_string(static_cast<int>(command))});
}
void BrowserWindow::scroll(int pixels) {
    if (isOpen()) state_->send({"scroll", std::to_string(pixels)});
}
void BrowserWindow::setLowMemory(bool enabled) {
    low_memory_ = enabled;
    if (isOpen()) state_->send({"low-memory", enabled ? "1" : "0"});
}

#ifdef LAUNCHME_UI_TEST
void BrowserWindow::testPage(std::wstring html, bool missing) { test_html_ = std::move(html); test_missing_ = missing; }
void BrowserWindow::scriptForTest(const std::wstring& script, std::function<void(std::wstring)> result) {
    if (!isOpen()) { result(L""); return; }
    const auto id = state_->next_id++;
    state_->scripts[id] = std::move(result);
    state_->send({"script", std::to_string(id), utf8(script)});
}
std::wstring BrowserWindow::urlForTest() const { return state_ ? state_->url : L""; }
bool BrowserWindow::loadingForTest() const { return state_ && state_->loading; }
std::wstring BrowserWindow::errorForTest() const { return state_ ? state_->error : L""; }
BrowserWindow::ProcessId BrowserWindow::processForTest() const { return isOpen() ? static_cast<ProcessId>(state_->pid) : 0; }
void BrowserWindow::captureForTest(const std::filesystem::path& file, std::function<void(bool)> completed) {
    if (!isOpen()) { completed(false); return; }
    const auto id = state_->next_id++;
    state_->captures[id] = std::move(completed);
    state_->send({"capture", std::to_string(id), file.string()});
}
void BrowserWindow::captureWindowForTest(const std::filesystem::path& file, std::function<void(bool)> completed) {
    if (!isOpen()) { completed(false); return; }
    const auto id = state_->next_id++;
    state_->captures[id] = std::move(completed);
    state_->send({"capture", std::to_string(id), file.string(), "window"});
}
#endif
