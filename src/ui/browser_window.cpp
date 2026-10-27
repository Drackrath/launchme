#include "browser_window.hpp"
#include "../i18n/i18n.hpp"
#include "../platform/text_encoding.hpp"
#include "../net/companion_links.hpp"
#include <objbase.h>
#include <WebView2.h>
#include <WebView2EnvironmentOptions.h>
#include <wrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <winhttp.h>
#include <algorithm>
#include <array>
#include <optional>

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Callback;
namespace {
constexpr UINT browser_command = WM_APP + 81;
constexpr wchar_t runtime_download[] = L"https://developer.microsoft.com/microsoft-edge/webview2/consumer/";
constexpr COLORREF background = RGB(17,19,24), muted = RGB(166,174,187), white = RGB(242,243,245);
struct Brush { HBRUSH value; explicit Brush(COLORREF c) : value(CreateSolidBrush(c)) {} ~Brush() { DeleteObject(value); } };
std::wstring take(LPWSTR value) { std::wstring result = value ? value : L""; CoTaskMemFree(value); return result; }
ComPtr<CoreWebView2EnvironmentOptions> evergreenOptions() {
    auto options=Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
    options->put_ReleaseChannels(COREWEBVIEW2_RELEASE_CHANNELS_STABLE);
    options->put_Language(i18n::localeName());
    return options;
}
}

