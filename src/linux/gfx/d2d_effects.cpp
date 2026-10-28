// The built-in Direct2D effects the launcher uses, computed on the CPU over premultiplied 8-bit
// pixels (Direct2D's default 8 bpc buffers): Gaussian blur, colour matrix, saturation, blend,
// 2D affine transform, arithmetic composite, point-specular lighting and composite. An effect
// keeps its last result until a property, an input or the requested resolution changes.
#include "d2d_internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace lm_d2d {
namespace {
enum class Kind { blur, color_matrix, saturation, blend, affine, arithmetic, specular, composite };

// A pixel region in the evaluation grid (pixels at the requested scale).
struct Area {
    int x = 0, y = 0, width = 0, height = 0;
    bool empty() const { return width <= 0 || height <= 0; }
};

Area areaOf(const ImageData& image) {
    if (!image.surface) return {};
    return {image.x, image.y, cairo_image_surface_get_width(image.surface.get()), cairo_image_surface_get_height(image.surface.get())};
}

Area unite(Area a, Area b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    const int left = std::min(a.x, b.x), top = std::min(a.y, b.y);
    return {left, top, std::max(a.x + a.width, b.x + b.width) - left, std::max(a.y + a.height, b.y + b.height) - top};
}

// Premultiplied pixels of an image read in grid coordinates (transparent outside it).
class Reader {
public:
    explicit Reader(const ImageData& image) : area_(areaOf(image)) {
        if (area_.empty()) return;
        cairo_surface_flush(image.surface.get());
        data_ = cairo_image_surface_get_data(image.surface.get());
        stride_ = cairo_image_surface_get_stride(image.surface.get());
    }
    // B, G, R, A in 0..1.
    std::array<float, 4> at(int x, int y) const {
        x -= area_.x;
        y -= area_.y;
        if (!data_ || x < 0 || y < 0 || x >= area_.width || y >= area_.height) return {0, 0, 0, 0};
        const uint8_t* p = data_ + static_cast<size_t>(y) * stride_ + static_cast<size_t>(x) * 4;
        return {p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f, p[3] / 255.0f};
    }
private:
    Area area_;
    const uint8_t* data_ = nullptr;
    int stride_ = 0;
};

// A new image over `area`, written pixel by pixel.
class Writer {
public:
    explicit Writer(Area area) : area_(area), surface_(imageSurface(CAIRO_FORMAT_ARGB32, area.width, area.height)) {
        if (!surface_) return;
        data_ = cairo_image_surface_get_data(surface_.get());
        stride_ = cairo_image_surface_get_stride(surface_.get());
    }
    bool valid() const { return data_ != nullptr; }
    void set(int x, int y, float b, float g, float r, float a) {
        uint8_t* p = data_ + static_cast<size_t>(y - area_.y) * stride_ + static_cast<size_t>(x - area_.x) * 4;
        const auto byte = [](float value) { return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255)); };
        p[0] = byte(b); p[1] = byte(g); p[2] = byte(r); p[3] = byte(a);
    }
    ImageData finish() {
        if (!surface_) return {};
        cairo_surface_mark_dirty(surface_.get());
        return {surface_, area_.x, area_.y};
    }
private:
    Area area_;
    Surface surface_;
    uint8_t* data_ = nullptr;
    int stride_ = 0;
};

// ---- Gaussian blur ------------------------------------------------------------------------

// Three box blurs whose sizes approximate a Gaussian of `sigma` (pixels).
std::array<int, 3> boxSizes(double sigma) {
    const double ideal = std::sqrt(4 * sigma * sigma + 1);
    int lower = static_cast<int>(std::floor(ideal));
    if (lower % 2 == 0) --lower;
    lower = std::max(lower, 1);
    const int upper = lower + 2;
    const int below = static_cast<int>(std::lround((12 * sigma * sigma - 3.0 * lower * lower - 12.0 * lower - 9) / (-4.0 * lower - 4)));
    return {0 < below ? lower : upper, 1 < below ? lower : upper, 2 < below ? lower : upper};
}

