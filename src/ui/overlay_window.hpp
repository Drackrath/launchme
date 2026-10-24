#pragma once
#include "renderer.hpp"
#include <functional>

class OverlayWindow {
public:
    using Dispatch = std::function<LRESULT(HWND, UINT, WPARAM, LPARAM)>;
    using Activate = std::function<BOOL(HWND)>;
    ~OverlayWindow();
    bool create(bool passive, Dispatch dispatch);
    bool show(const RECT& rect, bool activate, const Activate& focus = SetForegroundWindow);
    void hide();
    HWND handle() const { return window_; }
    Renderer& renderer() { return renderer_; }
    void invalidate() { if (window_) InvalidateRect(window_, nullptr, FALSE); }
    void appearance(const AppSettings& settings);
private:
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    HWND window_ = nullptr;
    bool passive_ = false;
    Dispatch dispatch_;
    Renderer renderer_;
};
