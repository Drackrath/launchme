// Linux BrowserWindow: the helper protocol, URL rules and the missing-runtime fallback always; the
// WebKitGTK window itself (navigation, scrolling, the served YouTube player, closing) when the
// launchme-web helper is built and a display is available (otherwise 77: skipped).
#include "../src/ui/browser_window.hpp"
#include "../src/ui/browser_protocol.hpp"
#include "../src/i18n/i18n.hpp"
#include "../src/net/companion_links.hpp"
#include <chrono>
#include <cstdlib>
#include <cwchar>
#include <iostream>
#include <optional>
#include <signal.h>
#include <thread>
#include <unistd.h>

namespace {
bool wait(BrowserWindow& browser, const std::function<bool()>& done, int timeout = 15000) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
    do {
        browser.pump();
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < end);
    return false;
}
std::wstring query(BrowserWindow& browser, const std::wstring& script) {
    auto value = std::make_shared<std::optional<std::wstring>>();
    browser.scriptForTest(script, [value](std::wstring result) { *value = std::move(result); });
    if (!wait(browser, [&] { return value->has_value(); })) return {};
    return **value;
}
bool alive(uint32_t pid) { return pid && ::kill(static_cast<pid_t>(pid), 0) == 0; }
}

int main() {
    using namespace browser_protocol;
    // Protocol.
    const std::string awkward = "tab\there\\back\nline\rend";
    if (unescape(escape(awkward)) != awkward || escape(awkward).find_first_of("\t\n\r") != std::string::npos) return 1;
    const auto parsed = fields(line({"page", awkward, "", "1"}).substr(0, line({"page", awkward, "", "1"}).size() - 1));
    if (parsed.size() != 4 || parsed[1] != awkward || !parsed[2].empty() || parsed[3] != "1") return 2;
    std::string buffer = "a\tb\nc\npartial";
    const auto lines = takeLines(buffer);
    if (lines.size() != 2 || lines[0] != "a\tb" || lines[1] != "c" || buffer != "partial") return 3;

    // URL rules, as WinHttpCrackUrl applies them on Windows.
    for (const auto* url : {L"file:///C:/test", L"javascript:alert(1)", L"https://user:password@example.com/", L"steam://run/10",
             L"https://", L"https:///path", L"https://exa mple.com/", L"https://example.com:80x/", L"https://example.com/\nx"})
        if (BrowserWindow::supportedUrl(url)) return 4;
    for (const auto* url : {L"https://store.steampowered.com/news/", L"http://127.0.0.1:32100/", L"HTTPS://Example.com", L"https://[::1]:8080/x?y#z"})
        if (!BrowserWindow::supportedUrl(url)) return 5;
    if (webHost("https://launchme.test/first") != "launchme.test" || webHost("https://launchme.invalid:443/youtube/x") != "launchme.invalid") return 6;

    i18n::setLanguage(i18n::Language::pl);
    const auto root = std::filesystem::temp_directory_path() / ("launchme-browser-test-" + std::to_string(::getpid()));
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code error; std::filesystem::remove_all(path, error); } } cleanup{root};
    int owner_token = 0;
    void* owner = &owner_token;
    int closes = 0, external = 0, wakes = 0;
    std::wstring external_url;
    BrowserWindow::Options options;
    options.activate = false;
    options.closed = [&] { ++closes; };
    options.external = [&](const std::wstring& url) { ++external; external_url = url; };
    options.wake = [&] { ++wakes; };

    // No helper: the page goes to the user's own browser.
    {
        BrowserWindow browser;
        browser.testPage(L"", true);
        if (browser.open(owner, L"file:///etc/passwd", root / "profile", options)) return 7;
        if (browser.open(owner, L"https://launchme.test/", "relative", options)) return 8;
        if (!browser.open(owner, L"https://launchme.test/", root / "profile", options) || browser.status() != BrowserWindow::Status::missing_runtime) return 9;
        if (browser.isOpen() || browser.handle() || external != 1 || external_url != L"https://launchme.test/") return 10;
        browser.command(BrowserWindow::Command::accept); browser.pump(); browser.close();
        if (closes != 0) return 11;
    }
    if (!BrowserWindow::runtimeAvailable()) { std::cout << "Protocol and missing-runtime fallback passed; launchme-web not built (WebKitGTK 6.0)\n"; return 77; }
    if (!std::getenv("WAYLAND_DISPLAY") && !std::getenv("DISPLAY")) { std::cout << "Protocol and missing-runtime fallback passed; no display for the WebKitGTK window\n"; return 77; }

    BrowserWindow browser;
    browser.testPage(LR"(<!doctype html><html><head><title>Launchme browser fixture</title><style>body{background:#151824;color:#fff;font:24px system-ui;margin:40px;min-height:6000px}a{color:#abe}#inner{display:none}</style></head><body><h1>Glance · WebKitGTK</h1><p>Controlled local response. Scroll with the controller; B/Esc closes.</p><a href="/second">Next page</a><div id="inner"><div style="height:5000px">Nested scrolling area</div></div></body></html>)");
    if (!browser.open(owner, L"https://launchme.test/first", root / "profile", options) || !browser.isOpen() || !browser.owns(browser.handle()) || browser.owns(owner)) return 12;
    if (!wait(browser, [&] { return !browser.isOpen() || browser.status() == BrowserWindow::Status::missing_runtime ||
            (browser.status() == BrowserWindow::Status::ready && !browser.loadingForTest() && browser.urlForTest().ends_with(L"/first")); }, 30000)) return 13;
    if (browser.status() == BrowserWindow::Status::missing_runtime) {
        // The helper could not start (its libraries or the display): the page went to the default browser.
        if (browser.isOpen() || external != 2 || external_url != L"https://launchme.test/first" || closes != 1) return 43;
        std::cout << "Protocol and fallbacks passed; launchme-web could not start here\n";
        return 77;
    }
    if (!browser.isOpen()) return 44; // The helper started and then ended (see its messages on stderr).
    if (!browser.errorForTest().empty()) return 14;
    if (query(browser, L"document.title") != L"\"Launchme browser fixture\"") return 15;
    if (query(browser, L"location.origin") != L"\"https://launchme.test\"") return 16;
    browser.scroll(600);
    if (!wait(browser, [&] { return std::wcstol(query(browser, L"window.scrollY").c_str(), nullptr, 10) > 0; })) return 17;
    query(browser, L"window.scrollTo(0,0);document.querySelector('#inner').style.cssText='display:block;position:fixed;inset:0;overflow-y:auto;background:#202532'");
    browser.scroll(400);
    if (!wait(browser, [&] { return std::wcstol(query(browser, L"document.querySelector('#inner').scrollTop").c_str(), nullptr, 10) > 0; })) return 18;
    query(browser, L"location.href='https://launchme.test/second'");
    if (!wait(browser, [&] { return browser.urlForTest().ends_with(L"/second") && !browser.loadingForTest(); })) return 19;
    if (query(browser, L"document.title") != L"\"Launchme browser fixture\"") return 20;
    browser.command(BrowserWindow::Command::back);
    if (!wait(browser, [&] { return browser.urlForTest().ends_with(L"/first") && !browser.loadingForTest(); })) return 21;
    query(browser, L"history.pushState({},'', '/spa')");
    if (!wait(browser, [&] { return browser.urlForTest().ends_with(L"/spa"); })) return 22;
    browser.command(BrowserWindow::Command::back);
    if (!wait(browser, [&] { return browser.urlForTest().ends_with(L"/first"); })) return 23;
    // Store and local links never leave the page (WebKit itself refuses file: from a web page).
    query(browser, L"location.href='steam://run/10'");
    if (!wait(browser, [&] { return !browser.errorForTest().empty(); }) || !browser.urlForTest().ends_with(L"/first")) return 24;
    query(browser, L"location.href='file:///etc/hostname'");
    wait(browser, [] { return false; }, 500);
    if (!browser.urlForTest().ends_with(L"/first") || query(browser, L"location.protocol") != L"\"https:\"") return 41;

    if (const char* folder = std::getenv("LAUNCHME_UI_CAPTURE_DIR")) {
        auto captured = std::make_shared<std::optional<bool>>();
        browser.captureForTest(std::filesystem::path(folder) / "browser-fixture-linux.png", [captured](bool ok) { *captured = ok; });
        if (!wait(browser, [&] { return captured->has_value(); }) || !**captured) return 25;
        auto window = std::make_shared<std::optional<bool>>();
        browser.captureWindowForTest(std::filesystem::path(folder) / "browser-window-linux.png", [window](bool ok) { *window = ok; });
        if (!wait(browser, [&] { return window->has_value(); }) || !**window) return 42;
    }

    // The genuine served player, under https://launchme.invalid.
    const auto player = companion::links::playerUrl("AbCdEF_12-3");
    if (!browser.open(owner, player, root / "profile", options) || !wait(browser, [&] { return browser.urlForTest() == player && !browser.loadingForTest(); })) return 26;
    if (query(browser, L"document.querySelector('iframe').src").find(L"youtube-nocookie.com/embed/AbCdEF_12-3") == std::wstring::npos) return 27;
    if (query(browser, L"location.origin") != L"\"https://launchme.invalid\"") return 28;
    if (query(browser, L"document.querySelector('a').href") != L"\"https://www.youtube.com/watch?v=AbCdEF_12-3\"") return 29;
    query(browser, L"window.launchmeToggle=()=>window.testToggle=true");
    browser.command(BrowserWindow::Command::accept);
    if (!wait(browser, [&] { return query(browser, L"window.testToggle") == L"true"; })) return 30;
    browser.command(BrowserWindow::Command::previous_button); // Refresh, then right returns to play.
    browser.command(BrowserWindow::Command::next_button);
    query(browser, L"window.testToggle=false");
    browser.command(BrowserWindow::Command::accept);
    if (!wait(browser, [&] { return query(browser, L"window.testToggle") == L"true"; })) return 31;
    browser.command(BrowserWindow::Command::reload);
    if (!wait(browser, [&] { return query(browser, L"document.title") == L"\"YouTube — Launchme\"" && !browser.loadingForTest(); })) return 32;
    if (query(browser, L"window.testToggle") == L"true") return 33; // A fresh page.

    const auto pid = browser.processForTest();
    if (!alive(pid) || wakes == 0) return 34;
    browser.command(BrowserWindow::Command::close);
    if (!wait(browser, [&] { return !browser.isOpen(); }) || closes != 1) return 35;
    if (!wait(browser, [&] { return !alive(pid); }, 10000)) return 36;

    // Closing while the helper starts: no window comes back, and the launcher hears it once.
    if (!browser.open(owner, L"https://launchme.test/late", root / "profile-late", options)) return 37;
    const auto late = browser.processForTest();
    browser.close();
    wait(browser, [] { return false; }, 1000);
    if (browser.isOpen() || closes != 2) return 38;
    if (!wait(browser, [&] { return !alive(late); }, 10000)) return 39;
    // Persistent website data went into the profile folder.
    std::error_code error;
    if (!std::filesystem::is_directory(root / "profile" / "data", error) || !std::filesystem::is_directory(root / "profile" / "cache", error)) return 40;
    std::cout << "WebKitGTK browser: navigation, scrolling, served player, blocked schemes, close and late close passed\n";
}