// One box pass along `count` pixels `step` bytes apart; outside them is the mirror image
// (hard border) or transparent.
void boxLine(uint8_t* pixels, int count, ptrdiff_t step, int radius, bool mirror, std::vector<uint8_t>& line) {
    line.resize(static_cast<size_t>(count) * 4);
    for (int i = 0; i < count; ++i) std::memcpy(&line[static_cast<size_t>(i) * 4], pixels + i * step, 4);
    const auto edge = [&](int i) {
        if (i >= 0 && i < count) return i;
        if (!mirror) return -1;
        i = i < 0 ? -i - 1 : 2 * count - i - 1;
        return std::clamp(i, 0, count - 1);
    };
    const int size = radius * 2 + 1;
    const uint64_t reciprocal = ((uint64_t{1} << 24) + size / 2) / size;
    std::array<uint32_t, 4> sum{};
    const auto add = [&](int i, int sign) {
        const int at = edge(i);
        if (at < 0) return;
        for (int c = 0; c < 4; ++c) sum[c] += static_cast<uint32_t>(sign * line[static_cast<size_t>(at) * 4 + c]);
    };
    for (int i = -radius; i <= radius; ++i) add(i, 1);
    for (int i = 0; i < count; ++i) {
        uint8_t* out = pixels + i * step;
        for (int c = 0; c < 4; ++c) out[c] = static_cast<uint8_t>((sum[c] * reciprocal + (1u << 23)) >> 24);
        add(i + radius + 1, 1);
        add(i - radius, -1);
    }
}

void boxBlur(cairo_surface_t* surface, double sigma, bool mirror) {
    cairo_surface_flush(surface);
    uint8_t* data = cairo_image_surface_get_data(surface);
    const int width = cairo_image_surface_get_width(surface), height = cairo_image_surface_get_height(surface);
    const int stride = cairo_image_surface_get_stride(surface);
    std::vector<uint8_t> line;
    for (const int size : boxSizes(sigma)) {
        const int radius = size / 2;
        if (!radius) continue;
        for (int y = 0; y < height; ++y) boxLine(data + static_cast<size_t>(y) * stride, width, 4, radius, mirror, line);
        for (int x = 0; x < width; ++x) boxLine(data + static_cast<size_t>(x) * 4, height, stride, radius, mirror, line);
    }
    cairo_surface_mark_dirty(surface);
}

// A soft border grows the image by three deviations (padded with transparency); a hard one
// keeps its size and mirrors its edges. Wide blurs run on a copy reduced by a power of two and
// are enlarged back, as Direct2D's balanced optimisation does.
ImageData gaussianBlur(const ImageData& input, double sigma, bool hard) {
    if (!input.surface || sigma < 0.3) return input;
    const Area in = areaOf(input);
    const int pad = hard ? 0 : static_cast<int>(std::ceil(sigma * 3));
    const Area out{in.x - pad, in.y - pad, in.width + 2 * pad, in.height + 2 * pad};
    int factor = 1;
    while (sigma / (factor * 2) >= 2 && factor < 16) factor *= 2;
    const int small_width = (out.width + factor - 1) / factor, small_height = (out.height + factor - 1) / factor;
    Surface work = imageSurface(CAIRO_FORMAT_ARGB32, small_width, small_height);
    Surface result = factor == 1 ? work : imageSurface(CAIRO_FORMAT_ARGB32, out.width, out.height);
    if (!work || !result) return {};
    cairo_t* context = cairo_create(work.get());
    cairo_scale(context, 1.0 / factor, 1.0 / factor);
    cairo_set_source_surface(context, input.surface.get(), pad, pad);
    cairo_pattern_set_filter(cairo_get_source(context), CAIRO_FILTER_GOOD);
    cairo_set_operator(context, CAIRO_OPERATOR_SOURCE);
    cairo_paint(context);
    cairo_destroy(context);
    boxBlur(work.get(), sigma / factor, hard);
    if (factor > 1) {
        context = cairo_create(result.get());
        cairo_scale(context, factor, factor);
        cairo_set_source_surface(context, work.get(), 0, 0);
        cairo_pattern_set_filter(cairo_get_source(context), CAIRO_FILTER_BILINEAR);
        cairo_pattern_set_extend(cairo_get_source(context), CAIRO_EXTEND_PAD);
        cairo_set_operator(context, CAIRO_OPERATOR_SOURCE);
        cairo_paint(context);
        cairo_destroy(context);
    }
    return {result, out.x, out.y};
}

