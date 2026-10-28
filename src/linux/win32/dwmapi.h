#pragma once
// Linux only: Desktop Window Manager attributes. The compositor (Wayland or X11's) decides
// corners, dark frames and composition; these calls succeed and change nothing.
#include "Windows.h"

enum DWMWINDOWATTRIBUTE {
    DWMWA_NCRENDERING_POLICY = 2,
    DWMWA_TRANSITIONS_FORCEDISABLED = 3,
    DWMWA_USE_IMMERSIVE_DARK_MODE = 20,
    DWMWA_WINDOW_CORNER_PREFERENCE = 33,
    DWMWA_BORDER_COLOR = 34,
    DWMWA_CAPTION_COLOR = 35,
    DWMWA_SYSTEMBACKDROP_TYPE = 38,
};
enum DWM_WINDOW_CORNER_PREFERENCE { DWMWCP_DEFAULT = 0, DWMWCP_DONOTROUND = 1, DWMWCP_ROUND = 2, DWMWCP_ROUNDSMALL = 3 };
struct MARGINS { int cxLeftWidth, cxRightWidth, cyTopHeight, cyBottomHeight; };

inline HRESULT DwmSetWindowAttribute(HWND, DWORD, const void*, DWORD) { return S_OK; }
inline HRESULT DwmGetWindowAttribute(HWND, DWORD, void*, DWORD) { return E_NOTIMPL; }
inline HRESULT DwmIsCompositionEnabled(BOOL* enabled) { if (enabled) *enabled = TRUE; return S_OK; }
inline HRESULT DwmExtendFrameIntoClientArea(HWND, const MARGINS*) { return S_OK; }
inline HRESULT DwmFlush() { return S_OK; }
