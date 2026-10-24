#pragma once
#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <optional>
#include <span>
#include <vector>
#include <cstdint>

namespace image_validation {
struct Info {
    UINT width = 0, height = 0;
    bool png = false;
    size_t transparent = 0, opaque = 0;
    size_t pixels() const { return static_cast<size_t>(width) * height; }
    bool hasTransparency() const { return transparent > 0 && opaque > 0 && transparent * 100 >= pixels(); }
};
// Call on a COM-initialized worker. Decode pixels, rather than trusting the URL,
// extension, or a PNG container whose actual alpha channel is fully opaque.
inline std::optional<Info> inspect(std::span<const uint8_t> bytes) {
    if (bytes.empty() || bytes.size() > 12 * 1024 * 1024) return {};
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory; ComPtr<IWICStream> stream; ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame; ComPtr<IWICFormatConverter> converter;
    Info info; GUID format{};
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf()))) ||
        FAILED(factory->CreateStream(stream.GetAddressOf())) ||
        FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size()))) ||
        FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf())) ||
        FAILED(decoder->GetContainerFormat(&format)) || FAILED(decoder->GetFrame(0, frame.GetAddressOf())) ||
        FAILED(frame->GetSize(&info.width, &info.height)) || !info.width || !info.height || info.width > 4096 || info.height > 4096 ||
        FAILED(factory->CreateFormatConverter(converter.GetAddressOf())) ||
        FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return {};
    std::vector<uint8_t> pixels(info.pixels() * 4);
    if (FAILED(converter->CopyPixels(nullptr, info.width * 4, static_cast<UINT>(pixels.size()), pixels.data()))) return {};
    info.png = format == GUID_ContainerFormatPng;
    for (size_t i = 3; i < pixels.size(); i += 4) { if (!pixels[i]) ++info.transparent; if (pixels[i] == 255) ++info.opaque; }
    return info;
}
}
