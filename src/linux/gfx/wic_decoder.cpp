// WIC's decoders on image_codec: the stream read whole (the caller's memory in place), the header
// parsed at once, the pixels decoded at the first CopyPixels and kept by the frame. A JPEG frame
// also decodes at 1/2, 1/4 or 1/8 of its size through libjpeg-turbo's DCT scaling.
#include "wic_internal.hpp"
#include "../win32/wrl/client.h"
#include "../../artwork/image_codec.hpp"

#include <turbojpeg.h>
#include <webp/decode.h>

#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <span>

using Microsoft::WRL::ComPtr;

namespace lm_wic {
namespace {
// The encoded image a decoder and its frames share.
struct Encoded {
    std::vector<uint8_t> owned;
    std::span<const uint8_t> bytes;  // `owned`, or the memory of `stream`.
    ComPtr<IStream> stream;
    image_codec::Info info;
    const Format* format = nullptr;  // As the file stores it.
};

uint32_t little(std::span<const uint8_t> bytes, size_t at, int size) {
    uint32_t value = 0;
    for (int i = 0; i < size; ++i) value |= static_cast<uint32_t>(bytes[at + i]) << (8 * i);
    return value;
}
uint32_t big(std::span<const uint8_t> bytes, size_t at) {
    return static_cast<uint32_t>(bytes[at]) << 24 | bytes[at + 1] << 16 | bytes[at + 2] << 8 | bytes[at + 3];
}

struct Turbo {
    tjhandle handle = tj3Init(TJINIT_DECOMPRESS);
    ~Turbo() { tj3Destroy(handle); }
};

// The format WIC reports for what the file stores; everything decodes to 8-bit channels here, so
// 16-bit PNGs report their 8-bit counterpart, palettes and CMYK their colour format.
const Format& storedFormat(std::span<const uint8_t> bytes, image_codec::Format container) {
    switch (container) {
    case image_codec::Format::png: {
        // The IHDR colour type, with a tRNS chunk before the image data meaning alpha.
        const uint8_t colour = bytes[25];
        bool alpha = colour == 4 || colour == 6;
        for (size_t at = 8; !alpha && at + 8 <= bytes.size();) {
            if (!std::memcmp(bytes.data() + at + 4, "IDAT", 4)) break;
            alpha = !std::memcmp(bytes.data() + at + 4, "tRNS", 4);
            at += 12 + size_t{big(bytes, at)};
        }
        return alpha ? format_bgra : colour == 0 ? format_gray : format_bgr;
    }
    case image_codec::Format::jpeg: {
        Turbo turbo;
        const bool gray = turbo.handle && tj3DecompressHeader(turbo.handle, bytes.data(), bytes.size()) == 0 &&
                          tj3Get(turbo.handle, TJPARAM_COLORSPACE) == TJCS_GRAY;
        return gray ? format_gray : format_bgr;
    }
    case image_codec::Format::webp: {
        WebPBitstreamFeatures features{};
        return WebPGetFeatures(bytes.data(), bytes.size(), &features) == VP8_STATUS_OK && features.has_alpha ? format_bgra : format_bgr;
    }
    default: {
        // BMP: 32 bits keep alpha only through an explicit alpha mask (BI_BITFIELDS, V4 header or later).
        const uint32_t header = little(bytes, 14, 4), bits = little(bytes, 28, 2), compression = little(bytes, 30, 4);
        if (bits != 32) return format_bgr;
        return compression == 3 && header >= 56 && bytes.size() >= 70 && little(bytes, 66, 4) ? format_bgra : format_bgrx;
    }
    }
}

// A JPEG decoded at 1/denominator of its size, as straight 32bppBGRA.
std::optional<Image> decodeReduced(std::span<const uint8_t> bytes, int denominator) {
    Turbo turbo;
    const tjscalingfactor factor{1, denominator};
    if (!turbo.handle || tj3DecompressHeader(turbo.handle, bytes.data(), bytes.size()) != 0 ||
        tj3SetScalingFactor(turbo.handle, factor) != 0) return {};
    const int width = TJSCALED(tj3Get(turbo.handle, TJPARAM_JPEGWIDTH), factor), height = TJSCALED(tj3Get(turbo.handle, TJPARAM_JPEGHEIGHT), factor);
    if (width <= 0 || height <= 0) return {};
    Image image = makeImage(format_bgra, static_cast<UINT>(width), static_cast<UINT>(height));
    const int colorspace = tj3Get(turbo.handle, TJPARAM_COLORSPACE);
    if (colorspace != TJCS_CMYK && colorspace != TJCS_YCCK)
        return tj3Decompress8(turbo.handle, bytes.data(), bytes.size(), image.bytes.data(), 0, TJPF_BGRA) == 0 ? std::optional(std::move(image)) : std::nullopt;
    // Inverted CMYK as Photoshop writes it, converted as image_codec does.
    if (tj3Decompress8(turbo.handle, bytes.data(), bytes.size(), image.bytes.data(), 0, TJPF_CMYK) != 0) return {};
    for (size_t i = 0; i < image.bytes.size(); i += 4) {
        BYTE* p = image.bytes.data() + i;
        const unsigned c = p[0], m = p[1], y = p[2], k = p[3];
        p[0] = static_cast<BYTE>((y * k + 127) / 255);
        p[1] = static_cast<BYTE>((m * k + 127) / 255);
        p[2] = static_cast<BYTE>((c * k + 127) / 255);
        p[3] = 255;
    }
    return image;
}

class Frame final : public lm_com::Object<IWICBitmapFrameDecode, IWICBitmapSourceTransform>, public Source {
public:
    explicit Frame(std::shared_ptr<const Encoded> encoded) : encoded_(std::move(encoded)) {}

