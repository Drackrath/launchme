// WIC's bitmap sources over other sources: format converters (converting each CopyPixels' rows),
// scalers (reading and resampling the source once, then copying from the result) and clippers.
#include "wic_internal.hpp"
#include "../win32/wrl/client.h"

#include <mutex>

using Microsoft::WRL::ComPtr;

namespace lm_wic {
namespace {
// What a source over another one answers before and after Initialize.
class Over : public Source {
protected:
    ~Over() = default;
    // Initialize's checks of `source`: none taken yet, a format this port has; its size.
    HRESULT check(IWICBitmapSource* source, UINT& width, UINT& height) const {
        if (source_) return WINCODEC_ERR_WRONGSTATE;
        if (!source) return E_INVALIDARG;
        if (!formatOf(source)) return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
        return source->GetSize(&width, &height);
    }
    void attach(IWICBitmapSource* source, const Format& format, UINT width, UINT height) {
        source_ = source; format_ = &format; width_ = width; height_ = height;
    }
    HRESULT reportSize(UINT* width, UINT* height) const {
        if (!width || !height) return E_INVALIDARG;
        if (!source_) return WINCODEC_ERR_NOTINITIALIZED;
        *width = width_; *height = height_;
        return S_OK;
    }
    HRESULT reportFormat(WICPixelFormatGUID* pixelFormat) const {
        if (!pixelFormat) return E_INVALIDARG;
        if (!source_) return WINCODEC_ERR_NOTINITIALIZED;
        *pixelFormat = format_->guid;
        return S_OK;
    }
    HRESULT reportResolution(double* dpiX, double* dpiY) const {
        if (!dpiX || !dpiY) return E_INVALIDARG;
        return source_ ? source_->GetResolution(dpiX, dpiY) : WINCODEC_ERR_NOTINITIALIZED;
    }
    // The source's own view, when it has one.
    HRESULT sourceView(View& pixels) const {
        pixels = {};
        if (!source_) return WINCODEC_ERR_NOTINITIALIZED;
        auto* own = dynamic_cast<Source*>(source_.Get());
        return own ? own->view(pixels) : S_OK;
    }

