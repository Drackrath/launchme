#pragma once
#include <Windows.h>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

// An owned top-level reader window. All methods and COM callbacks run on the UI STA;
// asynchronous initialization retains weak state, never the Launcher or a destroyed HWND.
class BrowserWindow {
public:
    enum class Command { back, forward, reload, close, previous_button, next_button, accept };
    enum class Status { closed, starting, ready, missing_runtime, failed };
    struct Options {
        std::function<void()> closed;
        std::function<void(const std::wstring&)> external;
        bool activate = true;
    };
    BrowserWindow();
    ~BrowserWindow();
    BrowserWindow(const BrowserWindow&) = delete;
    BrowserWindow& operator=(const BrowserWindow&) = delete;
    bool open(HWND owner, const std::wstring& url, const std::filesystem::path& profile, Options options);
    void close(bool restore = true);
    bool isOpen() const;
    bool owns(HWND window) const;
    HWND handle() const;
    Status status() const;
    void command(Command command);
    void scroll(int pixels);
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
    DWORD processForTest() const;
    void captureForTest(const std::filesystem::path& file, std::function<void(bool)> completed);
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
