// SwapChainTarget (swap_chain.hpp): Windows only; Linux compiles this file empty.
#include "swap_chain.hpp"
#ifdef _WIN32
#include <d2d1_1helper.h>
#include <d3d10.h>   // ID3D10Multithread, which Direct3D 11 devices answer too.
#include <dxgi1_3.h> // IDXGISwapChain2 (frame latency), IDXGIDevice3 (Trim).
#include <dxgi1_5.h> // IDXGIFactory5 (tearing support).

#include <algorithm>

using Microsoft::WRL::ComPtr;

namespace {
HRESULT createDevice(D3D_DRIVER_TYPE type, UINT flags, ComPtr<ID3D11Device>& device, ComPtr<ID3D11DeviceContext>& context) {
    static constexpr D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                                   D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_9_3, D3D_FEATURE_LEVEL_9_2, D3D_FEATURE_LEVEL_9_1};
    device.Reset(); context.Reset();
    return D3D11CreateDevice(nullptr, type, nullptr, flags, levels, static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
                             device.GetAddressOf(), nullptr, context.GetAddressOf());
}
}

bool SwapChainTarget::create(ID2D1Factory* factory, HWND window, UINT width, UINT height, bool tearing) {
    reset();
    ComPtr<ID2D1Factory1> factory1;
    if (!factory || !window || FAILED(factory->QueryInterface(IID_PPV_ARGS(factory1.GetAddressOf())))) return false;
    // Video support lets Media Foundation decode game videos on this device (game_video.cpp); a
    // driver without it still draws, and WARP stands in for a missing GPU as the HWND target's does.
    constexpr UINT bgra = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    if (FAILED(createDevice(D3D_DRIVER_TYPE_HARDWARE, bgra | D3D11_CREATE_DEVICE_VIDEO_SUPPORT, device3d_, immediate_)) &&
        FAILED(createDevice(D3D_DRIVER_TYPE_HARDWARE, bgra, device3d_, immediate_)) &&
        FAILED(createDevice(D3D_DRIVER_TYPE_WARP, bgra, device3d_, immediate_))) { reset(); return false; }
    ComPtr<IDXGIDevice1> dxgi;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory2> dxgi_factory;
    if (FAILED(device3d_.As(&dxgi)) || FAILED(dxgi->GetAdapter(adapter.GetAddressOf())) ||
        FAILED(adapter->GetParent(IID_PPV_ARGS(dxgi_factory.GetAddressOf()))) ||
        FAILED(factory1->CreateDevice(dxgi.Get(), device2d_.GetAddressOf())) ||
        FAILED(device2d_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, context_.GetAddressOf()))) { reset(); return false; }
    ComPtr<IDXGIFactory5> factory5;
    BOOL allowed = FALSE;
    tearing_ = tearing && SUCCEEDED(dxgi_factory.As(&factory5)) &&
               SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowed, sizeof(allowed))) && allowed;
    flags_ = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT | (tearing_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u);
    DXGI_SWAP_CHAIN_DESC1 description{};
    description.Width = std::max(1u, width);
    description.Height = std::max(1u, height);
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc = {1, 0};
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = 2;
    description.Scaling = DXGI_SCALING_STRETCH;
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    description.Flags = flags_;
    if (FAILED(dxgi_factory->CreateSwapChainForHwnd(device3d_.Get(), window, &description, nullptr, nullptr, swap_chain_.GetAddressOf()))) {
        // Every frame is drawn whole, so the sequential flip model shows the same.
        description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        if (FAILED(dxgi_factory->CreateSwapChainForHwnd(device3d_.Get(), window, &description, nullptr, nullptr, swap_chain_.GetAddressOf()))) {
            reset();
            return false;
        }
    }
    // Alt+Enter stays the launcher's own (F11 and Tryb TV's window modes), never DXGI's full screen.
    dxgi_factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
    ComPtr<IDXGISwapChain2> latency;
    if (SUCCEEDED(swap_chain_.As(&latency)) && SUCCEEDED(latency->SetMaximumFrameLatency(1)))
        frame_latency_ = latency->GetFrameLatencyWaitableObject();
    context_->SetDpi(96, 96);
    if (!bindBackBuffer()) { reset(); return false; }
    return true;
}

bool SwapChainTarget::bindBackBuffer() {
    ComPtr<IDXGISurface> surface;
    // The swap chain's buffers are no shader input: drawn into and copied from, never drawn.
    const auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                                                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96, 96);
    if (FAILED(swap_chain_->GetBuffer(0, IID_PPV_ARGS(surface.GetAddressOf()))) ||
        FAILED(context_->CreateBitmapFromDxgiSurface(surface.Get(), &properties, back_buffer_.ReleaseAndGetAddressOf()))) return false;
    context_->SetTarget(back_buffer_.Get());
    return true;
}

void SwapChainTarget::reset() {
    if (context_) context_->SetTarget(nullptr);
    back_buffer_.Reset();
    context_.Reset();
    swap_chain_.Reset();
    device2d_.Reset();
    if (immediate_) { immediate_->ClearState(); immediate_->Flush(); }
    immediate_.Reset();
    device3d_.Reset();
    if (frame_latency_) { CloseHandle(frame_latency_); frame_latency_ = nullptr; }
    missed_waits_ = 0;
    flags_ = 0;
    tearing_ = protected_ = false;
}

bool SwapChainTarget::resize(UINT width, UINT height) {
    if (!swap_chain_ || !context_) return false;
    // ResizeBuffers needs every reference to the old buffers gone, the device context's too.
    context_->SetTarget(nullptr);
    back_buffer_.Reset();
    immediate_->OMSetRenderTargets(0, nullptr, nullptr);
    immediate_->Flush();
    if (FAILED(swap_chain_->ResizeBuffers(0, std::max(1u, width), std::max(1u, height), DXGI_FORMAT_UNKNOWN, flags_))) return false;
    return bindBackBuffer();
}

void SwapChainTarget::waitForFrame(DWORD timeout_ms) {
    if (!frame_latency_ || missed_waits_ >= 3) return;
    missed_waits_ = WaitForSingleObject(frame_latency_, timeout_ms) == WAIT_TIMEOUT ? missed_waits_ + 1 : 0;
}

HRESULT SwapChainTarget::present(bool immediate) {
    if (!swap_chain_) return E_POINTER;
    const HRESULT shown = swap_chain_->Present(immediate ? 0 : 1, immediate && tearing_ ? DXGI_PRESENT_ALLOW_TEARING : 0);
    // Occluded (minimized, a locked screen) and the like are no failures: the next frame tries again.
    return shown == DXGI_ERROR_DEVICE_REMOVED || shown == DXGI_ERROR_DEVICE_RESET || shown == DXGI_ERROR_DEVICE_HUNG ? shown : S_OK;
}

void SwapChainTarget::protectForVideo() {
    if (protected_ || !device3d_) return;
    ComPtr<ID3D10Multithread> multithread;
    if (SUCCEEDED(device3d_.As(&multithread))) { multithread->SetMultithreadProtected(TRUE); protected_ = true; }
}

void SwapChainTarget::trim() {
    if (device2d_) device2d_->ClearResources(0);
    if (immediate_) { immediate_->ClearState(); immediate_->Flush(); }
    ComPtr<IDXGIDevice3> dxgi;
    if (device3d_ && SUCCEEDED(device3d_.As(&dxgi))) dxgi->Trim();
}

void SwapChainTarget::limitTextureMemory(UINT64 bytes) {
    if (device2d_) device2d_->SetMaximumTextureMemory(bytes);
}
#endif
