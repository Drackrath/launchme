// WIC's in-memory bitmaps and their locks: what Direct2D's WIC render targets draw into
// (d2d_wic.cpp) and what decoded or generated pixels are wrapped in.
#include "wic_internal.hpp"
#include "../win32/wrl/client.h"

#include <climits>
#include <cstring>
#include <mutex>

using Microsoft::WRL::ComPtr;

namespace lm_wic {
namespace {
class Bitmap final : public lm_com::Object<IWICBitmap>, public Source {
public:
    explicit Bitmap(Image image) : image_(std::move(image)) {}

    HRESULT STDMETHODCALLTYPE GetSize(UINT* width, UINT* height) override {
        if (!width || !height) return E_INVALIDARG;
        *width = image_.width; *height = image_.height;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPixelFormat(WICPixelFormatGUID* pixelFormat) override {
        if (!pixelFormat) return E_INVALIDARG;
        *pixelFormat = image_.format->guid;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetResolution(double* dpiX, double* dpiY) override {
        if (!dpiX || !dpiY) return E_INVALIDARG;
        *dpiX = *dpiY = 96;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CopyPixels(const WICRect* rect, UINT stride, UINT bufferSize, BYTE* buffer) override {
        WICRect area;
        const HRESULT result = checkCopy(rect, image_.width, image_.height, *image_.format, stride, bufferSize, buffer, area);
        if (SUCCEEDED(result)) copyView(image_.view(), area, *image_.format, stride, buffer);
        return result;
    }
    HRESULT STDMETHODCALLTYPE Lock(const WICRect* rect, DWORD flags, IWICBitmapLock** lock) override;

    HRESULT view(View& pixels) override { pixels = image_.view(); return S_OK; }

    // Lock bookkeeping: any number of readers or one writer.
    bool acquire(bool write) {
        std::lock_guard guard(mutex_);
        if (writing_ || (write && readers_)) return false;
        if (write) writing_ = true; else ++readers_;
        return true;
    }
    void release(bool write) {
        std::lock_guard guard(mutex_);
        if (write) writing_ = false; else --readers_;
    }
    const Image& image() const { return image_; }
    BYTE* at(const WICRect& area) { return image_.bytes.data() + size_t(area.Y) * image_.stride + image_.format->rowBytes(UINT(area.X)); }

private:
    Image image_;
    std::mutex mutex_;
    unsigned readers_ = 0;
    bool writing_ = false;
};

class BitmapLock final : public lm_com::Object<IWICBitmapLock> {
public:
    BitmapLock(ComPtr<Bitmap> bitmap, const WICRect& area, bool write) : bitmap_(std::move(bitmap)), area_(area), write_(write) {}
    ~BitmapLock() override { bitmap_->release(write_); }

    HRESULT STDMETHODCALLTYPE GetSize(UINT* width, UINT* height) override {
        if (!width || !height) return E_INVALIDARG;
        *width = static_cast<UINT>(area_.Width); *height = static_cast<UINT>(area_.Height);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetStride(UINT* stride) override {
        if (!stride) return E_INVALIDARG;
        *stride = static_cast<UINT>(bitmap_->image().stride);
        return S_OK;
    }
    // From the rectangle's first pixel to its last.
    HRESULT STDMETHODCALLTYPE GetDataPointer(UINT* bufferSize, WICInProcPointer* data) override {
        if (!bufferSize || !data) return E_INVALIDARG;
        const Image& image = bitmap_->image();
        *bufferSize = static_cast<UINT>(image.stride * (area_.Height - 1) + image.format->rowBytes(static_cast<UINT>(area_.Width)));
        *data = bitmap_->at(area_);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPixelFormat(WICPixelFormatGUID* pixelFormat) override {
        if (!pixelFormat) return E_INVALIDARG;
        *pixelFormat = bitmap_->image().format->guid;
        return S_OK;
    }

private:
    ComPtr<Bitmap> bitmap_;
    WICRect area_;
    bool write_;
};

HRESULT STDMETHODCALLTYPE Bitmap::Lock(const WICRect* rect, DWORD flags, IWICBitmapLock** lock) {
    if (!lock) return E_INVALIDARG;
    *lock = nullptr;
    const bool write = flags & WICBitmapLockWrite;
    if (!(flags & (WICBitmapLockRead | WICBitmapLockWrite))) return E_INVALIDARG;
    const WICRect area = rect ? *rect : WICRect{0, 0, static_cast<INT>(image_.width), static_cast<INT>(image_.height)};
    if (area.X < 0 || area.Y < 0 || area.Width <= 0 || area.Height <= 0 || uint64_t(area.X) + area.Width > image_.width ||
        uint64_t(area.Y) + area.Height > image_.height) return E_INVALIDARG;
    if (!acquire(write)) return WINCODEC_ERR_ALREADYLOCKED;
    const HRESULT result = lm_com::make<BitmapLock>(lock, ComPtr<Bitmap>(this), area, write);
    if (FAILED(result)) release(write);
    return result;
}
} // namespace

HRESULT createBitmap(UINT width, UINT height, const Format& format, const BYTE* pixels, UINT stride, UINT size, IWICBitmap** bitmap) {
    if (!bitmap) return E_INVALIDARG;
    *bitmap = nullptr;
    if (!width || !height) return E_INVALIDARG;
    // GetDataPointer reports sizes in a UINT.
    if (((format.rowBytes(width) + 3) & ~size_t{3}) * height > UINT_MAX) return WINCODEC_ERR_IMAGESIZEOUTOFRANGE;
    const size_t row = format.rowBytes(width);
    if (pixels && stride < row) return E_INVALIDARG;
    if (pixels && size_t{stride} * (height - 1) + row > size) return WINCODEC_ERR_INSUFFICIENTBUFFER;
    return lm_com::guarded([&] {
        Image image = makeImage(format, width, height);
        if (pixels)
            for (UINT y = 0; y < height; ++y) std::memcpy(image.bytes.data() + y * image.stride, pixels + size_t{y} * stride, row);
        return lm_com::make<Bitmap>(bitmap, std::move(image));
    });
}
} // namespace lm_wic
