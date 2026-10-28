// WIC's PNG and JPEG encoders on image_codec: a frame gathers its rows in the format it agreed on,
// and at Commit becomes straight BGRA, is encoded and goes to the encoder's stream.
#include "wic_internal.hpp"
#include "../win32/wrl/client.h"
#include "../../artwork/image_codec.hpp"

#include <algorithm>
#include <cstring>
#include <cwchar>

using Microsoft::WRL::ComPtr;

namespace lm_wic {
namespace {
// Encoder options: JPEG's "ImageQuality"; PNG has none here.
class Options final : public lm_com::Object<IPropertyBag2> {
public:
    explicit Options(bool jpeg) : jpeg_(jpeg) {}
    HRESULT STDMETHODCALLTYPE Write(ULONG count, PROPBAG2* properties, VARIANT* values) override {
        if (count && (!properties || !values)) return E_INVALIDARG;
        for (ULONG i = 0; i < count; ++i) {
            if (!jpeg_ || !properties[i].pstrName || std::wcscmp(properties[i].pstrName, L"ImageQuality") != 0)
                return WINCODEC_ERR_PROPERTYNOTSUPPORTED;
            if (values[i].vt != VT_R4) return WINCODEC_ERR_PROPERTYUNEXPECTEDTYPE;
            if (!(values[i].fltVal >= 0.0f && values[i].fltVal <= 1.0f)) return E_INVALIDARG;
            quality = values[i].fltVal;
        }
        return S_OK;
    }
    float quality = 0.9f; // WIC's default.
private:
    bool jpeg_;
};

class Encoder final : public lm_com::Object<IWICBitmapEncoder> {
public:
    explicit Encoder(bool jpeg) : jpeg_(jpeg) {}
    HRESULT STDMETHODCALLTYPE Initialize(IStream* stream, WICBitmapEncoderCacheOption) override {
        if (stream_) return WINCODEC_ERR_WRONGSTATE;
        if (!stream) return E_INVALIDARG;
        stream_ = stream;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateNewFrame(IWICBitmapFrameEncode** frame, IPropertyBag2** encoderOptions) override;
    HRESULT STDMETHODCALLTYPE Commit() override {
        if (!stream_) return WINCODEC_ERR_NOTINITIALIZED;
        if (committed_) return WINCODEC_ERR_WRONGSTATE;
        if (!written_) return WINCODEC_ERR_FRAMEMISSING;
        committed_ = true;
        return stream_->Commit(STGC_DEFAULT);
    }

    bool jpeg() const { return jpeg_; }
    // A committed frame's file.
    HRESULT write(const std::vector<uint8_t>& bytes) {
        for (size_t at = 0; at < bytes.size();) {
            const auto part = static_cast<ULONG>(std::min<size_t>(bytes.size() - at, 1u << 30));
            ULONG written = 0;
            if (const HRESULT result = stream_->Write(bytes.data() + at, part, &written); FAILED(result)) return result;
            if (!written) return WINCODEC_ERR_STREAMWRITE;
            at += written;
        }
        written_ = true;
        return S_OK;
    }

private:
    bool jpeg_, framed_ = false, written_ = false, committed_ = false;
    ComPtr<IStream> stream_;
};

class FrameEncode final : public lm_com::Object<IWICBitmapFrameEncode> {
public:
    explicit FrameEncode(ComPtr<Encoder> encoder) : encoder_(std::move(encoder)) {}

