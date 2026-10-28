// The reader window on Linux: GTK 4 and WebKitGTK 6.0 in the helper process `launchme-browser`
// (src/linux/browser), so GTK's main loop never runs inside the SDL process. The helper ships next
// to the launcher's executable; it is driven over a socketpair (src/linux/browser/browser_protocol.hpp)
// and its messages are delivered on the UI thread (lm_window::post). It ends with the socket, so a
// launcher that dies takes the window with it.
//
// The GTK window is not an HWND: handle() is null, and owns() answers whether the helper's window
// is the active one while the SDL layer reports no foreground window of the launcher's
// (GetForegroundWindow() is then null). Without the helper or WebKitGTK the page opens in the
// user's browser through Options::external and status() is missing_runtime.
#include "browser_window.hpp"
#include "../i18n/i18n.hpp"
#include "../linux/browser/browser_protocol.hpp"
#include "../linux/window/window_system.hpp"
#include "../net/companion_links.hpp"
#include "../platform/process_spawn.hpp"
#include "../platform/system.hpp"
#include "../platform/text_encoding.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <sys/socket.h>
#include <sys/time.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {
namespace protocol = browser_protocol;

std::string utf8(std::wstring_view text) { return text_encoding::utf8Lenient(text); }
std::wstring wide(std::string_view text) { return text_encoding::wideLenient(text); }

std::filesystem::path helperPath() {
    const auto executable = system_info::executablePath();
    return executable.empty() ? std::filesystem::path{} : executable.parent_path() / "launchme-browser";
}

bool executable(const std::filesystem::path& file) { return !file.empty() && ::access(file.c_str(), X_OK) == 0; }

// The launcher's end of the socketpair: the UI thread writes, the reader thread reads and, once the
// helper is gone, closes it (under the lock, so a write never reaches a reused descriptor).
struct Channel {
    std::mutex mutex;
    int fd = -1;
    bool write(const std::string& line) {
        std::lock_guard lock(mutex);
        size_t sent = 0;
        while (fd >= 0 && sent < line.size()) {
            const ssize_t written = ::send(fd, line.data() + sent, line.size() - sent, MSG_NOSIGNAL);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) { ::shutdown(fd, SHUT_RDWR); return false; } // Gone or hung (SO_SNDTIMEO).
            sent += static_cast<size_t>(written);
        }
        return fd >= 0;
    }
    void shutdown() { std::lock_guard lock(mutex); if (fd >= 0) ::shutdown(fd, SHUT_RDWR); }
    void close() { std::lock_guard lock(mutex); if (fd >= 0) ::close(fd); fd = -1; }
};
}

struct BrowserWindow::State : std::enable_shared_from_this<State> {
    HWND owner = nullptr;
    Options options;
    Status status = Status::closed;
    std::wstring url, title, error;
    std::filesystem::path profile;
    bool open = false, ready = false, loading = true, can_back = false, can_forward = false, focused = false;
    bool owner_enabled = false, low_memory = false;
    pid_t pid = 0;
    std::shared_ptr<Channel> channel;
    int next_request = 1;
    std::map<int, std::function<void(std::wstring)>> scripts;
    std::map<int, std::function<void(bool)>> captures;
#ifdef LAUNCHME_UI_TEST
    std::wstring fixture;
    bool missing = false;
#endif

    bool send(std::initializer_list<std::string_view> fields) {
        return open && channel && channel->write(protocol::message(fields));
    }

    // No helper window: the page opens in the user's browser (the caller's xdg-open) instead.
    void fallBack(Status value) {
        status = value;
        loading = false;
        if (options.external) options.external(url);
    }

    void navigate(const std::wstring& destination) {
        url = destination;
        error.clear();
        loading = true;
        // The TV video player is served by the launcher under https://launchme.invalid/ (as on Windows).
        if (const auto html = companion::links::playerHtml(destination, i18n::english()); !html.empty())
            send({"page", utf8(destination), utf8(html)});
        send({"navigate", utf8(destination)});
    }

