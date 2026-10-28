// Direct2D render targets on Cairo: the shared drawing (Canvas) and the window and bitmap
// targets. Cairo's user space is the world transform (times the DPI scale) while drawing;
// axis-aligned clips are Cairo clips and layers Cairo groups, both on Cairo's save stack, which
// Direct2D's strict nesting keeps in step.
#include "d2d_internal.hpp"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstring>

namespace lm_d2d {
namespace {
cairo_user_data_key_t world_key;

class LayerResource final : public lm_com::Object<ID2D1Layer> {};

// A geometry and how it is drawn: Direct2D tessellates once; this port draws it each time.
class Realization final : public lm_com::Object<ID2D1GeometryRealization> {
public:
    Realization(ComPtr<ID2D1Geometry> geometry, bool filled, float width, ComPtr<ID2D1StrokeStyle> style)
        : geometry(std::move(geometry)), filled(filled), width(width), style(std::move(style)) {}
    const ComPtr<ID2D1Geometry> geometry;
    const bool filled;
    const float width;
    const ComPtr<ID2D1StrokeStyle> style;
};

cairo_operator_t operatorFor(D2D1_PRIMITIVE_BLEND blend) {
    switch (blend) {
    case D2D1_PRIMITIVE_BLEND_COPY: return CAIRO_OPERATOR_SOURCE;
    case D2D1_PRIMITIVE_BLEND_MIN: return CAIRO_OPERATOR_DARKEN;
    case D2D1_PRIMITIVE_BLEND_ADD: return CAIRO_OPERATOR_ADD;
    case D2D1_PRIMITIVE_BLEND_MAX: return CAIRO_OPERATOR_LIGHTEN;
    default: return CAIRO_OPERATOR_OVER;
    }
}

cairo_operator_t operatorFor(D2D1_COMPOSITE_MODE mode) {
    switch (mode) {
    case D2D1_COMPOSITE_MODE_DESTINATION_OVER: return CAIRO_OPERATOR_DEST_OVER;
    case D2D1_COMPOSITE_MODE_SOURCE_IN: return CAIRO_OPERATOR_IN;
    case D2D1_COMPOSITE_MODE_DESTINATION_IN: return CAIRO_OPERATOR_DEST_IN;
    case D2D1_COMPOSITE_MODE_SOURCE_OUT: return CAIRO_OPERATOR_OUT;
    case D2D1_COMPOSITE_MODE_DESTINATION_OUT: return CAIRO_OPERATOR_DEST_OUT;
    case D2D1_COMPOSITE_MODE_SOURCE_ATOP: return CAIRO_OPERATOR_ATOP;
    case D2D1_COMPOSITE_MODE_DESTINATION_ATOP: return CAIRO_OPERATOR_DEST_ATOP;
    case D2D1_COMPOSITE_MODE_XOR: return CAIRO_OPERATOR_XOR;
    case D2D1_COMPOSITE_MODE_PLUS: return CAIRO_OPERATOR_ADD;
    case D2D1_COMPOSITE_MODE_SOURCE_COPY:
    case D2D1_COMPOSITE_MODE_BOUNDED_SOURCE_COPY: return CAIRO_OPERATOR_SOURCE;
    case D2D1_COMPOSITE_MODE_MASK_INVERT: return CAIRO_OPERATOR_DIFFERENCE;
    default: return CAIRO_OPERATOR_OVER;
    }
}

struct Box {
    double left = -DBL_MAX, top = -DBL_MAX, right = DBL_MAX, bottom = DBL_MAX;
    void intersect(const Box& other) {
        left = std::max(left, other.left); top = std::max(top, other.top);
        right = std::min(right, other.right); bottom = std::min(bottom, other.bottom);
    }
};

// The surface and a pixel around it: clip boxes stay within Cairo's fixed-point range.
Box extentOf(cairo_surface_t* surface) {
    return {-1, -1, cairo_image_surface_get_width(surface) + 1.0, cairo_image_surface_get_height(surface) + 1.0};
}

Box deviceBox(const cairo_matrix_t& m, const D2D1_RECT_F& r) {
    Box box{DBL_MAX, DBL_MAX, -DBL_MAX, -DBL_MAX};
    for (const D2D1_POINT_2F corner : {D2D1_POINT_2F{r.left, r.top}, {r.right, r.top}, {r.right, r.bottom}, {r.left, r.bottom}}) {
        const double x = m.xx * corner.x + m.xy * corner.y + m.x0, y = m.yx * corner.x + m.yy * corner.y + m.y0;
        box.left = std::min(box.left, x); box.top = std::min(box.top, y);
        box.right = std::max(box.right, x); box.bottom = std::max(box.bottom, y);
    }
    return box;
}

bool unbounded(const D2D1_RECT_F& r) {
    constexpr float huge = 1e30f;
    return r.left <= -huge || r.top <= -huge || r.right >= huge || r.bottom >= huge;
}

bool invertible(const cairo_matrix_t& m) {
    cairo_matrix_t copy = m;
    return cairo_matrix_invert(&copy) == CAIRO_STATUS_SUCCESS;
}

// Maps `destination` (user space) onto `source` (DIPs of a bitmap with `scaleX`, `scaleY` pixels per DIP).
bool rectMatrix(const D2D1_RECT_F& destination, const D2D1_RECT_F& source, double scaleX, double scaleY, cairo_matrix_t& out) {
    const double dw = destination.right - destination.left, dh = destination.bottom - destination.top;
    const double sw = source.right - source.left, sh = source.bottom - source.top;
    if (dw == 0 || dh == 0 || sw == 0 || sh == 0) return false;
    cairo_matrix_init(&out, sw / dw * scaleX, 0, 0, sh / dh * scaleY, (source.left - destination.left * sw / dw) * scaleX,
                      (source.top - destination.top * sh / dh) * scaleY);
    return true;
}

// Clips to whole device pixels around a user-space box (keeps the matrix).
void clipToDevice(cairo_t* context, double x1, double y1, double x2, double y2) {
    Box box{DBL_MAX, DBL_MAX, -DBL_MAX, -DBL_MAX};
    for (const auto& corner : {std::array<double, 2>{x1, y1}, {x2, y1}, {x2, y2}, {x1, y2}}) {
        double x = corner[0], y = corner[1];
        cairo_user_to_device(context, &x, &y);
        box.left = std::min(box.left, x); box.top = std::min(box.top, y);
        box.right = std::max(box.right, x); box.bottom = std::max(box.bottom, y);
    }
    cairo_matrix_t matrix;
    cairo_get_matrix(context, &matrix);
    cairo_identity_matrix(context);
    cairo_rectangle(context, std::floor(box.left), std::floor(box.top), std::ceil(box.right) - std::floor(box.left),
                    std::ceil(box.bottom) - std::floor(box.top));
    cairo_clip(context);
    cairo_set_matrix(context, &matrix);
}

// ---- Perspective sampling -----------------------------------------------------------------

struct Homography {
    double m[3][3];
    // Row vector (x, y, 1) times the matrix, divided by w; false behind the eye.
    bool map(double x, double y, double& outX, double& outY) const {
        const double w = x * m[0][2] + y * m[1][2] + m[2][2];
        if (w <= 1e-9) return false;
        outX = (x * m[0][0] + y * m[1][0] + m[2][0]) / w;
        outY = (x * m[0][1] + y * m[1][1] + m[2][1]) / w;
        return true;
    }
};

Homography product(const Homography& a, const Homography& b) {
    Homography result{};
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            for (int k = 0; k < 3; ++k) result.m[r][c] += a.m[r][k] * b.m[k][c];
    return result;
}

bool inverse(const Homography& h, Homography& out) {
    const auto& m = h.m;
    const double det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                       m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    if (std::abs(det) < 1e-12) return false;
    const double i = 1 / det;
    out.m[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) * i;
    out.m[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * i;
    out.m[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * i;
    out.m[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) * i;
    out.m[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * i;
    out.m[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * i;
    out.m[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) * i;
    out.m[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * i;
    out.m[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * i;
    return true;
}

// A premultiplied ARGB32 pixel at (u, v) in pixels, clamped to [left, right) x [top, bottom).
struct Sampler {
    const uint8_t* data;
    int stride, left, top, right, bottom;
    bool nearest;
    uint32_t at(int x, int y) const {
        x = std::clamp(x, left, right - 1);
        y = std::clamp(y, top, bottom - 1);
        uint32_t pixel;
        std::memcpy(&pixel, data + static_cast<size_t>(y) * stride + static_cast<size_t>(x) * 4, 4);
        return pixel;
    }
    uint32_t sample(double u, double v) const {
        if (nearest) return at(static_cast<int>(std::floor(u)), static_cast<int>(std::floor(v)));
        const double fu = u - 0.5, fv = v - 0.5;
        const int x = static_cast<int>(std::floor(fu)), y = static_cast<int>(std::floor(fv));
        const uint32_t wx = static_cast<uint32_t>((fu - x) * 256), wy = static_cast<uint32_t>((fv - y) * 256);
        const uint32_t p00 = at(x, y), p10 = at(x + 1, y), p01 = at(x, y + 1), p11 = at(x + 1, y + 1);
        uint32_t result = 0;
        for (int shift = 0; shift < 32; shift += 8) {
            const uint32_t top_row = ((p00 >> shift) & 255) * (256 - wx) + ((p10 >> shift) & 255) * wx;
            const uint32_t bottom_row = ((p01 >> shift) & 255) * (256 - wx) + ((p11 >> shift) & 255) * wx;
            result |= ((top_row * (256 - wy) + bottom_row * wy + 32768) >> 16) << shift;
        }
        return result;
    }
};
} // namespace

const cairo_user_data_key_t* worldKey() { return &world_key; }

Canvas::Canvas(D2D1_PIXEL_FORMAT format, float dpi) : format_(format), dpi_(dpi > 0 ? dpi : 96), transform_(D2D1::IdentityMatrix()) {}

Canvas::~Canvas() {
    if (context_) cairo_destroy(context_);
}

Canvas* Canvas::from(ID2D1RenderTarget* target) { return dynamic_cast<Canvas*>(target); }

HRESULT Canvas::attach(Surface surface) {
    if (!surface) return E_OUTOFMEMORY;
    cairo_t* context = cairo_create(surface.get());
    if (cairo_status(context) != CAIRO_STATUS_SUCCESS) {
        cairo_destroy(context);
        return E_OUTOFMEMORY;
    }
    unwind();
    if (context_) cairo_destroy(context_);
    context_ = context;
    surface_ = std::move(surface);
    return S_OK;
}

cairo_matrix_t Canvas::world() const {
    cairo_matrix_t dpi;
    cairo_matrix_init_scale(&dpi, dpi_ / 96.0, dpi_ / 96.0);
    return multiply(toCairo(transform_), dpi);
}

cairo_t* Canvas::prepare() {
    const cairo_matrix_t matrix = world();
    if (!context_ || !invertible(matrix)) return nullptr; // A collapsed transform draws nothing.
    cairo_new_path(context_);
    cairo_set_matrix(context_, &matrix);
    cairo_set_antialias(context_, antialias_ == D2D1_ANTIALIAS_MODE_ALIASED ? CAIRO_ANTIALIAS_NONE : CAIRO_ANTIALIAS_DEFAULT);
    cairo_set_operator(context_, operatorFor(blend_));
    return context_;
}

void Canvas::fillCurrentPath(cairo_t* context, const Brush& brush) {
    const auto paint = brush.paint();
    cairo_set_source(context, paint.pattern.get());
    if (!paint.bounded && paint.alpha >= 0.999) { cairo_fill(context); return; }
    cairo_save(context);
    if (paint.bounded) {
        cairo_path_t* path = cairo_copy_path(context);
        cairo_new_path(context);
        cairo_rectangle(context, paint.left, paint.top, paint.right - paint.left, paint.bottom - paint.top);
        cairo_clip(context);
        cairo_append_path(context, path);
        cairo_path_destroy(path);
        if (paint.alpha >= 0.999) { cairo_fill(context); cairo_restore(context); return; }
    }
    cairo_clip(context);
    cairo_paint_with_alpha(context, std::max(paint.alpha, 0.0));
    cairo_restore(context);
}

void Canvas::strokeCurrentPath(cairo_t* context, const Brush& brush, double width, ID2D1StrokeStyle* style) {
    cairo_set_line_width(context, std::max(width, 0.0));
    if (const auto* own = StrokeStyle::from(style)) own->apply(context, width);
    else applyDefaultStroke(context);
    const auto paint = brush.paint();
    cairo_set_source(context, paint.pattern.get());
    if (paint.alpha >= 0.999) { cairo_stroke(context); return; }
    // A bitmap brush's opacity: the stroke alone in a group as small as it is.
    double x1, y1, x2, y2;
    cairo_stroke_extents(context, &x1, &y1, &x2, &y2);
    cairo_path_t* path = cairo_copy_path(context);
    cairo_new_path(context);
    cairo_save(context);
    clipToDevice(context, x1, y1, x2, y2);
    cairo_push_group(context);
    cairo_append_path(context, path);
    cairo_stroke(context);
    cairo_pop_group_to_source(context);
    cairo_paint_with_alpha(context, std::max(paint.alpha, 0.0));
    cairo_restore(context);
    cairo_path_destroy(path);
}

// ---- Resources ----------------------------------------------------------------------------

HRESULT Canvas::createBitmap(D2D1_SIZE_U size, const void* data, UINT32 pitch, const D2D1_BITMAP_PROPERTIES* properties, ID2D1Bitmap** bitmap) {
    if (!bitmap) return E_POINTER;
    *bitmap = nullptr;
    const auto format = properties ? properties->pixelFormat : format_;
    ComPtr<Bitmap> created;
    const HRESULT result = Bitmap::create(size, data, pitch, format, properties ? properties->dpiX : dpi_, properties ? properties->dpiY : dpi_, created);
    if (SUCCEEDED(result)) *bitmap = created.Detach();
    return result;
}

HRESULT Canvas::createBitmap1(D2D1_SIZE_U size, const void* data, UINT32 pitch, const D2D1_BITMAP_PROPERTIES1* properties, ID2D1Bitmap1** bitmap) {
    if (!bitmap) return E_POINTER;
    *bitmap = nullptr;
    if (!properties) return E_INVALIDARG;
    ComPtr<Bitmap> created;
    const HRESULT result = Bitmap::create(size, data, pitch, properties->pixelFormat, properties->dpiX, properties->dpiY, created);
    if (SUCCEEDED(result)) *bitmap = created.Detach();
    return result;
}

// Without a pixel size the target has the parent's DPI; with both sizes, the DPI they imply.
HRESULT Canvas::createCompatibleRenderTarget(const D2D1_SIZE_F* size, const D2D1_SIZE_U* pixels, const D2D1_PIXEL_FORMAT* format,
                                             ID2D1BitmapRenderTarget** target) {
    if (!target) return E_POINTER;
    *target = nullptr;
    D2D1_SIZE_U extent = pixelSize();
    float dpi = dpi_;
    if (pixels) {
        extent = *pixels;
        if (size && size->width > 0) dpi = static_cast<float>(pixels->width / size->width * 96.0);
    } else if (size) {
        extent = {static_cast<UINT32>(std::ceil(std::max(0.0f, size->width) * dpi_ / 96)), static_cast<UINT32>(std::ceil(std::max(0.0f, size->height) * dpi_ / 96))};
    }
    D2D1_PIXEL_FORMAT wanted = format ? *format : D2D1_PIXEL_FORMAT{format_.format, D2D1_ALPHA_MODE_PREMULTIPLIED};
    if (wanted.format == DXGI_FORMAT_UNKNOWN) wanted.format = format_.format;
    wanted = resolveFormat(wanted);
    if (wanted.format == DXGI_FORMAT_A8_UNORM) return D2DERR_UNSUPPORTED_PIXEL_FORMAT; // Targets draw in colour.
    ComPtr<Bitmap> bitmap;
    const HRESULT result = Bitmap::create(extent, nullptr, 0, wanted, dpi, dpi, bitmap);
    return FAILED(result) ? result : createBitmapTarget(std::move(bitmap), target);
}

HRESULT Canvas::createLayer(ID2D1Layer** layer) {
    if (!layer) return E_POINTER;
    *layer = new LayerResource;
    return S_OK;
}

HRESULT Canvas::createFilledRealization(ID2D1Geometry* geometry, ID2D1GeometryRealization** realization) {
    if (!realization) return E_POINTER;
    *realization = nullptr;
    if (!Geometry::from(geometry)) return E_INVALIDARG;
    *realization = new Realization(ComPtr<ID2D1Geometry>(geometry), true, 0, nullptr);
    return S_OK;
}

HRESULT Canvas::createStrokedRealization(ID2D1Geometry* geometry, FLOAT width, ID2D1StrokeStyle* style, ID2D1GeometryRealization** realization) {
    if (!realization) return E_POINTER;
    *realization = nullptr;
    if (!Geometry::from(geometry)) return E_INVALIDARG;
    *realization = new Realization(ComPtr<ID2D1Geometry>(geometry), false, width, ComPtr<ID2D1StrokeStyle>(style));
    return S_OK;
}

// ---- Primitives ---------------------------------------------------------------------------

void Canvas::drawLine(D2D1_POINT_2F from, D2D1_POINT_2F to, ID2D1Brush* brush, FLOAT width, ID2D1StrokeStyle* style) {
    const auto* own = Brush::from(brush);
    cairo_t* context = own ? prepare() : nullptr;
    if (!context) return;
    cairo_move_to(context, from.x, from.y);
    cairo_line_to(context, to.x, to.y);
    strokeCurrentPath(context, *own, width, style);
}

void Canvas::drawRectangle(const D2D1_RECT_F* rect, ID2D1Brush* brush, FLOAT width, ID2D1StrokeStyle* style) {
    const auto* own = Brush::from(brush);
    cairo_t* context = own && rect ? prepare() : nullptr;
    if (!context) return;
    cairo_rectangle(context, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top);
    strokeCurrentPath(context, *own, width, style);
}

void Canvas::fillRectangle(const D2D1_RECT_F* rect, ID2D1Brush* brush) {
    const auto* own = Brush::from(brush);
    cairo_t* context = own && rect ? prepare() : nullptr;
    if (!context) return;
    cairo_rectangle(context, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top);
    fillCurrentPath(context, *own);
}

void Canvas::drawRoundedRectangle(const D2D1_ROUNDED_RECT* rounded, ID2D1Brush* brush, FLOAT width, ID2D1StrokeStyle* style) {
    const auto* own = Brush::from(brush);
    cairo_t* context = own && rounded ? prepare() : nullptr;
    if (!context) return;
    appendRoundedRectangle(context, *rounded);
    strokeCurrentPath(context, *own, width, style);
}

void Canvas::fillRoundedRectangle(const D2D1_ROUNDED_RECT* rounded, ID2D1Brush* brush) {
    const auto* own = Brush::from(brush);
    cairo_t* context = own && rounded ? prepare() : nullptr;
    if (!context) return;
    appendRoundedRectangle(context, *rounded);
    fillCurrentPath(context, *own);
}

void Canvas::drawEllipse(const D2D1_ELLIPSE* ellipse, ID2D1Brush* brush, FLOAT width, ID2D1StrokeStyle* style) {
    const auto* own = Brush::from(brush);
    cairo_t* context = own && ellipse ? prepare() : nullptr;
    if (!context) return;
    appendEllipse(context, *ellipse);
    strokeCurrentPath(context, *own, width, style);
}

void Canvas::fillEllipse(const D2D1_ELLIPSE* ellipse, ID2D1Brush* brush) {
    const auto* own = Brush::from(brush);
    cairo_t* context = own && ellipse ? prepare() : nullptr;
    if (!context) return;
    appendEllipse(context, *ellipse);
    fillCurrentPath(context, *own);
}

void Canvas::drawGeometry(ID2D1Geometry* geometry, ID2D1Brush* brush, FLOAT width, ID2D1StrokeStyle* style) {
    const auto* own = Brush::from(brush);
    const auto* shape = Geometry::from(geometry);
    cairo_t* context = own && shape ? prepare() : nullptr;
    if (!context) return;
    appendPath(context, *shape, false);
    strokeCurrentPath(context, *own, width, style);
}

void Canvas::fillGeometry(ID2D1Geometry* geometry, ID2D1Brush* brush, ID2D1Brush* opacityBrush) {
    const auto* own = Brush::from(brush);
    const auto* shape = Geometry::from(geometry);
    cairo_t* context = own && shape ? prepare() : nullptr;
    if (!context) return;
    appendPath(context, *shape, true);
    const auto* opacity = Brush::from(opacityBrush);
    if (!opacity) { fillCurrentPath(context, *own); return; }
    // The opacity brush's alpha masks the fill.
    const auto fill = own->paint(), mask = opacity->paint();
    cairo_save(context);
    cairo_clip(context);
    cairo_push_group(context);
    cairo_set_source(context, fill.pattern.get());
    cairo_mask(context, mask.pattern.get());
    cairo_pop_group_to_source(context);
    cairo_paint_with_alpha(context, std::max(fill.alpha * mask.alpha, 0.0));
    cairo_restore(context);
}

void Canvas::drawGeometryRealization(ID2D1GeometryRealization* realization, ID2D1Brush* brush) {
    const auto* own = dynamic_cast<Realization*>(realization);
    if (!own) return;
    if (own->filled) fillGeometry(own->geometry.Get(), brush, nullptr);
    else drawGeometry(own->geometry.Get(), brush, own->width, own->style.Get());
}

void Canvas::fillOpacityMask(ID2D1Bitmap* mask, ID2D1Brush* brush, const D2D1_RECT_F* destination, const D2D1_RECT_F* source) {
    auto* bitmap = Bitmap::from(mask);
    const auto* own = Brush::from(brush);
    cairo_t* context = bitmap && own ? prepare() : nullptr;
    if (!context) return;
    const auto size = bitmap->GetSize();
    const D2D1_RECT_F whole{0, 0, size.width, size.height};
    const D2D1_RECT_F to = destination ? *destination : whole, from = source ? *source : whole;
    cairo_matrix_t matrix;
    if (!rectMatrix(to, from, bitmap->scaleX(), bitmap->scaleY(), matrix)) return;
    Pattern alpha(cairo_pattern_create_for_surface(bitmap->surface()));
    cairo_pattern_set_matrix(alpha.get(), &matrix);
    cairo_pattern_set_filter(alpha.get(), CAIRO_FILTER_GOOD);
    cairo_pattern_set_extend(alpha.get(), CAIRO_EXTEND_PAD);
    const auto paint = own->paint();
    cairo_save(context);
    cairo_rectangle(context, to.left, to.top, to.right - to.left, to.bottom - to.top);
    cairo_clip(context);
    cairo_set_source(context, paint.pattern.get());
    if (paint.alpha >= 0.999) cairo_mask(context, alpha.get());
    else {
        cairo_push_group(context);
        cairo_mask(context, alpha.get());
        cairo_pop_group_to_source(context);
        cairo_paint_with_alpha(context, std::max(paint.alpha, 0.0));
    }
    cairo_restore(context);
}

void Canvas::drawBitmap(ID2D1Bitmap* bitmap, const D2D1_RECT_F* destination, FLOAT opacity, D2D1_INTERPOLATION_MODE mode,
                        const D2D1_RECT_F* source, const D2D1_MATRIX_4X4_F* perspective) {
    auto* own = Bitmap::from(bitmap);
    if (!own || opacity <= 0) return;
    const auto size = own->GetSize();
    const D2D1_RECT_F whole{0, 0, size.width, size.height};
    const D2D1_RECT_F to = destination ? *destination : whole, from = source ? *source : whole;
    if (perspective) { drawPerspective(*own, to, opacity, mode, from, *perspective); return; }
    cairo_t* context = prepare();
    cairo_matrix_t matrix;
    if (!context || !rectMatrix(to, from, own->scaleX(), own->scaleY(), matrix)) return;
    Pattern pattern(cairo_pattern_create_for_surface(own->surface()));
    cairo_pattern_set_matrix(pattern.get(), &matrix);
    cairo_pattern_set_filter(pattern.get(), filterFor(mode));
    cairo_pattern_set_extend(pattern.get(), CAIRO_EXTEND_PAD); // Edge pixels repeat, as Direct2D clamps.
    cairo_set_source(context, pattern.get());
    cairo_rectangle(context, to.left, to.top, to.right - to.left, to.bottom - to.top);
    if (opacity >= 0.999f) { cairo_fill(context); return; }
    cairo_save(context);
    cairo_clip(context);
    cairo_paint_with_alpha(context, opacity);
    cairo_restore(context);
}

// Direct2D 1.1's 3D DrawBitmap: the destination rectangle's plane mapped by a 4x4 matrix (row
// vectors (x, y, 0, 1), divided by w), then by the world transform. Cairo has no projective
// patterns: every covered device pixel is mapped back and sampled here, then composited through
// the projected quad (antialiased edges). Shrinking modes other than linear sample a smaller copy.
void Canvas::drawPerspective(Bitmap& bitmap, const D2D1_RECT_F& destination, double opacity, D2D1_INTERPOLATION_MODE mode,
                             const D2D1_RECT_F& source, const D2D1_MATRIX_4X4_F& perspective) {
    cairo_t* context = prepare();
    if (!context) return;
    const cairo_matrix_t w = world();
    const Homography plane{{{perspective._11, perspective._12, perspective._14},
                            {perspective._21, perspective._22, perspective._24},
                            {perspective._41, perspective._42, perspective._44}}};
    const Homography to_device = product(plane, Homography{{{w.xx, w.yx, 0}, {w.xy, w.yy, 0}, {w.x0, w.y0, 1}}});
    Homography to_plane;
    if (!inverse(to_device, to_plane)) return;
    std::array<double, 8> quad{};
    const std::array<D2D1_POINT_2F, 4> corners{D2D1_POINT_2F{destination.left, destination.top}, {destination.right, destination.top},
                                               {destination.right, destination.bottom}, {destination.left, destination.bottom}};
    for (size_t i = 0; i < 4; ++i)
        if (!to_device.map(corners[i].x, corners[i].y, quad[i * 2], quad[i * 2 + 1])) return; // Behind the viewer.
    // The covered device pixels, within the clip.
    cairo_identity_matrix(context);
    double clip_left, clip_top, clip_right, clip_bottom;
    cairo_clip_extents(context, &clip_left, &clip_top, &clip_right, &clip_bottom);
    const int left = static_cast<int>(std::floor(std::max(clip_left, std::min({quad[0], quad[2], quad[4], quad[6]}))));
    const int top = static_cast<int>(std::floor(std::max(clip_top, std::min({quad[1], quad[3], quad[5], quad[7]}))));
    const int right = static_cast<int>(std::ceil(std::min(clip_right, std::max({quad[0], quad[2], quad[4], quad[6]}))));
    const int bottom = static_cast<int>(std::ceil(std::min(clip_bottom, std::max({quad[1], quad[3], quad[5], quad[7]}))));
    if (right <= left || bottom <= top) return;
    const double dw = destination.right - destination.left, dh = destination.bottom - destination.top;
    const double sw = source.right - source.left, sh = source.bottom - source.top;
    if (dw == 0 || dh == 0 || sw <= 0 || sh <= 0) return;

    // The level to sample: the source pixels per covered device pixel.
    const double quad_area = 0.5 * std::abs((quad[0] - quad[4]) * (quad[3] - quad[7]) - (quad[2] - quad[6]) * (quad[1] - quad[5]));
    const double shrink = std::sqrt(sw * bitmap.scaleX() * sh * bitmap.scaleY() / std::max(quad_area, 1.0));
    const bool mipmapped = mode != D2D1_INTERPOLATION_MODE_LINEAR && mode != D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR;
    double level = 1;
    while (mipmapped && shrink / (level * 2) >= 1) level *= 2;
    Surface pixels;
    if (level > 1 || bitmap.alphaOnly())
        pixels = bitmap.scaled(std::max(1, static_cast<int>(bitmap.width() / level)), std::max(1, static_cast<int>(bitmap.height() / level)));
    else pixels = Surface(cairo_surface_reference(bitmap.surface()));
    if (!pixels) return;
    cairo_surface_flush(pixels.get());
    const double px = static_cast<double>(cairo_image_surface_get_width(pixels.get())) / bitmap.width() * bitmap.scaleX();
    const double py = static_cast<double>(cairo_image_surface_get_height(pixels.get())) / bitmap.height() * bitmap.scaleY();
    const Sampler sampler{cairo_image_surface_get_data(pixels.get()), cairo_image_surface_get_stride(pixels.get()),
                          std::max(0, static_cast<int>(std::floor(source.left * px))), std::max(0, static_cast<int>(std::floor(source.top * py))),
                          std::min(cairo_image_surface_get_width(pixels.get()), static_cast<int>(std::ceil(source.right * px))),
                          std::min(cairo_image_surface_get_height(pixels.get()), static_cast<int>(std::ceil(source.bottom * py))),
                          mode == D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR};
    if (sampler.right <= sampler.left || sampler.bottom <= sampler.top) return;

    Surface drawn = imageSurface(CAIRO_FORMAT_ARGB32, right - left, bottom - top);
    if (!drawn) return;
    uint8_t* out = cairo_image_surface_get_data(drawn.get());
    const int stride = cairo_image_surface_get_stride(drawn.get());
    for (int y = top; y < bottom; ++y) {
        auto* row = reinterpret_cast<uint32_t*>(out + static_cast<size_t>(y - top) * stride);
        for (int x = left; x < right; ++x) {
            double u, v;
            if (!to_plane.map(x + 0.5, y + 0.5, u, v)) continue;
            u = ((u - destination.left) * sw / dw + source.left) * px;
            v = ((v - destination.top) * sh / dh + source.top) * py;
            row[x - left] = sampler.sample(u, v);
        }
    }
    cairo_surface_mark_dirty(drawn.get());
    cairo_move_to(context, quad[0], quad[1]);
    for (size_t i = 1; i < 4; ++i) cairo_line_to(context, quad[i * 2], quad[i * 2 + 1]);
    cairo_close_path(context);
    cairo_save(context);
    cairo_clip(context);
    cairo_set_source_surface(context, drawn.get(), left, top);
    cairo_paint_with_alpha(context, opacity);
    cairo_restore(context);
}

// Effects are computed at the resolution they are shown at (never above the image's own): the
// world transform's scale, rounded up to quarter steps of a power of two so a resized drawing
// reuses an effect's cached result.
void Canvas::drawImage(ID2D1Image* image, const D2D1_POINT_2F* offset, const D2D1_RECT_F* imageRectangle, D2D1_INTERPOLATION_MODE mode,
                       D2D1_COMPOSITE_MODE composite) {
    cairo_t* context = image ? prepare() : nullptr;
    if (!context) return;
    double scale = 1;
    ImageData data;
    if (auto* bitmap = Bitmap::from(image)) {
        scale = bitmap->scaleX();
        data = {Surface(cairo_surface_reference(bitmap->surface())), 0, 0};
    } else if (auto* source = ImageSource::from(image)) {
        const double device = scaleOf(world());
        if (device < 1) scale = std::max(1.0 / 16, std::exp2(std::ceil(std::log2(std::max(device, 1e-6)) * 4) / 4));
        data = source->render(scale);
    }
    if (!data.surface) return;
    const D2D1_POINT_2F at = offset ? *offset : D2D1_POINT_2F{0, 0};
    cairo_save(context);
    cairo_set_operator(context, operatorFor(composite));
    if (imageRectangle) {
        cairo_translate(context, at.x - imageRectangle->left, at.y - imageRectangle->top);
        cairo_rectangle(context, imageRectangle->left, imageRectangle->top, imageRectangle->right - imageRectangle->left,
                        imageRectangle->bottom - imageRectangle->top);
        cairo_clip(context);
    } else cairo_translate(context, at.x, at.y);
    cairo_scale(context, 1 / scale, 1 / scale);
    cairo_set_source_surface(context, data.surface.get(), data.x, data.y);
    cairo_pattern_set_filter(cairo_get_source(context), filterFor(mode));
    cairo_rectangle(context, data.x, data.y, cairo_image_surface_get_width(data.surface.get()), cairo_image_surface_get_height(data.surface.get()));
    cairo_fill(context);
    cairo_restore(context);
}

// ---- Clips and layers ---------------------------------------------------------------------

// The rectangle's device-space bounding box (a rotated rectangle clips to its bounds).
void Canvas::pushAxisAlignedClip(const D2D1_RECT_F* rect, D2D1_ANTIALIAS_MODE mode) {
    stack_.push_back({true});
    if (!context_) return;
    cairo_save(context_);
    if (!rect) return;
    Box box = deviceBox(world(), *rect);
    box.intersect(extentOf(surface_.get()));
    cairo_new_path(context_);
    cairo_identity_matrix(context_);
    cairo_rectangle(context_, box.left, box.top, box.right - box.left, box.bottom - box.top);
    cairo_set_antialias(context_, mode == D2D1_ANTIALIAS_MODE_ALIASED ? CAIRO_ANTIALIAS_NONE : CAIRO_ANTIALIAS_DEFAULT);
    cairo_clip(context_);
}

void Canvas::popAxisAlignedClip() {
    if (stack_.empty() || !stack_.back().clip) { unbalanced_ = true; return; }
    stack_.pop_back();
    if (context_) cairo_restore(context_);
}

// Drawing goes to a Cairo group as large as the content bounds and the mask allow (whole device
// pixels); PopLayer composites it through the mask, the opacity and the opacity brush.
void Canvas::pushLayer(const D2D1_LAYER_PARAMETERS1& parameters) {
    const cairo_matrix_t at = world();
    stack_.push_back({false, at, ComPtr<ID2D1Geometry>(parameters.geometricMask), parameters.maskAntialiasMode,
                      toCairo(parameters.maskTransform), std::clamp<double>(parameters.opacity, 0, 1),
                      ComPtr<ID2D1Brush>(parameters.opacityBrush)});
    if (!context_) return;
    cairo_save(context_);
    Box box;
    if (!unbounded(parameters.contentBounds)) box = deviceBox(at, parameters.contentBounds);
    if (const auto* mask = Geometry::from(parameters.geometricMask)) {
        Box extent{DBL_MAX, DBL_MAX, -DBL_MAX, -DBL_MAX};
        for (const auto& line : flatten(*mask, multiply(stack_.back().mask_transform, at), 0.5))
            for (const auto& p : line.points) {
                extent.left = std::min(extent.left, p.x); extent.top = std::min(extent.top, p.y);
                extent.right = std::max(extent.right, p.x); extent.bottom = std::max(extent.bottom, p.y);
            }
        box.intersect({extent.left - 1, extent.top - 1, extent.right + 1, extent.bottom + 1});
    }
    box.intersect(extentOf(surface_.get()));
    cairo_new_path(context_);
    cairo_identity_matrix(context_);
    const double left = std::floor(box.left), top = std::floor(box.top);
    cairo_rectangle(context_, left, top, std::max(0.0, std::ceil(box.right) - left), std::max(0.0, std::ceil(box.bottom) - top));
    cairo_clip(context_);
    cairo_push_group(context_);
}

void Canvas::popLayer() {
    if (stack_.empty() || stack_.back().clip) { unbalanced_ = true; return; }
    const Layer layer = std::move(stack_.back());
    stack_.pop_back();
    if (!context_) return;
    Pattern group(cairo_pop_group(context_)); // In device space: the matrix was the identity when pushed.
    cairo_set_operator(context_, CAIRO_OPERATOR_OVER);
    if (const auto* mask = Geometry::from(layer.mask.Get())) {
        const cairo_matrix_t matrix = multiply(layer.mask_transform, layer.world);
        if (!invertible(matrix)) { cairo_restore(context_); return; } // A collapsed mask shows nothing.
        cairo_set_matrix(context_, &matrix);
        appendPath(context_, *mask, true);
        cairo_set_antialias(context_, layer.mask_antialias == D2D1_ANTIALIAS_MODE_ALIASED ? CAIRO_ANTIALIAS_NONE : CAIRO_ANTIALIAS_DEFAULT);
        cairo_clip(context_);
        cairo_identity_matrix(context_);
    }
    cairo_set_source(context_, group.get());
    const auto* brush = Brush::from(layer.opacity_brush.Get());
    if (!brush) cairo_paint_with_alpha(context_, layer.opacity);
    else if (invertible(layer.world)) {
        // The brush lies in the world space the layer was pushed in.
        const auto paint = brush->paint(layer.opacity);
        cairo_set_matrix(context_, &layer.world);
        if (paint.alpha >= 0.999) cairo_mask(context_, paint.pattern.get());
        else {
            cairo_push_group_with_content(context_, CAIRO_CONTENT_ALPHA);
            cairo_set_source(context_, paint.pattern.get());
            cairo_paint_with_alpha(context_, std::max(paint.alpha, 0.0));
            Pattern alpha(cairo_pop_group(context_));
            cairo_mask(context_, alpha.get());
        }
    }
    cairo_restore(context_);
}

void Canvas::unwind() {
    if (context_)
        for (auto layer = stack_.rbegin(); layer != stack_.rend(); ++layer) {
            if (!layer->clip) cairo_pattern_destroy(cairo_pop_group(context_));
            cairo_restore(context_);
        }
    stack_.clear();
}

// Ignores the transform; the clips and the current layer bound it. Targets without alpha keep
// their pixels opaque: the colour is laid over black.
void Canvas::clear(const D2D1_COLOR_F* color) {
    if (!context_) return;
    const D2D1_COLOR_F c = color ? *color : D2D1_COLOR_F{0, 0, 0, 0};
    cairo_save(context_);
    cairo_identity_matrix(context_);
    cairo_set_operator(context_, CAIRO_OPERATOR_SOURCE);
    if (format_.alphaMode == D2D1_ALPHA_MODE_IGNORE) cairo_set_source_rgba(context_, c.r * c.a, c.g * c.a, c.b * c.a, 1);
    else cairo_set_source_rgba(context_, c.r, c.g, c.b, c.a);
    cairo_paint(context_);
    cairo_restore(context_);
}

void Canvas::beginDraw() {
    unbalanced_ = false;
    if (context_ && cairo_status(context_) != CAIRO_STATUS_SUCCESS) {
        stack_.clear();
        cairo_destroy(context_);
        context_ = cairo_create(surface_.get());
    }
    changed();
}

HRESULT Canvas::endDraw() {
    const bool balanced = stack_.empty() && !unbalanced_;
    unwind();
    if (!context_) return D2DERR_RECREATE_TARGET;
    cairo_surface_flush(surface_.get());
    changed();
    if (cairo_status(context_) != CAIRO_STATUS_SUCCESS) return D2DERR_RECREATE_TARGET;
    return balanced ? S_OK : D2DERR_PUSH_POP_UNBALANCED;
}

void Canvas::getDpi(FLOAT* dpiX, FLOAT* dpiY) const {
    if (dpiX) *dpiX = dpi_;
    if (dpiY) *dpiY = dpi_;
}

D2D1_SIZE_U Canvas::pixelSize() const {
    if (!surface_) return {0, 0};
    return {static_cast<UINT32>(cairo_image_surface_get_width(surface_.get())), static_cast<UINT32>(cairo_image_surface_get_height(surface_.get()))};
}

D2D1_SIZE_F Canvas::size() const {
    const auto pixels = pixelSize();
    return {static_cast<float>(pixels.width * 96.0 / dpi_), static_cast<float>(pixels.height * 96.0 / dpi_)};
}

// ---- Targets ------------------------------------------------------------------------------

namespace {
// A window's back buffer: EndDraw hands the finished frame to the window layer.
class HwndTarget final : public RenderTarget<ID2D1HwndRenderTarget, ID2D1DeviceContext1> {
public:
    HwndTarget(HWND window, D2D1_PIXEL_FORMAT format, float dpi) : RenderTarget(format, dpi), window_(window) {}
    HRESULT STDMETHODCALLTYPE Resize(const D2D1_SIZE_U* size) override {
        if (!size) return E_INVALIDARG;
        return attach(imageSurface(CAIRO_FORMAT_ARGB32, static_cast<int>(std::max(1u, size->width)), static_cast<int>(std::max(1u, size->height))));
    }
    HRESULT STDMETHODCALLTYPE EndDraw(D2D1_TAG* tag1, D2D1_TAG* tag2) override {
        const HRESULT result = RenderTarget::EndDraw(tag1, tag2);
        cairo_surface_t* frame = surface();
        if (!frame) return result;
        if (!lm_gfx::presentWindow(window_, cairo_image_surface_get_data(frame), static_cast<UINT32>(cairo_image_surface_get_width(frame)),
                                   static_cast<UINT32>(cairo_image_surface_get_height(frame)),
                                   static_cast<UINT32>(cairo_image_surface_get_stride(frame))))
            return D2DERR_RECREATE_TARGET;
        return result;
    }
private:
    HWND window_;
};

// CreateCompatibleRenderTarget: draws into its bitmap's own pixels (GetBitmap shares them).
class BitmapTarget final : public RenderTarget<ID2D1BitmapRenderTarget, ID2D1DeviceContext1> {
public:
    explicit BitmapTarget(ComPtr<Bitmap> bitmap)
        : RenderTarget(bitmap->GetPixelFormat(), static_cast<float>(bitmap->scaleX() * 96)), bitmap_(std::move(bitmap)) {}
    HRESULT STDMETHODCALLTYPE GetBitmap(ID2D1Bitmap** bitmap) override {
        if (!bitmap) return E_POINTER;
        *bitmap = bitmap_.Get();
        bitmap_->AddRef();
        return S_OK;
    }
protected:
    void changed() override { bitmap_->touch(); }
private:
    ComPtr<Bitmap> bitmap_;
};
} // namespace

HRESULT createBitmapTarget(ComPtr<Bitmap> bitmap, ID2D1BitmapRenderTarget** target) {
    if (!target) return E_POINTER;
    *target = nullptr;
    if (!bitmap || bitmap->alphaOnly()) return E_INVALIDARG;
    Surface pixels(cairo_surface_reference(bitmap->surface()));
    ComPtr<BitmapTarget> created;
    created.Attach(new BitmapTarget(std::move(bitmap)));
    const HRESULT result = created->attach(std::move(pixels));
    if (SUCCEEDED(result)) *target = created.Detach();
    return result;
}

HRESULT createHwndTarget(const D2D1_RENDER_TARGET_PROPERTIES& properties, const D2D1_HWND_RENDER_TARGET_PROPERTIES& window,
                         ID2D1HwndRenderTarget** target) {
    if (!target) return E_POINTER;
    *target = nullptr;
    D2D1_PIXEL_FORMAT format = properties.pixelFormat;
    if (format.format == DXGI_FORMAT_UNKNOWN) format.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    if (format.alphaMode == D2D1_ALPHA_MODE_UNKNOWN) format.alphaMode = D2D1_ALPHA_MODE_IGNORE; // A window shows no alpha.
    if (format.format != DXGI_FORMAT_B8G8R8A8_UNORM || format.alphaMode == D2D1_ALPHA_MODE_STRAIGHT) return D2DERR_UNSUPPORTED_PIXEL_FORMAT;
    D2D1_SIZE_U size = window.pixelSize;
    if (!size.width || !size.height) lm_gfx::windowPixelSize(window.hwnd, size.width, size.height);
    ComPtr<HwndTarget> created;
    created.Attach(new HwndTarget(window.hwnd, format, properties.dpiX));
    const HRESULT result = created->Resize(&size);
    if (SUCCEEDED(result)) *target = created.Detach();
    return result;
}
} // namespace lm_d2d
