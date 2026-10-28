// The built-in Direct2D effects the launcher uses, as Skia image and colour filters (run on the
// GPU for GPU targets): Gaussian blur, colour matrix, saturation, blend, 2D affine transform,
// arithmetic composite, point-specular lighting and composite. An effect keeps its last result
// until a property, an input, the requested resolution or the drawing context changes.
#include "d2d_internal.hpp"

#include "include/core/SkColorFilter.h"
#include "include/core/SkData.h"
#include "include/core/SkPoint3.h"
#include "include/effects/SkImageFilters.h"
#include "include/effects/SkRuntimeEffect.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace lm_d2d {
namespace {
enum class Kind { blur, color_matrix, saturation, blend, affine, arithmetic, specular, composite };

// A pixel region in the evaluation grid (pixels at the requested scale).
SkIRect areaOf(const ImageData& image) {
    if (!image.image) return SkIRect::MakeEmpty();
    return SkIRect::MakeXYWH(image.x, image.y, image.image->width(), image.image->height());
}

SkIRect unite(SkIRect a, const SkIRect& b) {
    if (a.isEmpty()) return b;
    if (!b.isEmpty()) a.join(b);
    return a;
}

// `input` drawn through `filter` (and `colors`), the result's pixels over `area`.
ImageData evaluate(GrDirectContext* gpu, const SkIRect& area, const ImageData& input, sk_sp<SkImageFilter> filter,
                   sk_sp<SkColorFilter> colors = nullptr) {
    if (area.isEmpty() || !input.image) return {};
    sk_sp<SkSurface> surface = makeSurface(gpu, bgraInfo(area.width(), area.height()));
    if (!surface) return {};
    SkCanvas* canvas = surface->getCanvas();
    canvas->clear(SK_ColorTRANSPARENT);
    canvas->translate(static_cast<float>(-area.x()), static_cast<float>(-area.y()));
    SkPaint paint;
    paint.setImageFilter(std::move(filter));
    paint.setColorFilter(std::move(colors));
    canvas->drawImage(input.image, static_cast<float>(input.x), static_cast<float>(input.y), SkSamplingOptions(SkFilterMode::kLinear), &paint);
    return {surface->makeImageSnapshot(), area.x(), area.y()};
}

// An image placed where it lies in the grid, as a filter input.
sk_sp<SkImageFilter> placed(const ImageData& image) {
    if (!image.image) return SkImageFilters::Empty();
    const SkRect bounds = SkRect::MakeIWH(image.image->width(), image.image->height());
    return SkImageFilters::Image(image.image, bounds, bounds.makeOffset(static_cast<float>(image.x), static_cast<float>(image.y)),
                                 SkSamplingOptions(SkFilterMode::kLinear));
}

// Direct2D's colour matrix applied to premultiplied values as they are (straight alpha mode).
sk_sp<SkColorFilter> premultipliedMatrix(const D2D1_MATRIX_5X4_F& m, bool clamp) {
    static const auto effect = [] {
        return SkRuntimeEffect::MakeForColorFilter(SkString(
            "uniform half4x4 m; uniform half4 t; uniform half clampOutput;"
            "half4 main(half4 c) { half4 r = m * c + t; return clampOutput > 0.5 ? saturate(r) : r; }")).effect;
    }();
    if (!effect) return nullptr;
    // Direct2D's rows (one per input channel) are the shader's columns: the first 16 floats as they are.
    std::array<float, 21> uniforms{};
    std::memcpy(uniforms.data(), &m, sizeof(float) * 20);
    uniforms[20] = clamp ? 1.0f : 0.0f;
    return effect->makeColorFilter(SkData::MakeWithCopy(uniforms.data(), std::min(sizeof(uniforms), effect->uniformSize())));
}

SkBlendMode blendFor(D2D1_BLEND_MODE mode) {
    switch (mode) {
    case D2D1_BLEND_MODE_MULTIPLY: return SkBlendMode::kMultiply;
    case D2D1_BLEND_MODE_SCREEN: return SkBlendMode::kScreen;
    case D2D1_BLEND_MODE_DARKEN: return SkBlendMode::kDarken;
    case D2D1_BLEND_MODE_LIGHTEN: return SkBlendMode::kLighten;
    default: return SkBlendMode::kSrcOver; // Other modes: the source over the destination.
    }
}

SkBlendMode blendFor(D2D1_COMPOSITE_MODE mode) {
    switch (mode) {
    case D2D1_COMPOSITE_MODE_DESTINATION_OVER: return SkBlendMode::kDstOver;
    case D2D1_COMPOSITE_MODE_SOURCE_IN: return SkBlendMode::kSrcIn;
    case D2D1_COMPOSITE_MODE_DESTINATION_IN: return SkBlendMode::kDstIn;
    case D2D1_COMPOSITE_MODE_SOURCE_OUT: return SkBlendMode::kSrcOut;
    case D2D1_COMPOSITE_MODE_DESTINATION_OUT: return SkBlendMode::kDstOut;
    case D2D1_COMPOSITE_MODE_SOURCE_ATOP: return SkBlendMode::kSrcATop;
    case D2D1_COMPOSITE_MODE_DESTINATION_ATOP: return SkBlendMode::kDstATop;
    case D2D1_COMPOSITE_MODE_XOR: return SkBlendMode::kXor;
    case D2D1_COMPOSITE_MODE_PLUS: return SkBlendMode::kPlus;
    case D2D1_COMPOSITE_MODE_SOURCE_COPY:
    case D2D1_COMPOSITE_MODE_BOUNDED_SOURCE_COPY: return SkBlendMode::kSrc;
    default: return SkBlendMode::kSrcOver;
    }
}

template <class T>
bool read(const BYTE* data, UINT32 size, T& value) {
    if (!data || size != sizeof(T)) return false;
    std::memcpy(&value, data, sizeof(T));
    return true;
}

class Effect final : public lm_com::Object<ID2D1Effect> {
public:
    explicit Effect(Kind kind) : kind_(kind), inputs_(kind == Kind::blend || kind == Kind::arithmetic || kind == Kind::composite ? 2 : 1) {}
    HRESULT STDMETHODCALLTYPE SetValue(UINT32 index, D2D1_PROPERTY_TYPE, const BYTE* data, UINT32 size) override;
    void STDMETHODCALLTYPE SetInput(UINT32 index, ID2D1Image* input, BOOL) override {
        if (index >= inputs_.size() || inputs_[index].Get() == input) return;
        inputs_[index] = input;
        version_ = newVersion();
        cached_ = {}; // The old result's memory goes with the old input.
    }
    void STDMETHODCALLTYPE GetOutput(ID2D1Image** output) const override;

