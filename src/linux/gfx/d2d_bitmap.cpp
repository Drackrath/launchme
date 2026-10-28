// Direct2D bitmaps on Cairo image surfaces: premultiplied ARGB32 (B8G8R8A8 in memory on
// little-endian hosts), or A8 for alpha-only bitmaps. Straight, ignored-alpha and R8G8B8A8
// pixels are converted on upload; block-compressed formats are refused.
#include "d2d_internal.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>

namespace lm_d2d {
namespace {
bool supported(DXGI_FORMAT format) {
    return format == DXGI_FORMAT_B8G8R8A8_UNORM || format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || format == DXGI_FORMAT_B8G8R8X8_UNORM ||
           format == DXGI_FORMAT_R8G8B8A8_UNORM || format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || format == DXGI_FORMAT_A8_UNORM;
}

// One row of `format` pixels as premultiplied B8G8R8A8.
void convertRow(const uint8_t* source, uint8_t* target, int width, D2D1_PIXEL_FORMAT format) {
    const bool swap = format.format == DXGI_FORMAT_R8G8B8A8_UNORM || format.format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    const bool opaque = format.alphaMode == D2D1_ALPHA_MODE_IGNORE || format.format == DXGI_FORMAT_B8G8R8X8_UNORM;
    const bool straight = format.alphaMode == D2D1_ALPHA_MODE_STRAIGHT;
    if (!swap && !opaque && !straight) { std::memcpy(target, source, static_cast<size_t>(width) * 4); return; }
    for (int x = 0; x < width; ++x, source += 4, target += 4) {
        uint8_t b = source[0], g = source[1], r = source[2];
        if (swap) std::swap(b, r);
        const uint8_t a = opaque ? 255 : source[3];
        if (straight && a != 255) {
            b = static_cast<uint8_t>((b * a + 127) / 255);
            g = static_cast<uint8_t>((g * a + 127) / 255);
            r = static_cast<uint8_t>((r * a + 127) / 255);
        }
        target[0] = b; target[1] = g; target[2] = r; target[3] = a;
    }
}
} // namespace

Surface imageSurface(cairo_format_t format, int width, int height) {
    Surface surface(cairo_image_surface_create(format, std::max(width, 0), std::max(height, 0)));
    if (cairo_surface_status(surface.get()) != CAIRO_STATUS_SUCCESS) return {};
    return surface;
}

uint64_t newVersion() {
    static std::atomic<uint64_t> counter{0};
    return ++counter;
}

D2D1_PIXEL_FORMAT resolveFormat(D2D1_PIXEL_FORMAT format) {
    if (format.format == DXGI_FORMAT_UNKNOWN) format.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    if (format.alphaMode == D2D1_ALPHA_MODE_UNKNOWN)
        format.alphaMode = format.format == DXGI_FORMAT_B8G8R8X8_UNORM ? D2D1_ALPHA_MODE_IGNORE : D2D1_ALPHA_MODE_PREMULTIPLIED;
    return format;
}

// Linear is bilinear, as Direct2D samples (Cairo's GOOD box-filters every reduction on the CPU,
// several times slower); the cubic modes keep GOOD's smoother reduction.
cairo_filter_t filterFor(D2D1_INTERPOLATION_MODE mode) {
    switch (mode) {
    case D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR: return CAIRO_FILTER_NEAREST;
    case D2D1_INTERPOLATION_MODE_CUBIC:
    case D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC:
    case D2D1_INTERPOLATION_MODE_ANISOTROPIC: return CAIRO_FILTER_GOOD;
    default: return CAIRO_FILTER_BILINEAR;
    }
}

Bitmap::Bitmap(Surface surface, int width, int height, D2D1_PIXEL_FORMAT format, float dpiX, float dpiY)
    : surface_(std::move(surface)), width_(width), height_(height), format_(format), dpi_x_(dpiX > 0 ? dpiX : 96),
      dpi_y_(dpiY > 0 ? dpiY : 96), version_(newVersion()) {}

HRESULT Bitmap::create(D2D1_SIZE_U size, const void* data, UINT32 pitch, D2D1_PIXEL_FORMAT format, float dpiX, float dpiY,
                       ComPtr<Bitmap>& out) {
    format = resolveFormat(format);
    if (!supported(format.format)) return D2DERR_UNSUPPORTED_PIXEL_FORMAT;
    if (size.width > 32767 || size.height > 32767) return E_INVALIDARG; // Cairo's limit.
    const int width = static_cast<int>(size.width), height = static_cast<int>(size.height);
    Surface surface = imageSurface(format.format == DXGI_FORMAT_A8_UNORM ? CAIRO_FORMAT_A8 : CAIRO_FORMAT_ARGB32, width, height);
    if (!surface) return E_OUTOFMEMORY;
    out.Attach(new Bitmap(std::move(surface), width, height, format, dpiX, dpiY));
    return data ? out->CopyFromMemory(nullptr, data, pitch) : S_OK;
}

Bitmap* Bitmap::from(ID2D1Image* image) { return dynamic_cast<Bitmap*>(image); }
ImageSource* ImageSource::from(ID2D1Image* image) { return dynamic_cast<ImageSource*>(image); }

D2D1_SIZE_F STDMETHODCALLTYPE Bitmap::GetSize() const {
    return {static_cast<float>(width_ / scaleX()), static_cast<float>(height_ / scaleY())};
}
D2D1_SIZE_U STDMETHODCALLTYPE Bitmap::GetPixelSize() const { return {static_cast<UINT32>(width_), static_cast<UINT32>(height_)}; }

HRESULT STDMETHODCALLTYPE Bitmap::CopyFromMemory(const D2D1_RECT_U* rect, const void* data, UINT32 pitch) {
    if (!data) return E_INVALIDARG;
    const D2D1_RECT_U area = rect ? *rect : D2D1_RECT_U{0, 0, static_cast<UINT32>(width_), static_cast<UINT32>(height_)};
    if (area.right > static_cast<UINT32>(width_) || area.bottom > static_cast<UINT32>(height_) || area.left > area.right || area.top > area.bottom)
        return E_INVALIDARG;
    cairo_surface_flush(surface_.get());
    uint8_t* pixels = cairo_image_surface_get_data(surface_.get());
    const int stride = cairo_image_surface_get_stride(surface_.get());
    const int columns = static_cast<int>(area.right - area.left);
    const auto* source = static_cast<const uint8_t*>(data);
    for (UINT32 y = area.top; y < area.bottom; ++y, source += pitch) {
        uint8_t* row = pixels + static_cast<size_t>(y) * stride;
        if (alphaOnly()) std::memcpy(row + area.left, source, static_cast<size_t>(columns));
        else convertRow(source, row + static_cast<size_t>(area.left) * 4, columns, format_);
    }
    cairo_surface_mark_dirty(surface_.get());
    touch();
    return S_OK;
}

HRESULT Bitmap::copyFrom(cairo_surface_t* source, const D2D1_POINT_2U* destPoint, const D2D1_RECT_U* srcRect) {
    if (!source || source == surface_.get()) return E_INVALIDARG;
    cairo_surface_flush(source);
    const int source_width = cairo_image_surface_get_width(source), source_height = cairo_image_surface_get_height(source);
    const D2D1_RECT_U area = srcRect ? *srcRect : D2D1_RECT_U{0, 0, static_cast<UINT32>(source_width), static_cast<UINT32>(source_height)};
    const D2D1_POINT_2U at = destPoint ? *destPoint : D2D1_POINT_2U{0, 0};
    if (area.left > area.right || area.top > area.bottom || area.right > static_cast<UINT32>(source_width) ||
        area.bottom > static_cast<UINT32>(source_height))
        return E_INVALIDARG;
    cairo_t* context = cairo_create(surface_.get());
    cairo_set_operator(context, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_surface(context, source, static_cast<double>(at.x) - area.left, static_cast<double>(at.y) - area.top);
    cairo_rectangle(context, at.x, at.y, area.right - area.left, area.bottom - area.top);
    cairo_fill(context);
    cairo_destroy(context);
    touch();
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Bitmap::CopyFromBitmap(const D2D1_POINT_2U* destPoint, ID2D1Bitmap* bitmap, const D2D1_RECT_U* srcRect) {
    auto* source = Bitmap::from(bitmap);
    return source ? copyFrom(source->surface(), destPoint, srcRect) : E_INVALIDARG;
}

HRESULT STDMETHODCALLTYPE Bitmap::CopyFromRenderTarget(const D2D1_POINT_2U* destPoint, ID2D1RenderTarget* renderTarget,
                                                       const D2D1_RECT_U* srcRect) {
    auto* canvas = Canvas::from(renderTarget);
    return canvas ? copyFrom(canvas->surface(), destPoint, srcRect) : E_INVALIDARG;
}

Surface Bitmap::scaled(int width, int height) {
    width = std::max(width, 1);
    height = std::max(height, 1);
    if (scaled_ && scaled_version_ == version_ && cairo_image_surface_get_width(scaled_.get()) == width &&
        cairo_image_surface_get_height(scaled_.get()) == height)
        return scaled_;
    Surface result = imageSurface(CAIRO_FORMAT_ARGB32, width, height);
    if (!result) return {};
    cairo_t* context = cairo_create(result.get());
    cairo_scale(context, static_cast<double>(width) / std::max(width_, 1), static_cast<double>(height) / std::max(height_, 1));
    cairo_set_source_surface(context, surface_.get(), 0, 0);
    cairo_pattern_set_filter(cairo_get_source(context), CAIRO_FILTER_GOOD);
    cairo_pattern_set_extend(cairo_get_source(context), CAIRO_EXTEND_PAD);
    cairo_set_operator(context, CAIRO_OPERATOR_SOURCE);
    cairo_paint(context);
    cairo_destroy(context);
    scaled_ = result;
    scaled_version_ = version_;
    return result;
}

ImageData Bitmap::render(double scale) {
    const int width = static_cast<int>(std::lround(width_ / scaleX() * scale)), height = static_cast<int>(std::lround(height_ / scaleY() * scale));
    if (width == width_ && height == height_ && !alphaOnly()) return {surface_, 0, 0};
    return {scaled(width, height), 0, 0};
}
} // namespace lm_d2d
