// The reader window on Linux (src/ui/browser_window_linux.cpp) with the real launchme-browser helper
// (GTK 4, WebKitGTK 6.0), as tests/browser_window_tests.cpp checks the WebView2 one. Pages come from
// the helper's loopback fixture proxy (http:// only), never from the network; `--live` opens
// https://example.com instead. Skipped (77) without a display or without the helper/WebKitGTK.
#include "../src/ui/browser_window.hpp"
#include "../src/i18n/i18n.hpp"
#include "../src/net/companion_links.hpp"

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cwchar>
#include <iostream>
#include <memory>
#include <optional>
#include <string_view>
#include <unistd.h>

namespace {
bool wait(const std::function<bool()>& done, DWORD timeout = 15000) {
    const auto end = GetTickCount64() + timeout;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        if (done()) return true;
        Sleep(10);
    } while (GetTickCount64() < end);
    return false;
}

std::wstring query(BrowserWindow& browser, const std::wstring& script) {
    auto value = std::make_shared<std::optional<std::wstring>>();
    browser.scriptForTest(script, [value](std::wstring result) { *value = std::move(result); });
    if (!wait([&] { return value->has_value(); })) return {};
    return **value;
}

long number(BrowserWindow& browser, const std::wstring& script) { return std::wcstol(query(browser, script).c_str(), nullptr, 10); }

bool exited(pid_t pid) { return pid > 0 && ::kill(pid, 0) != 0 && errno == ESRCH; }
}