    ImageData render(double scale, GrDirectContext* gpu) {
        const uint64_t current = version();
        if (current != cached_version_ || scale != cached_scale_ || gpu != cached_gpu_) {
            cached_ = compute(scale, gpu);
            cached_version_ = current;
            cached_scale_ = scale;
            cached_gpu_ = gpu;
        }
        return cached_;
    }
    // Own settings and every input's content.
    uint64_t version() const {
        uint64_t hash = version_;
        for (const auto& input : inputs_) {
            const auto* source = ImageSource::from(input.Get());
            hash = (hash ^ (source ? source->version() : 0)) * 0x100000001B3ull + 0x9E3779B97F4A7C15ull;
        }
        return hash;
    }

private:
    template <class T>
    HRESULT store(T& field, const BYTE* data, UINT32 size) {
        T value;
        if (!read(data, size, value)) return E_INVALIDARG;
        if (std::memcmp(&value, &field, sizeof(T)) != 0) { field = value; version_ = newVersion(); }
        return S_OK;
    }
    ImageData input(size_t index, double scale, GrDirectContext* gpu) const {
        auto* source = index < inputs_.size() ? ImageSource::from(inputs_[index].Get()) : nullptr;
        return source ? source->render(scale, gpu) : ImageData{};
    }
    ImageData compute(double scale, GrDirectContext* gpu) const;
    ImageData twoInputs(GrDirectContext* gpu, const ImageData& destination, const ImageData& source) const;

