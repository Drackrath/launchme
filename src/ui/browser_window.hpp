#pragma once
#ifdef _WIN32
#include <Windows.h>
#else
#include <cstdint>
#endif
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

// An owned top-level reader window (Glance articles, store pages, the TV YouTube player).
// Windows (browser_window_windows.cpp): WebView2 in a popup owned by the launcher's HWND. All methods
// and COM callbacks run on the UI STA; asynchronous initialization retains weak state, never the
// Launcher or a destroyed HWND.
// Linux (browser_window_linux.cpp): WebKitGTK 6.0 in a GTK4 window of its own, run by the
// `launchme-web` helper process (src/linux/web_helper.cpp) so GTK never shares the launcher's
// process or main loop with SDL. Events from the helper are applied by pump(), on the UI thread;
// Options::wake tells the app loop (from another thread) that pump() has work.
class BrowserWindow {
public:
#ifdef _WIN32
    using NativeWindow = HWND;
    using ProcessId = DWORD;
#else
    using NativeWindow = void*; // Only compared: the launcher's window (SDL_Window*), or what handle() returned.
    using ProcessId = uint32_t;
#endif
    enum class Command { back, forward, reload, close, previous_button, next_button, accept };
    enum class Status { closed, starting, ready, missing_runtime, failed };
    struct Options {
        std::function<void()> closed;
        std::function<void(const std::wstring&)> external;
        bool activate = true;
#ifndef _WIN32
        // Called on the helper's reader thread when pump() has events to apply; it must only wake
        // the app loop (for example SDL_PushEvent), never touch launcher state.
        std::function<void()> wake;
        // Wayland: an xdg-activation token for the helper's first window (XDG_ACTIVATION_TOKEN).
        std::string activation_token;
#endif
    };
    BrowserWindow();
    ~BrowserWindow();
    BrowserWindow(const BrowserWindow&) = delete;
    BrowserWindow& operator=(const BrowserWindow&) = delete;
    bool open(NativeWindow owner, const std::wstring& url, const std::filesystem::path& profile, Options options);
    void close(bool restore = true);
    bool isOpen() const;
    bool owns(NativeWindow window) const;
    NativeWindow handle() const;
    // The reader window has keyboard focus (on Windows: it or a child is the foreground window).
    bool focused() const;
    Status status() const;
    void command(Command command);
    void scroll(int pixels);
    // Applies what the window reported since the last call (no-op on Windows, where its messages
    // arrive through the launcher's own message loop). Callbacks in Options run from here.
    void pump();
    // AppSettings::low_memory: while inactive the page is asked to use less memory
    // (MemoryUsageTargetLevel), and minimized it is hidden and suspended until shown again.
    void setLowMemory(bool enabled);
    static bool supportedUrl(const std::wstring& url);
    static bool runtimeAvailable();
#ifdef LAUNCHME_UI_TEST
    void testPage(std::wstring html, bool simulate_missing = false);
    void scriptForTest(const std::wstring& script, std::function<void(std::wstring)> result);
    std::wstring urlForTest() const;
    bool loadingForTest() const;
    std::wstring errorForTest() const;
    ProcessId processForTest() const;
    void captureForTest(const std::filesystem::path& file, std::function<void(bool)> completed);
#ifndef _WIN32
    void captureWindowForTest(const std::filesystem::path& file, std::function<void(bool)> completed); // Toolbar and page.
#endif
#endif
private:
    struct State;
    std::shared_ptr<State> state_;
    bool low_memory_ = false;
#ifdef LAUNCHME_UI_TEST
    std::wstring test_html_;
    bool test_missing_ = false;
#endif
};
