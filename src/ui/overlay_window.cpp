#include "overlay_window.hpp"

OverlayWindow::~OverlayWindow() { if (window_) DestroyWindow(window_); }
LRESULT CALLBACK OverlayWindow::proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<OverlayWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (message == WM_ERASEBKGND) return 1;
    if (self && message == WM_MOUSEACTIVATE && self->passive_) return MA_NOACTIVATE;
    if (self && message == WM_NCHITTEST && self->passive_) return HTTRANSPARENT;
    if (self && message == WM_SIZE) self->renderer_.resize();
    if (self && self->dispatch_) return self->dispatch_(window, message, wparam, lparam);
    return DefWindowProcW(window, message, wparam, lparam);
}
bool OverlayWindow::create(bool passive, Dispatch dispatch) {
    if (window_) return true;
    passive_ = passive; dispatch_ = std::move(dispatch);
    WNDCLASSW type{}; type.lpfnWndProc = proc; type.hInstance = GetModuleHandleW(nullptr);
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW); type.lpszClassName = L"LaunchmeGameOverlay";
    if (!RegisterClassW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    window_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST |
        (passive ? WS_EX_NOACTIVATE | WS_EX_TRANSPARENT : 0), type.lpszClassName, L"Launchme Guide",
        WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, type.hInstance, this);
    if (window_ && renderer_.initialize(window_, true)) return true;
    if (window_) DestroyWindow(window_);
    window_ = nullptr; return false;
}
bool OverlayWindow::show(const RECT& rect, bool activate, const Activate& focus) {
    RECT previous{};
    if (window_ && !activate && IsWindowVisible(window_) && GetWindowRect(window_, &previous) && EqualRect(&previous, &rect)) return true;
    if (!window_ || !SetWindowPos(window_, HWND_TOPMOST, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
        SWP_SHOWWINDOW | SWP_NOACTIVATE)) return false;
    // A layered HWND has no visible pixels until its first UpdateLayeredWindow.
    // Paint synchronously before requesting focus. XInput polling does not grant
    // Windows foreground permission: denial must not hide the Guide again.
    invalidate(); UpdateWindow(window_);
    if (activate && focus) focus(window_);
    return IsWindowVisible(window_) != FALSE;
}
void OverlayWindow::hide() { if (window_) ShowWindow(window_, SW_HIDE); }
void OverlayWindow::appearance(const AppSettings& settings) {
    renderer_.setDrawerStyle(settings.drawer); renderer_.setFontFamily(settings.font_family);
    renderer_.setUiAccent(settings.ui_accent_rgb); renderer_.setTileShape(settings.tile_shape);
    renderer_.setPanelBorders(settings.panel_borders);
    invalidate();
}