    Kind kind_;
    std::vector<ComPtr<ID2D1Image>> inputs_;
    uint64_t version_ = newVersion();
    // Properties, with Direct2D's defaults.
    float deviation_ = 3;
    UINT32 optimization_ = D2D1_GAUSSIANBLUR_OPTIMIZATION_BALANCED; // Accepted; Skia picks its own blur method.
    D2D1_BORDER_MODE border_ = D2D1_BORDER_MODE_SOFT;
    D2D1_MATRIX_5X4_F matrix_{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0};
    D2D1_COLORMATRIX_ALPHA_MODE alpha_mode_ = D2D1_COLORMATRIX_ALPHA_MODE_PREMULTIPLIED;
    BOOL clamp_ = FALSE;
    float saturation_ = 0.5f;
    D2D1_BLEND_MODE blend_ = D2D1_BLEND_MODE_MULTIPLY;
    D2D1_2DAFFINETRANSFORM_INTERPOLATION_MODE interpolation_ = D2D1_2DAFFINETRANSFORM_INTERPOLATION_MODE_LINEAR;
    D2D1_MATRIX_3X2_F transform_ = D2D1::IdentityMatrix();
    float sharpness_ = 1; // Accepted; cubic sharpness is not modelled.
    D2D1_VECTOR_4F coefficients_{1, 0, 0, 0};
    D2D1_VECTOR_3F light_{0, 0, 0}, color_{1, 1, 1};
    float exponent_ = 1, constant_ = 1, surface_scale_ = 1;
    D2D1_VECTOR_2F kernel_{1, 1}; // Accepted; Skia's lighting samples one pixel apart.
    UINT32 scale_mode_ = D2D1_POINTSPECULAR_SCALE_MODE_LINEAR; // Accepted; the height map is never rescaled.
    D2D1_COMPOSITE_MODE composite_mode_ = D2D1_COMPOSITE_MODE_SOURCE_OVER;