    // Size and place the helper's window like the WebView2 one: the owner's size less a margin, at
    // least 900 x 600, within the display; over a full-screen (TV) or display-filling launcher it is
    // maximized. It becomes the owner's transient window when GTK and SDL use the same display
    // server (the X11 window; the xdg-foreign handle on Wayland). GTK picks its backend itself
    // (GDK_BACKEND overrides): SDL may use XWayland on a Wayland desktop, GTK should not.
    void placement(std::vector<std::string>& arguments) const {
        SDL_Window* window = lm_window::sdlWindow(owner);
        int width = 0, height = 0;
        if (window) SDL_GetWindowSize(window, &width, &height);
        const SDL_DisplayID display = window ? SDL_GetDisplayForWindow(window) : SDL_GetPrimaryDisplay();
        SDL_Rect usable{};
        if (!display || !SDL_GetDisplayUsableBounds(display, &usable) || usable.w <= 0 || usable.h <= 0)
            usable = SDL_Rect{0, 0, std::max(width, 1280), std::max(height, 800)};
        constexpr int inset = 48;
        const int w = std::min(std::max(900, width - inset), usable.w), h = std::min(std::max(600, height - inset), usable.h);
        const bool full = window && (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN);
        arguments.insert(arguments.end(), {"--width", std::to_string(w), "--height", std::to_string(h)});
        if (full || (w >= usable.w - inset && h >= usable.h - inset)) arguments.push_back("--maximize");
        const char* driver = SDL_GetCurrentVideoDriver();
        const std::string_view backend = driver ? driver : "";
        const SDL_PropertiesID properties = window ? SDL_GetWindowProperties(window) : 0;
        if (backend == "x11" && properties) {
            if (const auto xid = SDL_GetNumberProperty(properties, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0))
                arguments.insert(arguments.end(), {"--parent", "x11:" + std::to_string(xid)});
        }
#ifdef SDL_PROP_WINDOW_WAYLAND_XDG_TOPLEVEL_EXPORT_HANDLE_STRING
        if (backend == "wayland" && properties) {
            if (const char* handle = SDL_GetStringProperty(properties, SDL_PROP_WINDOW_WAYLAND_XDG_TOPLEVEL_EXPORT_HANDLE_STRING, nullptr); handle && *handle)
                arguments.insert(arguments.end(), {"--parent", std::string("wayland:") + handle});
        }
#endif
    }

    void start() {
        status = Status::starting;
        loading = true; ready = false; can_back = can_forward = false; focused = false;
        error.clear(); title.clear();
        const auto helper = helperPath();
        bool available = executable(helper);
#ifdef LAUNCHME_UI_TEST
        if (missing) available = false;
#endif
        if (!available) { fallBack(Status::missing_runtime); return; }
        std::error_code failure;
        std::filesystem::create_directories(profile, failure);
        if (failure) { error = tr(L"Nie można utworzyć profilu przeglądarki."); fallBack(Status::failed); return; }
        int pair[2]{-1, -1};
        if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) != 0) { fallBack(Status::failed); return; }
        std::vector<std::string> arguments{helper.string(), "--profile", profile.string(), "--title", "Glance \u2014 Launchme",
                                           "--language", utf8(i18n::localeName())};
        placement(arguments);
        if (low_memory) arguments.push_back("--low-memory");
        if (!options.activate) arguments.push_back("--no-activate");
        process_spawn::Options spawn;
        spawn.output = pair[1]; // The helper's stdout is its channel; it moves it off fd 1.
        spawn.session = true;   // Its group (with WebKit's processes) can be ended together.

        pid_t child = 0;
        const int spawned = process_spawn::spawn(arguments, spawn, child);
        ::close(pair[1]);
        if (spawned) {
            ::close(pair[0]);
            fallBack(spawned == ENOENT || spawned == EACCES || spawned == ENOEXEC ? Status::missing_runtime : Status::failed);
            return;
        }
        const timeval patience{2, 0}; // A hung helper never blocks the UI thread for long.
        ::setsockopt(pair[0], SOL_SOCKET, SO_SNDTIMEO, &patience, sizeof(patience));
        pid = child;
        channel = std::make_shared<Channel>();
        channel->fd = pair[0];
        open = true;
        try {
            std::thread(readChannel, weak_from_this(), channel, pid).detach();
        } catch (...) {
            open = false;
            channel->close();
            process_spawn::killGroup(pid);
            fallBack(Status::failed);
            return;
        }
        owner_enabled = IsWindowEnabled(owner) != FALSE;
        if (owner_enabled) EnableWindow(owner, FALSE);
        const std::pair<const char*, const wchar_t*> texts[] = {
            {"reload", trKey(L"Odśwież")}, {"close", trKey(L"Zamknij · Esc / B")}, {"play", trKey(L"Odtwórz / Pauza")},
            {"loading", trKey(L"Wczytywanie…")}, {"starting", trKey(L"Uruchamianie przeglądarki…")},
            {"hint", trKey(L"↑ ↓ / gałka: przewijaj    ← →: przyciski    A: wybierz    B / Esc: zamknij")},
            {"unsupported", trKey(L"Ten rodzaj linku nie jest obsługiwany w przeglądarce.")},
            {"load-failed", trKey(L"Nie udało się wczytać strony. Użyj Odśwież.")},
            {"downloads", trKey(L"To okno służy do czytania stron; pobieranie plików jest wyłączone.")},
            {"crashed", trKey(L"Przeglądarka została zatrzymana. Użyj Odśwież.")}};
        for (const auto& [key, polish] : texts) send({"text", key, utf8(tr(polish))});
