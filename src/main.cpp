#include "app/launcher.hpp"
#include "app/message_dispatch.hpp"
#include "app/window_input.hpp"
#include "platform/diagnostics.hpp"
#include "platform/frame_clock.hpp"

#include <Windows.h>
#include <windowsx.h>
#include <dwmapi.h>

#include <memory>
#include <string>

namespace {
// WM_COPYDATA from a second launchme.exe started by a Jump List entry (src/app/jump_list.cpp),
// or by a plain second start ("show": the running launcher comes to the front instead).
constexpr ULONG_PTR jump_copy_data = 0x4C4D4A50;
HWND main_window = nullptr;
// Keys, text, the wheel and clicks, as the SDL window on Linux routes them (app/window_input.hpp).
window_input::Dispatcher<Launcher> input;

platform::KeyEvent keyEvent(WPARAM wparam, LPARAM lparam, bool pressed) {
    const platform::Modifiers modifiers{(GetKeyState(VK_SHIFT) & 0x8000) != 0, (GetKeyState(VK_CONTROL) & 0x8000) != 0,
                                        (GetKeyState(VK_MENU) & 0x8000) != 0};
    return {static_cast<unsigned>(wparam), static_cast<std::uint32_t>(wparam), pressed, (lparam & (1LL << 30)) != 0, modifiers};
}
// GUID_CONSOLE_DISPLAY_STATE (WinNT.h), defined here so no GUID library is needed.
constexpr GUID console_display_state{0x6fe69556, 0x704a, 0x47a0, {0x8f, 0x24, 0xc2, 0x8d, 0x93, 0x6f, 0xda, 0x47}};
constexpr WPARAM DBT_DEVNODES_CHANGED_VALUE = 0x0007; // Dbt.h: a device was added or removed.

// EVENT_SYSTEM_FOREGROUND for every process but ours: a game that starts outside the launcher
// takes the foreground, so this replaces a periodic process scan (Launcher::foregroundChanged).
void CALLBACK foregroundEvent(HWINEVENTHOOK, DWORD, HWND window, LONG, LONG, DWORD, DWORD) {
    if (!main_window || window == main_window) return;
    if (auto* app = reinterpret_cast<Launcher*>(GetWindowLongPtrW(main_window, GWLP_USERDATA)); app && !app->closing())
        app->foregroundChanged();
}

// One launcher per data folder: a second start brings the first one to the front (or hands it
// its Jump List command). The mutex lives until this process ends, also through its shutdown.
// nullptr: another launcher took over (this process ends). Otherwise this process continues,
// owning the returned mutex (INVALID_HANDLE_VALUE: none could be had; start anyway).
HANDLE singleInstance(const std::filesystem::path& data_folder, const std::wstring& command) {
    const std::wstring name = L"Local\\Launchme-" + std::to_wstring(std::hash<std::wstring>{}(data_folder.lexically_normal().wstring()));
    const HANDLE mutex = CreateMutexW(nullptr, TRUE, name.c_str());
    if (!mutex) return INVALID_HANDLE_VALUE;
    if (GetLastError() != ERROR_ALREADY_EXISTS) return mutex;
    std::wstring text = command.empty() ? std::wstring(L"show") : command;
    // The other launcher may still be starting (no window yet) or closing (window gone, mutex
    // released when its process ends): look for its window, or take the mutex once it is free.
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (HWND running = FindWindowW(L"LaunchmeNativeWindow", nullptr)) {
            DWORD process = 0;
            GetWindowThreadProcessId(running, &process);
            AllowSetForegroundWindow(process); // Its window comes forward, as the click asked.
            COPYDATASTRUCT data{jump_copy_data, static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t)), text.data()};
            DWORD_PTR accepted = 0;
            if (SendMessageTimeoutW(running, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data), SMTO_ABORTIFHUNG, 1000, &accepted) && accepted) {
                CloseHandle(mutex);
                return nullptr;
            }
        }
        const DWORD waited = WaitForSingleObject(mutex, 100);
        if (waited == WAIT_OBJECT_0 || waited == WAIT_ABANDONED) return mutex; // It ended: this one is the launcher now.
    }
    CloseHandle(mutex);
    return INVALID_HANDLE_VALUE; // It neither answers nor ends: start rather than leave the user with nothing.
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* app = reinterpret_cast<Launcher*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    const bool closing = app && app->closing();
    // Keep owning/freeing posted payloads, without changing the departing scene.
    if (dispatchWorkerMessage(closing ? nullptr : app, message, lparam)) return 0;
    if (closing) {
        if ((message >= WM_KEYFIRST && message <= WM_KEYLAST) ||
            (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) ||
            message == WM_HOTKEY || message == WM_TIMER || message == WM_INPUT_DEVICE_CHANGE) return 0;
        if (message == WM_INPUT) return DefWindowProcW(hwnd, message, wparam, lparam);
        if (message == WM_SETCURSOR && LOWORD(lparam) == HTCLIENT) { SetCursor(nullptr); return TRUE; }
    }
    // The notification-area icon while a game runs (src/app/tray.cpp), and Explorer's restart, after
    // which the icon must be added again.
    static const UINT taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    if (app && message == Launcher::tray_message) { if (!closing) app->trayIconEvent(wparam, lparam); return 0; }
    if (app && taskbar_created && message == taskbar_created) { if (!closing) app->taskbarCreated(); return 0; }
    // Screensaver: while it runs, and for the rest of the input that ends it, keyboard
    // and mouse messages never reach the screen beneath.
    if (app && app->screensaverInput(message, wparam, lparam)) return 0;
    if (app && app->launchAnimationActive()) {
        // Back hides the starting game's picture; every other key and click waits for its end.
        if ((message == WM_KEYDOWN && (wparam == VK_ESCAPE || wparam == VK_BACK) && !(lparam & (1LL << 30))) ||
            (message == WM_XBUTTONUP && GET_XBUTTON_WPARAM(wparam) == XBUTTON1)) app->dismissLaunchAnimation();
        if ((message >= WM_KEYFIRST && message <= WM_KEYLAST) ||
            (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST)) return 0;
        if (message == WM_SETCURSOR && LOWORD(lparam) == HTCLIENT) { SetCursor(nullptr); return TRUE; }
    }
    switch (message) {
    case WM_CREATE: {
        auto instance = std::make_unique<Launcher>();
        if (!instance->initialize(hwnd)) return -1;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(instance.release()));
        return 0;
    }
    case WM_INPUT:
        if (app) app->rawControllerInput(reinterpret_cast<HRAWINPUT>(lparam));
        break; // DefWindowProc releases foreground Raw Input resources.
    case WM_INPUT_DEVICE_CHANGE:
        if (app) app->controllerDeviceChanged(reinterpret_cast<HANDLE>(lparam));
        return 0;
    case WM_SIZE:
        if (app) {
            app->resize();
            if (wparam == SIZE_MAXIMIZED || wparam == SIZE_RESTORED) app->windowPlacementChanged();
        }
        return 0;
    // Desktop mode draws its own top bar: the caption becomes client area (the side and bottom
    // frames keep resizing), the top bar answers as caption and maximize button (snap layouts).
    case WM_NCCALCSIZE:
        if (app && app->desktopMode() && wparam) {
            auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lparam);
            const LONG top = params->rgrc[0].top;
            const LRESULT result = DefWindowProcW(hwnd, message, wparam, lparam);
            params->rgrc[0].top = top;
            if (IsZoomed(hwnd)) { // A maximized window reaches past the monitor by its frame.
                const UINT dpi = GetDpiForWindow(hwnd);
                params->rgrc[0].top += GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            }
            return result;
        }
        break;
    case WM_NCHITTEST:
        if (app && app->desktopMode()) {
            const LRESULT hit = DefWindowProcW(hwnd, message, wparam, lparam);
            if (hit != HTCLIENT) return hit;
            POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ScreenToClient(hwnd, &point);
            if (!IsZoomed(hwnd)) { // The top resize edge now lies inside the client area.
                const UINT dpi = GetDpiForWindow(hwnd);
                const int border = GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                if (point.y < border) {
                    RECT client{};
                    GetClientRect(hwnd, &client);
                    if (point.x < border * 2) return HTTOPLEFT;
                    if (point.x >= client.right - border * 2) return HTTOPRIGHT;
                    return HTTOP;
                }
            }
            return app->desktopNonClientHit(point.x, point.y);
        }
        break;
    case WM_NCMOUSEMOVE:
        if (app && app->desktopMode()) {
            TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT), TME_LEAVE | TME_NONCLIENT, hwnd, 0};
            TrackMouseEvent(&tracking);
            app->desktopMaximizeHover(wparam == HTMAXBUTTON);
        }
        break;
    case WM_NCMOUSELEAVE:
        if (app) app->desktopMaximizeHover(false);
        break;
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONDBLCLK:
        // Its own maximize button: not the classic one DefWindowProc would draw.
        if (app && app->desktopMode() && wparam == HTMAXBUTTON) { app->noteUserInput(); app->desktopMaximizePress(true); return 0; }
        break;
    case WM_NCLBUTTONUP:
        if (app && app->desktopMode() && wparam == HTMAXBUTTON) { app->desktopMaximizePress(false); return 0; }
        break;
    case WM_GETMINMAXINFO:
        if (app) app->minimumWindowSize(*reinterpret_cast<MINMAXINFO*>(lparam));
        return 0;
    case WM_ACTIVATE:
        if (app) app->windowActivated(LOWORD(wparam) != WA_INACTIVE);
        break;
    case WM_ENTERSIZEMOVE:
        if (app) app->sizeMove(true); // The system's loop runs no frames: paint as it asks.
        return 0;
    case WM_EXITSIZEMOVE:
        if (app) { app->sizeMove(false); app->windowPlacementChanged(); }
        return 0;
    case WM_COPYDATA: {
        const auto* data = reinterpret_cast<const COPYDATASTRUCT*>(lparam);
        if (!app || !data || data->dwData != jump_copy_data || !data->lpData || data->cbData < sizeof(wchar_t) || data->cbData > 4096) break;
        std::wstring command(static_cast<const wchar_t*>(data->lpData), data->cbData / sizeof(wchar_t));
        while (!command.empty() && command.back() == L'\0') command.pop_back();
        app->showFromTray(); // Hidden while a game runs: back first.
        if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
        SetForegroundWindow(hwnd);
        app->jumpCommand(command);
        return TRUE;
    }
    case WM_DROPFILES:
        if (app) app->dropFiles(reinterpret_cast<HDROP>(wparam));
        else DragFinish(reinterpret_cast<HDROP>(wparam));
        return 0;
    case WM_PAINT:
        if (app && !app->gameOverlayOpen()) {
            if (app->deferPaint()) ValidateRect(hwnd, nullptr); // The next frame paints it.
            else app->paint();
        } else {
            PAINTSTRUCT ps; BeginPaint(hwnd, &ps); EndPaint(hwnd, &ps);
            if (app) InvalidateRect(app->frameWindow(), nullptr, FALSE);
        }
        return 0;
    case WM_TIMER:
        if (app && wparam == Launcher::guide_timer) { app->pollGuideInput(); return 0; }
        if (app && wparam == Launcher::foreground_scan_timer) { app->foregroundScanDue(); return 0; }
        break;
    case WM_HOTKEY:
        if (app && wparam == Launcher::guide_hotkey) { app->guideKeyboardHotkey(); return 0; }
        break;
    case WM_CLOSE:
        if (app && app->requestClose()) return 0; // Confirmation, then the one-second exit.
        break;
    case WM_QUERYENDSESSION:
        // Windows is shutting down, restarting or signing out: nothing is lost even if it never
        // gets as far as WM_ENDSESSION (another program refused, or it is killed).
        if (app) app->saveSessionState();
        return TRUE;
    case WM_ENDSESSION:
        if (app && wparam) {
            app->resumeFrozenGames(); // Let games handle logoff/shutdown themselves.
            app->saveSessionState();  // The process ends after this; ~Launcher never runs.
        }
        return 0;
    case WM_SYSCOMMAND:
        // A lone Alt or F10 would enter the system menu's modal loop: frames, the pad and the
        // whole launcher would stop until the next key. Alt+F4 (SC_CLOSE) is unaffected; the
        // desktop window keeps Alt+Space for its system menu.
        if ((wparam & 0xFFF0) == SC_KEYMENU && (lparam == 0 || !app || !app->desktopMode())) return 0;
        break;
    case WM_DEVICECHANGE:
        if (app && wparam == DBT_DEVNODES_CHANGED_VALUE) app->devicesChanged();
        return TRUE;
    case WM_DISPLAYCHANGE:
        if (app) app->displayChanged(); // A game changed the resolution, a TV switched mode, a monitor left.
        break;
    case WM_POWERBROADCAST:
        if (app && wparam == PBT_APMRESUMEAUTOMATIC) app->resumedFromSleep();
        if (app && wparam == PBT_POWERSETTINGCHANGE && lparam) {
            const auto* setting = reinterpret_cast<const POWERBROADCAST_SETTING*>(lparam);
            if (IsEqualGUID(setting->PowerSetting, console_display_state) && setting->DataLength >= sizeof(DWORD))
                app->displayPower(*reinterpret_cast<const DWORD*>(setting->Data) != 0); // 0 off, 1 on, 2 dimmed.
        }
        return TRUE;
    case WM_XBUTTONUP:
        // The mouse's Back button: what Esc does on every screen.
        if (app && GET_XBUTTON_WPARAM(wparam) == XBUTTON1) {
            SendMessageW(hwnd, WM_KEYDOWN, VK_ESCAPE, 1);
            SendMessageW(hwnd, WM_KEYUP, VK_ESCAPE, 0xC0000001);
        }
        return TRUE;
    case WM_KEYDOWN:
        if (app) input.keyDown(*app, keyEvent(wparam, lparam, true)); // app/window_input.hpp
        return 0;
    case WM_PASTE:
        if (app) app->companionPaste();
        return 0;
    case WM_CHAR:
        if (app) input.character(*app, static_cast<wchar_t>(wparam));
        return 0;
    case WM_KEYUP:
        if (app) input.keyUp(*app, static_cast<unsigned>(wparam));
        return 0;
    case WM_KILLFOCUS:
        if (app) input.focusLost(*app);
        return 0;
    case WM_SETFOCUS:
        if (app) input.focusGained(*app);
        return 0;
    case WM_MOUSEWHEEL:
        if (app) {
            POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ScreenToClient(hwnd, &point);
            input.wheel(*app, static_cast<float>(point.x), static_cast<float>(point.y), GET_WHEEL_DELTA_WPARAM(wparam),
                        (GET_KEYSTATE_WPARAM(wparam) & MK_CONTROL) != 0);
        }
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        if (app) input.leftDown(*app, static_cast<float>(GET_X_LPARAM(lparam)), static_cast<float>(GET_Y_LPARAM(lparam)),
                                message == WM_LBUTTONDBLCLK);
        return 0;
    case WM_RBUTTONUP:
        if (app) input.rightUp(*app, static_cast<float>(GET_X_LPARAM(lparam)), static_cast<float>(GET_Y_LPARAM(lparam)));
        return 0;
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&tracking);
        if (app) input.mouseMove(*app, static_cast<float>(GET_X_LPARAM(lparam)), static_cast<float>(GET_Y_LPARAM(lparam)));
        return 0;
    }
    case WM_MOUSELEAVE:
        if (app) input.mouseLeave(*app);
        return 0;
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        // Desktop buttons act on release over the same target; drags drop here.
        if (app && message == WM_LBUTTONUP) input.leftUp(*app, static_cast<float>(GET_X_LPARAM(lparam)), static_cast<float>(GET_Y_LPARAM(lparam)));
        else if (app) input.captureLost(*app);
        return 0;
    case WM_SETCURSOR:
        if (app && app->screensaverActive() && LOWORD(lparam) == HTCLIENT) { SetCursor(nullptr); return TRUE; }
        if (app && LOWORD(lparam) == HTCLIENT) {
            POINT cursor{};
            GetCursorPos(&cursor);
            ScreenToClient(hwnd, &cursor);
            if (const auto shape = app->desktopCursor(static_cast<float>(cursor.x), static_cast<float>(cursor.y))) {
                SetCursor(LoadCursorW(nullptr, shape)); // Text beam over the search, a hand over what acts.
                return TRUE;
            }
            SetCursor(LoadCursorW(nullptr, app->hoverButton(static_cast<float>(cursor.x),
                                                            static_cast<float>(cursor.y)) ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_DPICHANGED:
        // Full-screen Tryb TV keeps covering its monitor; a window takes Windows' suggestion.
        if (app && app->coversMonitor()) app->displayChanged();
        else if (const auto* recommended = reinterpret_cast<RECT*>(lparam))
            SetWindowPos(hwnd, nullptr, recommended->left, recommended->top,
                         recommended->right - recommended->left, recommended->bottom - recommended->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    case WM_DESTROY: {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        delete app;
        // Workers own their payloads until PostMessage succeeds. Drain any
        // posted payloads still queued as the window shuts down.
        discardWorkerMessages<Launcher>(hwnd);
        PostQuitMessage(0);
        return 0;
    }
    default: break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // A Jump List entry (--jump=<command>): a running launcher takes it and this process ends; else it
    // waits in this one until its library is ready.
    std::wstring jump;
    std::filesystem::path companion_copy;
    bool start_minimized = false;
    DWORD updated_from = 0;
    int argument_count = 0;
    if (auto** arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count)) {
        for (int i = 1; i < argument_count; ++i) {
            if (std::wstring_view(arguments[i]).starts_with(L"--jump=")) jump = arguments[i] + 7;
            else if (std::wstring_view(arguments[i]).starts_with(L"--after-update=")) updated_from = static_cast<DWORD>(_wtoi(arguments[i] + 15));
            else if (std::wstring_view(arguments[i]) == L"--minimized") start_minimized = true;
            else if (std::wstring_view(arguments[i]) == L"--companion-copy" && i + 1 < argument_count) companion_copy = arguments[++i];
        }
        LocalFree(arguments);
    }
    if (companion_copy.empty()) {
        wchar_t module[32768]{}; GetModuleFileNameW(nullptr, module, 32768);
        const auto beside = std::filesystem::path(module).parent_path();
        std::error_code ec;
        if (std::filesystem::is_regular_file(beside / L"backup.json", ec)) companion_copy = beside;
    }
    if (!companion_copy.empty()) {
        std::error_code ec; companion_copy = std::filesystem::weakly_canonical(companion_copy, ec);
        const auto restored = ec ? companion::backup::Result{} : companion::backup::openCopy(companion_copy);
        if (!restored.ok || !SetEnvironmentVariableW(L"LAUNCHME_CONFIG_DIR", restored.path.c_str())) {
            MessageBoxW(nullptr, restored.error.empty() ? L"Cannot open companion backup." : restored.error.c_str(), L"Launchme — companion backup", MB_ICONERROR | MB_OK);
            return 1;
        }
    }
    // Started by an update (src/app/updates.cpp): the previous version closes first (up to 30 s), so
    // this one takes the single-instance lock and its old files can be removed.
    if (updated_from) {
        if (const HANDLE previous = OpenProcess(SYNCHRONIZE, FALSE, updated_from)) {
            WaitForSingleObject(previous, 30000);
            CloseHandle(previous);
        }
    }
    const auto data_folder = userConfigDirectory();
    diagnostics::install(data_folder);
    const HANDLE instance_mutex = singleInstance(data_folder, jump);
    if (!instance_mutex) return 0; // The running launcher came to the front (or took the Jump List command).
    struct MutexRelease { HANDLE handle; ~MutexRelease() { if (handle != INVALID_HANDLE_VALUE) { ReleaseMutex(handle); CloseHandle(handle); } } } release{instance_mutex};
    // A start that fails says why (and logs it) instead of ending without a word.
    const auto startupFailure = [](const wchar_t* what, HRESULT code) {
        wchar_t text[512]{};
        swprintf_s(text, L"%s (0x%08lX)", what, static_cast<unsigned long>(code));
        diagnostics::log("error", std::wstring_view(text));
        MessageBoxW(nullptr, text, L"Launchme", MB_ICONERROR | MB_OK);
        return 1;
    };
    if (const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); FAILED(com))
        return startupFailure(L"Launchme cannot start: COM initialization failed", com);
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = windowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    // Resource 1 (res/launchme.rc.in): taskbar, Alt+Tab and the EXE in Explorer.
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    wc.lpszClassName = L"LaunchmeNativeWindow";
    if (!RegisterClassExW(&wc)) { const auto error = HRESULT_FROM_WIN32(GetLastError()); CoUninitialize(); return startupFailure(L"Launchme cannot start: window class registration failed", error); }
    const int width = GetSystemMetrics(SM_CXSCREEN), height = GetSystemMetrics(SM_CYSCREEN);
    const int x = 0, y = 0;
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"launchme", WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX,
                                x, y, width, height, nullptr, nullptr, instance, nullptr);
    if (!hwnd) {
        const auto error = HRESULT_FROM_WIN32(GetLastError());
        CoUninitialize();
        // Launcher::initialize logged which part failed (graphics, mostly: a missing or broken GPU driver).
        return startupFailure(L"Launchme cannot start: the window or its graphics could not be created. See launchme.log in %LOCALAPPDATA%\\Launchme", error);
    }
    main_window = hwnd;
    // Running elevated (a user's "Run as administrator"): a second start, the Jump List and dropped
    // files come from normal-integrity processes and would otherwise be blocked (UIPI).
    for (const UINT allowed : {static_cast<UINT>(WM_COPYDATA), static_cast<UINT>(WM_DROPFILES), 0x0049u /* WM_COPYGLOBALDATA */})
        ChangeWindowMessageFilterEx(hwnd, allowed, MSGFLT_ALLOW, nullptr);
    const HWINEVENTHOOK foreground_hook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, foregroundEvent,
                                                          0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    const HPOWERNOTIFY display_notify = RegisterPowerSettingNotification(hwnd, &console_display_state, DEVICE_NOTIFY_WINDOW_HANDLE);
    const auto* launcher = reinterpret_cast<Launcher*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (launcher && launcher->desktopMode()) {
        // The desktop window (Launcher::applyWindowMode set its style and place): now that the
        // launcher answers WM_NCCALCSIZE, its own top bar replaces the caption.
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
        const DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_DONOTROUND;
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    }
    // A shortcut set to "Run: Minimized", or --minimized (Uruchamiaj z systemem Windows): on the
    // taskbar, ready, without taking the screen.
    const bool minimized = start_minimized || show_command == SW_SHOWMINNOACTIVE || show_command == SW_MINIMIZE ||
                           show_command == SW_SHOWMINIMIZED || show_command == SW_FORCEMINIMIZE;
    ShowWindow(hwnd, minimized ? SW_SHOWMINNOACTIVE : launcher && launcher->startMaximized() ? SW_SHOWMAXIMIZED : SW_SHOW);
    UpdateWindow(hwnd);
    if (auto* app = reinterpret_cast<Launcher*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)); app && !jump.empty()) app->jumpCommand(jump);
    MSG message{};
    FrameClock frame_clock;
    bool running = true;
    while (running) {
        for (int batch = 0; batch < 64 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++batch) {
            if (message.message == WM_QUIT) { running = false; break; }
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        if (!running) break;
        auto* active = reinterpret_cast<Launcher*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        // Nobody watches a switched-off display; the screensaver's slow pans need no 144 Hz.
        frame_clock.limit(active && active->displayOff() ? 5 : active && active->screensaverActive() ? 30 : 240);
        frame_clock.update(active ? active->frameWindow() : hwnd);
        if (frame_clock.ready()) {
            frame_clock.consume();
            if (auto* app = reinterpret_cast<Launcher*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
                app->setFrameRate(frame_clock.targetHz(), frame_clock.refreshHz());
                app->tick();
                app->updateFrame();
            }
        }
        frame_clock.wait();
    }
    if (foreground_hook) UnhookWinEvent(foreground_hook);
    if (display_notify) UnregisterPowerSettingNotification(display_notify);
    main_window = nullptr;
    CoUninitialize();
    return static_cast<int>(message.wParam);
}