    uint64_t cached_version_ = 0;
    double cached_scale_ = 0;
    GrDirectContext* cached_gpu_ = nullptr;
    ImageData cached_;
};

// Its effect's result; holds the effect alive (SetInputEffect keeps the output only).
class EffectOutput final : public lm_com::Object<ID2D1Image>, public ImageSource {
public:
    explicit EffectOutput(ComPtr<Effect> effect) : effect_(std::move(effect)) {}
    ImageData render(double scale, GrDirectContext* gpu) override { return effect_->render(scale, gpu); }
    uint64_t version() const override { return effect_->version(); }
private:
    ComPtr<Effect> effect_;
};

void STDMETHODCALLTYPE Effect::GetOutput(ID2D1Image** output) const {
    if (output) *output = new EffectOutput(ComPtr<Effect>(const_cast<Effect*>(this)));
}

HRESULT STDMETHODCALLTYPE Effect::SetValue(UINT32 index, D2D1_PROPERTY_TYPE, const BYTE* data, UINT32 size) {
    switch (kind_) {
    case Kind::blur:
        if (index == D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION) return store(deviation_, data, size);
        if (index == D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION) return store(optimization_, data, size);
        if (index == D2D1_GAUSSIANBLUR_PROP_BORDER_MODE) return store(border_, data, size);
        break;
    case Kind::color_matrix:
        if (index == D2D1_COLORMATRIX_PROP_COLOR_MATRIX) return store(matrix_, data, size);
        if (index == D2D1_COLORMATRIX_PROP_ALPHA_MODE) return store(alpha_mode_, data, size);
        if (index == D2D1_COLORMATRIX_PROP_CLAMP_OUTPUT) return store(clamp_, data, size);
        break;
    case Kind::saturation:
        if (index == D2D1_SATURATION_PROP_SATURATION) return store(saturation_, data, size);
        break;
    case Kind::blend:
        if (index == D2D1_BLEND_PROP_MODE) return store(blend_, data, size);
        break;
    case Kind::affine:
        if (index == D2D1_2DAFFINETRANSFORM_PROP_INTERPOLATION_MODE) return store(interpolation_, data, size);
        if (index == D2D1_2DAFFINETRANSFORM_PROP_BORDER_MODE) return store(border_, data, size);
        if (index == D2D1_2DAFFINETRANSFORM_PROP_TRANSFORM_MATRIX) return store(transform_, data, size);
        if (index == D2D1_2DAFFINETRANSFORM_PROP_SHARPNESS) return store(sharpness_, data, size);
        break;
    case Kind::arithmetic:
        if (index == D2D1_ARITHMETICCOMPOSITE_PROP_COEFFICIENTS) return store(coefficients_, data, size);
        if (index == D2D1_ARITHMETICCOMPOSITE_PROP_CLAMP_OUTPUT) return store(clamp_, data, size);
        break;
    case Kind::specular:
        if (index == D2D1_POINTSPECULAR_PROP_LIGHT_POSITION) return store(light_, data, size);
        if (index == D2D1_POINTSPECULAR_PROP_SPECULAR_EXPONENT) return store(exponent_, data, size);
        if (index == D2D1_POINTSPECULAR_PROP_SPECULAR_CONSTANT) return store(constant_, data, size);
        if (index == D2D1_POINTSPECULAR_PROP_SURFACE_SCALE) return store(surface_scale_, data, size);
        if (index == D2D1_POINTSPECULAR_PROP_COLOR) return store(color_, data, size);
        if (index == D2D1_POINTSPECULAR_PROP_KERNEL_UNIT_LENGTH) return store(kernel_, data, size);
        if (index == D2D1_POINTSPECULAR_PROP_SCALE_MODE) return store(scale_mode_, data, size);
        break;
    case Kind::composite:
        if (index == D2D1_COMPOSITE_PROP_MODE) return store(composite_mode_, data, size);
        break;
    }
    return E_INVALIDARG;
}

// Blend, arithmetic composite and composite: input 0 the destination (background), 1 the source
// (foreground), over both inputs' area. The present input is drawn; the other is placed in it.
ImageData Effect::twoInputs(GrDirectContext* gpu, const ImageData& destination, const ImageData& source) const {
    const SkIRect area = unite(areaOf(destination), areaOf(source));
    if (area.isEmpty()) return {};
    const bool drawn_destination = destination.image != nullptr;
    sk_sp<SkImageFilter> background = drawn_destination ? nullptr : SkImageFilters::Empty();
    sk_sp<SkImageFilter> foreground = drawn_destination ? placed(source) : nullptr;
    sk_sp<SkImageFilter> filter;
    switch (kind_) {
    case Kind::blend: filter = SkImageFilters::Blend(blendFor(blend_), background, foreground); break;
    case Kind::composite: filter = SkImageFilters::Blend(blendFor(composite_mode_), background, foreground); break;
    default: {
        const auto [k1, k2, k3, k4] = coefficients_;
        filter = SkImageFilters::Arithmetic(k1, k2, k3, k4, true, background, foreground, SkImageFilters::CropRect(SkRect::Make(area)));
        break;
    }
    }
    return evaluate(gpu, area, drawn_destination ? destination : source, std::move(filter));
}

ImageData Effect::compute(double scale, GrDirectContext* gpu) const {
    switch (kind_) {
    case Kind::blur: {
        const ImageData in = input(0, scale, gpu);
        const float sigma = static_cast<float>(deviation_ * scale);
        if (!in.image || sigma < 0.3f) return in;
        // A soft border grows the image by three deviations (fading into transparency); a hard
        // one keeps its size and mirrors its edges.
        const bool hard = border_ == D2D1_BORDER_MODE_HARD;
        const int pad = hard ? 0 : static_cast<int>(std::ceil(sigma * 3));
        const SkIRect area = areaOf(in).makeOutset(pad, pad);
        return evaluate(gpu, area, in, SkImageFilters::Blur(sigma, sigma, hard ? SkTileMode::kMirror : SkTileMode::kDecal, nullptr));
    }
    case Kind::color_matrix:
    case Kind::saturation: {
        const ImageData in = input(0, scale, gpu);
        if (!in.image) return {};
        sk_sp<SkColorFilter> colors;
        if (kind_ == Kind::saturation) {
            const float s = std::clamp(saturation_, 0.0f, 1.0f);
            const float m[20] = {0.213f + 0.787f * s, 0.715f - 0.715f * s, 0.072f - 0.072f * s, 0, 0,
                                 0.213f - 0.213f * s, 0.715f + 0.285f * s, 0.072f - 0.072f * s, 0, 0,
                                 0.213f - 0.213f * s, 0.715f - 0.715f * s, 0.072f + 0.928f * s, 0, 0,
                                 0, 0, 0, 1, 0};
            colors = SkColorFilters::Matrix(m);
        } else if (alpha_mode_ == D2D1_COLORMATRIX_ALPHA_MODE_PREMULTIPLIED) {
            // Skia's matrix works on straight colour and premultiplies the result, as this mode does.
            const auto& d = matrix_;
            const float m[20] = {d._11, d._21, d._31, d._41, d._51, d._12, d._22, d._32, d._42, d._52,
                                 d._13, d._23, d._33, d._43, d._53, d._14, d._24, d._34, d._44, d._54};
            colors = SkColorFilters::Matrix(m, clamp_ ? SkColorFilters::Clamp::kYes : SkColorFilters::Clamp::kNo);
        } else {
            colors = premultipliedMatrix(matrix_, clamp_ != FALSE);
        }
        return evaluate(gpu, areaOf(in), in, nullptr, std::move(colors));
    }
    case Kind::blend:
    case Kind::arithmetic:
    case Kind::composite: return twoInputs(gpu, input(0, scale, gpu), input(1, scale, gpu));
    case Kind::affine: {
        const ImageData in = input(0, scale, gpu);
        if (!in.image) return {};
        // The matrix maps DIPs: in grid pixels it is scaled around it.
        SkMatrix matrix = toSk(transform_);
        matrix.preScale(static_cast<float>(1 / scale), static_cast<float>(1 / scale));
        matrix.postScale(static_cast<float>(scale), static_cast<float>(scale));
        if (!matrix.invert(nullptr)) return {};
        const SkRect mapped = matrix.mapRect(SkRect::Make(areaOf(in)));
        const bool hard = border_ == D2D1_BORDER_MODE_HARD;
        const SkIRect area = hard ? mapped.round() : mapped.roundOut().makeOutset(1, 1);
        const SkSamplingOptions sampling(interpolation_ == D2D1_2DAFFINETRANSFORM_INTERPOLATION_MODE_NEAREST_NEIGHBOR ? SkFilterMode::kNearest
                                                                                                                      : SkFilterMode::kLinear);
        return evaluate(gpu, area, in, SkImageFilters::MatrixTransform(matrix, sampling, nullptr));
    }
    case Kind::specular: {
        const ImageData in = input(0, scale, gpu);
        if (!in.image) return {};
        const auto byte = [](float value) { return static_cast<U8CPU>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255)); };
        const SkPoint3 light = SkPoint3::Make(static_cast<float>(light_.x * scale), static_cast<float>(light_.y * scale),
                                              static_cast<float>(light_.z * scale));
        return evaluate(gpu, areaOf(in), in,
                        SkImageFilters::PointLitSpecular(light, SkColorSetRGB(byte(color_.x), byte(color_.y), byte(color_.z)),
                                                         surface_scale_, constant_, std::clamp(exponent_, 1.0f, 128.0f), nullptr));
    }
    }
    return {};
}
} // namespace

HRESULT createEffect(REFCLSID id, ID2D1Effect** effect) {
    if (!effect) return E_POINTER;
    *effect = nullptr;
    Kind kind;
    if (id == CLSID_D2D1GaussianBlur) kind = Kind::blur;
    else if (id == CLSID_D2D1ColorMatrix) kind = Kind::color_matrix;
    else if (id == CLSID_D2D1Saturation) kind = Kind::saturation;
    else if (id == CLSID_D2D1Blend) kind = Kind::blend;
    else if (id == CLSID_D2D12DAffineTransform) kind = Kind::affine;
    else if (id == CLSID_D2D1ArithmeticComposite) kind = Kind::arithmetic;
    else if (id == CLSID_D2D1PointSpecular) kind = Kind::specular;
    else if (id == CLSID_D2D1Composite) kind = Kind::composite;
    else return HRESULT_FROM_WIN32(1168); // ERROR_NOT_FOUND, as for an unregistered effect.
    *effect = new Effect(kind);
    return S_OK;
}
} // namespace lm_d2d