struct BrowserWindow::State : std::enable_shared_from_this<State> {
    HWND window = nullptr, owner = nullptr;
    Options options;
    Status status = Status::closed;
    std::wstring url, title, error;
    std::filesystem::path profile;
    ComPtr<ICoreWebView2Environment> environment;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> web;
    uint64_t generation = 0, navigation = 0;
    bool loading = true, can_back = false, can_forward = false, owner_enabled = false, restore = true, notify = true;
    bool return_focus = false, video = false;
    bool low_memory = false; // AppSettings::low_memory: inactive pages use less memory, minimized ones are suspended.
    int focus = 2, hover = -1;
    int buttons() const { return status==Status::missing_runtime || (video && status==Status::ready) ? 4 : 3; }
#ifdef LAUNCHME_UI_TEST
    std::wstring fixture;
    bool missing = false;
#endif
    float scale() const { return window ? static_cast<float>(GetDpiForWindow(window))/96.0f : 1; }
    int px(float value) const { return static_cast<int>(value*scale()+0.5f); }
    RECT client() const { RECT r{}; if (window) GetClientRect(window,&r); return r; }
    RECT button(int index) const {
        const auto r=client();
        if (index==0) return {px(20),px(17),px(72),px(65)};
        if (index==1) return {px(84),px(17),px(222),px(65)};
        if (index==2) return {r.right-px(208),px(17),r.right-px(20),px(65)};
        if (video && status==Status::ready) return {px(234),px(17),px(426),px(65)};
        return {std::max<LONG>(px(20),(r.right-px(280))/2),r.bottom/2+px(36),std::max<LONG>(px(20),(r.right-px(280))/2)+px(280),r.bottom/2+px(86)};
    }
    void invalidate() { if (window) InvalidateRect(window,nullptr,FALSE); }
    void resize() {
        if (controller) { auto r=client(); r.top=px(84); r.bottom=std::max(r.top,r.bottom-px(42)); controller->put_Bounds(r); controller->NotifyParentWindowPositionChanged(); }
        invalidate();
    }
    // Low memory: the page asks for less memory while the window is inactive, normal again when used.
    void memoryTarget(std::optional<bool> active = {}) {
        ComPtr<ICoreWebView2_19> memory;
        if (!web || FAILED(web.As(&memory))) return;
        const bool used = active ? *active : window && (GetForegroundWindow()==window || IsChild(window,GetForegroundWindow()));
        const bool inactive = low_memory && !used;
        memory->put_MemoryUsageTargetLevel(inactive ? COREWEBVIEW2_MEMORY_USAGE_TARGET_LEVEL_LOW : COREWEBVIEW2_MEMORY_USAGE_TARGET_LEVEL_NORMAL);
    }
    // Low memory: minimized, the page is hidden and suspended (scripts and timers stop); shown again it resumes.
    void minimized(bool hidden) {
        if (!controller) return;
        if (!hidden) { controller->put_IsVisible(TRUE); memoryTarget(); return; }
        if (!low_memory) return;
        controller->put_IsVisible(FALSE);
        ComPtr<ICoreWebView2_3> suspend;
        if (web && SUCCEEDED(web.As(&suspend))) suspend->TrySuspend(nullptr);
    }
    void fail(Status value, std::wstring text) { status=value; error=std::move(text); loading=false; invalidate(); }
    void release() {
        ++generation;
        if (controller) controller->Close();
        web.Reset(); controller.Reset(); environment.Reset();
    }
    void navigate(const std::wstring& destination) {
        if (!BrowserWindow::supportedUrl(destination)) { error=tr(L"Ten rodzaj linku nie jest obsługiwany w przeglądarce."); invalidate(); return; }
        url=destination; error.clear(); loading=true; invalidate();
        video=!companion::links::playerHtml(url,true).empty();
        if (video) focus=3; else focus=std::min(focus,2);
        if (web && FAILED(web->Navigate(url.c_str()))) fail(Status::failed,tr(L"Nie można otworzyć strony"));
    }
    void initialize() {
        release(); status=Status::starting; loading=true; can_back=can_forward=false; focus=std::min(focus,2); error.clear(); title.clear(); invalidate();
        bool available=BrowserWindow::runtimeAvailable();
#ifdef LAUNCHME_UI_TEST
        if (missing) available=false;
#endif
        if (!available) { fail(Status::missing_runtime,tr(L"Zainstaluj Microsoft Edge WebView2 Runtime, aby otwierać strony w launcherze.")); return; }
        std::error_code ec; std::filesystem::create_directories(profile,ec);
        if (ec) { fail(Status::failed,tr(L"Nie można utworzyć profilu przeglądarki.")); return; }
        const auto weak=weak_from_this(); const auto token=generation;
        const auto environment_options=evergreenOptions();
        const HRESULT start=CreateCoreWebView2EnvironmentWithOptions(nullptr,profile.c_str(),environment_options.Get(),
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([weak,token](HRESULT hr,ICoreWebView2Environment* env)->HRESULT {
                const auto s=weak.lock(); if (!s || !s->window || s->generation!=token) return S_OK;
                if (FAILED(hr) || !env) { s->fail(Status::failed,tr(L"Nie można uruchomić przeglądarki WebView2.")); return S_OK; }
                s->environment=env;
                const HRESULT created=env->CreateCoreWebView2Controller(s->window,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>([weak,token](HRESULT result,ICoreWebView2Controller* control)->HRESULT {
                        const auto state=weak.lock();
                        if (!state || !state->window || state->generation!=token) { if (control) control->Close(); return S_OK; }
                        if (FAILED(result) || !control) { state->fail(Status::failed,tr(L"Nie można uruchomić przeglądarki WebView2.")); return S_OK; }
                        state->controller=control;
                        if (FAILED(control->get_CoreWebView2(&state->web)) || !state->web) { state->fail(Status::failed,tr(L"Nie można uruchomić przeglądarki WebView2.")); return S_OK; }
                        state->configure(); state->resize(); state->status=Status::ready; control->put_IsVisible(TRUE); state->memoryTarget();
                        state->navigate(state->url);
                        return S_OK;
                    }).Get());
                if (FAILED(created)) s->fail(Status::failed,tr(L"Nie można uruchomić przeglądarki WebView2."));
                return S_OK;
            }).Get());
        if (FAILED(start)) fail(Status::failed,tr(L"Nie można uruchomić przeglądarki WebView2."));
    }
    void configure() {
        ComPtr<ICoreWebView2Settings> settings;
        if (SUCCEEDED(web->get_Settings(&settings))) {
            settings->put_IsStatusBarEnabled(FALSE); settings->put_AreDevToolsEnabled(FALSE);
            settings->put_AreDefaultContextMenusEnabled(FALSE); settings->put_IsWebMessageEnabled(FALSE);
            settings->put_AreHostObjectsAllowed(FALSE); settings->put_AreDefaultScriptDialogsEnabled(FALSE);
            ComPtr<ICoreWebView2Settings3> keys; if (SUCCEEDED(settings.As(&keys))) keys->put_AreBrowserAcceleratorKeysEnabled(FALSE);
            ComPtr<ICoreWebView2Settings4> passwords; if (SUCCEEDED(settings.As(&passwords))) {
                passwords->put_IsPasswordAutosaveEnabled(FALSE); passwords->put_IsGeneralAutofillEnabled(FALSE);
            }
        }
        ComPtr<ICoreWebView2Controller2> color;
        if (SUCCEEDED(controller.As(&color))) color->put_DefaultBackgroundColor(COREWEBVIEW2_COLOR{255,17,19,24});
        ComPtr<ICoreWebView2_13> profiles; ComPtr<ICoreWebView2Profile> browser_profile;
        if (SUCCEEDED(web.As(&profiles)) && SUCCEEDED(profiles->get_Profile(&browser_profile))) browser_profile->put_PreferredColorScheme(COREWEBVIEW2_PREFERRED_COLOR_SCHEME_DARK);
        const auto weak=weak_from_this(); const auto epoch=generation; EventRegistrationToken token{};
        controller->add_AcceleratorKeyPressed(Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>([weak,epoch](auto*,ICoreWebView2AcceleratorKeyPressedEventArgs* args)->HRESULT {
            const auto s=weak.lock(); if (!s || !s->window || s->generation!=epoch) return S_OK;
            COREWEBVIEW2_KEY_EVENT_KIND kind{}; UINT key=0; args->get_KeyEventKind(&kind); args->get_VirtualKey(&key);
            if (kind==COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN || kind==COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN) {
                std::optional<Command> cmd;
                if (key==VK_ESCAPE) cmd=Command::close;
                else if (key==VK_F5) cmd=Command::reload;
                else if (key==VK_LEFT && (GetKeyState(VK_MENU)&0x8000)) cmd=Command::back;
                if (cmd) { args->put_Handled(TRUE); PostMessageW(s->window,browser_command,static_cast<WPARAM>(*cmd),0); }
            }
            return S_OK;
        }).Get(),&token);
        web->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>([weak,epoch](auto*,ICoreWebView2NavigationStartingEventArgs* args)->HRESULT {
            const auto s=weak.lock(); if (!s || !s->window || s->generation!=epoch) return S_OK;
            LPWSTR uri=nullptr; args->get_Uri(&uri); auto url=take(uri);
            if (!BrowserWindow::supportedUrl(url)) { args->put_Cancel(TRUE); s->error=tr(L"Ten rodzaj linku nie jest obsługiwany w przeglądarce."); }
            else {
                s->url=std::move(url); s->error.clear(); s->loading=true; args->get_NavigationId(&s->navigation);
                s->video=!companion::links::playerHtml(s->url,true).empty();
                s->focus=s->video ? 3 : std::min(s->focus,2);
            }
            s->invalidate(); return S_OK;
        }).Get(),&token);
        web->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>([weak,epoch](auto*,ICoreWebView2NavigationCompletedEventArgs* args)->HRESULT {
            const auto s=weak.lock(); if (!s || !s->window || s->generation!=epoch) return S_OK;
            UINT64 id=0; args->get_NavigationId(&id); if (id!=s->navigation) return S_OK;
            BOOL ok=FALSE; args->get_IsSuccess(&ok); s->loading=false;
            if (!ok) s->error=tr(L"Nie udało się wczytać strony. Użyj Odśwież.");
            s->history();
            s->invalidate(); return S_OK;
        }).Get(),&token);
        web->add_HistoryChanged(Callback<ICoreWebView2HistoryChangedEventHandler>([weak,epoch](auto*,auto*)->HRESULT {
            const auto s=weak.lock(); if (s && s->window && s->generation==epoch) { s->history(); s->invalidate(); } return S_OK;
        }).Get(),&token);
        web->add_SourceChanged(Callback<ICoreWebView2SourceChangedEventHandler>([weak,epoch](auto*,auto*)->HRESULT {
            const auto s=weak.lock(); if (s && s->window && s->generation==epoch) { s->history(); s->invalidate(); } return S_OK;
        }).Get(),&token);
        web->add_DocumentTitleChanged(Callback<ICoreWebView2DocumentTitleChangedEventHandler>([weak,epoch](auto*,auto*)->HRESULT {
            const auto s=weak.lock(); if (!s || !s->window || s->generation!=epoch) return S_OK;
            LPWSTR text=nullptr; s->web->get_DocumentTitle(&text); s->title=take(text); s->invalidate(); return S_OK;
        }).Get(),&token);
        web->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>([weak,epoch](auto*,ICoreWebView2NewWindowRequestedEventArgs* args)->HRESULT {
            args->put_Handled(TRUE);
            const auto s=weak.lock(); if (!s || !s->window || s->generation!=epoch) return S_OK;
            BOOL user=FALSE; args->get_IsUserInitiated(&user);
            LPWSTR uri=nullptr; args->get_Uri(&uri); auto url=take(uri);
            if (user && BrowserWindow::supportedUrl(url)) s->navigate(url);
            return S_OK;
        }).Get(),&token);
        web->add_PermissionRequested(Callback<ICoreWebView2PermissionRequestedEventHandler>([](auto*,ICoreWebView2PermissionRequestedEventArgs* args)->HRESULT {
            args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY); return S_OK;
        }).Get(),&token);
        web->add_ProcessFailed(Callback<ICoreWebView2ProcessFailedEventHandler>([weak,epoch](auto*,auto*)->HRESULT {
            const auto s=weak.lock(); if (s && s->window && s->generation==epoch) s->fail(Status::failed,tr(L"Przeglądarka została zatrzymana. Użyj Odśwież.")); return S_OK;
        }).Get(),&token);
        ComPtr<ICoreWebView2_4> downloads;
        if (SUCCEEDED(web.As(&downloads))) downloads->add_DownloadStarting(Callback<ICoreWebView2DownloadStartingEventHandler>([weak,epoch](auto*,ICoreWebView2DownloadStartingEventArgs* args)->HRESULT {
            args->put_Cancel(TRUE);
            const auto s=weak.lock(); if (s && s->window && s->generation==epoch) { s->error=tr(L"To okno służy do czytania stron; pobieranie plików jest wyłączone."); s->invalidate(); }
            return S_OK;
        }).Get(),&token);
        // The TV player is served locally under HTTPS so YouTube receives an origin/referrer
        // (NavigateToString/about:blank embeds can fail with error 153). No local server/files.
        web->AddWebResourceRequestedFilter(L"https://launchme.invalid/youtube/*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_DOCUMENT);
        web->add_WebResourceRequested(Callback<ICoreWebView2WebResourceRequestedEventHandler>([weak,epoch](auto*,ICoreWebView2WebResourceRequestedEventArgs* args)->HRESULT {
            const auto s=weak.lock(); if (!s || !s->environment || s->generation!=epoch) return S_OK;
            ComPtr<ICoreWebView2WebResourceRequest> request;
            if (FAILED(args->get_Request(&request))) return S_OK;
            LPWSTR uri=nullptr; request->get_Uri(&uri);
            const auto html=companion::links::playerHtml(take(uri), i18n::language()==i18n::Language::en);
            if (html.empty()) return S_OK;
            const auto bytes=text_encoding::utf8(html);
            ComPtr<IStream> stream; stream.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(bytes.data()),static_cast<UINT>(bytes.size())));
            ComPtr<ICoreWebView2WebResourceResponse> response;
            if (SUCCEEDED(s->environment->CreateWebResourceResponse(stream.Get(),200,L"OK",L"Content-Type: text/html; charset=utf-8\r\nCache-Control: no-store",&response))) args->put_Response(response.Get());
            return S_OK;
        }).Get(),&token);