    ComPtr<IWICBitmapSource> source_;
    const Format* format_ = nullptr; // Of the output.
    UINT width_ = 0, height_ = 0;
};

class Converter final : public lm_com::Object<IWICFormatConverter>, public Over {
public:
    HRESULT STDMETHODCALLTYPE Initialize(IWICBitmapSource* source, REFWICPixelFormatGUID dstFormat, WICBitmapDitherType, IWICPalette*,
                                         double, WICBitmapPaletteType) override {
        UINT width = 0, height = 0;
        if (const HRESULT result = check(source, width, height); FAILED(result)) return result;
        const Format* to = findFormat(dstFormat);
        if (!to) return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
        attach(source, *to, width, height);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSize(UINT* width, UINT* height) override { return reportSize(width, height); }
    HRESULT STDMETHODCALLTYPE GetPixelFormat(WICPixelFormatGUID* pixelFormat) override { return reportFormat(pixelFormat); }
    HRESULT STDMETHODCALLTYPE GetResolution(double* dpiX, double* dpiY) override { return reportResolution(dpiX, dpiY); }
    HRESULT STDMETHODCALLTYPE CopyPixels(const WICRect* rect, UINT stride, UINT bufferSize, BYTE* buffer) override {
        if (!source_) return WINCODEC_ERR_NOTINITIALIZED;
        WICRect area;
        const HRESULT result = checkCopy(rect, width_, height_, *format_, stride, bufferSize, buffer, area);
        return FAILED(result) ? result : copyConverted(source_.Get(), area, *format_, stride, buffer);
    }
    // The source's pixels when they are already in the format asked for.
    HRESULT view(View& pixels) override {
        const HRESULT result = sourceView(pixels);
        if (pixels.format != format_) pixels = {};
        return result;
    }
};

class Scaler final : public lm_com::Object<IWICBitmapScaler>, public Over {
public:
    HRESULT STDMETHODCALLTYPE Initialize(IWICBitmapSource* source, UINT width, UINT height, WICBitmapInterpolationMode mode) override {
        UINT source_width = 0, source_height = 0;
        if (const HRESULT result = check(source, source_width, source_height); FAILED(result)) return result;
        if (!width || !height || mode > WICBitmapInterpolationModeHighQualityCubic) return E_INVALIDARG;
        attach(source, *formatOf(source), width, height);
        mode_ = mode;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSize(UINT* width, UINT* height) override { return reportSize(width, height); }
    HRESULT STDMETHODCALLTYPE GetPixelFormat(WICPixelFormatGUID* pixelFormat) override { return reportFormat(pixelFormat); }
    HRESULT STDMETHODCALLTYPE GetResolution(double* dpiX, double* dpiY) override { return reportResolution(dpiX, dpiY); }
    HRESULT STDMETHODCALLTYPE CopyPixels(const WICRect* rect, UINT stride, UINT bufferSize, BYTE* buffer) override {
        if (!source_) return WINCODEC_ERR_NOTINITIALIZED;
        WICRect area;
        HRESULT result = checkCopy(rect, width_, height_, *format_, stride, bufferSize, buffer, area);
        View pixels;
        if (SUCCEEDED(result)) result = view(pixels);
        if (SUCCEEDED(result)) copyView(pixels, area, *format_, stride, buffer);
        return result;
    }
    // The scaled image, made at the first call: premultiplied for a premultiplied format, else straight.
    HRESULT view(View& pixels) override {
        pixels = {};
        if (!source_) return WINCODEC_ERR_NOTINITIALIZED;
        std::call_once(scaled_, [this] {
            status_ = lm_com::guarded([this] {
                Image copy;
                View input;
                const HRESULT result = viewOf(source_.Get(), format_->premultiplied ? format_pbgra : format_bgra, copy, input);
                if (SUCCEEDED(result)) image_ = scale(input, width_, height_, mode_, format_->premultiplied);
                return result;
            });
        });
        if (SUCCEEDED(status_)) pixels = image_.view();
        return status_;
    }
private:
    WICBitmapInterpolationMode mode_ = WICBitmapInterpolationModeNearestNeighbor;
    std::once_flag scaled_;
    HRESULT status_ = S_OK;
    Image image_;
};

class Clipper final : public lm_com::Object<IWICBitmapClipper>, public Over {
public:
    HRESULT STDMETHODCALLTYPE Initialize(IWICBitmapSource* source, const WICRect* rect) override {
        UINT width = 0, height = 0;
        if (const HRESULT result = check(source, width, height); FAILED(result)) return result;
        if (!rect || rect->X < 0 || rect->Y < 0 || rect->Width <= 0 || rect->Height <= 0 || uint64_t(rect->X) + rect->Width > width ||
            uint64_t(rect->Y) + rect->Height > height) return E_INVALIDARG;
        attach(source, *formatOf(source), static_cast<UINT>(rect->Width), static_cast<UINT>(rect->Height));
        rect_ = *rect;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSize(UINT* width, UINT* height) override { return reportSize(width, height); }
    HRESULT STDMETHODCALLTYPE GetPixelFormat(WICPixelFormatGUID* pixelFormat) override { return reportFormat(pixelFormat); }
    HRESULT STDMETHODCALLTYPE GetResolution(double* dpiX, double* dpiY) override { return reportResolution(dpiX, dpiY); }
    HRESULT STDMETHODCALLTYPE CopyPixels(const WICRect* rect, UINT stride, UINT bufferSize, BYTE* buffer) override {
        if (!source_) return WINCODEC_ERR_NOTINITIALIZED;
        WICRect area;
        const HRESULT result = checkCopy(rect, width_, height_, *format_, stride, bufferSize, buffer, area);
        if (FAILED(result) || !area.Width || !area.Height) return result;
        area.X += rect_.X; area.Y += rect_.Y;
        return source_->CopyPixels(&area, stride, bufferSize, buffer);
    }
    // The source's view, narrowed to the rectangle.
    HRESULT view(View& pixels) override {
        const HRESULT result = sourceView(pixels);
        if (pixels.data) {
            pixels.data += pixels.stride * static_cast<size_t>(rect_.Y) + pixels.format->rowBytes(static_cast<UINT>(rect_.X));
            pixels.width = width_; pixels.height = height_;
        }
        return result;
    }
private:
    WICRect rect_{};
};
} // namespace

HRESULT createConverter(IWICFormatConverter** converter) { return lm_com::make<Converter>(converter); }
HRESULT createScaler(IWICBitmapScaler** scaler) { return lm_com::make<Scaler>(scaler); }
HRESULT createClipper(IWICBitmapClipper** clipper) { return lm_com::make<Clipper>(clipper); }
} // namespace lm_wic
