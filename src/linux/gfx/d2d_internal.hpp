#pragma once
// Linux only: the Direct2D port's private types, shared by d2d_*.cpp. Pixels are Cairo image
// surfaces (premultiplied ARGB32, or A8 for alpha-only bitmaps); geometry is kept as figures of
// lines and cubic Béziers and replayed into Cairo under any transform.
#include "gfx_internal.hpp"
#include "../win32/d2d1_2.h"
#include "../win32/wrl/client.h"

#include <cairo.h>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace lm_d2d {
using Microsoft::WRL::ComPtr;

// ---- Cairo handles ------------------------------------------------------------------------

// A counted reference to a Cairo object (Cairo's own reference count).
template <class T, T* (*Reference)(T*), void (*Destroy)(T*)>
class Handle {
public:
    Handle() = default;
    explicit Handle(T* adopted) : value_(adopted) {}
    Handle(const Handle& other) : value_(other.value_ ? Reference(other.value_) : nullptr) {}
    Handle(Handle&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
    Handle& operator=(Handle other) noexcept { std::swap(value_, other.value_); return *this; }
    ~Handle() { if (value_) Destroy(value_); }
    T* get() const { return value_; }
    explicit operator bool() const { return value_ != nullptr; }
private:
    T* value_ = nullptr;
};
using Surface = Handle<cairo_surface_t, cairo_surface_reference, cairo_surface_destroy>;
using Pattern = Handle<cairo_pattern_t, cairo_pattern_reference, cairo_pattern_destroy>;

// A zero-filled image surface; empty when Cairo cannot allocate it.
Surface imageSurface(cairo_format_t format, int width, int height);
// A fresh stamp for content that changed (bitmap pixels, effect settings): never repeats.
uint64_t newVersion();

// ---- Matrices -----------------------------------------------------------------------------

inline cairo_matrix_t toCairo(const D2D1_MATRIX_3X2_F& m) {
    cairo_matrix_t result;
    cairo_matrix_init(&result, m._11, m._12, m._21, m._22, m._31, m._32);
    return result;
}
inline cairo_matrix_t identityMatrix() {
    cairo_matrix_t result;
    cairo_matrix_init_identity(&result);
    return result;
}
// `first` then `second`.
inline cairo_matrix_t multiply(const cairo_matrix_t& first, const cairo_matrix_t& second) {
    cairo_matrix_t result;
    cairo_matrix_multiply(&result, &first, &second);
    return result;
}
inline D2D1_POINT_2F apply(const cairo_matrix_t& m, D2D1_POINT_2F p) {
    return {static_cast<float>(m.xx * p.x + m.xy * p.y + m.x0), static_cast<float>(m.yx * p.x + m.yy * p.y + m.y0)};
}
// The factor a matrix scales areas by, as a length (1 for rotations).
inline double scaleOf(const cairo_matrix_t& m) {
    const double determinant = m.xx * m.yy - m.xy * m.yx;
    return determinant < 0 ? std::sqrt(-determinant) : std::sqrt(determinant);
}

// ---- Geometry (d2d_geometry.cpp) ----------------------------------------------------------

// Receives a geometry's figures: lines and cubic Béziers (arcs and quadratics are converted).
class PathBuilder {
public:
    virtual void begin(D2D1_POINT_2F point, bool filled) = 0;
    virtual void line(D2D1_POINT_2F point) = 0;
    virtual void cubic(D2D1_POINT_2F control1, D2D1_POINT_2F control2, D2D1_POINT_2F end) = 0;
    virtual void end(bool closed) = 0;
protected:
    ~PathBuilder() = default;
};

// Every geometry object of this port.
class Geometry {
public:
    virtual ~Geometry() = default;
    // Writes every figure with its points mapped by `matrix`.
    virtual void emit(PathBuilder& out, const cairo_matrix_t& matrix) const = 0;
    virtual D2D1_FILL_MODE fillMode() const { return D2D1_FILL_MODE_ALTERNATE; }
    static const Geometry* from(ID2D1Geometry* geometry);
};

struct Point { double x, y; };
struct Polyline { std::vector<Point> points; bool closed = false; bool filled = false; };
// The figures as straight segments within `tolerance` of the curves (points mapped by `matrix`).
std::vector<Polyline> flatten(const Geometry& geometry, const cairo_matrix_t& matrix, double tolerance);
// The figures as Cairo's current path in user space: only filled figures when `fill`, which
// also sets the fill rule.
void appendPath(cairo_t* context, const Geometry& geometry, bool fill);
// Shapes drawn directly (FillRoundedRectangle, DrawEllipse…) as Cairo's current path: the
// figure their geometry objects have.
void appendRoundedRectangle(cairo_t* context, const D2D1_ROUNDED_RECT& rounded);
void appendEllipse(cairo_t* context, const D2D1_ELLIPSE& ellipse);

HRESULT createRectangleGeometry(const D2D1_RECT_F& rect, ID2D1RectangleGeometry** geometry);
HRESULT createRoundedRectangleGeometry(const D2D1_ROUNDED_RECT& rounded, ID2D1RoundedRectangleGeometry** geometry);
HRESULT createEllipseGeometry(const D2D1_ELLIPSE& ellipse, ID2D1EllipseGeometry** geometry);
HRESULT createGeometryGroup(D2D1_FILL_MODE fillMode, ID2D1Geometry** geometries, UINT32 count, ID2D1GeometryGroup** group);
HRESULT createTransformedGeometry(ID2D1Geometry* source, const D2D1_MATRIX_3X2_F& transform, ID2D1TransformedGeometry** geometry);
HRESULT createPathGeometry(ID2D1PathGeometry** geometry);

// CombineWithGeometry (d2d_combine.cpp): `first` and `second` (mapped by `secondTransform`) as
// polygons within `tolerance`, the result's outline written to `sink` as closed figures.
HRESULT combineGeometries(const Geometry& first, const Geometry& second, const cairo_matrix_t& secondTransform, D2D1_COMBINE_MODE mode,
                          double tolerance, ID2D1SimplifiedGeometrySink* sink);

// ---- Stroke styles (d2d_factory.cpp) ------------------------------------------------------

class StrokeStyle final : public lm_com::Object<ID2D1StrokeStyle> {
public:
    StrokeStyle(const D2D1_STROKE_STYLE_PROPERTIES& properties, const FLOAT* dashes, UINT32 count);
    // Sets Cairo's line caps, join, miter limit and dashes for a stroke `width` wide.
    void apply(cairo_t* context, double width) const;
    static const StrokeStyle* from(ID2D1StrokeStyle* style);
private:
    D2D1_STROKE_STYLE_PROPERTIES properties_;
    std::vector<double> dashes_; // In stroke widths.
};
// The defaults of a stroke without a style: flat caps, mitred joins.
void applyDefaultStroke(cairo_t* context);

// ---- Bitmaps (d2d_bitmap.cpp) -------------------------------------------------------------

// Effect inputs: bitmaps and effect outputs. `scale` is the resolution the result is wanted at
// (pixels per DIP of the image); the result's origin is in those pixels.
struct ImageData {
    Surface surface; // Premultiplied ARGB32; empty for nothing.
    int x = 0, y = 0;
};
class ImageSource {
public:
    virtual ~ImageSource() = default;
    virtual ImageData render(double scale) = 0;
    // Changes whenever what render() returns would change.
    virtual uint64_t version() const = 0;
    static ImageSource* from(ID2D1Image* image);
};

class Bitmap final : public lm_com::Object<ID2D1Bitmap1>, public ImageSource {
public:
    // Pixels from memory in `format` (nullptr: transparent). Fails for formats it cannot hold
    // (block-compressed ones among them).
    static HRESULT create(D2D1_SIZE_U size, const void* data, UINT32 pitch, D2D1_PIXEL_FORMAT format, float dpiX, float dpiY,
                          ComPtr<Bitmap>& out);
    static Bitmap* from(ID2D1Image* image);