#ifdef LAUNCHME_UI_TEST
        if (!fixture.empty()) {
            web->AddWebResourceRequestedFilter(L"*",COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
            web->add_WebResourceRequested(Callback<ICoreWebView2WebResourceRequestedEventHandler>([weak,epoch](auto*,ICoreWebView2WebResourceRequestedEventArgs* args)->HRESULT {
                const auto s=weak.lock(); if (!s || !s->environment || s->generation!=epoch) return S_OK;
                ComPtr<ICoreWebView2WebResourceRequest> request;
                if (SUCCEEDED(args->get_Request(&request))) {
                    LPWSTR uri=nullptr; request->get_Uri(&uri);
                    if (!companion::links::playerHtml(take(uri),true).empty()) return S_OK;
                }
                const auto bytes=text_encoding::utf8(s->fixture);
                ComPtr<IStream> stream; stream.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(bytes.data()),static_cast<UINT>(bytes.size())));
                ComPtr<ICoreWebView2WebResourceResponse> response;
                if (SUCCEEDED(s->environment->CreateWebResourceResponse(stream.Get(),200,L"OK",L"Content-Type: text/html; charset=utf-8\r\nCache-Control: no-store",&response))) args->put_Response(response.Get());
                return S_OK;
            }).Get(),&token);
        }
