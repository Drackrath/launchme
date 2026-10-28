#pragma once
// Linux only: the contracts between the Direct2D (d2d_*.cpp, Cairo), DirectWrite (dwrite_*.cpp,
// Pango) and WIC (wic_*.cpp) ports, and the window layer that shows a render target's pixels
// (src/linux/window). Everything else talks through the COM-shaped interfaces in src/linux/win32.
#include "../win32/win32_base.hpp"

#include <cairo.h>
#include <cstdint>

struct IDWriteTextFormat;
struct IDWriteTextLayout;

namespace lm_gfx {
// ---- Window layer (src/linux/window, implemented there) ----------------------------------
// The window's drawable size in pixels (0 x 0 for an unknown window).
void windowPixelSize(HWND window, UINT32& width, UINT32& height);
// Shows a finished frame: premultiplied BGRA (CAIRO_FORMAT_ARGB32 on little-endian), `stride`
// bytes a row. Called by ID2D1HwndRenderTarget::EndDraw. False when the window is gone.
bool presentWindow(HWND window, const uint8_t* pixels, UINT32 width, UINT32 height, UINT32 stride);

// ---- DirectWrite → Direct2D (implemented in dwrite_*.cpp) --------------------------------
// Draws `layout` with its top-left corner at (x, y) in the context's user space, with the
// context's current source as the text's paint. A range with a drawing effect
// (IDWriteTextLayout::SetDrawingEffect, an ID2D1Brush) is painted through applyBrush.
void drawTextLayout(cairo_t* context, IDWriteTextLayout* layout, float x, float y);
// DrawText: `text` laid out in `format` within the box, drawn as drawTextLayout would.
// clip: D2D1_DRAW_TEXT_OPTIONS_CLIP (nothing outside the box).
void drawText(cairo_t* context, const wchar_t* text, UINT32 length, IDWriteTextFormat* format,
              float left, float top, float right, float bottom, bool clip);

// ---- Direct2D → DirectWrite (implemented in d2d_*.cpp) -----------------------------------
// Sets the context's source to `brush` (an ID2D1Brush passed as a drawing effect). False when
// it is not one of this port's brushes (the current source stays).
bool applyBrush(cairo_t* context, IUnknown* brush);
} // namespace lm_gfx