    HRESULT STDMETHODCALLTYPE GetSize(UINT* width, UINT* height) override {
        if (!width || !height) return E_INVALIDARG;
        *width = encoded_->info.width; *height = encoded_->info.height;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPixelFormat(WICPixelFormatGUID* pixelFormat) override {
        if (!pixelFormat) return E_INVALIDARG;
        *pixelFormat = encoded_->format->guid;
        return S_OK;
    }
    // The files' own resolution is not read: 96 DPI, as WIC reports for images without one.
    HRESULT STDMETHODCALLTYPE GetResolution(double* dpiX, double* dpiY) override {
        if (!dpiX || !dpiY) return E_INVALIDARG;
        *dpiX = *dpiY = 96;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CopyPixels(const WICRect* rect, UINT stride, UINT bufferSize, BYTE* buffer) override {
        WICRect area;
        HRESULT result = checkCopy(rect, encoded_->info.width, encoded_->info.height, *encoded_->format, stride, bufferSize, buffer, area);
        View pixels;
        if (SUCCEEDED(result)) result = view(pixels);
        if (SUCCEEDED(result)) copyView(pixels, area, *encoded_->format, stride, buffer);
        return result;
    }

    HRESULT STDMETHODCALLTYPE CopyPixels(const WICRect* rect, UINT width, UINT height, WICPixelFormatGUID* dstFormat,
                                         WICBitmapTransformOptions dstTransform, UINT stride, UINT bufferSize, BYTE* buffer) override {
        if (dstTransform != WICBitmapTransformRotate0) return WINCODEC_ERR_UNSUPPORTEDOPERATION;
        const Format* to = dstFormat ? findFormat(*dstFormat) : encoded_->format;
        if (!to) return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
        const int denominator = reduction(width, height);
        if (!denominator) return E_INVALIDARG;
        WICRect area;
        if (const HRESULT result = checkCopy(rect, width, height, *to, stride, bufferSize, buffer, area); FAILED(result)) return result;
        if (denominator == 1) return copyConverted(this, area, *to, stride, buffer);
        return lm_com::guarded([&] {
            const auto reduced = decodeReduced(encoded_->bytes, denominator);
            if (!reduced || reduced->width != width || reduced->height != height) return WINCODEC_ERR_BADIMAGE;
            copyView(reduced->view(), area, *to, stride, buffer);
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE GetClosestSize(UINT* width, UINT* height) override {
        if (!width || !height) return E_INVALIDARG;
        const UINT full_width = encoded_->info.width, full_height = encoded_->info.height;
        UINT closest_width = full_width, closest_height = full_height;
        if (encoded_->info.format == image_codec::Format::jpeg)
            for (const UINT denominator : {8u, 4u, 2u}) {
                const UINT w = (full_width + denominator - 1) / denominator, h = (full_height + denominator - 1) / denominator;
                if (w >= *width && h >= *height) { closest_width = w; closest_height = h; break; }
            }
        *width = closest_width; *height = closest_height;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetClosestPixelFormat(WICPixelFormatGUID* dstFormat) override {
        if (!dstFormat) return E_INVALIDARG;
        if (!findFormat(*dstFormat)) *dstFormat = encoded_->format->guid;
        return S_OK;
    }

    HRESULT view(View& pixels) override {
        std::call_once(decoded_, [this] {
            status_ = lm_com::guarded([this] {
                auto decoded = image_codec::decode(encoded_->bytes);
                if (!decoded || decoded->width != encoded_->info.width || decoded->height != encoded_->info.height) return WINCODEC_ERR_BADIMAGE;
                image_ = {&format_bgra, decoded->width, decoded->height, size_t{decoded->width} * 4, std::move(decoded->pixels)};
                return S_OK;
            });
        });
        pixels = SUCCEEDED(status_) ? image_.view() : View{};
        return status_;
    }

private:
    // 1 for the full size, 2, 4 or 8 for a JPEG's native reductions; 0 for any other size.
    int reduction(UINT width, UINT height) const {
        const UINT full_width = encoded_->info.width, full_height = encoded_->info.height;
        if (width == full_width && height == full_height) return 1;
        if (encoded_->info.format != image_codec::Format::jpeg) return 0;
        for (const UINT denominator : {2u, 4u, 8u})
            if (width == (full_width + denominator - 1) / denominator && height == (full_height + denominator - 1) / denominator)
                return static_cast<int>(denominator);
        return 0;
    }

    std::shared_ptr<const Encoded> encoded_;
    std::once_flag decoded_;
    HRESULT status_ = S_OK;
    Image image_; // Straight 32bppBGRA, whatever the reported format.
};

class Decoder final : public lm_com::Object<IWICBitmapDecoder> {
public:
    explicit Decoder(std::shared_ptr<const Encoded> encoded) : encoded_(std::move(encoded)) {}
    HRESULT STDMETHODCALLTYPE GetContainerFormat(GUID* containerFormat) override {
        if (!containerFormat) return E_INVALIDARG;
        switch (encoded_->info.format) {
        case image_codec::Format::png: *containerFormat = GUID_ContainerFormatPng; break;
        case image_codec::Format::jpeg: *containerFormat = GUID_ContainerFormatJpeg; break;
        case image_codec::Format::webp: *containerFormat = GUID_ContainerFormatWebp; break;
        default: *containerFormat = GUID_ContainerFormatBmp; break;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFrameCount(UINT* count) override {
        if (!count) return E_INVALIDARG;
        *count = 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFrame(UINT index, IWICBitmapFrameDecode** frame) override {
        if (frame) *frame = nullptr;
        if (index) return WINCODEC_ERR_FRAMEMISSING;
        return lm_com::make<Frame>(frame, encoded_);
    }
private:
    std::shared_ptr<const Encoded> encoded_;
};

// The whole stream from its start.
HRESULT readAll(IStream* stream, std::vector<uint8_t>& bytes) {
    if (const HRESULT result = stream->Seek({}, STREAM_SEEK_SET, nullptr); FAILED(result)) return result;
    STATSTG statistics{};
    if (SUCCEEDED(stream->Stat(&statistics, STATFLAG_NONAME))) bytes.reserve(static_cast<size_t>(statistics.cbSize.QuadPart));
    for (;;) {
        constexpr ULONG chunk = 1 << 16;
        const size_t at = bytes.size();
        bytes.resize(at + chunk);
        ULONG read = 0;
        const HRESULT result = stream->Read(bytes.data() + at, chunk, &read);
        bytes.resize(at + read);
        if (FAILED(result)) return result;
        if (read < chunk) return S_OK;
    }
}
} // namespace

HRESULT createDecoder(IStream* stream, IWICBitmapDecoder** decoder) {
    if (!decoder) return E_INVALIDARG;
    *decoder = nullptr;
    if (!stream) return E_INVALIDARG;
    return lm_com::guarded([&] {
        auto encoded = std::make_shared<Encoded>();
        if (auto* own = dynamic_cast<lm_com::Stream*>(stream); own && !own->borrowed().empty()) {
            encoded->bytes = own->borrowed();
            encoded->stream = stream;
        } else {
            if (const HRESULT result = readAll(stream, encoded->owned); FAILED(result)) return result;
            encoded->bytes = encoded->owned;
        }
        const auto info = image_codec::probe(encoded->bytes);
        if (!info || !info->width || !info->height) return WINCODEC_ERR_COMPONENTNOTFOUND;
        encoded->info = *info;
        encoded->format = &storedFormat(encoded->bytes, info->format);
        return lm_com::make<Decoder>(decoder, std::shared_ptr<const Encoded>(std::move(encoded)));
    });
}
} // namespace lm_wic