    D2D1_SIZE_F STDMETHODCALLTYPE GetSize() const override;
    D2D1_SIZE_U STDMETHODCALLTYPE GetPixelSize() const override;
    D2D1_PIXEL_FORMAT STDMETHODCALLTYPE GetPixelFormat() const override { return format_; }
    HRESULT STDMETHODCALLTYPE CopyFromBitmap(const D2D1_POINT_2U* destPoint, ID2D1Bitmap* bitmap, const D2D1_RECT_U* srcRect) override;
    HRESULT STDMETHODCALLTYPE CopyFromRenderTarget(const D2D1_POINT_2U* destPoint, ID2D1RenderTarget* renderTarget,
                                                   const D2D1_RECT_U* srcRect) override;
    HRESULT STDMETHODCALLTYPE CopyFromMemory(const D2D1_RECT_U* dstRect, const void* srcData, UINT32 pitch) override;

    ImageData render(double scale) override;
    uint64_t version() const override { return version_; }

    cairo_surface_t* surface() const { return surface_.get(); }
    int width() const { return width_; }
    int height() const { return height_; }
    // Pixels per DIP.
    double scaleX() const { return dpi_x_ / 96.0; }
    double scaleY() const { return dpi_y_ / 96.0; }
    bool alphaOnly() const { return format_.format == DXGI_FORMAT_A8_UNORM; }
    // The pixels changed (drawn into by its render target).
    void touch() { version_ = newVersion(); }
    // A premultiplied ARGB32 copy at `width` x `height` pixels (box-filtered when smaller); kept
    // until the pixels or the size change.
    Surface scaled(int width, int height);
private:
    Bitmap(Surface surface, int width, int height, D2D1_PIXEL_FORMAT format, float dpiX, float dpiY);
    HRESULT copyFrom(cairo_surface_t* source, const D2D1_POINT_2U* destPoint, const D2D1_RECT_U* srcRect);