#endif
    }
    void history() {
        if (!web) return;
        LPWSTR uri=nullptr; web->get_Source(&uri); auto current=take(uri);
        if (BrowserWindow::supportedUrl(current)) url=std::move(current);
        BOOL value=FALSE; web->get_CanGoBack(&value); can_back=value!=FALSE;
        value=FALSE; web->get_CanGoForward(&value); can_forward=value!=FALSE;
    }
    void scroll(int pixels) {
        if (!web || status!=Status::ready) return;
        // Follow an inner scroll container (Steam uses these), then the document itself.
        const auto script=L"(()=>{const d="+std::to_wstring(std::clamp(pixels,-1600,1600))+L";let e=document.elementFromPoint(innerWidth/2,innerHeight/2);while(e&&e!==document.body){if(e.scrollHeight>e.clientHeight+1&&/auto|scroll/.test(getComputedStyle(e).overflowY)&&(d>0?e.scrollTop+e.clientHeight<e.scrollHeight-1:e.scrollTop>0)){e.scrollBy(0,d);return;}e=e.parentElement;}(document.scrollingElement||document.documentElement).scrollBy(0,d);})()";
        web->ExecuteScript(script.c_str(),nullptr);
    }
    void command(Command cmd) {
        if (!window) return;
        switch (cmd) {
        case Command::close: PostMessageW(window,WM_CLOSE,0,0); break;
        case Command::back: if (web && can_back) web->GoBack(); break;
        case Command::forward: if (web && can_forward) web->GoForward(); break;
        case Command::reload: if (status!=Status::ready) initialize(); else if (web) web->Reload(); break;
        case Command::previous_button:
            if (video && status==Status::ready && (focus==2 || focus==3)) focus=focus==2 ? 3 : 1;
            else focus=std::max(can_back ? 0 : 1,focus-1);
            invalidate(); break;
        case Command::next_button:
            if (video && status==Status::ready && (focus==1 || focus==3)) focus=focus==1 ? 3 : 2;
            else focus=std::min(status==Status::missing_runtime ? 3 : 2,focus+1);
            invalidate(); break;
        case Command::accept:
            if (focus==3 && video && status==Status::ready && web) web->ExecuteScript(L"if(typeof launchmeToggle==='function')launchmeToggle();",nullptr);
            else if (focus==3 && status==Status::missing_runtime) { if (options.external) options.external(runtime_download); }
            else command(focus==0 ? Command::back : focus==1 ? Command::reload : Command::close);
            break;
        }
    }
    void paint() {
        PAINTSTRUCT ps{}; HDC dc=BeginPaint(window,&ps); const auto r=client();
        Brush bg(background); FillRect(dc,&ps.rcPaint,bg.value); SetBkMode(dc,TRANSPARENT);
        HFONT font=CreateFontW(-px(17),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        const auto previous=SelectObject(dc,font);
        const auto text=[&](const std::wstring& value,RECT rect,COLORREF ink,UINT flags=DT_LEFT|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS) { SetTextColor(dc,ink); DrawTextW(dc,value.c_str(),static_cast<int>(value.size()),&rect,flags); };
        for (int i=0;i<buttons();++i) {
            const auto b=button(i); const bool chosen=focus==i;
            Brush fill(chosen ? white : hover==i ? RGB(66,69,77) : RGB(39,42,49));
            const auto old_brush=SelectObject(dc,fill.value); const auto old_pen=SelectObject(dc,GetStockObject(NULL_PEN));
            RoundRect(dc,b.left,b.top,b.right,b.bottom,px(46),px(46)); SelectObject(dc,old_brush); SelectObject(dc,old_pen);
            text(i==0 ? L"‹" : i==1 ? tr(L"Odśwież") : i==2 ? tr(L"Zamknij · Esc / B") : video && status==Status::ready ? tr(L"Odtwórz / Pauza") : tr(L"Pobierz WebView2"),b,
                chosen ? background : (i==0 && !can_back ? muted : white),DT_CENTER|DT_SINGLELINE|DT_VCENTER);
        }
        const int heading_left=video && status==Status::ready ? 446 : 244;
        const RECT heading{px(static_cast<float>(heading_left)),px(10),r.right-px(228),px(42)}, address{px(static_cast<float>(heading_left)),px(43),r.right-px(228),px(73)};
        text(loading ? tr(L"Wczytywanie…") : title.empty() ? L"Glance" : title,heading,white);
        text(url,address,muted);
        const RECT footer{px(20),r.bottom-px(40),r.right-px(20),r.bottom};
        text(error.empty() ? tr(L"↑ ↓ / gałka: przewijaj    ← →: przyciski    A: wybierz    B / Esc: zamknij") : error,footer,muted);
        if (status==Status::missing_runtime || status==Status::failed || status==Status::starting) {
            const RECT message{px(40),r.bottom/2-px(65),r.right-px(40),r.bottom/2+px(25)};
            text(status==Status::starting ? tr(L"Uruchamianie przeglądarki…") : error,message,white,DT_CENTER|DT_WORDBREAK);
        }
        SelectObject(dc,previous); DeleteObject(font); EndPaint(window,&ps);
    }
    static LRESULT CALLBACK proc(HWND window,UINT msg,WPARAM wp,LPARAM lp) {
        auto* s=reinterpret_cast<State*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if (msg==WM_NCCREATE) { s=static_cast<State*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams); s->window=window; SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s)); }
        if (!s) return DefWindowProcW(window,msg,wp,lp);
        switch (msg) {
        case WM_NCHITTEST: {
            const auto hit=DefWindowProcW(window,msg,wp,lp);
            if (hit!=HTCLIENT) return hit; // Keep the native resize edges.
            POINT p{static_cast<SHORT>(LOWORD(lp)),static_cast<SHORT>(HIWORD(lp))}; ScreenToClient(window,&p);
            if (p.y>=0 && p.y<s->px(84)) {
                for (int i=0;i<s->buttons();++i) { auto b=s->button(i); if (PtInRect(&b,p)) return HTCLIENT; }
                return HTCAPTION; // Drag/double-click the existing toolbar, with no extra title bar.
            }
            return HTCLIENT;
        }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: s->paint(); return 0;
        case WM_SIZE: s->minimized(wp==SIZE_MINIMIZED); if (wp!=SIZE_MINIMIZED) s->resize(); return 0;
        case WM_ACTIVATE: s->memoryTarget(LOWORD(wp)!=WA_INACTIVE); break;
        case WM_MOVE: if (s->controller) s->controller->NotifyParentWindowPositionChanged(); return 0;
        case WM_DPICHANGED: { const auto* r=reinterpret_cast<RECT*>(lp); SetWindowPos(window,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE); s->resize(); return 0; }
        case WM_GETMINMAXINFO: { auto* info=reinterpret_cast<MINMAXINFO*>(lp); info->ptMinTrackSize={s->px(700),s->px(450)}; return 0; }
        case WM_MOUSEMOVE: {
            POINT p{static_cast<SHORT>(LOWORD(lp)),static_cast<SHORT>(HIWORD(lp))}; int hover=-1;
            for (int i=0;i<s->buttons();++i) { auto b=s->button(i); if (PtInRect(&b,p)) hover=i; }
            if (hover!=s->hover) { s->hover=hover; s->invalidate(); }
            TRACKMOUSEEVENT tracking{sizeof(tracking),TME_LEAVE,window,0}; TrackMouseEvent(&tracking); return 0;
        }
        case WM_MOUSELEAVE: s->hover=-1; s->invalidate(); return 0;
        case WM_LBUTTONUP: {
            POINT p{static_cast<SHORT>(LOWORD(lp)),static_cast<SHORT>(HIWORD(lp))};
            for (int i=0;i<s->buttons();++i) { auto b=s->button(i); if (PtInRect(&b,p)) { s->focus=i; s->command(Command::accept); break; } }
            return 0;
        }
        case WM_MOUSEWHEEL: s->scroll(-static_cast<SHORT>(HIWORD(wp))*2); return 0;
        case WM_KEYDOWN:
            if (wp==VK_ESCAPE) s->command(Command::close);
            else if (wp==VK_F5) s->command(Command::reload);
            else if (wp==VK_UP || wp==VK_PRIOR) s->scroll(wp==VK_UP ? -120 : -600);
            else if (wp==VK_DOWN || wp==VK_NEXT) s->scroll(wp==VK_DOWN ? 120 : 600);
            else if (wp==VK_LEFT) s->command(Command::previous_button);
            else if (wp==VK_RIGHT || wp==VK_TAB) s->command(Command::next_button);
            else if (wp==VK_RETURN || wp==VK_SPACE) s->command(Command::accept);
            return 0;
        case browser_command: s->command(static_cast<Command>(wp)); return 0;
        case WM_CLOSE:
            s->return_focus=GetForegroundWindow()==window || IsChild(window,GetForegroundWindow());
            DestroyWindow(window); return 0;
        case WM_DESTROY: {
            const bool had_focus=s->return_focus || GetForegroundWindow()==window || IsChild(window,GetForegroundWindow());
            s->release(); s->window=nullptr; s->status=Status::closed;
            if (s->owner_enabled && IsWindow(s->owner)) EnableWindow(s->owner,TRUE);
            if (s->restore && had_focus && IsWindow(s->owner)) { SetForegroundWindow(s->owner); SetFocus(s->owner); }
            if (s->notify && s->options.closed) s->options.closed();
            return 0;
        }
        }
        return DefWindowProcW(window,msg,wp,lp);
    }
};