int main(int argc, char** argv) {
    i18n::setLanguage(i18n::Language::pl);
    const bool live = argc == 2 && std::string_view(argv[1]) == "--live";
    if (!std::getenv("DISPLAY") && !std::getenv("WAYLAND_DISPLAY")) { std::cout << "No display: skipped\n"; return 77; }
    const auto root = std::filesystem::temp_directory_path() / ("launchme-browser-test-" + std::to_string(getpid()));
    WNDCLASSW type{}; type.lpfnWndProc = DefWindowProcW; type.hInstance = GetModuleHandleW(nullptr); type.lpszClassName = L"LaunchmeBrowserTestOwner";
    RegisterClassW(&type);
    const HWND owner = CreateWindowW(type.lpszClassName, L"Test", WS_OVERLAPPEDWINDOW, 20, 20, 1100, 800, nullptr, nullptr, type.hInstance, nullptr);
    if (!owner) { std::cout << "No SDL window: skipped\n"; return 77; }
    struct Cleanup {
        HWND owner; std::filesystem::path path;
        ~Cleanup() { DestroyWindow(owner); std::error_code error; std::filesystem::remove_all(path, error); }
    } cleanup{owner, root};

    BrowserWindow browser;
    int closes = 0, external = 0;
    std::wstring opened_externally;
    BrowserWindow::Options options; options.activate = false; options.closed = [&] { ++closes; };
    options.external = [&](const std::wstring& url) { ++external; opened_externally = url; };
    for (const auto* url : {L"file:///home/test", L"javascript:alert(1)", L"https://user:password@example.com/", L"steam://run/10", L"https://", L"http://a\nb/"})
        if (BrowserWindow::supportedUrl(url)) return 1;
    if (!BrowserWindow::supportedUrl(L"https://store.steampowered.com/news/") || !BrowserWindow::supportedUrl(L"http://127.0.0.1:32100/") ||
        !BrowserWindow::supportedUrl(L"HTTPS://[::1]:8080/a?b#c")) return 2;

    // Without the helper (or WebKitGTK) the page opens in the user's browser instead.
    browser.testPage(L"", true);
    if (!browser.open(owner, L"https://launchme.test/", root / "profile", options) || browser.status() != BrowserWindow::Status::missing_runtime) return 3;
    if (browser.isOpen() || !IsWindowEnabled(owner) || external != 1 || opened_externally != L"https://launchme.test/" || closes != 0) return 4;
    browser.testPage(L"", false);
    if (argc == 2 && std::string_view(argv[1]) == "--fallback") {
        // Run next to a launchme-browser that dies before its window shows (as the loader does,
        // 127, without WebKitGTK): the page goes to the user's browser once the helper is gone.
        if (!browser.open(owner, L"http://launchme.test/fallback", root / "profile", options) || !browser.isOpen()) return 50;
        if (!wait([&] { return !browser.isOpen(); }) || browser.status() != BrowserWindow::Status::missing_runtime) return 51;
        if (external != 2 || opened_externally != L"http://launchme.test/fallback" || closes != 1 || !IsWindowEnabled(owner)) return 52;
        std::cout << "Helper failing at start: fallback to the user's browser passed\n";
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--focus") {
        // Manual (focus is the compositor's choice): an activated window owns the pad while it is
        // the active window, which the SDL layer reports as no foreground window of the launcher's.
        BrowserWindow::Options front = options; front.activate = true;
        browser.testPage(L"<!doctype html><title>Focus</title><body style='background:#202532;color:#fff'>Focus</body>");
        if (!browser.open(owner, L"http://launchme.test/focus", root / "profile", front)) return 60;
        if (!wait([&] { return browser.owns(nullptr); }, 15000)) return 61;
        if (GetForegroundWindow() || !browser.owns(GetForegroundWindow()) || browser.owns(owner)) return 62;
        browser.command(BrowserWindow::Command::close);
        if (browser.owns(nullptr)) return 63;
        std::cout << "Focused helper window owns the pad passed\n";
        return 0;
    }
    if (!BrowserWindow::runtimeAvailable()) { std::cout << "Missing-runtime fallback passed; launchme-browser/WebKitGTK unavailable: skipped\n"; return 77; }

    browser.testPage(live ? L"" : LR"(<!doctype html><html><head><title>Launchme browser fixture</title><style>body{background:#151824;color:#fff;font:24px sans-serif;margin:40px;min-height:6000px}a{color:#abe}#inner{display:none}</style></head><body><h1>Glance · WebKitGTK</h1><p>Controlled local response. Scroll with the controller; B/Esc closes.</p><a href="/second">Next page</a><div id="inner"><div style="height:5000px">Nested scrolling area</div></div></body></html>)");
    if (!browser.open(owner, live ? L"https://example.com/" : L"http://launchme.test/first", root / "profile", options)) return 6;
    if (!browser.isOpen() || IsWindowEnabled(owner)) return 5;
    if (!wait([&] { return browser.status() == BrowserWindow::Status::ready && !browser.loadingForTest(); }, 30000)) return 7;
    if (!browser.errorForTest().empty()) return 17;
    if (!live) {
        if (query(browser, L"document.title") != L"\"Launchme browser fixture\"") return 8;
        browser.scroll(600);
        if (!wait([&] { return number(browser, L"window.scrollY") > 0; })) return 9;
        query(browser, L"window.scrollTo(0,0);document.querySelector('#inner').style.cssText='display:block;position:fixed;inset:0;overflow-y:auto;background:#202532'");
        browser.scroll(400);
        if (!wait([&] { return number(browser, L"document.querySelector('#inner').scrollTop") > 0; })) return 10;
        query(browser, L"document.querySelector('#inner').style.cssText=''");
        query(browser, L"location.href='http://launchme.test/second'");
        if (!wait([&] { return browser.urlForTest().ends_with(L"/second") && !browser.loadingForTest(); })) return 11;
        browser.command(BrowserWindow::Command::back);
        if (!wait([&] { return browser.urlForTest().ends_with(L"/first") && !browser.loadingForTest(); })) return 12;
        query(browser, L"history.pushState({},'', '/spa')");
        if (!wait([&] { return browser.urlForTest().ends_with(L"/spa"); })) return 21;
        browser.command(BrowserWindow::Command::back);
        if (!wait([&] { return browser.urlForTest().ends_with(L"/first"); })) return 22;
        browser.command(BrowserWindow::Command::forward);
        if (!wait([&] { return browser.urlForTest().ends_with(L"/spa"); })) return 36;
        // Pages see the dark preference; links to other programs stay out of the reader window.
        if (query(browser, L"matchMedia('(prefers-color-scheme: dark)').matches") != L"true") return 37;
        query(browser, L"location.href='steam://run/10'");
        if (!wait([&] { return browser.errorForTest() == tr(L"Ten rodzaj linku nie jest obsługiwany w przeglądarce."); }) ||
            !browser.urlForTest().ends_with(L"/spa")) return 38;
        browser.command(BrowserWindow::Command::reload);
        if (!wait([&] { return browser.errorForTest().empty() && !browser.loadingForTest(); })) return 39;
    } else {
        if (!wait([&] { return number(browser, L"document.body.innerText.length") > 100; }, 20000)) return 19;
    }
    if (const char* folder = std::getenv("LAUNCHME_UI_CAPTURE_DIR")) {
        auto captured = std::make_shared<std::optional<bool>>();
        browser.captureForTest(std::filesystem::path(folder) / (live ? "browser-live.png" : "browser-fixture.png"), [captured](bool ok) { *captured = ok; });
        if (!wait([&] { return captured->has_value(); }) || !**captured) return 13;
    }
    if (!live) {
        // The genuine launcher-served player document, under its https origin; its external
        // resources fail at the fixture proxy.
        const auto player = companion::links::playerUrl("AbCdEF_12-3");
        if (!browser.open(owner, player, root / "profile", options) ||
            !wait([&] { return browser.urlForTest() == player && !browser.loadingForTest(); })) return 23;
        if (query(browser, L"document.querySelector('iframe').src").find(L"youtube-nocookie.com/embed/AbCdEF_12-3") == std::wstring::npos) return 24;
        if (query(browser, L"location.origin") != L"\"https://launchme.invalid\"") return 25;
        if (query(browser, L"document.querySelector('a').href") != L"\"https://www.youtube.com/watch?v=AbCdEF_12-3\"") return 26;
        query(browser, L"window.launchmeToggle=()=>window.testToggle=true");
        browser.command(BrowserWindow::Command::accept);
        if (!wait([&] { return query(browser, L"window.testToggle") == L"true"; })) return 28;
        browser.command(BrowserWindow::Command::previous_button); // Reload, then right returns to play.
        browser.command(BrowserWindow::Command::next_button);
        query(browser, L"window.testToggle=false");
        browser.command(BrowserWindow::Command::accept);
        if (!wait([&] { return query(browser, L"window.testToggle") == L"true"; })) return 29;
        browser.command(BrowserWindow::Command::reload);
        if (!wait([&] { return !browser.loadingForTest() && query(browser, L"document.title") == L"\"YouTube \u2014 Launchme\""; })) return 27;
        if (query(browser, L"typeof window.testToggle") != L"\"undefined\"") return 30; // A fresh document.
        browser.command(BrowserWindow::Command::back);
        if (!wait([&] { return browser.urlForTest().starts_with(L"http://launchme.test/") && !browser.loadingForTest(); })) return 31;
        // Low memory (no page cache, smallest caches) applies to the open page.
        browser.setLowMemory(true);
        if (query(browser, L"document.title") != L"\"Launchme browser fixture\"") return 32;
        browser.setLowMemory(false);
        // A page that cannot load (https stops at the fixture proxy) says so, also after Reload.
        const auto failed = tr(L"Nie udało się wczytać strony. Użyj Odśwież.");
        if (!browser.open(owner, L"https://launchme.test/unreachable", root / "profile", options) ||
            !wait([&] { return browser.errorForTest() == failed && !browser.loadingForTest(); })) return 43;
        browser.command(BrowserWindow::Command::reload);
        if (!wait([&] { return browser.errorForTest() == failed && !browser.loadingForTest(); }) ||
            browser.urlForTest() != L"https://launchme.test/unreachable") return 44;
    }
    const auto pid = static_cast<pid_t>(browser.processForTest());
    if (pid <= 0) return 33;
    browser.command(BrowserWindow::Command::close);
    if (browser.isOpen() || !IsWindowEnabled(owner) || closes != 1) return 14;
    if (!wait([&] { return exited(pid); }, 10000)) return 18;
    // Closing at once after opening cannot leave a window or a helper behind.
    if (!browser.open(owner, L"http://launchme.test/late", root / "profile-late", options)) return 15;
    const auto late = static_cast<pid_t>(browser.processForTest());
    browser.close();
    if (browser.isOpen() || !IsWindowEnabled(owner) || closes != 2) return 16;
    if (!wait([&] { return exited(late); }, 10000)) return 34;
    // The user closing the window (Escape in the helper) reports back.
    if (!browser.open(owner, L"http://launchme.test/escape", root / "profile", options) ||
        !wait([&] { return browser.status() == BrowserWindow::Status::ready && !browser.loadingForTest(); }, 30000)) return 35;
    query(browser, L"0"); // The window is up and answering.
    const auto escape = static_cast<pid_t>(browser.processForTest());
    browser.command(BrowserWindow::Command::next_button); // Close stays chosen; A presses it in the helper.
    browser.command(BrowserWindow::Command::accept);
    if (!wait([&] { return !browser.isOpen(); }) || closes != 3 || !IsWindowEnabled(owner)) return 41;
    if (!wait([&] { return exited(escape); }, 10000)) return 42;
    std::cout << (live ? "WebKitGTK browser: live page, close and cleanup passed\n"
                       : "WebKitGTK browser: navigation, scrolling, player, close, missing runtime and helper exit passed\n");
    return 0;
}
