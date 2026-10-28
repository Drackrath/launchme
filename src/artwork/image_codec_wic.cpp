#include "image_codec.hpp"

#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <climits>

using Microsoft::WRL::ComPtr;

namespace image_codec {
namespace {
// A thread already in a single-threaded apartment keeps it (RPC_E_CHANGED_MODE); WIC works in both.
struct Com {
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~Com() { if (SUCCEEDED(result)) CoUninitialize(); }
};

ComPtr<IWICImagingFactory> factory() {
    ComPtr<IWICImagingFactory> result;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(result.GetAddressOf())))) return {};
    return result;
}

ComPtr<IWICBitmapDecoder> open(IWICImagingFactory* wic, std::span<const uint8_t> bytes, ComPtr<IWICStream>& stream) {
    ComPtr<IWICBitmapDecoder> decoder;
    if (bytes.empty() || bytes.size() > MAXDWORD || FAILED(wic->CreateStream(stream.GetAddressOf())) ||
        FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size()))) ||
        FAILED(wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf()))) return {};
    return decoder;
}

ComPtr<IWICBitmapDecoder> open(IWICImagingFactory* wic, const std::filesystem::path& file) {
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(wic->CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf()))) return {};
    return decoder;
}

std::optional<Info> info(IWICBitmapDecoder* decoder, ComPtr<IWICBitmapFrameDecode>& frame) {
    Info result;
    GUID container{};
    if (SUCCEEDED(decoder->GetContainerFormat(&container)))
        result.format = container == GUID_ContainerFormatPng ? Format::png : container == GUID_ContainerFormatJpeg ? Format::jpeg : Format::other;
    if (FAILED(decoder->GetFrame(0, frame.GetAddressOf())) || FAILED(frame->GetSize(&result.width, &result.height)) ||
        !result.width || !result.height) return {};
    return result;
}

std::optional<Image> pixels(IWICImagingFactory* wic, IWICBitmapDecoder* decoder, const Fit& fit, Filter filter, bool premultiplied) {
    if (!decoder) return {};
    ComPtr<IWICBitmapFrameDecode> frame;
    const auto stored = info(decoder, frame);
    std::optional<Size> size;
    if (stored) size = fit(*stored);
    if (!size || !size->width || !size->height) return {};
    ComPtr<IWICBitmapSource> source = frame;
    if (size->width != stored->width || size->height != stored->height) {
        ComPtr<IWICBitmapScaler> scaler;
        if (FAILED(wic->CreateBitmapScaler(scaler.GetAddressOf())) ||
            FAILED(scaler->Initialize(frame.Get(), size->width, size->height,
                                      filter == Filter::cubic ? WICBitmapInterpolationModeHighQualityCubic : WICBitmapInterpolationModeFant))) return {};
        source = scaler;
    }
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(wic->CreateFormatConverter(converter.GetAddressOf())) ||
        FAILED(converter->Initialize(source.Get(), premultiplied ? GUID_WICPixelFormat32bppPBGRA : GUID_WICPixelFormat32bppBGRA,
                                     WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return {};
    Image image{stored->format, size->width, size->height, std::vector<uint8_t>(size_t(size->width) * size->height * 4)};
    if (image.pixels.size() > UINT_MAX ||
        FAILED(converter->CopyPixels(nullptr, image.width * 4, static_cast<UINT>(image.pixels.size()), image.pixels.data()))) return {};
    return image;
}

std::vector<uint8_t> encode(const Image& image, bool jpeg, float quality) {
    if (!image.width || !image.height || image.pixels.size() != size_t(image.width) * image.height * 4 || image.pixels.size() > UINT_MAX) return {};
    Com com;
    const auto wic = factory();
    ComPtr<IWICBitmap> bitmap;
    if (!wic || FAILED(wic->CreateBitmapFromMemory(image.width, image.height, GUID_WICPixelFormat32bppBGRA, image.width * 4,
                                                   static_cast<UINT>(image.pixels.size()), const_cast<BYTE*>(image.pixels.data()), &bitmap))) return {};
    ComPtr<IStream> output;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &output))) return {};
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> target;
    ComPtr<IPropertyBag2> options;
    if (!output || FAILED(wic->CreateEncoder(jpeg ? GUID_ContainerFormatJpeg : GUID_ContainerFormatPng, nullptr, &encoder)) ||
        FAILED(encoder->Initialize(output.Get(), WICBitmapEncoderNoCache)) || FAILED(encoder->CreateNewFrame(&target, &options))) return {};
    if (jpeg) {
        PROPBAG2 option{}; option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
        VARIANT value{}; value.vt = VT_R4; value.fltVal = quality;
        options->Write(1, &option, &value);
    }
    if (FAILED(target->Initialize(options.Get())) || FAILED(target->SetSize(image.width, image.height))) return {};
    ComPtr<IWICBitmapSource> written = bitmap;
    if (jpeg) { // The JPEG encoder takes 24-bit BGR.
        ComPtr<IWICFormatConverter> opaque;
        if (FAILED(wic->CreateFormatConverter(&opaque)) ||
            FAILED(opaque->Initialize(bitmap.Get(), GUID_WICPixelFormat24bppBGR, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return {};
        written = opaque;
    }
    if (FAILED(target->WriteSource(written.Get(), nullptr)) || FAILED(target->Commit()) || FAILED(encoder->Commit())) return {};
    STATSTG stat{};
    if (FAILED(output->Stat(&stat, STATFLAG_NONAME)) || !stat.cbSize.QuadPart || stat.cbSize.QuadPart > ULONG_MAX) return {};
    std::vector<uint8_t> result(static_cast<size_t>(stat.cbSize.QuadPart));
    LARGE_INTEGER start{};
    ULONG read = 0;
    if (FAILED(output->Seek(start, STREAM_SEEK_SET, nullptr)) || FAILED(output->Read(result.data(), static_cast<ULONG>(result.size()), &read)) ||
        read != result.size()) return {};
    return result;
}
} // namespace

std::optional<Info> probe(std::span<const uint8_t> bytes) {
    Com com;
    const auto wic = factory();
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    if (wic) decoder = open(wic.Get(), bytes, stream);
    ComPtr<IWICBitmapFrameDecode> frame;
    if (!decoder) return {};
    return info(decoder.Get(), frame);
}

std::optional<Info> probeFile(const std::filesystem::path& file) {
    Com com;
    const auto wic = factory();
    ComPtr<IWICBitmapDecoder> decoder;
    if (wic) decoder = open(wic.Get(), file);
    ComPtr<IWICBitmapFrameDecode> frame;
    if (!decoder) return {};
    return info(decoder.Get(), frame);
}

std::optional<Image> decode(std::span<const uint8_t> bytes, const Fit& fit, Filter filter, bool premultiplied) {
    Com com;
    const auto wic = factory();
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    if (wic) decoder = open(wic.Get(), bytes, stream);
    return pixels(wic.Get(), decoder.Get(), fit, filter, premultiplied);
}

std::optional<Image> decodeFile(const std::filesystem::path& file, const Fit& fit, Filter filter, bool premultiplied) {
    Com com;
    const auto wic = factory();
    ComPtr<IWICBitmapDecoder> decoder;
    if (wic) decoder = open(wic.Get(), file);
    return pixels(wic.Get(), decoder.Get(), fit, filter, premultiplied);
}

std::vector<uint8_t> encodePng(const Image& image) { return encode(image, false, 0); }
std::vector<uint8_t> encodeJpeg(const Image& image, float quality) { return encode(image, true, quality); }
} // namespace image_codec