    HRESULT STDMETHODCALLTYPE Initialize(IPropertyBag2* encoderOptions) override {
        if (initialized_) return WINCODEC_ERR_WRONGSTATE;
        if (auto* options = dynamic_cast<Options*>(encoderOptions)) quality_ = options->quality;
        initialized_ = true;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetSize(UINT width, UINT height) override {
        if (!initialized_) return WINCODEC_ERR_NOTINITIALIZED;
        if (lines_ || committed_) return WINCODEC_ERR_WRONGSTATE;
        if (!width || !height) return E_INVALIDARG;
        width_ = width; height_ = height;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPixelFormat(WICPixelFormatGUID* pixelFormat) override {
        if (!pixelFormat) return E_INVALIDARG;
        if (!initialized_) return WINCODEC_ERR_NOTINITIALIZED;
        if (lines_ || committed_) return WINCODEC_ERR_WRONGSTATE;
        format_ = &closest(findFormat(*pixelFormat));
        *pixelFormat = format_->guid;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE WritePixels(UINT lineCount, UINT stride, UINT bufferSize, BYTE* pixels) override {
        if (!initialized_) return WINCODEC_ERR_NOTINITIALIZED;
        if (!width_ || !format_ || committed_) return WINCODEC_ERR_WRONGSTATE;
        if (!lineCount) return S_OK;
        const size_t row = format_->rowBytes(width_);
        if (!pixels || stride < row) return E_INVALIDARG;
        if (size_t{stride} * (lineCount - 1) + row > bufferSize) return WINCODEC_ERR_INSUFFICIENTBUFFER;
        if (uint64_t{lines_} + lineCount > height_) return WINCODEC_ERR_CODECTOOMANYSCANLINES;
        return lm_com::guarded([&] {
            prepare();
            for (UINT y = 0; y < lineCount; ++y)
                std::memcpy(pixels_.bytes.data() + (size_t{lines_} + y) * pixels_.stride, pixels + size_t{y} * stride, row);
            lines_ += lineCount;
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE WriteSource(IWICBitmapSource* source, WICRect* rect) override {
        if (!initialized_) return WINCODEC_ERR_NOTINITIALIZED;
        if (committed_) return WINCODEC_ERR_WRONGSTATE;
        if (!source) return E_INVALIDARG;
        UINT width = 0, height = 0;
        if (const HRESULT result = source->GetSize(&width, &height); FAILED(result)) return result;
        const WICRect area = rect ? *rect : WICRect{0, 0, static_cast<INT>(width), static_cast<INT>(height)};
        if (area.X < 0 || area.Y < 0 || area.Width <= 0 || area.Height <= 0 || uint64_t(area.X) + area.Width > width ||
            uint64_t(area.Y) + area.Height > height) return E_INVALIDARG;
        if (!width_) { width_ = static_cast<UINT>(area.Width); height_ = static_cast<UINT>(area.Height); }
        if (!format_) format_ = &closest(formatOf(source));
        // Whole rows of the frame, appended as WritePixels does.
        if (static_cast<UINT>(area.Width) != width_) return E_INVALIDARG;
        if (uint64_t{lines_} + area.Height > height_) return WINCODEC_ERR_CODECTOOMANYSCANLINES;
        return lm_com::guarded([&] {
            prepare();
            const HRESULT result = copyConverted(source, area, *format_, static_cast<UINT>(pixels_.stride),
                                                 pixels_.bytes.data() + size_t{lines_} * pixels_.stride);
            if (SUCCEEDED(result)) lines_ += static_cast<UINT>(area.Height);
            return result;
        });
    }
    HRESULT STDMETHODCALLTYPE Commit() override {
        if (!initialized_) return WINCODEC_ERR_NOTINITIALIZED;
        if (committed_ || !height_ || lines_ != height_) return WINCODEC_ERR_WRONGSTATE;
        return lm_com::guarded([&] {
            image_codec::Image image{encoder_->jpeg() ? image_codec::Format::jpeg : image_codec::Format::png, width_, height_, {}};
            image.pixels.resize(size_t{width_} * height_ * 4);
            for (UINT y = 0; y < height_; ++y)
                convertRow(*format_, pixels_.bytes.data() + y * pixels_.stride, format_bgra, image.pixels.data() + size_t{y} * width_ * 4, width_);
            pixels_ = {};
            const auto bytes = encoder_->jpeg() ? image_codec::encodeJpeg(image, quality_) : image_codec::encodePng(image);
            if (bytes.empty()) return WINCODEC_ERR_INTERNALERROR;
            const HRESULT result = encoder_->write(bytes);
            if (SUCCEEDED(result)) committed_ = true;
            return result;
        });
    }

private:
    // What the container stores closest to `wanted` (null: the container's default).
    const Format& closest(const Format* wanted) const {
        if (wanted && wanted->layout == Layout::gray) return format_gray;
        if (encoder_->jpeg() || (wanted && !wanted->alpha)) return format_bgr;
        return format_bgra;
    }
    void prepare() {
        if (pixels_.bytes.empty()) pixels_ = makeImage(*format_, width_, height_);
    }

    ComPtr<Encoder> encoder_;
    bool initialized_ = false, committed_ = false;
    float quality_ = 0.9f;
    UINT width_ = 0, height_ = 0, lines_ = 0;
    const Format* format_ = nullptr;
    Image pixels_;
};

HRESULT STDMETHODCALLTYPE Encoder::CreateNewFrame(IWICBitmapFrameEncode** frame, IPropertyBag2** encoderOptions) {
    if (encoderOptions) *encoderOptions = nullptr;
    if (!frame) return E_INVALIDARG;
    *frame = nullptr;
    if (!stream_) return WINCODEC_ERR_NOTINITIALIZED;
    if (framed_ || committed_) return WINCODEC_ERR_WRONGSTATE; // PNG and JPEG hold one frame.
    if (encoderOptions) {
        if (const HRESULT result = lm_com::make<Options>(encoderOptions, jpeg_); FAILED(result)) return result;
    }
    const HRESULT result = lm_com::make<FrameEncode>(frame, ComPtr<Encoder>(this));
    if (FAILED(result) && encoderOptions) { (*encoderOptions)->Release(); *encoderOptions = nullptr; }
    if (SUCCEEDED(result)) framed_ = true;
    return result;
}
} // namespace

HRESULT createEncoder(bool jpeg, IWICBitmapEncoder** encoder) { return lm_com::make<Encoder>(encoder, jpeg); }
} // namespace lm_wic