// ---- Effect objects -----------------------------------------------------------------------

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

    ImageData render(double scale) {
        const uint64_t current = version();
        if (current != cached_version_ || scale != cached_scale_) {
            cached_ = compute(scale);
            cached_version_ = current;
            cached_scale_ = scale;
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
    ImageData input(size_t index, double scale) const {
        auto* source = index < inputs_.size() ? ImageSource::from(inputs_[index].Get()) : nullptr;
        return source ? source->render(scale) : ImageData{};
    }
    ImageData compute(double scale) const;
    ImageData colorMatrix(const ImageData& image) const;
    ImageData pointwise(const ImageData& destination, const ImageData& source) const;
    ImageData affine(const ImageData& image, double scale) const;
    ImageData specular(const ImageData& image, double scale) const;
    ImageData composite(const ImageData& destination, const ImageData& source) const;

    Kind kind_;
    std::vector<ComPtr<ID2D1Image>> inputs_;
    uint64_t version_ = newVersion();
    // Properties, with Direct2D's defaults.
    float deviation_ = 3;
    UINT32 optimization_ = D2D1_GAUSSIANBLUR_OPTIMIZATION_BALANCED; // Accepted; the blur always runs balanced.
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
    D2D1_VECTOR_2F kernel_{1, 1};
    UINT32 scale_mode_ = D2D1_POINTSPECULAR_SCALE_MODE_LINEAR; // Accepted; the height map is never rescaled.
    D2D1_COMPOSITE_MODE composite_mode_ = D2D1_COMPOSITE_MODE_SOURCE_OVER;

    uint64_t cached_version_ = 0;
    double cached_scale_ = 0;
    ImageData cached_;
};

// Its effect's result; holds the effect alive (SetInputEffect keeps the output only).
class EffectOutput final : public lm_com::Object<ID2D1Image>, public ImageSource {
public:
    explicit EffectOutput(ComPtr<Effect> effect) : effect_(std::move(effect)) {}
    ImageData render(double scale) override { return effect_->render(scale); }
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

ImageData Effect::compute(double scale) const {
    switch (kind_) {
    case Kind::blur: return gaussianBlur(input(0, scale), deviation_ * scale, border_ == D2D1_BORDER_MODE_HARD);
    case Kind::color_matrix:
    case Kind::saturation: return colorMatrix(input(0, scale));
    case Kind::blend:
    case Kind::arithmetic: return pointwise(input(0, scale), input(1, scale));
    case Kind::affine: return affine(input(0, scale), scale);
    case Kind::specular: return specular(input(0, scale), scale);
    case Kind::composite: return composite(input(0, scale), input(1, scale));
    }
    return {};
}

// Colour matrix: row vector (r, g, b, a, 1) times the 5x4 matrix. Premultiplied mode works on
// straight colour and premultiplies the result; straight mode applies the matrix to the stored
// values as they are. Saturation is the matrix of the SVG/Direct2D saturate operation.
ImageData Effect::colorMatrix(const ImageData& image) const {
    const Area area = areaOf(image);
    if (area.empty()) return {};
    D2D1_MATRIX_5X4_F m = matrix_;
    bool unpremultiply = alpha_mode_ == D2D1_COLORMATRIX_ALPHA_MODE_PREMULTIPLIED, clamp = clamp_;
    if (kind_ == Kind::saturation) {
        const float s = std::clamp(saturation_, 0.0f, 1.0f);
        m = {0.213f + 0.787f * s, 0.213f - 0.213f * s, 0.213f - 0.213f * s, 0,
             0.715f - 0.715f * s, 0.715f + 0.285f * s, 0.715f - 0.715f * s, 0,
             0.072f - 0.072f * s, 0.072f - 0.072f * s, 0.072f + 0.928f * s, 0,
             0, 0, 0, 1, 0, 0, 0, 0};
        unpremultiply = false; // Linear in colour with alpha untouched: the same on premultiplied values.
        clamp = true;
    }
    const Reader in(image);
    Writer out(area);
    if (!out.valid()) return {};
    for (int y = area.y; y < area.y + area.height; ++y)
        for (int x = area.x; x < area.x + area.width; ++x) {
            auto [b, g, r, a] = in.at(x, y);
            if (unpremultiply && a > 0) { r /= a; g /= a; b /= a; }
            float nr = r * m._11 + g * m._21 + b * m._31 + a * m._41 + m._51;
            float ng = r * m._12 + g * m._22 + b * m._32 + a * m._42 + m._52;
            float nb = r * m._13 + g * m._23 + b * m._33 + a * m._43 + m._53;
            float na = r * m._14 + g * m._24 + b * m._34 + a * m._44 + m._54;
            if (clamp) {
                nr = std::clamp(nr, 0.0f, 1.0f); ng = std::clamp(ng, 0.0f, 1.0f);
                nb = std::clamp(nb, 0.0f, 1.0f); na = std::clamp(na, 0.0f, 1.0f);
            }
            if (unpremultiply) {
                na = std::clamp(na, 0.0f, 1.0f);
                nr *= na; ng *= na; nb *= na;
            }
            out.set(x, y, nb, ng, nr, na);
        }
    return out.finish();
}

// Blend (input 0 the destination, 1 the source; separable modes on premultiplied colour) and
// arithmetic composite (C1 * source * destination + C2 * source + C3 * destination + C4).
ImageData Effect::pointwise(const ImageData& destination, const ImageData& source) const {
    const Area area = unite(areaOf(destination), areaOf(source));
    if (area.empty()) return {};
    const Reader under(destination), over(source);
    Writer out(area);
    if (!out.valid()) return {};
    const auto [k1, k2, k3, k4] = coefficients_;
    for (int y = area.y; y < area.y + area.height; ++y)
        for (int x = area.x; x < area.x + area.width; ++x) {
            const auto d = under.at(x, y), s = over.at(x, y);
            std::array<float, 4> result;
            if (kind_ == Kind::arithmetic) {
                for (int c = 0; c < 4; ++c) result[c] = k1 * s[c] * d[c] + k2 * s[c] + k3 * d[c] + k4;
            } else {
                const float as = s[3], ad = d[3];
                for (int c = 0; c < 3; ++c) {
                    float mixed; // as * ad * B(cd, cs), from premultiplied values.
                    switch (blend_) {
                    case D2D1_BLEND_MODE_MULTIPLY: mixed = s[c] * d[c]; break;
                    case D2D1_BLEND_MODE_SCREEN: mixed = s[c] * ad + d[c] * as - s[c] * d[c]; break;
                    case D2D1_BLEND_MODE_DARKEN: mixed = std::min(s[c] * ad, d[c] * as); break;
                    case D2D1_BLEND_MODE_LIGHTEN: mixed = std::max(s[c] * ad, d[c] * as); break;
                    default: mixed = s[c] * ad; break; // Other modes: the source over the destination.
                    }
                    result[c] = s[c] * (1 - ad) + d[c] * (1 - as) + mixed;
                }
                result[3] = as + ad - as * ad;
            }
            out.set(x, y, result[0], result[1], result[2], result[3]);
        }
    return out.finish();
}

// The image mapped by the matrix (DIPs). A hard border keeps the edge pixels to the edge of the
// mapped image; a soft one fades them out over a pixel.
ImageData Effect::affine(const ImageData& image, double scale) const {
    const Area in = areaOf(image);
    if (in.empty()) return {};
    cairo_matrix_t to_grid, from_grid;
    cairo_matrix_init_scale(&to_grid, scale, scale);
    cairo_matrix_init_scale(&from_grid, 1 / scale, 1 / scale);
    const cairo_matrix_t matrix = multiply(multiply(from_grid, toCairo(transform_)), to_grid);
    cairo_matrix_t check = matrix;
    if (cairo_matrix_invert(&check) != CAIRO_STATUS_SUCCESS) return {};
    double left = 1e300, top = 1e300, right = -1e300, bottom = -1e300;
    for (const auto& corner : {std::array<double, 2>{double(in.x), double(in.y)}, {double(in.x + in.width), double(in.y)},
                               {double(in.x + in.width), double(in.y + in.height)}, {double(in.x), double(in.y + in.height)}}) {
        double x = corner[0], y = corner[1];
        cairo_matrix_transform_point(&matrix, &x, &y);
        left = std::min(left, x); top = std::min(top, y); right = std::max(right, x); bottom = std::max(bottom, y);
    }
    const bool hard = border_ == D2D1_BORDER_MODE_HARD;
    const Area out = hard ? Area{static_cast<int>(std::lround(left)), static_cast<int>(std::lround(top)),
                                 static_cast<int>(std::lround(right) - std::lround(left)), static_cast<int>(std::lround(bottom) - std::lround(top))}
                          : Area{static_cast<int>(std::floor(left)) - 1, static_cast<int>(std::floor(top)) - 1,
                                 static_cast<int>(std::ceil(right) - std::floor(left)) + 2, static_cast<int>(std::ceil(bottom) - std::floor(top)) + 2};
    if (out.empty()) return {};
    Surface result = imageSurface(CAIRO_FORMAT_ARGB32, out.width, out.height);
    if (!result) return {};
    cairo_t* context = cairo_create(result.get());
    cairo_translate(context, -out.x, -out.y);
    cairo_transform(context, &matrix);
    cairo_set_source_surface(context, image.surface.get(), in.x, in.y);
    cairo_pattern_set_filter(cairo_get_source(context), filterFor(static_cast<D2D1_INTERPOLATION_MODE>(interpolation_)));
    cairo_pattern_set_extend(cairo_get_source(context), hard ? CAIRO_EXTEND_PAD : CAIRO_EXTEND_NONE);
    cairo_set_operator(context, CAIRO_OPERATOR_SOURCE);
    cairo_paint(context);
    cairo_destroy(context);
    return {result, out.x, out.y};
}

// The specular part of Phong lighting from a point light over the alpha channel as a height map
// (normals by Sobel gradient, the eye at +z). Colour = Ks * (N.H)^exponent * light colour,
// alpha = the brightest channel. Positions are DIPs of the image.
ImageData Effect::specular(const ImageData& image, double scale) const {
    const Area area = areaOf(image);
    if (area.empty()) return {};
    const Reader in(image);
    Writer out(area);
    if (!out.valid()) return {};
    std::vector<float> height(static_cast<size_t>(area.width) * area.height);
    for (int y = 0; y < area.height; ++y)
        for (int x = 0; x < area.width; ++x) height[static_cast<size_t>(y) * area.width + x] = in.at(area.x + x, area.y + y)[3];
    const auto h = [&](int x, int y) {
        return height[static_cast<size_t>(std::clamp(y, 0, area.height - 1)) * area.width + std::clamp(x, 0, area.width - 1)];
    };
    // Sobel over one grid pixel, as a slope per DIP times the kernel unit (DIPs).
    const float gx = static_cast<float>(-surface_scale_ * 0.25 * scale * kernel_.x), gy = static_cast<float>(-surface_scale_ * 0.25 * scale * kernel_.y);
    for (int y = 0; y < area.height; ++y)
        for (int x = 0; x < area.width; ++x) {
            const float nx = gx * (h(x + 1, y - 1) + 2 * h(x + 1, y) + h(x + 1, y + 1) - h(x - 1, y - 1) - 2 * h(x - 1, y) - h(x - 1, y + 1));
            const float ny = gy * (h(x - 1, y + 1) + 2 * h(x, y + 1) + h(x + 1, y + 1) - h(x - 1, y - 1) - 2 * h(x, y - 1) - h(x + 1, y - 1));
            const float normal = 1 / std::sqrt(nx * nx + ny * ny + 1);
            const float px = static_cast<float>((area.x + x + 0.5) / scale), py = static_cast<float>((area.y + y + 0.5) / scale);
            float lx = light_.x - px, ly = light_.y - py, lz = light_.z - surface_scale_ * h(x, y);
            const float length = std::sqrt(lx * lx + ly * ly + lz * lz);
            if (length > 0) { lx /= length; ly /= length; lz /= length; }
            float hx = lx, hy = ly, hz = lz + 1;
            const float half = std::sqrt(hx * hx + hy * hy + hz * hz);
            if (half > 0) { hx /= half; hy /= half; hz /= half; }
            const float facing = std::max(0.0f, (nx * hx + ny * hy + hz) * normal);
            const float shine = constant_ * std::pow(facing, exponent_);
            const float r = std::clamp(color_.x * shine, 0.0f, 1.0f), g = std::clamp(color_.y * shine, 0.0f, 1.0f),
                        b = std::clamp(color_.z * shine, 0.0f, 1.0f);
            out.set(area.x + x, area.y + y, b, g, r, std::max({r, g, b}));
        }
    return out.finish();
}

// Composite: input 1 laid on input 0 with the composite mode's Porter-Duff operator.
ImageData Effect::composite(const ImageData& destination, const ImageData& source) const {
    const Area area = unite(areaOf(destination), areaOf(source));
    if (area.empty()) return {};
    Surface result = imageSurface(CAIRO_FORMAT_ARGB32, area.width, area.height);
    if (!result) return {};
    cairo_t* context = cairo_create(result.get());
    cairo_translate(context, -area.x, -area.y);
    if (destination.surface) {
        cairo_set_source_surface(context, destination.surface.get(), destination.x, destination.y);
        cairo_set_operator(context, CAIRO_OPERATOR_SOURCE);
        cairo_paint(context);
    }
    if (source.surface) {
        cairo_set_source_surface(context, source.surface.get(), source.x, source.y);
        cairo_operator_t op = CAIRO_OPERATOR_OVER;
        switch (composite_mode_) {
        case D2D1_COMPOSITE_MODE_DESTINATION_OVER: op = CAIRO_OPERATOR_DEST_OVER; break;
        case D2D1_COMPOSITE_MODE_SOURCE_IN: op = CAIRO_OPERATOR_IN; break;
        case D2D1_COMPOSITE_MODE_DESTINATION_IN: op = CAIRO_OPERATOR_DEST_IN; break;
        case D2D1_COMPOSITE_MODE_SOURCE_OUT: op = CAIRO_OPERATOR_OUT; break;
        case D2D1_COMPOSITE_MODE_DESTINATION_OUT: op = CAIRO_OPERATOR_DEST_OUT; break;
        case D2D1_COMPOSITE_MODE_SOURCE_ATOP: op = CAIRO_OPERATOR_ATOP; break;
        case D2D1_COMPOSITE_MODE_DESTINATION_ATOP: op = CAIRO_OPERATOR_DEST_ATOP; break;
        case D2D1_COMPOSITE_MODE_XOR: op = CAIRO_OPERATOR_XOR; break;
        case D2D1_COMPOSITE_MODE_PLUS: op = CAIRO_OPERATOR_ADD; break;
        case D2D1_COMPOSITE_MODE_SOURCE_COPY:
        case D2D1_COMPOSITE_MODE_BOUNDED_SOURCE_COPY: op = CAIRO_OPERATOR_SOURCE; break;
        default: break;
        }
        cairo_set_operator(context, op);
        cairo_paint(context);
    }
    cairo_destroy(context);
    return {result, area.x, area.y};
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
