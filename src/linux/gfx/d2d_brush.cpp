// Direct2D brushes as Cairo patterns. A brush is defined in the render target's world space
// mapped by its own transform; while drawing, Cairo's user space is that world space, so a
// pattern's matrix is the inverse of the brush transform.
#include "d2d_internal.hpp"

#include <algorithm>

namespace lm_d2d {
namespace {
cairo_extend_t extendFor(D2D1_EXTEND_MODE mode) {
    switch (mode) {
    case D2D1_EXTEND_MODE_WRAP: return CAIRO_EXTEND_REPEAT;
    case D2D1_EXTEND_MODE_MIRROR: return CAIRO_EXTEND_REFLECT;
    default: return CAIRO_EXTEND_PAD;
    }
}

Pattern transparent() { return Pattern(cairo_pattern_create_rgba(0, 0, 0, 0)); }

template <class Interface>
class BrushObject : public lm_com::Object<Interface>, public Brush {
public:
    void STDMETHODCALLTYPE SetOpacity(FLOAT opacity) override { opacity_ = opacity; }
    void STDMETHODCALLTYPE SetTransform(const D2D1_MATRIX_3X2_F* transform) override { if (transform) transform_ = *transform; }
    FLOAT STDMETHODCALLTYPE GetOpacity() const override { return opacity_; }
    void STDMETHODCALLTYPE GetTransform(D2D1_MATRIX_3X2_F* transform) const override { if (transform) *transform = transform_; }
protected:
    explicit BrushObject(const D2D1_BRUSH_PROPERTIES* properties) {
        if (properties) { opacity_ = properties->opacity; transform_ = properties->transform; }
    }
    // World space to the brush's own space; false for a degenerate transform (nothing painted).
    bool toBrush(cairo_matrix_t& matrix) const {
        matrix = toCairo(transform_);
        return cairo_matrix_invert(&matrix) == CAIRO_STATUS_SUCCESS;
    }
    float opacity_ = 1;
    D2D1_MATRIX_3X2_F transform_ = D2D1::IdentityMatrix();
};

class SolidColorBrush final : public BrushObject<ID2D1SolidColorBrush> {
public:
    SolidColorBrush(const D2D1_COLOR_F& color, const D2D1_BRUSH_PROPERTIES* properties) : BrushObject(properties), color_(color) {}
    void STDMETHODCALLTYPE SetColor(const D2D1_COLOR_F* color) override { if (color) color_ = *color; }
    D2D1_COLOR_F STDMETHODCALLTYPE GetColor() const override { return color_; }
    Paint paint(double extra) const override {
        return {Pattern(cairo_pattern_create_rgba(color_.r, color_.g, color_.b, std::clamp(color_.a * opacity_ * extra, 0.0, 1.0)))};
    }
private:
    D2D1_COLOR_F color_;
};

class GradientStopCollection final : public lm_com::Object<ID2D1GradientStopCollection> {
public:
    GradientStopCollection(std::vector<D2D1_GRADIENT_STOP> stops, D2D1_EXTEND_MODE extend) : stops_(std::move(stops)), extend_(extend) {
        std::stable_sort(stops_.begin(), stops_.end(), [](const auto& a, const auto& b) { return a.position < b.position; });
    }
    UINT32 STDMETHODCALLTYPE GetGradientStopCount() const override { return static_cast<UINT32>(stops_.size()); }
    // The stops (straight colours, interpolated by Cairo) and extend mode, at `alpha`.
    void fill(cairo_pattern_t* pattern, double alpha) const {
        for (const auto& stop : stops_)
            cairo_pattern_add_color_stop_rgba(pattern, stop.position, stop.color.r, stop.color.g, stop.color.b,
                                              std::clamp(stop.color.a * alpha, 0.0, 1.0));
        cairo_pattern_set_extend(pattern, extendFor(extend_));
    }
    // Clamped and fully transparent past the last stop: nothing shows outside the gradient's extent.
    bool fadesOut() const { return extend_ == D2D1_EXTEND_MODE_CLAMP && (stops_.empty() || stops_.back().color.a <= 0.0f); }
private:
    std::vector<D2D1_GRADIENT_STOP> stops_;
    D2D1_EXTEND_MODE extend_;
};

class LinearGradientBrush final : public BrushObject<ID2D1LinearGradientBrush> {
public:
    LinearGradientBrush(const D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES& gradient, const D2D1_BRUSH_PROPERTIES* properties,
                        ComPtr<GradientStopCollection> stops)
        : BrushObject(properties), gradient_(gradient), stops_(std::move(stops)) {}
    void STDMETHODCALLTYPE SetStartPoint(D2D1_POINT_2F point) override { gradient_.startPoint = point; }
    void STDMETHODCALLTYPE SetEndPoint(D2D1_POINT_2F point) override { gradient_.endPoint = point; }
    D2D1_POINT_2F STDMETHODCALLTYPE GetStartPoint() const override { return gradient_.startPoint; }
    D2D1_POINT_2F STDMETHODCALLTYPE GetEndPoint() const override { return gradient_.endPoint; }
    Paint paint(double extra) const override {
        cairo_matrix_t matrix;
        if (!toBrush(matrix)) return {transparent()};
        Pattern pattern(cairo_pattern_create_linear(gradient_.startPoint.x, gradient_.startPoint.y, gradient_.endPoint.x, gradient_.endPoint.y));
        stops_->fill(pattern.get(), opacity_ * extra);
        cairo_pattern_set_matrix(pattern.get(), &matrix);
        return {std::move(pattern)};
    }
private:
    D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES gradient_;
    ComPtr<GradientStopCollection> stops_;
};

// An elliptical gradient: drawn as a circular one of radius 1 in a space scaled by the radii,
// the focus at the origin offset.
class RadialGradientBrush final : public BrushObject<ID2D1RadialGradientBrush> {
public:
    RadialGradientBrush(const D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES& gradient, const D2D1_BRUSH_PROPERTIES* properties,
                        ComPtr<GradientStopCollection> stops)
        : BrushObject(properties), gradient_(gradient), stops_(std::move(stops)) {}
    void STDMETHODCALLTYPE SetCenter(D2D1_POINT_2F center) override { gradient_.center = center; }
    void STDMETHODCALLTYPE SetGradientOriginOffset(D2D1_POINT_2F offset) override { gradient_.gradientOriginOffset = offset; }
    void STDMETHODCALLTYPE SetRadiusX(FLOAT radius) override { gradient_.radiusX = radius; }
    void STDMETHODCALLTYPE SetRadiusY(FLOAT radius) override { gradient_.radiusY = radius; }
    D2D1_POINT_2F STDMETHODCALLTYPE GetCenter() const override { return gradient_.center; }
    D2D1_POINT_2F STDMETHODCALLTYPE GetGradientOriginOffset() const override { return gradient_.gradientOriginOffset; }
    FLOAT STDMETHODCALLTYPE GetRadiusX() const override { return gradient_.radiusX; }
    FLOAT STDMETHODCALLTYPE GetRadiusY() const override { return gradient_.radiusY; }
    Paint paint(double extra) const override {
        cairo_matrix_t matrix;
        const double rx = std::max(std::abs(gradient_.radiusX), 1e-6f), ry = std::max(std::abs(gradient_.radiusY), 1e-6f);
        if (!toBrush(matrix)) return {transparent()};
        cairo_matrix_t unit;
        cairo_matrix_init_translate(&unit, -gradient_.center.x, -gradient_.center.y);
        cairo_matrix_t scale;
        cairo_matrix_init_scale(&scale, 1 / rx, 1 / ry);
        matrix = multiply(multiply(matrix, unit), scale);
        Pattern pattern(cairo_pattern_create_radial(gradient_.gradientOriginOffset.x / rx, gradient_.gradientOriginOffset.y / ry, 0, 0, 0, 1));
        stops_->fill(pattern.get(), opacity_ * extra);
        cairo_pattern_set_matrix(pattern.get(), &matrix);
        Paint result{std::move(pattern)};
        if (stops_->fadesOut()) {
            // The ellipse's box in world space (the brush transform maps its corners).
            const double xs[] = {gradient_.center.x - rx, gradient_.center.x + rx}, ys[] = {gradient_.center.y - ry, gradient_.center.y + ry};
            const cairo_matrix_t world = toCairo(transform_);
            result.bounded = true;
            result.left = result.top = 1e300; result.right = result.bottom = -1e300;
            for (const double x : xs) for (const double y : ys) {
                double px = x, py = y;
                cairo_matrix_transform_point(&world, &px, &py);
                result.left = std::min(result.left, px); result.right = std::max(result.right, px);
                result.top = std::min(result.top, py); result.bottom = std::max(result.bottom, py);
            }
        }
        return result;
    }
private:
    D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES gradient_;
    ComPtr<GradientStopCollection> stops_;
};

// The bitmap tiles its DIP space. Cairo has one extend mode for both axes: X's applies (the
// launcher always sets both alike).
class BitmapBrush final : public BrushObject<ID2D1BitmapBrush> {
public:
    BitmapBrush(ComPtr<Bitmap> bitmap, const D2D1_BITMAP_BRUSH_PROPERTIES* bitmapProperties, const D2D1_BRUSH_PROPERTIES* properties)
        : BrushObject(properties), bitmap_(std::move(bitmap)) {
        if (bitmapProperties) {
            extend_x_ = bitmapProperties->extendModeX;
            interpolation_ = bitmapProperties->interpolationMode;
        }
    }
    void STDMETHODCALLTYPE SetExtendModeX(D2D1_EXTEND_MODE mode) override { extend_x_ = mode; }
    void STDMETHODCALLTYPE SetExtendModeY(D2D1_EXTEND_MODE) override {}
    void STDMETHODCALLTYPE SetInterpolationMode(D2D1_BITMAP_INTERPOLATION_MODE mode) override { interpolation_ = mode; }
    void STDMETHODCALLTYPE SetBitmap(ID2D1Bitmap* bitmap) override { bitmap_ = Bitmap::from(bitmap); }
    Paint paint(double extra) const override {
        cairo_matrix_t matrix;
        if (!bitmap_ || !toBrush(matrix)) return {transparent()};
        cairo_matrix_t pixels;
        cairo_matrix_init_scale(&pixels, bitmap_->scaleX(), bitmap_->scaleY());
        matrix = multiply(matrix, pixels);
        Pattern pattern(cairo_pattern_create_for_surface(bitmap_->surface()));
        cairo_pattern_set_matrix(pattern.get(), &matrix);
        cairo_pattern_set_extend(pattern.get(), extendFor(extend_x_));
        cairo_pattern_set_filter(pattern.get(), filterFor(interpolation_ == D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR ?
                                                          D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR : D2D1_INTERPOLATION_MODE_LINEAR));
        return {std::move(pattern), opacity_ * extra};
    }
private:
    ComPtr<Bitmap> bitmap_;
    D2D1_EXTEND_MODE extend_x_ = D2D1_EXTEND_MODE_CLAMP;
    D2D1_BITMAP_INTERPOLATION_MODE interpolation_ = D2D1_BITMAP_INTERPOLATION_MODE_LINEAR;
};

GradientStopCollection* stopsOf(ID2D1GradientStopCollection* collection) { return dynamic_cast<GradientStopCollection*>(collection); }

template <class Object, class Interface, class... Arguments>
HRESULT make(Interface** out, Arguments&&... arguments) {
    if (!out) return E_POINTER;
    *out = new Object(std::forward<Arguments>(arguments)...);
    return S_OK;
}
} // namespace

const Brush* Brush::from(IUnknown* brush) { return dynamic_cast<const Brush*>(brush); }

HRESULT createSolidColorBrush(const D2D1_COLOR_F& color, const D2D1_BRUSH_PROPERTIES* properties, ID2D1SolidColorBrush** brush) {
    return make<SolidColorBrush>(brush, color, properties);
}

HRESULT createGradientStopCollection(const D2D1_GRADIENT_STOP* stops, UINT32 count, D2D1_EXTEND_MODE extendMode,
                                     ID2D1GradientStopCollection** collection) {
    if (!stops || !count) return E_INVALIDARG;
    return make<GradientStopCollection>(collection, std::vector<D2D1_GRADIENT_STOP>(stops, stops + count), extendMode);
}

HRESULT createLinearGradientBrush(const D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES& gradient, const D2D1_BRUSH_PROPERTIES* properties,
                                  ID2D1GradientStopCollection* stops, ID2D1LinearGradientBrush** brush) {
    auto* collection = stopsOf(stops);
    if (!collection) return E_INVALIDARG;
    return make<LinearGradientBrush>(brush, gradient, properties, ComPtr<GradientStopCollection>(collection));
}

HRESULT createRadialGradientBrush(const D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES& gradient, const D2D1_BRUSH_PROPERTIES* properties,
                                  ID2D1GradientStopCollection* stops, ID2D1RadialGradientBrush** brush) {
    auto* collection = stopsOf(stops);
    if (!collection) return E_INVALIDARG;
    return make<RadialGradientBrush>(brush, gradient, properties, ComPtr<GradientStopCollection>(collection));
}

HRESULT createBitmapBrush(ID2D1Bitmap* bitmap, const D2D1_BITMAP_BRUSH_PROPERTIES* bitmapProperties, const D2D1_BRUSH_PROPERTIES* properties,
                          ID2D1BitmapBrush** brush) {
    return make<BitmapBrush>(brush, ComPtr<Bitmap>(Bitmap::from(bitmap)), bitmapProperties, properties);
}
} // namespace lm_d2d

namespace lm_gfx {
// The text's user space may be moved from the world space the brush was set up in: the world
// transform the render target left on the context (user data) maps between them.
bool applyBrush(cairo_t* context, IUnknown* brush) {
    const auto* own = lm_d2d::Brush::from(brush);
    if (!own) return false;
    auto paint = own->paint(1);
    if (const auto* world = static_cast<const cairo_matrix_t*>(cairo_get_user_data(context, lm_d2d::worldKey()))) {
        cairo_matrix_t current, to_world = *world, pattern;
        cairo_get_matrix(context, &current);
        if (cairo_matrix_invert(&to_world) == CAIRO_STATUS_SUCCESS) {
            cairo_pattern_get_matrix(paint.pattern.get(), &pattern);
            const cairo_matrix_t mapped = lm_d2d::multiply(lm_d2d::multiply(current, to_world), pattern);
            cairo_pattern_set_matrix(paint.pattern.get(), &mapped);
        }
    }
    cairo_set_source(context, paint.pattern.get());
    return true;
}
} // namespace lm_gfx
