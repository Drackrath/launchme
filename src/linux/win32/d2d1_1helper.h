#pragma once
// Linux only: the Direct2D 1.1 helpers (as the Windows SDK's d2d1_1helper.h) the launcher uses.
#include "d2d1_1.h"

namespace D2D1 {
inline D2D1_BITMAP_PROPERTIES1 BitmapProperties1(D2D1_BITMAP_OPTIONS bitmapOptions = D2D1_BITMAP_OPTIONS_NONE,
                                                 const D2D1_PIXEL_FORMAT pixelFormat = D2D1::PixelFormat(), FLOAT dpiX = 96.0f,
                                                 FLOAT dpiY = 96.0f, ID2D1ColorContext* colorContext = nullptr) {
    return {pixelFormat, dpiX, dpiY, bitmapOptions, colorContext};
}
inline D2D1_LAYER_PARAMETERS1 LayerParameters1(const D2D1_RECT_F& contentBounds = D2D1::InfiniteRect(), ID2D1Geometry* geometricMask = nullptr,
                                               D2D1_ANTIALIAS_MODE maskAntialiasMode = D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                               D2D1_MATRIX_3X2_F maskTransform = D2D1::IdentityMatrix(), FLOAT opacity = 1.0f,
                                               ID2D1Brush* opacityBrush = nullptr, D2D1_LAYER_OPTIONS1 layerOptions = D2D1_LAYER_OPTIONS1_NONE) {
    return {contentBounds, geometricMask, maskAntialiasMode, maskTransform, opacity, opacityBrush, layerOptions};
}
} // namespace D2D1
