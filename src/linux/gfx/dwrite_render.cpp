// Drawing DirectWrite layouts with Cairo: each Pango run's glyphs in its font, positioned as
// measured, in the context's source; ranges with a drawing effect in that brush (applyBrush).
// The fonts keep the measuring context's options except the antialiasing the target asked for.
#include "dwrite_internal.hpp"

#include <algorithm>

namespace lm_dwrite {
namespace {
constexpr double unit = 1.0 / PANGO_SCALE;

bool sameInk(const CharStyle* a, const CharStyle* b) {
    return (a ? a->effect.Get() : nullptr) == (b ? b->effect.Get() : nullptr) && (a && a->underline) == (b && b->underline);
}

// A missing glyph's box (as a font's .notdef): an outline the width of its advance.
void missingBox(cairo_t* context, double x, double baseline, double width, double ascent) {
    const double left = x + width * 0.1, right = x + width * 0.9, top = baseline - ascent * 0.7, line = std::max(1.0, width * 0.08);
    cairo_rectangle(context, left, top, right - left, baseline - top);
    // Counter-clockwise: a hole under the winding rule.
    cairo_move_to(context, left + line, top + line);
    cairo_line_to(context, left + line, baseline - line);
    cairo_line_to(context, right - line, baseline - line);
    cairo_line_to(context, right - line, top + line);
    cairo_close_path(context);
}
} // namespace

void TextLayout::drawRun(cairo_t* context, PangoGlyphItem* run, double x, double baseline, cairo_pattern_t* paint,
                         cairo_antialias_t antialias, cairo_scaled_font_t*& installed) const {
    PangoFont* font = run->item->analysis.font;
    cairo_scaled_font_t* scaled = font ? pango_cairo_font_get_scaled_font(PANGO_CAIRO_FONT(font)) : nullptr;
    if (!scaled || cairo_scaled_font_status(scaled) != CAIRO_STATUS_SUCCESS) return;
    if (scaled != installed) {
        cairo_set_scaled_font(context, scaled); // Cairo rasterizes it again for the context's transform.
        cairo_font_options_t* options = cairo_font_options_create();
        cairo_scaled_font_get_font_options(scaled, options);
        if (antialias != CAIRO_ANTIALIAS_DEFAULT && cairo_font_options_get_antialias(options) != antialias) {
            cairo_font_options_set_antialias(options, antialias);
            cairo_set_font_options(context, options);
        }
        cairo_font_options_destroy(options);
        installed = scaled;
    }
    PangoFontMetrics* metrics = nullptr; // Only for underlines and missing glyphs.
    const auto fontMetrics = [&] { return metrics ? metrics : (metrics = pango_font_get_metrics(font, nullptr)); };
    thread_local std::vector<cairo_glyph_t> glyphs;
    glyphs.clear();
    const PangoGlyphString* string = run->glyphs;
    const CharStyle* style = string->num_glyphs ? styleAt(run->item->offset + string->log_clusters[0]) : nullptr;
    double start = x;
    bool shapes = false; // Missing-glyph boxes in the path.
    // Draws the glyphs gathered since `start` (and their underline) in their style's ink.
    const auto flush = [&](double end) {
        const bool effect = style && style->effect && lm_gfx::applyBrush(context, style->effect.Get());
        if (!glyphs.empty()) cairo_show_glyphs(context, glyphs.data(), static_cast<int>(glyphs.size()));
        if (style && style->underline && end > start) {
            const double thickness = std::max(1, pango_font_metrics_get_underline_thickness(fontMetrics())) * unit;
            cairo_rectangle(context, start, baseline - pango_font_metrics_get_underline_position(fontMetrics()) * unit, end - start, thickness);
            shapes = true;
        }
        if (shapes) cairo_fill(context);
        if (effect) cairo_set_source(context, paint);
        glyphs.clear();
        shapes = false;
    };
    int advance = 0;
    for (int i = 0; i < string->num_glyphs; ++i) {
        const PangoGlyphInfo& glyph = string->glyphs[i];
        const double left = x + advance * unit;
        const CharStyle* at = styleAt(run->item->offset + string->log_clusters[i]);
        if (!sameInk(at, style)) {
            flush(left);
            start = left;
            style = at;
        }
        if (glyph.glyph & PANGO_GLYPH_UNKNOWN_FLAG) {
            missingBox(context, left, baseline, glyph.geometry.width * unit, pango_font_metrics_get_ascent(fontMetrics()) * unit);
            shapes = true;
        } else if (glyph.glyph != PANGO_GLYPH_EMPTY) {
            glyphs.push_back({glyph.glyph, left + glyph.geometry.x_offset * unit, baseline + glyph.geometry.y_offset * unit});
        }
        advance += glyph.geometry.width;
    }
    flush(x + advance * unit);
    if (metrics) pango_font_metrics_unref(metrics);
}

void TextLayout::draw(cairo_t* context, float x, float y) {
    layOut();
    cairo_save(context);
    cairo_new_path(context);
    cairo_font_options_t* options = cairo_font_options_create();
    cairo_get_font_options(context, options);
    const cairo_antialias_t antialias = cairo_font_options_get_antialias(options);
    cairo_font_options_destroy(options);
    cairo_pattern_t* paint = cairo_pattern_reference(cairo_get_source(context));
    cairo_scaled_font_t* installed = nullptr;
    for (const Line& line : lines_) {
        double pen = static_cast<double>(x) + line.left;
        const double baseline = static_cast<double>(y) + metrics_.top + line.top + line.baseline;
        for (const GSList* item = line.pango->runs; item; item = item->next) {
            auto* run = static_cast<PangoGlyphItem*>(item->data);
            drawRun(context, run, pen, baseline - run->y_offset * unit, paint, antialias, installed);
            pen += pango_glyph_string_get_width(run->glyphs) * unit;
        }
    }
    cairo_pattern_destroy(paint);
    cairo_restore(context);
}
} // namespace lm_dwrite

namespace lm_gfx {
void drawTextLayout(cairo_t* context, IDWriteTextLayout* layout, float x, float y) {
    if (auto* own = lm_dwrite::TextLayout::from(layout)) own->draw(context, x, y);
}

// The same layout and clip as a text layout of the box's size drawn at its corner with
// D2D1_DRAW_TEXT_OPTIONS_CLIP, so both draw the same pixels.
void drawText(cairo_t* context, const wchar_t* text, UINT32 length, IDWriteTextFormat* format, float left, float top, float right,
              float bottom, bool clip) {
    const float width = std::max(0.0f, right - left), height = std::max(0.0f, bottom - top);
    lm_dwrite::ComPtr<lm_dwrite::TextLayout> layout;
    if (FAILED(lm_dwrite::TextLayout::create(text, length, format, width, height, layout))) return;
    cairo_save(context);
    if (clip) {
        cairo_rectangle(context, left, top, width, height);
        cairo_clip(context);
    }
    layout->draw(context, left, top);
    cairo_restore(context);
}
} // namespace lm_gfx