    Surface surface_;
    int width_, height_;
    D2D1_PIXEL_FORMAT format_;
    float dpi_x_, dpi_y_;
    uint64_t version_;
    Surface scaled_;
    uint64_t scaled_version_ = 0;
};
// The pixel format a bitmap or render target gets for a requested one (unknown parts filled in).
D2D1_PIXEL_FORMAT resolveFormat(D2D1_PIXEL_FORMAT format);
// Interpolation modes as Cairo filters.
cairo_filter_t filterFor(D2D1_INTERPOLATION_MODE mode);

// ---- Brushes (d2d_brush.cpp) --------------------------------------------------------------

// What a brush paints with: the pattern (in the render target's world space, Cairo's user
// space while drawing) and the opacity the pattern could not take in (bitmap brushes).
struct Paint {
    Pattern pattern;
    double alpha = 1;
    // World-space box outside which the paint is transparent (a clamped radial gradient ending in
    // transparency): fills clip to it, so the gradient is computed only where it shows.
    bool bounded = false;
    double left = 0, top = 0, right = 0, bottom = 0;
};
class Brush {
public:
    virtual ~Brush() = default;
    // `extra`: an opacity multiplied in (a layer's).
    virtual Paint paint(double extra = 1) const = 0;
    static const Brush* from(IUnknown* brush);
};
HRESULT createSolidColorBrush(const D2D1_COLOR_F& color, const D2D1_BRUSH_PROPERTIES* properties, ID2D1SolidColorBrush** brush);
HRESULT createGradientStopCollection(const D2D1_GRADIENT_STOP* stops, UINT32 count, D2D1_EXTEND_MODE extendMode,
                                     ID2D1GradientStopCollection** collection);
HRESULT createLinearGradientBrush(const D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES& gradient, const D2D1_BRUSH_PROPERTIES* properties,
                                  ID2D1GradientStopCollection* stops, ID2D1LinearGradientBrush** brush);
HRESULT createRadialGradientBrush(const D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES& gradient, const D2D1_BRUSH_PROPERTIES* properties,
                                  ID2D1GradientStopCollection* stops, ID2D1RadialGradientBrush** brush);
HRESULT createBitmapBrush(ID2D1Bitmap* bitmap, const D2D1_BITMAP_BRUSH_PROPERTIES* bitmapProperties, const D2D1_BRUSH_PROPERTIES* properties,
                          ID2D1BitmapBrush** brush);
// The key under which a render target leaves its world transform (in pixels) on its Cairo
// context while text is drawn (lm_gfx::applyBrush).
const cairo_user_data_key_t* worldKey();

// ---- Effects (d2d_effects.cpp) ------------------------------------------------------------

HRESULT createEffect(REFCLSID effectId, ID2D1Effect** effect);

// ---- WIC interop (d2d_wic.cpp) ------------------------------------------------------------

HRESULT bitmapFromWic(IWICBitmapSource* source, const D2D1_BITMAP_PROPERTIES* properties, ID2D1Bitmap** bitmap);
HRESULT createWicTarget(IWICBitmap* target, const D2D1_RENDER_TARGET_PROPERTIES& properties, ID2D1RenderTarget** renderTarget);

// ---- Render targets (d2d_target.cpp, text in d2d_text.cpp) --------------------------------

// The drawing every render target shares: state, clips and layers over a Cairo context on the
// target's surface. Method names follow ID2D1RenderTarget / ID2D1DeviceContext1's.
class Canvas {
public:
    virtual ~Canvas();
    static Canvas* from(ID2D1RenderTarget* target);
    cairo_surface_t* surface() const { return surface_.get(); }
    // The pixels drawn into: a premultiplied ARGB32 image surface. State stays; pushed clips
    // and layers are dropped.
    HRESULT attach(Surface surface);

