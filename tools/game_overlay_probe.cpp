// Manual smoke test. Starts only its own disposable D3D11 window, identifies it
// like a custom game and attempts real PresentMon/ETW capture for up to 8 seconds.
// Does not require/elevate privileges or change system tracing permissions.
#include "../src/platform/game_overlay_monitor.hpp"
#include <d3d11.h>
#include <wrl/client.h>
#include <iostream>

using Microsoft::WRL::ComPtr;
namespace {
int presentWindow() {
    WNDCLASSW type{}; type.lpfnWndProc = DefWindowProcW; type.hInstance = GetModuleHandleW(nullptr);
    type.lpszClassName = L"LaunchmeFrameProbe";
    RegisterClassW(&type);
    const auto window = CreateWindowW(type.lpszClassName, L"Launchme FPS probe", WS_OVERLAPPEDWINDOW,
        80, 80, 640, 400, nullptr, nullptr, type.hInstance, nullptr);
    if (!window) return 2;
    struct Cleanup { HWND window; ~Cleanup() { if (IsWindow(window)) DestroyWindow(window); } } cleanup{window};
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 2; desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.OutputWindow = window;
    desc.SampleDesc.Count = 1; desc.Windowed = TRUE; desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context; ComPtr<IDXGISwapChain> swap;
    auto result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &desc, swap.GetAddressOf(), device.GetAddressOf(), nullptr, context.GetAddressOf());
    if (FAILED(result)) result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &desc, swap.GetAddressOf(), device.GetAddressOf(), nullptr, context.GetAddressOf());
    ComPtr<ID3D11Texture2D> texture; ComPtr<ID3D11RenderTargetView> target;
    if (FAILED(result) || FAILED(swap->GetBuffer(0, IID_PPV_ARGS(texture.GetAddressOf()))) ||
        FAILED(device->CreateRenderTargetView(texture.Get(), nullptr, target.GetAddressOf()))) return 3;
    ShowWindow(window, SW_SHOW); SetForegroundWindow(window);
    const auto end = GetTickCount64() + 15000;
    while (IsWindow(window) && GetTickCount64() < end) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        if (!IsWindow(window)) break;
        const float color[]{0.04f, 0.12f + static_cast<float>(GetTickCount64() % 1000) / 4000, 0.22f, 1};
        context->ClearRenderTargetView(target.Get(), color);
        if (FAILED(swap->Present(1, 0))) return 4;
    }
    return 0;
}
}
int wmain(int argc, wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (argc == 2 && std::wstring_view(argv[1]) == L"--window") return presentWindow();
    wchar_t path[32768]{};
    if (!GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)))) return 1;
    std::wstring command = L"\"" + std::wstring(path) + L"\" --window";
    STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
    const auto foreground = GetForegroundWindow();
    if (!CreateProcessW(path, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) return 1;
    struct Cleanup {
        PROCESS_INFORMATION process; HWND foreground;
        ~Cleanup() {
            EnumWindows([](HWND window, LPARAM pid) -> BOOL {
                DWORD owner = 0; GetWindowThreadProcessId(window, &owner);
                if (owner == static_cast<DWORD>(pid)) PostMessageW(window, WM_CLOSE, 0, 0);
                return TRUE;
            }, process.dwProcessId);
            if (WaitForSingleObject(process.hProcess, 2000) == WAIT_TIMEOUT) TerminateProcess(process.hProcess, 0);
            CloseHandle(process.hThread); CloseHandle(process.hProcess);
            if (IsWindow(foreground)) SetForegroundWindow(foreground);
        }
    } cleanup{process, foreground};
    BackgroundWork work; GameOverlayMonitor monitor;
    Game game; game.source = GameSource::custom; game.kind = GameKind::store;
    game.source_id = "fps-probe"; game.name = L"FPS fixture"; game.installed = true; game.executable = path;
    monitor.candidates({game}); monitor.configure(true, true); monitor.start(work);
    int code = 5;
    const auto end = GetTickCount64() + 8000;
    while (GetTickCount64() < end) {
        const auto sample = monitor.snapshot();
        if (sample.target && sample.target->process.pid != process.dwProcessId) { code = 6; break; }
        if (sample.fps.status == game_fps::Status::denied) {
            std::cout << "Fixture window/process identified; ETW access denied for this user (no elevation attempted).\n";
            code = 0; break;
        }
        if (sample.fps.status == game_fps::Status::failed) { std::cerr << "ETW capture failed.\n"; code = 7; break; }
        if (sample.fps.status == game_fps::Status::ready && sample.fps.low > 0) {
            std::cout << "Fixture PID " << sample.target->process.pid << ": " << sample.fps.fps << " FPS, 1% low " << sample.fps.low << '\n';
            code = 0; break;
        }
        Sleep(100);
    }
    monitor.stop();
    if (!work.shutdown(std::chrono::seconds(5))) { std::cerr << "Monitor shutdown timed out.\n"; return 8; }
    if (code == 5) std::cerr << "No samples; fixture must be in the foreground and the user must have ETW access.\n";
    return code;
}
