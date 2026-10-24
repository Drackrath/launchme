#pragma once
#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <vector>
#include <cstdint>

inline std::vector<uint8_t> discFixture(bool optical = true) {
    using Microsoft::WRL::ComPtr;
    constexpr UINT size = 256;
    std::vector<uint8_t> pixels(size * size * 4);
    for (UINT y = 0; y < size; ++y) for (UINT x = 0; x < size; ++x) {
        const int dx = static_cast<int>(x) - size / 2, dy = static_cast<int>(y) - size / 2;
        const int distance = dx * dx + dy * dy;
        auto* p = pixels.data() + (y * size + x) * 4;
        p[0] = static_cast<uint8_t>(x); p[1] = static_cast<uint8_t>(y); p[2] = 210;
        p[3] = !optical || (distance > 18 * 18 && distance < 124 * 124) ? 255 : 0;
    }
    ComPtr<IWICImagingFactory> wic; ComPtr<IWICBitmap> bitmap; ComPtr<IStream> stream;
    ComPtr<IWICBitmapEncoder> encoder; ComPtr<IWICBitmapFrameEncode> frame;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.GetAddressOf()))) ||
        FAILED(wic->CreateBitmapFromMemory(size, size, GUID_WICPixelFormat32bppBGRA, size * 4, static_cast<UINT>(pixels.size()), pixels.data(), bitmap.GetAddressOf())) ||
        FAILED(CreateStreamOnHGlobal(nullptr, TRUE, stream.GetAddressOf())) || FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf())) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) || FAILED(encoder->CreateNewFrame(frame.GetAddressOf(), nullptr)) ||
        FAILED(frame->Initialize(nullptr)) || FAILED(frame->SetSize(size, size)) || FAILED(frame->WriteSource(bitmap.Get(), nullptr)) ||
        FAILED(frame->Commit()) || FAILED(encoder->Commit())) return {};
    STATSTG stat{}; if (FAILED(stream->Stat(&stat, STATFLAG_NONAME))) return {};
    std::vector<uint8_t> bytes(static_cast<size_t>(stat.cbSize.QuadPart));
    LARGE_INTEGER start{}; ULONG read = 0;
    if (FAILED(stream->Seek(start, STREAM_SEEK_SET, nullptr)) || FAILED(stream->Read(bytes.data(), static_cast<ULONG>(bytes.size()), &read)) || read != bytes.size()) return {};
    return bytes;
}
