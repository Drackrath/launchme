// Text on Direct2D render targets: the brush becomes the Cairo source and the DirectWrite port
// (dwrite_*.cpp, Pango) lays the glyphs out and draws them (lm_gfx::drawText/drawTextLayout).
#include "d2d_internal.hpp"
#include "../win32/dwrite.h"

namespace lm_d2d {
// The brush as the source (a bitmap brush's opacity is not applied to text), the world
// transform as Cairo's matrix and as user data for brushes in drawing effects, the text
// antialiasing as the font options.
cairo_t* Canvas::prepareText(ID2D1Brush* brush) {
    const auto* own = Brush::from(brush);
    cairo_t* context = own ? prepare() : nullptr;
    if (!context) return nullptr;
    text_world_ = world();
    cairo_set_user_data(context, worldKey(), &text_world_, nullptr);
    cairo_set_source(context, own->paint().pattern.get());
    cairo_font_options_t* options = cairo_font_options_create();
    cairo_font_options_set_antialias(options, text_antialias_ == D2D1_TEXT_ANTIALIAS_MODE_ALIASED ? CAIRO_ANTIALIAS_NONE : CAIRO_ANTIALIAS_GRAY);
    cairo_set_font_options(context, options);
    cairo_font_options_destroy(options);
    return context;
}

void Canvas::drawText(const WCHAR* text, UINT32 length, IDWriteTextFormat* format, const D2D1_RECT_F* rect, ID2D1Brush* brush,
                      D2D1_DRAW_TEXT_OPTIONS options) {
    cairo_t* context = text && format && rect ? prepareText(brush) : nullptr;
    if (!context) return;
    lm_gfx::drawText(context, text, length, format, rect->left, rect->top, rect->right, rect->bottom,
                     (options & D2D1_DRAW_TEXT_OPTIONS_CLIP) != 0);
    cairo_set_user_data(context, worldKey(), nullptr, nullptr);
}

// D2D1_DRAW_TEXT_OPTIONS_CLIP: nothing outside the layout's box (its maximum width and height).
void Canvas::drawTextLayout(D2D1_POINT_2F origin, IDWriteTextLayout* layout, ID2D1Brush* brush, D2D1_DRAW_TEXT_OPTIONS options) {
    cairo_t* context = layout ? prepareText(brush) : nullptr;
    if (!context) return;
    cairo_save(context);
    DWRITE_TEXT_METRICS metrics{};
    if ((options & D2D1_DRAW_TEXT_OPTIONS_CLIP) && SUCCEEDED(layout->GetMetrics(&metrics))) {
        cairo_rectangle(context, origin.x, origin.y, metrics.layoutWidth, metrics.layoutHeight);
        cairo_clip(context);
    }
    lm_gfx::drawTextLayout(context, layout, origin.x, origin.y);
    cairo_restore(context);
    cairo_set_user_data(context, worldKey(), nullptr, nullptr);
}
} // namespace lm_d2d
