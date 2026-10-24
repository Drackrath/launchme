#include "../src/ui/browser_window.hpp"
#include "../src/i18n/i18n.hpp"
#include "../src/net/companion_links.hpp"
#include <objbase.h>
#include <iostream>
#include <optional>
#include <cwchar>
#include <wincodec.h>
#include <wrl/client.h>

namespace {
bool wait(const std::function<bool()>& done, DWORD timeout=15000) {
    const auto end=GetTickCount64()+timeout;
    do {
        MSG message{};
        while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        if (done()) return true;
        Sleep(10);
    } while (GetTickCount64()<end);
    return false;
}
std::wstring query(BrowserWindow& browser,const std::wstring& script) {
    auto value=std::make_shared<std::optional<std::wstring>>();
    browser.scriptForTest(script,[value](std::wstring result) { *value=std::move(result); });
    if (!wait([&] { return value->has_value(); })) return {};
    return **value;
}
bool captureWindow(HWND window,const std::filesystem::path& file) {
    RECT r{}; GetWindowRect(window,&r);
    HDC screen=GetDC(window), memory=CreateCompatibleDC(screen);
    HBITMAP bitmap=CreateCompatibleBitmap(screen,r.right-r.left,r.bottom-r.top);
    const auto old=SelectObject(memory,bitmap);
    const bool drawn=PrintWindow(window,memory,2)!=FALSE;
    SelectObject(memory,old); DeleteDC(memory); ReleaseDC(window,screen);
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory; ComPtr<IWICBitmap> source; ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder; ComPtr<IWICBitmapFrameEncode> frame;
    const bool result=drawn && SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory->CreateBitmapFromHBITMAP(bitmap,nullptr,WICBitmapIgnoreAlpha,&source)) && SUCCEEDED(factory->CreateStream(&stream)) &&
        SUCCEEDED(stream->InitializeFromFilename(file.c_str(),GENERIC_WRITE)) && SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder)) &&
        SUCCEEDED(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache)) && SUCCEEDED(encoder->CreateNewFrame(&frame,nullptr)) &&
        SUCCEEDED(frame->Initialize(nullptr)) && SUCCEEDED(frame->WriteSource(source.Get(),nullptr)) && SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
    DeleteObject(bitmap); return result;
}
}
int main(int argc,char** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    i18n::setLanguage(i18n::Language::pl);
    const bool live_video=argc==2 && std::string_view(argv[1])=="--live-youtube";
    const bool live=live_video || (argc==2 && std::string_view(argv[1])=="--live");
    const auto root=std::filesystem::temp_directory_path()/(L"launchme-browser-test-"+std::to_wstring(GetCurrentProcessId()));
    WNDCLASSW type{}; type.lpfnWndProc=DefWindowProcW; type.hInstance=GetModuleHandleW(nullptr); type.lpszClassName=L"LaunchmeBrowserTestOwner"; RegisterClassW(&type);
    const auto owner=CreateWindowW(type.lpszClassName,L"Test",WS_OVERLAPPEDWINDOW,20,20,live ? 2200 : 1100,live ? 1400 : 800,nullptr,nullptr,type.hInstance,nullptr);
    struct Cleanup { HWND owner; std::filesystem::path path; ~Cleanup() { DestroyWindow(owner); for (int i=0;i<20;++i) { std::error_code error; std::filesystem::remove_all(path,error); if (!error) break; Sleep(100); } CoUninitialize(); } } cleanup{owner,root};
    BrowserWindow browser;
    int closes=0, external=0;
    BrowserWindow::Options options; options.activate=false; options.closed=[&] { ++closes; };
    options.external=[&](const auto& url) { if (url==L"https://developer.microsoft.com/microsoft-edge/webview2/consumer/") ++external; };
    for (const auto* url : {L"file:///C:/test",L"javascript:alert(1)",L"https://user:password@example.com/",L"steam://run/10"})
        if (BrowserWindow::supportedUrl(url)) return 1;
    if (!BrowserWindow::supportedUrl(L"https://store.steampowered.com/news/") || !BrowserWindow::supportedUrl(L"http://127.0.0.1:32100/")) return 2;
    browser.testPage(L"",true);
    if (!browser.open(owner,L"https://launchme.test/",root/L"profile",options) || browser.status()!=BrowserWindow::Status::missing_runtime || IsWindowEnabled(owner)) return 3;
    browser.command(BrowserWindow::Command::next_button); browser.command(BrowserWindow::Command::accept);
    if (external!=1) return 4;
    PostMessageW(browser.handle(),WM_KEYDOWN,VK_ESCAPE,0);
    if (!wait([&] { return !browser.isOpen(); }) || closes!=1 || !IsWindowEnabled(owner)) return 5;
    if (!BrowserWindow::runtimeAvailable()) { std::cout << "Missing-runtime UI passed; Evergreen integration unavailable\n"; return 77; }
    browser.testPage(live ? L"" : LR"(<!doctype html><html><head><title>Launchme browser fixture</title><style>body{background:#151824;color:#fff;font:24px system-ui;margin:40px;min-height:6000px}a{color:#abe}#inner{display:none}</style></head><body><h1>Glance · WebView2</h1><p>Controlled local response. Scroll with the controller; B/Esc closes.</p><a href="/second">Next page</a><div id="inner"><div style="height:5000px">Nested scrolling area</div></div></body></html>)");
    if (!browser.open(owner,live_video ? companion::links::playerUrl("jNQXAC9IVRw") : live ? L"https://store.steampowered.com/news/app/570/" : L"https://launchme.test/first",root/L"profile",options)) return 6;
    if (!wait([&] { return browser.status()==BrowserWindow::Status::ready && !browser.loadingForTest(); },30000)) return 7;
    if (!browser.errorForTest().empty()) return 17;
    if (!live) {
        if (query(browser,L"document.title")!=L"\"Launchme browser fixture\"") return 8;
        browser.scroll(600);
        if (!wait([&] { return std::wcstol(query(browser,L"window.scrollY").c_str(),nullptr,10)>0; })) return 9;
        query(browser,L"window.scrollTo(0,0);document.querySelector('#inner').style.cssText='display:block;position:fixed;inset:0;overflow-y:auto;background:#202532'");
        browser.scroll(400);
        if (!wait([&] { return std::wcstol(query(browser,L"document.querySelector('#inner').scrollTop").c_str(),nullptr,10)>0; })) return 10;
        query(browser,L"location.href='https://launchme.test/second'");
        if (!wait([&] { return browser.urlForTest().ends_with(L"/second") && !browser.loadingForTest(); })) return 11;
        browser.command(BrowserWindow::Command::back);
        if (!wait([&] { return browser.urlForTest().ends_with(L"/first") && !browser.loadingForTest(); })) return 12;
        query(browser,L"history.pushState({},'', '/spa')");
        if (!wait([&] { return browser.urlForTest().ends_with(L"/spa"); })) return 21;
        browser.command(BrowserWindow::Command::back);
        if (!wait([&] { return browser.urlForTest().ends_with(L"/first"); })) return 22;
    } else if (live_video) {
        if (!wait([&] { const auto result=query(browser,L"document.documentElement.dataset.player || ''"); return result==L"\"ready\"" || result.find(L"error:")!=result.npos; },20000)) return 30;
        if (query(browser,L"document.documentElement.dataset.player")!=L"\"ready\"") return 31;
        browser.command(BrowserWindow::Command::accept);
        if (!wait([&] { return query(browser,L"document.documentElement.dataset.state")==L"\"1\""; },15000)) return 32;
        browser.command(BrowserWindow::Command::accept);
        if (!wait([&] { return query(browser,L"document.documentElement.dataset.state")==L"\"2\""; })) return 33;
    } else {
        if (!wait([&] { return std::wcstol(query(browser,L"document.body.innerText.length").c_str(),nullptr,10)>500; },20000)) return 19;
        wait([] { return false; },2000); // Site-rendered news/images arrive after document navigation.
    }
    wchar_t folder[4096]{};
    if (GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR",folder,4096)) {
        auto captured=std::make_shared<std::optional<bool>>();
        browser.captureForTest(std::filesystem::path(folder)/(live ? L"browser-steam.png" : L"browser-fixture.png"),[captured](bool ok) { *captured=ok; });
        if (!wait([&] { return captured->has_value(); }) || !**captured) return 13;
        if (!captureWindow(browser.handle(),std::filesystem::path(folder)/(live ? L"browser-steam-window.png" : L"browser-window.png"))) return 20;
    }
    if (!live) {
        // The genuine locally served wrapper, with external iframe resources fulfilled by the fixture.
        const auto player = companion::links::playerUrl("AbCdEF_12-3");
        if (!browser.open(owner,player,root/L"profile",options) || !wait([&] { return !browser.loadingForTest(); })) return 23;
        if (query(browser,L"document.querySelector('iframe').src").find(L"youtube-nocookie.com/embed/AbCdEF_12-3") == std::wstring::npos) return 24;
        if (query(browser,L"location.origin") != L"\"https://launchme.invalid\"") return 25;
        if (query(browser,L"document.querySelector('a').href") != L"\"https://www.youtube.com/watch?v=AbCdEF_12-3\"") return 26;
        query(browser,L"window.launchmeToggle=()=>window.testToggle=true");
        browser.command(BrowserWindow::Command::accept);
        if (!wait([&] { return query(browser,L"window.testToggle") == L"true"; })) return 28;
        browser.command(BrowserWindow::Command::previous_button); // Reload, then right returns to play.
        browser.command(BrowserWindow::Command::next_button);
        query(browser,L"window.testToggle=false"); browser.command(BrowserWindow::Command::accept);
        if (!wait([&] { return query(browser,L"window.testToggle") == L"true"; })) return 29;
        const float scale=static_cast<float>(GetDpiForWindow(browser.handle()))/96.0f;
        POINT play{static_cast<LONG>(330*scale),static_cast<LONG>(41*scale)}, screen_play=play;
        ClientToScreen(browser.handle(),&screen_play);
        if (SendMessageW(browser.handle(),WM_NCHITTEST,0,MAKELPARAM(screen_play.x,screen_play.y))!=HTCLIENT) return 34;
        query(browser,L"window.testToggle=false");
        SendMessageW(browser.handle(),WM_LBUTTONUP,0,MAKELPARAM(play.x,play.y));
        if (!wait([&] { return query(browser,L"window.testToggle") == L"true"; })) return 35;
        browser.command(BrowserWindow::Command::reload);
        if (!wait([&] { return query(browser,L"document.title") == L"\"YouTube — Launchme\""; })) return 27;
    }
    const auto pid=browser.processForTest();
    HANDLE process=pid ? OpenProcess(SYNCHRONIZE,FALSE,pid) : nullptr;
    browser.command(BrowserWindow::Command::close);
    if (!wait([&] { return !browser.isOpen(); }) || !IsWindowEnabled(owner) || closes!=2) return 14;
    const bool exited=process && wait([&] { return WaitForSingleObject(process,0)==WAIT_OBJECT_0; },10000);
    if (process) CloseHandle(process);
    if (!exited) return 18;
    // Closing during async environment/controller creation cannot resurrect a window.
    if (!browser.open(owner,L"https://launchme.test/late",root/L"profile-late",options)) return 15;
    browser.close();
    wait([] { return false; },1000);
    if (browser.isOpen() || !IsWindowEnabled(owner) || closes!=3) return 16;
    std::cout << (live_video ? "YouTube embed: live player ready, play/pause and cleanup passed\n" :
                              "Evergreen browser: navigation, scrolling, embed, close, missing runtime and late callbacks passed\n");
}