#ifdef LAUNCHME_UI_TEST
        if (!fixture.empty()) send({"fixture", utf8(fixture)});
#endif
        navigate(url);
        send({"show"});
    }

    // The helper's window is gone (closed by the user or by the launcher, or the helper ended).
    void finish(bool restore, bool notify) {
        if (!open) return;
        open = false; ready = false;
        status = Status::closed;
        const bool had_focus = focused;
        focused = false;
        if (channel) channel->shutdown();
        if (owner_enabled && IsWindow(owner)) EnableWindow(owner, TRUE);
        if (restore && had_focus && IsWindow(owner)) { SetForegroundWindow(owner); SetFocus(owner); }
        auto pending_scripts = std::move(scripts);
        auto pending_captures = std::move(captures);
        scripts.clear(); captures.clear();
        for (auto& [id, done] : pending_scripts) done(L"");
        for (auto& [id, done] : pending_captures) done(false);
        if (notify && options.closed) options.closed();
    }

    // The launcher closes the window: the helper is told to go, then its channel is shut, which ends
    // it anyway; the reader thread reaps it (or kills its group if it hangs).
    void stop(bool restore, bool notify) {
        if (!open) return;
        send({"close"});
        finish(restore, notify);
    }

    void exited(int code) {
        pid = 0;
        if (!open) return;
        const bool came_up = ready;
        finish(true, true);
        // It never showed a page: the loader could not find WebKitGTK (126/127) or GTK/WebKit could
        // not start; the page opens in the user's browser.
        if (!came_up) fallBack(code == 126 || code == 127 || code == protocol::exit_unavailable ? Status::missing_runtime : Status::failed);
    }

    void receive(const std::vector<std::string>& fields) {
        if (!open || fields.empty()) return;
        const auto field = [&](size_t index) -> std::string_view { return index < fields.size() ? std::string_view(fields[index]) : std::string_view{}; };
        const auto verb = field(0);
        if (verb == "status") {
            if (field(1) == "ready") { status = Status::ready; ready = true; }
            else if (field(1) == "failed") status = Status::failed;
            else if (field(1) == "starting") status = Status::starting;
        } else if (verb == "loading") loading = field(1) == "1";
        else if (verb == "url") url = wide(field(1));
        else if (verb == "title") title = wide(field(1));
        else if (verb == "error") error = wide(field(1));
        else if (verb == "history") { can_back = field(1) == "1"; can_forward = field(2) == "1"; }
        else if (verb == "focus") focused = field(1) == "1";
        else if (verb == "player") {
            const std::string target(field(1));
            send({"page", target, utf8(companion::links::playerHtml(wide(target), i18n::english()))});
        } else if (verb == "result" || verb == "snapshot") {
            const int id = std::atoi(std::string(field(1)).c_str());
            const bool ok = field(2) == "1";
            if (verb == "result") {
                if (const auto found = scripts.find(id); found != scripts.end()) {
                    auto done = std::move(found->second);
                    scripts.erase(found);
                    done(ok ? wide(field(3)) : std::wstring{});
                }
            } else if (const auto found = captures.find(id); found != captures.end()) {
                auto done = std::move(found->second);
                captures.erase(found);
                done(ok);
            }
        } else if (verb == "closed") finish(true, true);
    }

    // The reader thread: the helper's lines go to the UI thread in order; at the end of the channel
    // the helper is reaped and its exit code delivered too.
    static void readChannel(std::weak_ptr<State> weak, std::shared_ptr<Channel> channel, pid_t pid) {
        const int fd = channel->fd; // Only this thread closes it.
        std::string input;
        std::vector<char> chunk(65536);
        for (;;) {
            const ssize_t got = ::read(fd, chunk.data(), chunk.size());
            if (got < 0 && errno == EINTR) continue;
            if (got <= 0) break;
            input.append(chunk.data(), static_cast<size_t>(got));
            size_t start = 0;
            for (size_t end; (end = input.find('\n', start)) != std::string::npos; start = end + 1) {
                auto fields = protocol::fields(std::string_view(input).substr(start, end - start));
                lm_window::post([weak, fields = std::move(fields)] { if (const auto state = weak.lock()) state->receive(fields); });
            }
            input.erase(0, start);
            if (input.size() > protocol::max_line) break;
        }
        channel->close();
        auto code = process_spawn::waitExit(pid, std::chrono::seconds(3));
        if (!code) { process_spawn::killGroup(pid); code = 128 + SIGKILL; }
        lm_window::post([weak, code = *code] { if (const auto state = weak.lock()) state->exited(code); });
    }
};

