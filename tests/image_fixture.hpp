#pragma once
#include <Windows.h>
#ifdef _WIN32
#include <wincodec.h>
#include <wrl/client.h>
#else
// The fixtures through image_codec (libpng, libjpeg-turbo). COM exists only on Windows: the
// tests' apartment calls do nothing here.
#include "../src/artwork/image_codec.hpp"
using INT = int;
using HRESULT = long;
struct WICRect { INT X, Y, Width, Height; };
enum { COINIT_MULTITHREADED = 0, COINIT_APARTMENTTHREADED = 2 };
inline HRESULT CoInitializeEx(void*, DWORD) { return 0; }
inline void CoUninitialize() {}
#define SUCCEEDED(result) (static_cast<HRESULT>(result) >= 0)
#define FAILED(result) (static_cast<HRESULT>(result) < 0)
#endif
#include <algorithm>
#include <vector>
#include <cstdint>
#include <optional>

inline std::vector<uint8_t> pngFixture(uint8_t tint = 40, UINT width = 32, UINT height = 32, bool transparent = false,
                                     std::optional<WICRect> ink = {}) {
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
    for (size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i] = static_cast<uint8_t>((i / 4) % 32 * 7);
        pixels[i + 1] = tint;
        pixels[i + 2] = static_cast<uint8_t>((i / 128) * 7);
        const auto x = static_cast<INT>((i / 4) % width), y = static_cast<INT>((i / 4) / width);
        pixels[i + 3] = ink ? (x >= ink->X && x < ink->X + ink->Width && y >= ink->Y && y < ink->Y + ink->Height ? 255 : 0) :
            transparent && (i / 4) % width < width / 4 ? 0 : 255;
    }
#ifndef _WIN32
    auto result = image_codec::encodePng({image_codec::Format::png, width, height, std::move(pixels)});
    // libpng packs small fixtures tighter than WIC: stay above ScreenScraper's 128-byte minimum.
    if (!result.empty() && result.size() <= 128) result.resize(256, 0);
#else
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmap> bitmap;
    ComPtr<IStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf()))) ||
        FAILED(factory->CreateBitmapFromMemory(width, height, format, width * 4, static_cast<UINT>(pixels.size()), pixels.data(), bitmap.GetAddressOf())) ||
        FAILED(CreateStreamOnHGlobal(nullptr, TRUE, stream.GetAddressOf())) ||
        FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf())) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(frame.GetAddressOf(), nullptr)) || FAILED(frame->Initialize(nullptr)) ||
        FAILED(frame->SetSize(width, height)) || FAILED(frame->SetPixelFormat(&format)) ||
        FAILED(frame->WriteSource(bitmap.Get(), nullptr)) || FAILED(frame->Commit()) || FAILED(encoder->Commit())) return {};
    STATSTG stats{};
    HGLOBAL memory = nullptr;
    if (FAILED(stream->Stat(&stats, STATFLAG_NONAME)) || FAILED(GetHGlobalFromStream(stream.Get(), &memory))) return {};
    const auto* data = static_cast<const uint8_t*>(GlobalLock(memory));
    if (!data) return {};
    std::vector<uint8_t> result(data, data + stats.cbSize.QuadPart);
    GlobalUnlock(memory);
#endif
    if (result.size() < 128) result.resize(128, 0);
    return result;
}

// A photographic-sized JPEG (gradients, shapes and grain, ~100–200 KB at 600×900) for benchmarks;
// `seed` varies the picture so equal sizes still decode as distinct images.
inline std::vector<uint8_t> jpegFixture(UINT width, UINT height, uint32_t seed, float quality = 0.9f) {
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 3);
    uint32_t state = seed * 2654435761u + 12345u;
    const auto next = [&] { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; };
    const float hue = static_cast<float>(seed % 7) / 7.0f;
    for (UINT y = 0; y < height; ++y)
        for (UINT x = 0; x < width; ++x) {
            const float u = static_cast<float>(x) / width, v = static_cast<float>(y) / height;
            const bool block = ((x / 48 + y / 64 + seed) % 5) == 0;
            const int grain = static_cast<int>(next() % 41) - 20;
            const auto channel = [&](float base) { return static_cast<uint8_t>(std::clamp(static_cast<int>(base * 255) + grain, 0, 255)); };
            auto* p = &pixels[(static_cast<size_t>(y) * width + x) * 3];
            p[0] = channel(block ? 0.85f - v * 0.4f : 0.2f + 0.5f * u * hue);
            p[1] = channel(0.25f + 0.5f * v * (1 - hue));
            p[2] = channel(block ? 0.3f : 0.15f + 0.6f * (1 - u) * v);
        }
#ifndef _WIN32
    std::vector<uint8_t> bgra(static_cast<size_t>(width) * height * 4, 255);
    for (size_t i = 0, j = 0; i < pixels.size(); i += 3, j += 4) std::copy_n(&pixels[i], 3, &bgra[j]);
    return image_codec::encodeJpeg({image_codec::Format::jpeg, width, height, std::move(bgra)}, quality);
#else
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmap> bitmap;
    ComPtr<IStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> options;
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf()))) ||
        FAILED(factory->CreateBitmapFromMemory(width, height, format, width * 3, static_cast<UINT>(pixels.size()), pixels.data(), bitmap.GetAddressOf())) ||
        FAILED(CreateStreamOnHGlobal(nullptr, TRUE, stream.GetAddressOf())) ||
        FAILED(factory->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, encoder.GetAddressOf())) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(frame.GetAddressOf(), options.GetAddressOf()))) return {};
    PROPBAG2 option{}; option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
    VARIANT value; VariantInit(&value); value.vt = VT_R4; value.fltVal = quality;
    if (options) options->Write(1, &option, &value);
    if (FAILED(frame->Initialize(options.Get())) || FAILED(frame->SetSize(width, height)) || FAILED(frame->SetPixelFormat(&format)) ||
        FAILED(frame->WriteSource(bitmap.Get(), nullptr)) || FAILED(frame->Commit()) || FAILED(encoder->Commit())) return {};
    STATSTG stats{};
    HGLOBAL memory = nullptr;
    if (FAILED(stream->Stat(&stats, STATFLAG_NONAME)) || FAILED(GetHGlobalFromStream(stream.Get(), &memory))) return {};
    const auto* data = static_cast<const uint8_t*>(GlobalLock(memory));
    if (!data) return {};
    std::vector<uint8_t> result(data, data + stats.cbSize.QuadPart);
    GlobalUnlock(memory);
    return result;
#endif
}
