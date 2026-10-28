#pragma once
#include <Windows.h> // Linux: src/linux/win32.
#include <cstdint>

// Premultiplied BGRA backing store for small/event-driven transparent windows.
// The main HWND renderer keeps its existing GPU presentation path.
class LayeredSurface {
public:
    ~LayeredSurface();
    LayeredSurface() = default;
    LayeredSurface(const LayeredSurface&) = delete;
    LayeredSurface& operator=(const LayeredSurface&) = delete;
    bool resize(int width, int height);
    bool present(HWND window) const;
    HDC dc() const { return dc_; }
    RECT bounds() const { return {0, 0, width_, height_}; }
private:
    void reset();
    HDC dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ previous_ = nullptr;
    int width_ = 0, height_ = 0;
};