    HRESULT createBitmap(D2D1_SIZE_U size, const void* data, UINT32 pitch, const D2D1_BITMAP_PROPERTIES* properties, ID2D1Bitmap** bitmap);
    HRESULT createBitmap1(D2D1_SIZE_U size, const void* data, UINT32 pitch, const D2D1_BITMAP_PROPERTIES1* properties, ID2D1Bitmap1** bitmap);
    HRESULT createCompatibleRenderTarget(const D2D1_SIZE_F* size, const D2D1_SIZE_U* pixelSize, const D2D1_PIXEL_FORMAT* format,
                                         ID2D1BitmapRenderTarget** target);
    HRESULT createLayer(ID2D1Layer** layer);
    HRESULT createFilledRealization(ID2D1Geometry* geometry, ID2D1GeometryRealization** realization);
    HRESULT createStrokedRealization(ID2D1Geometry* geometry, FLOAT width, ID2D1StrokeStyle* style, ID2D1GeometryRealization** realization);

    void drawLine(D2D1_POINT_2F from, D2D1_POINT_2F to, ID2D1Brush* brush, FLOAT width, ID2D1StrokeStyle* style);
    void drawRectangle(const D2D1_RECT_F* rect, ID2D1Brush* brush, FLOAT width, ID2D1StrokeStyle* style);
    void fillRectangle(const D2D1_RECT_F* rect, ID2D1Brush* brush);
    void drawRoundedRectangle(const D2D1_ROUNDED_RECT* rounded, ID2D1Brush* brush, FLOAT width, ID2D1StrokeStyle* style);
    void fillRoundedRectangle(const D2D1_ROUNDED_RECT* rounded, ID2D1Brush* brush);
    void drawEllipse(const D2D1_ELLIPSE* ellipse, ID2D1Brush* brush, FLOAT width, ID2D1StrokeStyle* style);
    void fillEllipse(const D2D1_ELLIPSE* ellipse, ID2D1Brush* brush);
    void drawGeometry(ID2D1Geometry* geometry, ID2D1Brush* brush, FLOAT width, ID2D1StrokeStyle* style);
    void fillGeometry(ID2D1Geometry* geometry, ID2D1Brush* brush, ID2D1Brush* opacityBrush);
    void drawGeometryRealization(ID2D1GeometryRealization* realization, ID2D1Brush* brush);
    void fillOpacityMask(ID2D1Bitmap* mask, ID2D1Brush* brush, const D2D1_RECT_F* destination, const D2D1_RECT_F* source);
    void drawBitmap(ID2D1Bitmap* bitmap, const D2D1_RECT_F* destination, FLOAT opacity, D2D1_INTERPOLATION_MODE mode,
                    const D2D1_RECT_F* source, const D2D1_MATRIX_4X4_F* perspective);
    void drawImage(ID2D1Image* image, const D2D1_POINT_2F* offset, const D2D1_RECT_F* imageRectangle, D2D1_INTERPOLATION_MODE mode,
                   D2D1_COMPOSITE_MODE composite);
    void drawText(const WCHAR* text, UINT32 length, IDWriteTextFormat* format, const D2D1_RECT_F* rect, ID2D1Brush* brush,
                  D2D1_DRAW_TEXT_OPTIONS options);
    void drawTextLayout(D2D1_POINT_2F origin, IDWriteTextLayout* layout, ID2D1Brush* brush, D2D1_DRAW_TEXT_OPTIONS options);

