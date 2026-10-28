#pragma once
#include "native_window.hpp"
#include <cstdint>
#include <vector>

// Premultiplied BGRA backing store for small/event-driven transparent windows.
// The main HWND renderer keeps its existing GPU presentation path.
//
// Windows: a DIB section the renderer binds a DC render target to, presented with
// UpdateLayeredWindow. Linux: plain memory (pixels()/stride(), for a raster canvas), presented by
// copying it into the SDL window's surface; a window created with SDL_WINDOW_TRANSPARENT shows
// its alpha.
class LayeredSurface {
public:
    ~LayeredSurface();
    LayeredSurface() = default;
    LayeredSurface(const LayeredSurface&) = delete;
    LayeredSurface& operator=(const LayeredSurface&) = delete;
    bool resize(int width, int height);
    bool present(platform::NativeWindow window) const;
    int width() const { return width_; }
    int height() const { return height_; }
#ifdef _WIN32
    HDC dc() const { return dc_; }
    RECT bounds() const { return {0, 0, width_, height_}; }
#else
    // Row-major premultiplied BGRA (B, G, R, A bytes; SDL_PIXELFORMAT_ARGB8888 on little-endian).
    std::uint32_t* pixels() { return pixels_.empty() ? nullptr : pixels_.data(); }
    const std::uint32_t* pixels() const { return pixels_.empty() ? nullptr : pixels_.data(); }
    int stride() const { return width_ * 4; }
#endif
    // The sizes both systems accept: at most 16384 on a side and 64 Mi pixels (256 MiB).
    static bool acceptable(int width, int height) {
        return width > 0 && height > 0 && width <= 16384 && height <= 16384 &&
               static_cast<std::uint64_t>(width) * height <= 64u * 1024 * 1024;
    }
private:
    void reset();
#ifdef _WIN32
    HDC dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ previous_ = nullptr;
#else
    std::vector<std::uint32_t> pixels_;
#endif
    int width_ = 0, height_ = 0;
};
