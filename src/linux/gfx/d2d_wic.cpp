// Direct2D and WIC (wic_*.cpp): bitmaps from WIC bitmap sources and render targets drawing into a
// WIC bitmap, through the standard WIC methods only (GetSize, GetPixelFormat, CopyPixels, Lock).
#include "d2d_internal.hpp"
#include "../win32/wincodec.h"

#include <cstring>
#include <vector>

namespace lm_d2d {
namespace {
// The alpha mode of a 32-bit BGRA WIC format; unknown for any other.
D2D1_ALPHA_MODE alphaOf(const WICPixelFormatGUID& format) {
    if (format == GUID_WICPixelFormat32bppPBGRA) return D2D1_ALPHA_MODE_PREMULTIPLIED;
    if (format == GUID_WICPixelFormat32bppBGRA) return D2D1_ALPHA_MODE_STRAIGHT;
    return D2D1_ALPHA_MODE_UNKNOWN;
}

// A render target on its own surface, which starts as the WIC bitmap's pixels and is written
// back into it (Lock) at EndDraw.
class WicTarget final : public RenderTarget<ID2D1DeviceContext1> {
public:
    WicTarget(ComPtr<IWICBitmap> bitmap, float dpi)
        : RenderTarget({DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED}, dpi), bitmap_(std::move(bitmap)) {}
    HRESULT STDMETHODCALLTYPE EndDraw(D2D1_TAG* tag1, D2D1_TAG* tag2) override {
        const HRESULT result = RenderTarget::EndDraw(tag1, tag2);
        cairo_surface_t* pixels = surface();
        const int width = cairo_image_surface_get_width(pixels), height = cairo_image_surface_get_height(pixels);
        if (!width || !height) return result;
        const WICRect all{0, 0, width, height};
        ComPtr<IWICBitmapLock> lock;
        UINT stride = 0, size = 0;
        BYTE* data = nullptr;
        if (FAILED(bitmap_->Lock(&all, WICBitmapLockWrite, lock.GetAddressOf())) || FAILED(lock->GetStride(&stride)) ||
            FAILED(lock->GetDataPointer(&size, &data)) || !data || size < static_cast<UINT>(stride) * (height - 1) + width * 4u)
            return D2DERR_RECREATE_TARGET;
        const uint8_t* source = cairo_image_surface_get_data(pixels);
        const int source_stride = cairo_image_surface_get_stride(pixels);
        for (int y = 0; y < height; ++y)
            std::memcpy(data + static_cast<size_t>(y) * stride, source + static_cast<size_t>(y) * source_stride, static_cast<size_t>(width) * 4);
        return result;
    }
private:
    ComPtr<IWICBitmap> bitmap_;
};
} // namespace

HRESULT bitmapFromWic(IWICBitmapSource* source, const D2D1_BITMAP_PROPERTIES* properties, ID2D1Bitmap** bitmap) {
    if (!bitmap) return E_POINTER;
    *bitmap = nullptr;
    if (!source) return E_INVALIDARG;
    UINT width = 0, height = 0;
    WICPixelFormatGUID format{};
    HRESULT result = source->GetSize(&width, &height);
    if (SUCCEEDED(result)) result = source->GetPixelFormat(&format);
    if (FAILED(result)) return result;
    const D2D1_ALPHA_MODE alpha = alphaOf(format);
    if (alpha == D2D1_ALPHA_MODE_UNKNOWN) return D2DERR_UNSUPPORTED_PIXEL_FORMAT;
    std::vector<BYTE> pixels(static_cast<size_t>(width) * height * 4);
    result = source->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data());
    if (FAILED(result)) return result;
    ComPtr<Bitmap> created;
    result = Bitmap::create({width, height}, pixels.data(), width * 4, {DXGI_FORMAT_B8G8R8A8_UNORM, alpha},
                            properties ? properties->dpiX : 96, properties ? properties->dpiY : 96, created);
    if (SUCCEEDED(result)) *bitmap = created.Detach();
    return result;
}

HRESULT createWicTarget(IWICBitmap* target, const D2D1_RENDER_TARGET_PROPERTIES& properties, ID2D1RenderTarget** renderTarget) {
    if (!renderTarget) return E_POINTER;
    *renderTarget = nullptr;
    if (!target) return E_INVALIDARG;
    UINT width = 0, height = 0;
    WICPixelFormatGUID format{};
    HRESULT result = target->GetSize(&width, &height);
    if (SUCCEEDED(result)) result = target->GetPixelFormat(&format);
    if (FAILED(result)) return result;
    if (format != GUID_WICPixelFormat32bppPBGRA) return D2DERR_UNSUPPORTED_PIXEL_FORMAT;
    Surface pixels = imageSurface(CAIRO_FORMAT_ARGB32, static_cast<int>(width), static_cast<int>(height));
    if (!pixels) return E_OUTOFMEMORY;
    result = target->CopyPixels(nullptr, static_cast<UINT>(cairo_image_surface_get_stride(pixels.get())),
                                static_cast<UINT>(cairo_image_surface_get_stride(pixels.get())) * height, cairo_image_surface_get_data(pixels.get()));
    if (FAILED(result)) return result;
    cairo_surface_mark_dirty(pixels.get());
    ComPtr<WicTarget> created;
    created.Attach(new WicTarget(ComPtr<IWICBitmap>(target), properties.dpiX));
    result = created->attach(std::move(pixels));
    if (SUCCEEDED(result)) *renderTarget = created.Detach();
    return result;
}
} // namespace lm_d2d