    void setTransform(const D2D1_MATRIX_3X2_F* transform) { if (transform) transform_ = *transform; }
    void getTransform(D2D1_MATRIX_3X2_F* transform) const { if (transform) *transform = transform_; }
    void setAntialiasMode(D2D1_ANTIALIAS_MODE mode) { antialias_ = mode; }
    D2D1_ANTIALIAS_MODE antialiasMode() const { return antialias_; }
    void setTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE mode) { text_antialias_ = mode; }
    D2D1_TEXT_ANTIALIAS_MODE textAntialiasMode() const { return text_antialias_; }
    void setPrimitiveBlend(D2D1_PRIMITIVE_BLEND blend) { blend_ = blend; }
    D2D1_PRIMITIVE_BLEND primitiveBlend() const { return blend_; }

    void pushLayer(const D2D1_LAYER_PARAMETERS1& parameters);
    void popLayer();
    void pushAxisAlignedClip(const D2D1_RECT_F* rect, D2D1_ANTIALIAS_MODE mode);
    void popAxisAlignedClip();
    void clear(const D2D1_COLOR_F* color);
    void beginDraw();
    HRESULT endDraw();

    D2D1_PIXEL_FORMAT pixelFormat() const { return format_; }
    void getDpi(FLOAT* dpiX, FLOAT* dpiY) const;
    D2D1_SIZE_F size() const;
    D2D1_SIZE_U pixelSize() const;

protected:
    // `format`: resolved; an IGNORE alpha mode keeps the pixels opaque. `dpi`: 96 for one pixel per DIP.
    Canvas(D2D1_PIXEL_FORMAT format, float dpi);
    // Called around drawing (BeginDraw, EndDraw): the pixels change.
    virtual void changed() {}

private:
    struct Layer {
        bool clip = false;                  // PushAxisAlignedClip; otherwise a layer.
        cairo_matrix_t world;               // The transform when pushed.
        ComPtr<ID2D1Geometry> mask;
        D2D1_ANTIALIAS_MODE mask_antialias;
        cairo_matrix_t mask_transform;
        double opacity;
        ComPtr<ID2D1Brush> opacity_brush;
    };
    // Cairo's matrix, antialiasing and operator for a primitive; the world transform in pixels.
    cairo_t* prepare();
    cairo_matrix_t world() const;
    void fillCurrentPath(cairo_t* context, const Brush& brush);
    void strokeCurrentPath(cairo_t* context, const Brush& brush, double width, ID2D1StrokeStyle* style);
    // Sets the brush as the context's source for text (no opacity a pattern cannot carry).
    cairo_t* prepareText(ID2D1Brush* brush);
    void drawPerspective(Bitmap& bitmap, const D2D1_RECT_F& destination, double opacity, D2D1_INTERPOLATION_MODE mode,
                         const D2D1_RECT_F& source, const D2D1_MATRIX_4X4_F& perspective);
    void unwind();

    Surface surface_;
    cairo_t* context_ = nullptr;
    D2D1_PIXEL_FORMAT format_;
    float dpi_;
    D2D1_MATRIX_3X2_F transform_{};
    D2D1_ANTIALIAS_MODE antialias_ = D2D1_ANTIALIAS_MODE_PER_PRIMITIVE;
    D2D1_TEXT_ANTIALIAS_MODE text_antialias_ = D2D1_TEXT_ANTIALIAS_MODE_DEFAULT;
    D2D1_PRIMITIVE_BLEND blend_ = D2D1_PRIMITIVE_BLEND_SOURCE_OVER;
    std::vector<Layer> stack_;
    bool unbalanced_ = false;
    cairo_matrix_t text_world_{}; // Left on the context for applyBrush while text is drawn.
};

