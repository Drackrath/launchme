// LayeredSurface on Linux: no GDI device context exists, so per-pixel-alpha windows (the in-game
// overlay, src/ui/overlay_window_linux.cpp) are not created and this surface stays empty.
#include "layered_surface.hpp"

LayeredSurface::~LayeredSurface() { reset(); }
bool LayeredSurface::resize(int, int) { return false; }
bool LayeredSurface::present(HWND) const { return false; }
void LayeredSurface::reset() { width_ = height_ = 0; }
