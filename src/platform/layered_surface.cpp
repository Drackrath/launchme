#include "layered_surface.hpp"

LayeredSurface::~LayeredSurface() { reset(); }
void LayeredSurface::reset() {
    if (previous_) SelectObject(dc_, previous_);
    if (bitmap_) DeleteObject(bitmap_);
    if (dc_) DeleteDC(dc_);
    previous_ = nullptr; bitmap_ = nullptr; dc_ = nullptr; width_ = height_ = 0;
}
bool LayeredSurface::resize(int width, int height) {
    if (dc_ && width_ == width && height_ == height) return true;
    reset();
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384 ||
        static_cast<uint64_t>(width) * height > 64u * 1024 * 1024) return false;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    dc_ = CreateCompatibleDC(nullptr);
    if (dc_) bitmap_ = CreateDIBSection(dc_, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!bitmap_) { reset(); return false; }
    previous_ = SelectObject(dc_, bitmap_);
    width_ = width; height_ = height;
    return true;
}
bool LayeredSurface::present(HWND window) const {
    RECT position{};
    if (!dc_ || !GetWindowRect(window, &position)) return false;
    POINT destination{position.left, position.top}, source{};
    SIZE size{width_, height_};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    return UpdateLayeredWindow(window, nullptr, &destination, &size, dc_, &source, 0, &blend, ULW_ALPHA) != FALSE;
}
