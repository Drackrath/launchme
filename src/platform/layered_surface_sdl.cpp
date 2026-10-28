// LayeredSurface on Linux (layered_surface_windows.cpp on Windows): memory the renderer draws into,
// copied as it is (alpha included, no blending) into the SDL window's surface.
#include "layered_surface.hpp"

#include <SDL3/SDL.h>

#include <new>

LayeredSurface::~LayeredSurface() { reset(); }

void LayeredSurface::reset() {
    pixels_.clear();
    pixels_.shrink_to_fit();
    width_ = height_ = 0;
}

bool LayeredSurface::resize(int width, int height) {
    if (!pixels_.empty() && width_ == width && height_ == height) return true;
    reset();
    if (!acceptable(width, height)) return false;
    try {
        pixels_.assign(static_cast<size_t>(width) * height, 0u); // Transparent, as a new DIB section.
    } catch (const std::bad_alloc&) {
        reset();
        return false;
    }
    width_ = width; height_ = height;
    return true;
}

bool LayeredSurface::present(platform::NativeWindow window) const {
    if (pixels_.empty() || !window) return false;
    SDL_Surface* target = SDL_GetWindowSurface(window);
    if (!target) return false;
    // A view of the pixels (SDL does not copy or free them), copied without blending so the
    // window keeps their alpha.
    SDL_Surface* source = SDL_CreateSurfaceFrom(width_, height_, SDL_PIXELFORMAT_ARGB8888,
                                                const_cast<std::uint32_t*>(pixels_.data()), stride());
    if (!source) return false;
    SDL_SetSurfaceBlendMode(source, SDL_BLENDMODE_NONE);
    const bool copied = SDL_BlitSurface(source, nullptr, target, nullptr);
    SDL_DestroySurface(source);
    return copied && SDL_UpdateWindowSurface(window);
}