// The COM face of a Canvas for one set of render target interfaces (always with
// ID2D1DeviceContext1, as Windows' targets answer it); every method forwards to Canvas.
template <class... Interfaces>
class RenderTarget : public lm_com::Object<Interfaces...>, public Canvas {
public:
    HRESULT STDMETHODCALLTYPE CreateBitmap(D2D1_SIZE_U size, const void* data, UINT32 pitch, const D2D1_BITMAP_PROPERTIES* properties,
                                           ID2D1Bitmap** bitmap) override {
        return createBitmap(size, data, pitch, properties, bitmap);
    }
    HRESULT STDMETHODCALLTYPE CreateBitmap(D2D1_SIZE_U size, const void* data, UINT32 pitch, const D2D1_BITMAP_PROPERTIES1* properties,
                                           ID2D1Bitmap1** bitmap) override {
        return createBitmap1(size, data, pitch, properties, bitmap);
    }
    HRESULT STDMETHODCALLTYPE CreateBitmapFromWicBitmap(IWICBitmapSource* source, const D2D1_BITMAP_PROPERTIES* properties,
                                                        ID2D1Bitmap** bitmap) override {
        return bitmapFromWic(source, properties, bitmap);
    }
    HRESULT STDMETHODCALLTYPE CreateBitmapBrush(ID2D1Bitmap* bitmap, const D2D1_BITMAP_BRUSH_PROPERTIES* bitmapProperties,
                                                const D2D1_BRUSH_PROPERTIES* properties, ID2D1BitmapBrush** brush) override {
        return createBitmapBrush(bitmap, bitmapProperties, properties, brush);
    }
    HRESULT STDMETHODCALLTYPE CreateSolidColorBrush(const D2D1_COLOR_F* color, const D2D1_BRUSH_PROPERTIES* properties,
                                                    ID2D1SolidColorBrush** brush) override {
        if (!color) return E_INVALIDARG;
        return createSolidColorBrush(*color, properties, brush);
    }
    HRESULT STDMETHODCALLTYPE CreateGradientStopCollection(const D2D1_GRADIENT_STOP* stops, UINT32 count, D2D1_GAMMA,
                                                           D2D1_EXTEND_MODE extendMode, ID2D1GradientStopCollection** collection) override {
        return createGradientStopCollection(stops, count, extendMode, collection);
    }
    HRESULT STDMETHODCALLTYPE CreateLinearGradientBrush(const D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES* gradient, const D2D1_BRUSH_PROPERTIES* properties,
                                                        ID2D1GradientStopCollection* stops, ID2D1LinearGradientBrush** brush) override {
        if (!gradient) return E_INVALIDARG;
        return createLinearGradientBrush(*gradient, properties, stops, brush);
    }
    HRESULT STDMETHODCALLTYPE CreateRadialGradientBrush(const D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES* gradient, const D2D1_BRUSH_PROPERTIES* properties,
                                                        ID2D1GradientStopCollection* stops, ID2D1RadialGradientBrush** brush) override {
        if (!gradient) return E_INVALIDARG;
        return createRadialGradientBrush(*gradient, properties, stops, brush);
    }
    HRESULT STDMETHODCALLTYPE CreateCompatibleRenderTarget(const D2D1_SIZE_F* size, const D2D1_SIZE_U* pixelSize, const D2D1_PIXEL_FORMAT* format,
                                                           D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS, ID2D1BitmapRenderTarget** target) override {
        return createCompatibleRenderTarget(size, pixelSize, format, target);
    }
    HRESULT STDMETHODCALLTYPE CreateLayer(const D2D1_SIZE_F*, ID2D1Layer** layer) override { return createLayer(layer); }
    void STDMETHODCALLTYPE DrawLine(D2D1_POINT_2F from, D2D1_POINT_2F to, ID2D1Brush* brush, FLOAT width, ID2D1StrokeStyle* style) override {
        drawLine(from, to, brush, width, style);
    }
    void STDMETHODCALLTYPE DrawRectangle(const D2D1_RECT_F* rect, ID2D1Brush* brush, FLOAT width, ID2D1StrokeStyle* style) override {
        drawRectangle(rect, brush, width, style);
    }
    void STDMETHODCALLTYPE FillRectangle(const D2D1_RECT_F* rect, ID2D1Brush* brush) override { fillRectangle(rect, brush); }
    void STDMETHODCALLTYPE DrawRoundedRectangle(const D2D1_ROUNDED_RECT* rounded, ID2D1Brush* brush, FLOAT width, ID2D1StrokeStyle* style) override {
        drawRoundedRectangle(rounded, brush, width, style);
    }
    void STDMETHODCALLTYPE FillRoundedRectangle(const D2D1_ROUNDED_RECT* rounded, ID2D1Brush* brush) override { fillRoundedRectangle(rounded, brush); }
    void STDMETHODCALLTYPE DrawEllipse(const D2D1_ELLIPSE* ellipse, ID2D1Brush* brush, FLOAT width, ID2D1StrokeStyle* style) override {
        drawEllipse(ellipse, brush, width, style);
    }
    void STDMETHODCALLTYPE FillEllipse(const D2D1_ELLIPSE* ellipse, ID2D1Brush* brush) override { fillEllipse(ellipse, brush); }
    void STDMETHODCALLTYPE DrawGeometry(ID2D1Geometry* geometry, ID2D1Brush* brush, FLOAT width, ID2D1StrokeStyle* style) override {
        drawGeometry(geometry, brush, width, style);
    }
    void STDMETHODCALLTYPE FillGeometry(ID2D1Geometry* geometry, ID2D1Brush* brush, ID2D1Brush* opacityBrush) override {
        fillGeometry(geometry, brush, opacityBrush);
    }
    void STDMETHODCALLTYPE FillOpacityMask(ID2D1Bitmap* mask, ID2D1Brush* brush, D2D1_OPACITY_MASK_CONTENT, const D2D1_RECT_F* destination,
                                           const D2D1_RECT_F* source) override {
        fillOpacityMask(mask, brush, destination, source);
    }
    void STDMETHODCALLTYPE DrawBitmap(ID2D1Bitmap* bitmap, const D2D1_RECT_F* destination, FLOAT opacity, D2D1_BITMAP_INTERPOLATION_MODE mode,
                                      const D2D1_RECT_F* source) override {
        drawBitmap(bitmap, destination, opacity, mode == D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR ?
                   D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR : D2D1_INTERPOLATION_MODE_LINEAR, source, nullptr);
    }
    void STDMETHODCALLTYPE DrawBitmap(ID2D1Bitmap* bitmap, const D2D1_RECT_F* destination, FLOAT opacity, D2D1_INTERPOLATION_MODE mode,
                                      const D2D1_RECT_F* source, const D2D1_MATRIX_4X4_F* perspective) override {
        drawBitmap(bitmap, destination, opacity, mode, source, perspective);
    }
    void STDMETHODCALLTYPE DrawText(const WCHAR* text, UINT32 length, IDWriteTextFormat* format, const D2D1_RECT_F* rect, ID2D1Brush* brush,
                                    D2D1_DRAW_TEXT_OPTIONS options, DWRITE_MEASURING_MODE) override {
        drawText(text, length, format, rect, brush, options);
    }
    void STDMETHODCALLTYPE DrawTextLayout(D2D1_POINT_2F origin, IDWriteTextLayout* layout, ID2D1Brush* brush, D2D1_DRAW_TEXT_OPTIONS options) override {
        drawTextLayout(origin, layout, brush, options);
    }
    void STDMETHODCALLTYPE SetTransform(const D2D1_MATRIX_3X2_F* transform) override { setTransform(transform); }
    void STDMETHODCALLTYPE GetTransform(D2D1_MATRIX_3X2_F* transform) const override { getTransform(transform); }
    void STDMETHODCALLTYPE SetAntialiasMode(D2D1_ANTIALIAS_MODE mode) override { setAntialiasMode(mode); }
    D2D1_ANTIALIAS_MODE STDMETHODCALLTYPE GetAntialiasMode() const override { return antialiasMode(); }
    void STDMETHODCALLTYPE SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE mode) override { setTextAntialiasMode(mode); }
    D2D1_TEXT_ANTIALIAS_MODE STDMETHODCALLTYPE GetTextAntialiasMode() const override { return textAntialiasMode(); }
    void STDMETHODCALLTYPE PushLayer(const D2D1_LAYER_PARAMETERS* p, ID2D1Layer*) override {
        if (p) pushLayer({p->contentBounds, p->geometricMask, p->maskAntialiasMode, p->maskTransform, p->opacity, p->opacityBrush,
                          D2D1_LAYER_OPTIONS1_NONE});
    }
    void STDMETHODCALLTYPE PushLayer(const D2D1_LAYER_PARAMETERS1* parameters, ID2D1Layer*) override {
        if (parameters) pushLayer(*parameters);
    }
    void STDMETHODCALLTYPE PopLayer() override { popLayer(); }
    void STDMETHODCALLTYPE PushAxisAlignedClip(const D2D1_RECT_F* rect, D2D1_ANTIALIAS_MODE mode) override { pushAxisAlignedClip(rect, mode); }
    void STDMETHODCALLTYPE PopAxisAlignedClip() override { popAxisAlignedClip(); }
    void STDMETHODCALLTYPE Clear(const D2D1_COLOR_F* color) override { clear(color); }
    void STDMETHODCALLTYPE BeginDraw() override { beginDraw(); }
    HRESULT STDMETHODCALLTYPE EndDraw(D2D1_TAG* tag1, D2D1_TAG* tag2) override {
        if (tag1) *tag1 = 0;
        if (tag2) *tag2 = 0;
        return endDraw();
    }
    D2D1_PIXEL_FORMAT STDMETHODCALLTYPE GetPixelFormat() const override { return pixelFormat(); }
    void STDMETHODCALLTYPE GetDpi(FLOAT* dpiX, FLOAT* dpiY) const override { getDpi(dpiX, dpiY); }
    D2D1_SIZE_F STDMETHODCALLTYPE GetSize() const override { return size(); }
    D2D1_SIZE_U STDMETHODCALLTYPE GetPixelSize() const override { return pixelSize(); }

    HRESULT STDMETHODCALLTYPE CreateEffect(REFCLSID effectId, ID2D1Effect** effect) override { return createEffect(effectId, effect); }
    BOOL STDMETHODCALLTYPE IsDxgiFormatSupported(DXGI_FORMAT format) const override {
        return format == DXGI_FORMAT_B8G8R8A8_UNORM || format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || format == DXGI_FORMAT_B8G8R8X8_UNORM ||
               format == DXGI_FORMAT_R8G8B8A8_UNORM || format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || format == DXGI_FORMAT_A8_UNORM;
    }
    void STDMETHODCALLTYPE SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND blend) override { setPrimitiveBlend(blend); }
    D2D1_PRIMITIVE_BLEND STDMETHODCALLTYPE GetPrimitiveBlend() const override { return primitiveBlend(); }
    void STDMETHODCALLTYPE DrawImage(ID2D1Image* image, const D2D1_POINT_2F* offset, const D2D1_RECT_F* imageRectangle,
                                     D2D1_INTERPOLATION_MODE mode, D2D1_COMPOSITE_MODE composite) override {
        drawImage(image, offset, imageRectangle, mode, composite);
    }
    HRESULT STDMETHODCALLTYPE CreateFilledGeometryRealization(ID2D1Geometry* geometry, FLOAT, ID2D1GeometryRealization** realization) override {
        return createFilledRealization(geometry, realization);
    }
    HRESULT STDMETHODCALLTYPE CreateStrokedGeometryRealization(ID2D1Geometry* geometry, FLOAT, FLOAT width, ID2D1StrokeStyle* style,
                                                               ID2D1GeometryRealization** realization) override {
        return createStrokedRealization(geometry, width, style, realization);
    }
    void STDMETHODCALLTYPE DrawGeometryRealization(ID2D1GeometryRealization* realization, ID2D1Brush* brush) override {
        drawGeometryRealization(realization, brush);
    }

protected:
    RenderTarget(D2D1_PIXEL_FORMAT format, float dpi) : Canvas(format, dpi) {}
};

// A render target drawing into `bitmap`'s pixels (CreateCompatibleRenderTarget).
HRESULT createBitmapTarget(ComPtr<Bitmap> bitmap, ID2D1BitmapRenderTarget** target);
HRESULT createHwndTarget(const D2D1_RENDER_TARGET_PROPERTIES& properties, const D2D1_HWND_RENDER_TARGET_PROPERTIES& window,
                         ID2D1HwndRenderTarget** target);
} // namespace lm_d2d
