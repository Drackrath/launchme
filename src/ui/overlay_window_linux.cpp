// The in-game Guide's own window on Linux: not created. Wayland lets no client draw over another
// one's full-screen surface, and gamescope (Steam Deck) shows only the focused game, so the Guide
// opens in the launcher's window instead (Launcher::openGameOverlay's fallback when create fails).
#include "overlay_window.hpp"

OverlayWindow::~OverlayWindow() = default;
LRESULT CALLBACK OverlayWindow::proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) { return DefWindowProcW(window, message, wparam, lparam); }
bool OverlayWindow::create(bool, Dispatch) { return false; }
bool OverlayWindow::show(const RECT&, bool, const Activate&) { return false; }
void OverlayWindow::hide() {}
void OverlayWindow::appearance(const AppSettings&) {}
