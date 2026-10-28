// Direct2D and WIC (wic_*.cpp): bitmaps from WIC bitmap sources and render targets drawing into a
// WIC bitmap, through the standard WIC methods only (GetSize, GetPixelFormat, CopyPixels, Lock).
// WIC targets draw in memory (they serve worker threads and captures).
#include "d2d_internal.hpp"
#include "../win32/wincodec.h"

#include "include/core/SkPixmap.h"

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
    WicTarget(ComPtr<IWICBitmap> bitmap, float dpi, sk_sp<SkSurface> surface, int width, int height)
        : RenderTarget({DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED}, dpi), bitmap_(std::move(bitmap)) {
        attach(std::move(surface), width, height);
    }
    HRESULT STDMETHODCALLTYPE EndDraw(D2D1_TAG* tag1, D2D1_TAG* tag2) override {
        const HRESULT result = RenderTarget::EndDraw(tag1, tag2);
        SkSurface* pixels = surface();
        const D2D1_SIZE_U size = pixelSize();
        if (!pixels || !size.width || !size.height) return result;
        const WICRect all{0, 0, static_cast<INT>(size.width), static_cast<INT>(size.height)};
        ComPtr<IWICBitmapLock> lock;
        UINT stride = 0, bytes = 0;
        BYTE* data = nullptr;
        if (FAILED(bitmap_->Lock(&all, WICBitmapLockWrite, lock.GetAddressOf())) || FAILED(lock->GetStride(&stride)) ||
            FAILED(lock->GetDataPointer(&bytes, &data)) || !data || bytes < stride * (size.height - 1) + size.width * 4u)
            return D2DERR_RECREATE_TARGET;
        if (!pixels->readPixels(bgraInfo(static_cast<int>(size.width), static_cast<int>(size.height)), data, stride, 0, 0))
            return D2DERR_RECREATE_TARGET;
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
    const SkImageInfo info = bgraInfo(static_cast<int>(width), static_cast<int>(height));
    sk_sp<SkSurface> pixels = SkSurfaces::Raster(info);
    if (!pixels) return E_OUTOFMEMORY;
    SkPixmap memory;
    if (!pixels->peekPixels(&memory)) return E_FAIL;
    result = target->CopyPixels(nullptr, static_cast<UINT>(memory.rowBytes()), static_cast<UINT>(memory.computeByteSize()),
                                static_cast<BYTE*>(memory.writable_addr()));
    if (FAILED(result)) return result;
    pixels->notifyContentWillChange(SkSurface::kRetain_ContentChangeMode);
    ComPtr<WicTarget> created;
    created.Attach(new WicTarget(ComPtr<IWICBitmap>(target), properties.dpiX, std::move(pixels), static_cast<int>(width), static_cast<int>(height)));
    *renderTarget = created.Detach();
    return S_OK;
}
} // namespace lm_d2d
