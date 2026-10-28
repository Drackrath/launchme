// Direct2D brushes as Skia paints. A brush lies in the render target's world space mapped by its
// own transform: the shader's local matrix is the brush transform, the canvas matrix the world
// transform. Shaders are kept until a property changes; the opacity is the paint's alpha.
#include "d2d_internal.hpp"

#include "include/core/SkColorSpace.h"
#include "include/effects/SkGradient.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/GrRecordingContext.h"

#include <algorithm>

namespace lm_d2d {
namespace {
SkTileMode tileFor(D2D1_EXTEND_MODE mode) {
    switch (mode) {
    case D2D1_EXTEND_MODE_WRAP: return SkTileMode::kRepeat;
    case D2D1_EXTEND_MODE_MIRROR: return SkTileMode::kMirror;
    default: return SkTileMode::kClamp;
    }
}

template <class Interface>
class BrushObject : public lm_com::Object<Interface>, public Brush {
public:
    void STDMETHODCALLTYPE SetOpacity(FLOAT opacity) override { opacity_ = opacity; }
    void STDMETHODCALLTYPE SetTransform(const D2D1_MATRIX_3X2_F* transform) override {
        if (transform) { transform_ = *transform; shader_.reset(); }
    }
    FLOAT STDMETHODCALLTYPE GetOpacity() const override { return opacity_; }
    void STDMETHODCALLTYPE GetTransform(D2D1_MATRIX_3X2_F* transform) const override { if (transform) *transform = transform_; }
protected:
    explicit BrushObject(const D2D1_BRUSH_PROPERTIES* properties) {
        if (properties) { opacity_ = properties->opacity; transform_ = properties->transform; }
    }
    float alpha(float extra) const { return std::clamp(opacity_ * extra, 0.0f, 1.0f); }
    float opacity_ = 1;
    D2D1_MATRIX_3X2_F transform_ = D2D1::IdentityMatrix();
    mutable sk_sp<SkShader> shader_; // Reset by any setter that changes it.
};

class SolidColorBrush final : public BrushObject<ID2D1SolidColorBrush> {
public:
    SolidColorBrush(const D2D1_COLOR_F& color, const D2D1_BRUSH_PROPERTIES* properties) : BrushObject(properties), color_(color) {}
    void STDMETHODCALLTYPE SetColor(const D2D1_COLOR_F* color) override { if (color) color_ = *color; }
    D2D1_COLOR_F STDMETHODCALLTYPE GetColor() const override { return color_; }
    void paint(SkPaint& paint, GrDirectContext*, float extra) const override {
        paint.setShader(nullptr);
        paint.setColor4f({std::clamp(color_.r, 0.0f, 1.0f), std::clamp(color_.g, 0.0f, 1.0f), std::clamp(color_.b, 0.0f, 1.0f),
                          std::clamp(color_.a, 0.0f, 1.0f) * alpha(extra)});
    }
private:
    D2D1_COLOR_F color_;
};

class GradientStopCollection final : public lm_com::Object<ID2D1GradientStopCollection> {
public:
    GradientStopCollection(std::vector<D2D1_GRADIENT_STOP> stops, D2D1_EXTEND_MODE extend) : extend_(extend) {
        std::stable_sort(stops.begin(), stops.end(), [](const auto& a, const auto& b) { return a.position < b.position; });
        for (const auto& stop : stops) {
            colors_.push_back({std::clamp(stop.color.r, 0.0f, 1.0f), std::clamp(stop.color.g, 0.0f, 1.0f),
                               std::clamp(stop.color.b, 0.0f, 1.0f), std::clamp(stop.color.a, 0.0f, 1.0f)});
            positions_.push_back(std::clamp(stop.position, 0.0f, 1.0f));
        }
    }
    UINT32 STDMETHODCALLTYPE GetGradientStopCount() const override { return static_cast<UINT32>(colors_.size()); }
    // Interpolated in premultiplied colour, as Direct2D does (no dark fringe toward transparent stops).
    SkGradient gradient() const {
        return SkGradient(SkGradient::Colors(colors_, positions_, tileFor(extend_)),
                          SkGradient::Interpolation{SkGradient::Interpolation::InPremul::kYes});
    }
    SkColor4f last() const { return colors_.empty() ? SkColors::kTransparent : colors_.back(); }
private:
    std::vector<SkColor4f> colors_;
    std::vector<float> positions_;
    D2D1_EXTEND_MODE extend_;
};

// A gradient's shader, or its last colour when the gradient is degenerate.
void shaderPaint(SkPaint& paint, const sk_sp<SkShader>& shader, const GradientStopCollection& stops, float alpha) {
    if (shader) {
        paint.setShader(shader);
        paint.setColor4f({0, 0, 0, alpha});
    } else {
        SkColor4f color = stops.last();
        color.fA *= alpha;
        paint.setShader(nullptr);
        paint.setColor4f(color);
    }
}

class LinearGradientBrush final : public BrushObject<ID2D1LinearGradientBrush> {
public:
    LinearGradientBrush(const D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES& gradient, const D2D1_BRUSH_PROPERTIES* properties,
                        ComPtr<GradientStopCollection> stops)
        : BrushObject(properties), gradient_(gradient), stops_(std::move(stops)) {}
    void STDMETHODCALLTYPE SetStartPoint(D2D1_POINT_2F point) override { gradient_.startPoint = point; shader_.reset(); }
    void STDMETHODCALLTYPE SetEndPoint(D2D1_POINT_2F point) override { gradient_.endPoint = point; shader_.reset(); }
    D2D1_POINT_2F STDMETHODCALLTYPE GetStartPoint() const override { return gradient_.startPoint; }
    D2D1_POINT_2F STDMETHODCALLTYPE GetEndPoint() const override { return gradient_.endPoint; }
    void paint(SkPaint& paint, GrDirectContext*, float extra) const override {
        if (!shader_) {
            const SkPoint points[2] = {{gradient_.startPoint.x, gradient_.startPoint.y}, {gradient_.endPoint.x, gradient_.endPoint.y}};
            const SkMatrix local = toSk(transform_);
            if (points[0] != points[1]) shader_ = SkShaders::LinearGradient(points, stops_->gradient(), &local);
        }
        shaderPaint(paint, shader_, *stops_.Get(), alpha(extra));
    }
private:
    D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES gradient_;
    ComPtr<GradientStopCollection> stops_;
};

// An elliptical gradient: a circular one of radius 1 in a space scaled by the radii around the
// centre; an origin offset is the focus of a two-point conical gradient.
class RadialGradientBrush final : public BrushObject<ID2D1RadialGradientBrush> {
public:
    RadialGradientBrush(const D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES& gradient, const D2D1_BRUSH_PROPERTIES* properties,
                        ComPtr<GradientStopCollection> stops)
        : BrushObject(properties), gradient_(gradient), stops_(std::move(stops)) {}
    void STDMETHODCALLTYPE SetCenter(D2D1_POINT_2F center) override { gradient_.center = center; shader_.reset(); }
    void STDMETHODCALLTYPE SetGradientOriginOffset(D2D1_POINT_2F offset) override { gradient_.gradientOriginOffset = offset; shader_.reset(); }
    void STDMETHODCALLTYPE SetRadiusX(FLOAT radius) override { gradient_.radiusX = radius; shader_.reset(); }
    void STDMETHODCALLTYPE SetRadiusY(FLOAT radius) override { gradient_.radiusY = radius; shader_.reset(); }
    D2D1_POINT_2F STDMETHODCALLTYPE GetCenter() const override { return gradient_.center; }
    D2D1_POINT_2F STDMETHODCALLTYPE GetGradientOriginOffset() const override { return gradient_.gradientOriginOffset; }
    FLOAT STDMETHODCALLTYPE GetRadiusX() const override { return gradient_.radiusX; }
    FLOAT STDMETHODCALLTYPE GetRadiusY() const override { return gradient_.radiusY; }
    void paint(SkPaint& paint, GrDirectContext*, float extra) const override {
        const float rx = std::abs(gradient_.radiusX), ry = std::abs(gradient_.radiusY);
        if (rx < 1e-6f || ry < 1e-6f) { paint.setShader(nullptr); paint.setColor4f(SkColors::kTransparent); return; }
        if (!shader_) {
            SkMatrix local = toSk(transform_);
            local.preTranslate(gradient_.center.x, gradient_.center.y);
            local.preScale(rx, ry);
            const SkPoint focus{gradient_.gradientOriginOffset.x / rx, gradient_.gradientOriginOffset.y / ry};
            shader_ = focus.isZero() ? SkShaders::RadialGradient({0, 0}, 1, stops_->gradient(), &local)
                                     : SkShaders::TwoPointConicalGradient(focus, 0, {0, 0}, 1, stops_->gradient(), &local);
        }
        shaderPaint(paint, shader_, *stops_.Get(), alpha(extra));
    }
private:
    D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES gradient_;
    ComPtr<GradientStopCollection> stops_;
};

// The bitmap tiles its DIP space with an extend mode per axis.
class BitmapBrush final : public BrushObject<ID2D1BitmapBrush> {
public:
    BitmapBrush(ComPtr<Bitmap> bitmap, const D2D1_BITMAP_BRUSH_PROPERTIES* bitmapProperties, const D2D1_BRUSH_PROPERTIES* properties)
        : BrushObject(properties), bitmap_(std::move(bitmap)) {
        if (bitmapProperties) {
            extend_x_ = bitmapProperties->extendModeX;
            extend_y_ = bitmapProperties->extendModeY;
            interpolation_ = bitmapProperties->interpolationMode;
        }
    }
    void STDMETHODCALLTYPE SetExtendModeX(D2D1_EXTEND_MODE mode) override { extend_x_ = mode; }
    void STDMETHODCALLTYPE SetExtendModeY(D2D1_EXTEND_MODE mode) override { extend_y_ = mode; }
    void STDMETHODCALLTYPE SetInterpolationMode(D2D1_BITMAP_INTERPOLATION_MODE mode) override { interpolation_ = mode; }
    void STDMETHODCALLTYPE SetBitmap(ID2D1Bitmap* bitmap) override { bitmap_ = Bitmap::from(bitmap); }
    // Made per draw: the bitmap's pixels may have changed (a render target's).
    void paint(SkPaint& paint, GrDirectContext* gpu, float extra) const override {
        sk_sp<SkImage> image = bitmap_ ? bitmap_->image(gpu) : nullptr;
        if (!image) { paint.setShader(nullptr); paint.setColor4f(SkColors::kTransparent); return; }
        SkMatrix local = toSk(transform_);
        local.preScale(static_cast<float>(1 / bitmap_->scaleX()), static_cast<float>(1 / bitmap_->scaleY()));
        const SkSamplingOptions sampling(interpolation_ == D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR ? SkFilterMode::kNearest
                                                                                                            : SkFilterMode::kLinear);
        paint.setShader(image->makeShader(tileFor(extend_x_), tileFor(extend_y_), sampling, &local));
        paint.setColor4f({0, 0, 0, alpha(extra)});
    }
private:
    ComPtr<Bitmap> bitmap_;
    D2D1_EXTEND_MODE extend_x_ = D2D1_EXTEND_MODE_CLAMP, extend_y_ = D2D1_EXTEND_MODE_CLAMP;
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
bool brushPaint(IUnknown* brush, SkCanvas* canvas, SkPaint& paint) {
    const auto* own = lm_d2d::Brush::from(brush);
    if (!own) return false;
    GrRecordingContext* recording = canvas ? canvas->recordingContext() : nullptr;
    own->paint(paint, recording ? recording->asDirectContext() : nullptr, 1);
    return true;
}
} // namespace lm_gfx