BrowserWindow::BrowserWindow() = default;
BrowserWindow::~BrowserWindow() { close(false); }
bool BrowserWindow::supportedUrl(const std::wstring& url) {
    if (url.empty() || url.size()>8192 || url.find_first_of(L"\r\n\t")!=url.npos) return false;
    URL_COMPONENTS parts{sizeof(parts)}; parts.dwHostNameLength=parts.dwUserNameLength=parts.dwPasswordLength=1;
    return WinHttpCrackUrl(url.c_str(),static_cast<DWORD>(url.size()),0,&parts) &&
        (parts.nScheme==INTERNET_SCHEME_HTTP || parts.nScheme==INTERNET_SCHEME_HTTPS) && parts.dwHostNameLength && !parts.dwUserNameLength && !parts.dwPasswordLength;
}
bool BrowserWindow::runtimeAvailable() {
    const auto options=evergreenOptions();
    LPWSTR version=nullptr; const HRESULT result=GetAvailableCoreWebView2BrowserVersionStringWithOptions(nullptr,options.Get(),&version);
    const bool present=SUCCEEDED(result) && version && *version; CoTaskMemFree(version); return present;
}
bool BrowserWindow::open(HWND owner,const std::wstring& url,const std::filesystem::path& profile,Options options) {
    if (!supportedUrl(url) || !IsWindow(owner) || !profile.is_absolute()) return false;
    if (isOpen()) {
        if (state_->status==Status::failed || state_->status==Status::missing_runtime) { state_->url=url; state_->initialize(); }
        else state_->navigate(url);
        if (options.activate) SetForegroundWindow(handle());
        return true;
    }
    state_=std::make_shared<State>(); auto& s=*state_; s.owner=owner; s.url=url; s.profile=profile; s.options=std::move(options); s.low_memory=low_memory_;
#ifdef LAUNCHME_UI_TEST
    s.fixture=test_html_; s.missing=test_missing_;
#endif
    WNDCLASSW type{}; type.lpfnWndProc=State::proc; type.hInstance=GetModuleHandleW(nullptr); type.hCursor=LoadCursorW(nullptr,IDC_ARROW); type.lpszClassName=L"LaunchmeBrowserWindow";
    type.hIcon=LoadIconW(type.hInstance,MAKEINTRESOURCEW(1));
    if (!RegisterClassW(&type) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) return false;
    RECT rect{}; GetWindowRect(owner,&rect); MONITORINFO monitor{sizeof(monitor)}; GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&monitor);
    const auto& work=monitor.rcWork;
    const float dpi=static_cast<float>(GetDpiForWindow(owner))/96.0f;
    const LONG inset=static_cast<LONG>(48*dpi);
    const int width=std::min(std::max(static_cast<LONG>(900*dpi),rect.right-rect.left-inset),work.right-work.left);
    const int height=std::min(std::max(static_cast<LONG>(600*dpi),rect.bottom-rect.top-inset),work.bottom-work.top);
    const int x=work.left+(work.right-work.left-width)/2,y=work.top+(work.bottom-work.top-height)/2;
    if (!CreateWindowExW(0,type.lpszClassName,L"Glance — Launchme",WS_POPUP|WS_THICKFRAME|WS_SYSMENU|WS_MAXIMIZEBOX|WS_CLIPCHILDREN,x,y,width,height,owner,nullptr,type.hInstance,&s)) return false;
    BOOL dark=TRUE; DwmSetWindowAttribute(s.window,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
    s.owner_enabled=IsWindowEnabled(owner)!=FALSE;
    if (s.owner_enabled) EnableWindow(owner,FALSE);
    ShowWindow(s.window,s.options.activate ? SW_SHOW : SW_SHOWNOACTIVATE);
    if (s.options.activate) { SetForegroundWindow(s.window); SetFocus(s.window); }
    s.initialize(); return true;
}
void BrowserWindow::close(bool restore) {
    if (!state_) return;
    state_->restore=restore; state_->notify=restore;
    state_->return_focus=owns(GetForegroundWindow());
    if (state_->window) DestroyWindow(state_->window); else state_->release();
    state_.reset();
}
bool BrowserWindow::isOpen() const { return state_ && state_->window; }
bool BrowserWindow::owns(HWND window) const { return isOpen() && window && (window==state_->window || IsChild(state_->window,window)); }
HWND BrowserWindow::handle() const { return state_ ? state_->window : nullptr; }
BrowserWindow::Status BrowserWindow::status() const { return state_ ? state_->status : Status::closed; }
void BrowserWindow::command(Command command) { if (isOpen()) state_->command(command); }
void BrowserWindow::scroll(int pixels) { if (isOpen()) state_->scroll(pixels); }
void BrowserWindow::setLowMemory(bool enabled) { low_memory_=enabled; if (state_) { state_->low_memory=enabled; state_->memoryTarget(); } }
#ifdef LAUNCHME_UI_TEST
void BrowserWindow::testPage(std::wstring html,bool missing) { test_html_=std::move(html); test_missing_=missing; }
void BrowserWindow::scriptForTest(const std::wstring& script,std::function<void(std::wstring)> result) {
    if (!state_ || !state_->web) { result(L""); return; }
    state_->web->ExecuteScript(script.c_str(),Callback<ICoreWebView2ExecuteScriptCompletedHandler>([result=std::move(result)](HRESULT hr,LPCWSTR json)->HRESULT {
        result(SUCCEEDED(hr) && json ? json : L""); return S_OK;
    }).Get());
}
std::wstring BrowserWindow::urlForTest() const { return state_ ? state_->url : L""; }
bool BrowserWindow::loadingForTest() const { return state_ && state_->loading; }
std::wstring BrowserWindow::errorForTest() const { return state_ ? state_->error : L""; }
DWORD BrowserWindow::processForTest() const { UINT32 pid=0; if (state_ && state_->web) state_->web->get_BrowserProcessId(&pid); return pid; }
void BrowserWindow::captureForTest(const std::filesystem::path& file,std::function<void(bool)> completed) {
    if (!state_ || !state_->web) { completed(false); return; }
    ComPtr<IStream> stream;
    if (FAILED(SHCreateStreamOnFileEx(file.c_str(),STGM_CREATE|STGM_WRITE|STGM_SHARE_EXCLUSIVE,FILE_ATTRIBUTE_NORMAL,TRUE,nullptr,&stream))) { completed(false); return; }
    state_->web->CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG,stream.Get(),Callback<ICoreWebView2CapturePreviewCompletedHandler>([stream,completed=std::move(completed)](HRESULT hr)->HRESULT { completed(SUCCEEDED(hr)); return S_OK; }).Get());
}
#endif