BrowserWindow::BrowserWindow() = default;
BrowserWindow::~BrowserWindow() { close(false); }

bool BrowserWindow::supportedUrl(const std::wstring& url) {
    if (url.empty() || url.size() > 8192 || url.find_first_of(L"\r\n\t") != std::wstring::npos) return false;
    return protocol::supportedUrl(utf8(url));
}

bool BrowserWindow::runtimeAvailable() {
    // The helper is next to the executable and loads (GTK 4 and WebKitGTK 6.0 are installed).
    static std::once_flag once;
    static bool available = false;
    std::call_once(once, [] {
        const auto helper = helperPath();
        if (!executable(helper)) return;
        pid_t pid = 0;
        if (process_spawn::spawn({helper.string(), "--check"}, process_spawn::Options{}, pid) != 0) return;
        const auto code = process_spawn::waitExit(pid, std::chrono::seconds(10));
        if (!code) { process_spawn::killGroup(pid); return; }
        available = *code == 0;
    });
    return available;
}

bool BrowserWindow::open(HWND owner, const std::wstring& url, const std::filesystem::path& profile, Options options) {
    if (!supportedUrl(url) || !IsWindow(owner) || !profile.is_absolute()) return false;
    if (isOpen()) {
        state_->navigate(url);
        if (options.activate) state_->send({"raise"});
        return true;
    }
    state_ = std::make_shared<State>();
    auto& s = *state_;
    s.owner = owner; s.url = url; s.profile = profile; s.options = std::move(options); s.low_memory = low_memory_;
#ifdef LAUNCHME_UI_TEST
    s.fixture = test_html_; s.missing = test_missing_;
#endif
    s.start();
    return s.open || static_cast<bool>(s.options.external);
}

void BrowserWindow::close(bool restore) {
    if (!state_) return;
    const auto state = state_; // finish() may run the caller's `closed`, which may reset us.
    state->stop(restore, restore);
    if (state_ == state) state_.reset();
}

bool BrowserWindow::isOpen() const { return state_ && state_->open; }
bool BrowserWindow::owns(HWND window) const { return isOpen() && state_->focused && !window; }
HWND BrowserWindow::handle() const { return nullptr; }
BrowserWindow::Status BrowserWindow::status() const { return state_ ? state_->status : Status::closed; }

void BrowserWindow::command(Command command) {
    if (!isOpen()) return;
    const auto state = state_;
    switch (command) {
    case Command::close: state->stop(true, true); break;
    case Command::back: state->send({"back"}); break;
    case Command::forward: state->send({"forward"}); break;
    case Command::reload: if (state->send({"reload"})) state->loading = true; break;
    case Command::previous_button: state->send({"button", "previous"}); break;
    case Command::next_button: state->send({"button", "next"}); break;
    case Command::accept: state->send({"button", "accept"}); break;
    }
}

void BrowserWindow::scroll(int pixels) {
    if (isOpen()) state_->send({"scroll", std::to_string(std::clamp(pixels, -1600, 1600))});
}

void BrowserWindow::setLowMemory(bool enabled) {
    low_memory_ = enabled;
    if (!state_) return;
    state_->low_memory = enabled;
    state_->send({"low-memory", enabled ? "1" : "0"});
}

#ifdef LAUNCHME_UI_TEST
void BrowserWindow::testPage(std::wstring html, bool missing) { test_html_ = std::move(html); test_missing_ = missing; }

void BrowserWindow::scriptForTest(const std::wstring& script, std::function<void(std::wstring)> result) {
    if (!isOpen()) { result(L""); return; }
    const int id = state_->next_request++;
    state_->scripts[id] = std::move(result);
    if (!state_->send({"script", std::to_string(id), utf8(script)})) {
        auto done = std::move(state_->scripts[id]);
        state_->scripts.erase(id);
        done(L"");
    }
}

std::wstring BrowserWindow::urlForTest() const { return state_ ? state_->url : L""; }
bool BrowserWindow::loadingForTest() const { return state_ && state_->loading; }
std::wstring BrowserWindow::errorForTest() const { return state_ ? state_->error : L""; }
DWORD BrowserWindow::processForTest() const { return isOpen() ? static_cast<DWORD>(state_->pid) : 0; }

void BrowserWindow::captureForTest(const std::filesystem::path& file, std::function<void(bool)> completed) {
    if (!isOpen()) { completed(false); return; }
    const int id = state_->next_request++;
    state_->captures[id] = std::move(completed);
    if (!state_->send({"snapshot", std::to_string(id), "page", file.string()})) {
        auto done = std::move(state_->captures[id]);
        state_->captures.erase(id);
        done(false);
    }
}
#endif
