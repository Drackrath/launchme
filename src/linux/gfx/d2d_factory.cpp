// The Direct2D factory: geometries, stroke styles and render targets.
#include "d2d_internal.hpp"

#include <algorithm>
#include <numeric>

namespace lm_d2d {
namespace {
cairo_line_cap_t capFor(D2D1_CAP_STYLE cap) {
    switch (cap) {
    case D2D1_CAP_STYLE_SQUARE: return CAIRO_LINE_CAP_SQUARE;
    case D2D1_CAP_STYLE_ROUND:
    case D2D1_CAP_STYLE_TRIANGLE: return CAIRO_LINE_CAP_ROUND; // Cairo has no pointed cap; round reaches as far.
    default: return CAIRO_LINE_CAP_BUTT;
    }
}

class Factory final : public lm_com::Object<ID2D1Factory> {
public:
    HRESULT STDMETHODCALLTYPE CreateRectangleGeometry(const D2D1_RECT_F* rect, ID2D1RectangleGeometry** geometry) override {
        return rect ? createRectangleGeometry(*rect, geometry) : E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE CreateRoundedRectangleGeometry(const D2D1_ROUNDED_RECT* rounded, ID2D1RoundedRectangleGeometry** geometry) override {
        return rounded ? createRoundedRectangleGeometry(*rounded, geometry) : E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE CreateEllipseGeometry(const D2D1_ELLIPSE* ellipse, ID2D1EllipseGeometry** geometry) override {
        return ellipse ? createEllipseGeometry(*ellipse, geometry) : E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE CreateGeometryGroup(D2D1_FILL_MODE mode, ID2D1Geometry** geometries, UINT32 count, ID2D1GeometryGroup** group) override {
        return createGeometryGroup(mode, geometries, count, group);
    }
    HRESULT STDMETHODCALLTYPE CreateTransformedGeometry(ID2D1Geometry* source, const D2D1_MATRIX_3X2_F* transform,
                                                        ID2D1TransformedGeometry** geometry) override {
        return transform ? createTransformedGeometry(source, *transform, geometry) : E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE CreatePathGeometry(ID2D1PathGeometry** geometry) override { return createPathGeometry(geometry); }
    HRESULT STDMETHODCALLTYPE CreateStrokeStyle(const D2D1_STROKE_STYLE_PROPERTIES* properties, const FLOAT* dashes, UINT32 count,
                                                ID2D1StrokeStyle** style) override {
        if (!style) return E_POINTER;
        *style = nullptr;
        if (!properties || (properties->dashStyle == D2D1_DASH_STYLE_CUSTOM && (!dashes || !count))) return E_INVALIDARG;
        *style = new StrokeStyle(*properties, dashes, count);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateWicBitmapRenderTarget(IWICBitmap* target, const D2D1_RENDER_TARGET_PROPERTIES* properties,
                                                          ID2D1RenderTarget** renderTarget) override {
        return properties ? createWicTarget(target, *properties, renderTarget) : E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE CreateHwndRenderTarget(const D2D1_RENDER_TARGET_PROPERTIES* properties,
                                                     const D2D1_HWND_RENDER_TARGET_PROPERTIES* window,
                                                     ID2D1HwndRenderTarget** target) override {
        return properties && window ? createHwndTarget(*properties, *window, target) : E_INVALIDARG;
    }
    // There are no GDI device contexts to draw into.
    HRESULT STDMETHODCALLTYPE CreateDCRenderTarget(const D2D1_RENDER_TARGET_PROPERTIES*, ID2D1DCRenderTarget** target) override {
        if (target) *target = nullptr;
        return E_NOTIMPL;
    }
};
} // namespace

StrokeStyle::StrokeStyle(const D2D1_STROKE_STYLE_PROPERTIES& properties, const FLOAT* dashes, UINT32 count) : properties_(properties) {
    switch (properties.dashStyle) {
    case D2D1_DASH_STYLE_DASH: dashes_ = {2, 2}; break;
    case D2D1_DASH_STYLE_DOT: dashes_ = {0, 2}; break;
    case D2D1_DASH_STYLE_DASH_DOT: dashes_ = {2, 2, 0, 2}; break;
    case D2D1_DASH_STYLE_DASH_DOT_DOT: dashes_ = {2, 2, 0, 2, 0, 2}; break;
    case D2D1_DASH_STYLE_CUSTOM: dashes_.assign(dashes, dashes + count); break;
    default: break;
    }
    // Cairo refuses (and stops drawing on) negative or all-zero dashes: solid instead.
    if (std::any_of(dashes_.begin(), dashes_.end(), [](double dash) { return dash < 0; }) ||
        std::accumulate(dashes_.begin(), dashes_.end(), 0.0) <= 0)
        dashes_.clear();
}

const StrokeStyle* StrokeStyle::from(ID2D1StrokeStyle* style) { return dynamic_cast<const StrokeStyle*>(style); }

// Direct2D's miter limit is in half stroke widths, Cairo's in stroke widths. A plain MITER join
// clips long miters on Direct2D; Cairo bevels them.
void StrokeStyle::apply(cairo_t* context, double width) const {
    const bool dashed = !dashes_.empty();
    cairo_set_line_cap(context, capFor(dashed ? properties_.dashCap : properties_.startCap));
    switch (properties_.lineJoin) {
    case D2D1_LINE_JOIN_BEVEL: cairo_set_line_join(context, CAIRO_LINE_JOIN_BEVEL); break;
    case D2D1_LINE_JOIN_ROUND: cairo_set_line_join(context, CAIRO_LINE_JOIN_ROUND); break;
    default: cairo_set_line_join(context, CAIRO_LINE_JOIN_MITER); break;
    }
    cairo_set_miter_limit(context, std::max(1.0f, properties_.miterLimit) * 0.5);
    if (!dashed || width <= 0) { cairo_set_dash(context, nullptr, 0, 0); return; }
    // Cairo's image stroker drops zero-length dashes; Direct2D draws them as dots with round or
    // square dash caps: a hair-length dash, taken from the gap after it.
    std::vector<double> lengths;
    for (const double dash : dashes_) lengths.push_back(dash * width);
    const double hair = width * 0.01;
    for (size_t i = 0; i + 1 < lengths.size() && properties_.dashCap != D2D1_CAP_STYLE_FLAT; i += 2)
        if (lengths[i] == 0) { lengths[i] = hair; lengths[i + 1] = std::max(0.0, lengths[i + 1] - hair); }
    cairo_set_dash(context, lengths.data(), static_cast<int>(lengths.size()), properties_.dashOffset * width);
}

void applyDefaultStroke(cairo_t* context) {
    cairo_set_line_cap(context, CAIRO_LINE_CAP_BUTT);
    cairo_set_line_join(context, CAIRO_LINE_JOIN_MITER);
    cairo_set_miter_limit(context, 5); // Direct2D's 10 half widths.
    cairo_set_dash(context, nullptr, 0, 0);
}
} // namespace lm_d2d

HRESULT WINAPI D2D1CreateFactory(D2D1_FACTORY_TYPE, REFIID riid, const D2D1_FACTORY_OPTIONS*, void** factory) {
    if (!factory) return E_POINTER;
    *factory = nullptr;
    lm_d2d::ComPtr<ID2D1Factory> created;
    created.Attach(new lm_d2d::Factory);
    return created->QueryInterface(riid, factory);
}
