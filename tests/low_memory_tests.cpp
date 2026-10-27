// Low memory mode (AppSettings::low_memory): BC1 block compression of decoded artwork, the
// artwork cache's budget dropping everything, and this process's memory accounting.
// LAUNCHME_BC1_SAMPLE=<image> LAUNCHME_BC1_OUT=<png>: the sample as the GPU would sample it, to look at.
#include "../src/artwork/block_compression.hpp"
#include "../src/model/lru_cache.hpp"
#include "../src/platform/memory_usage.hpp"

#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {
double psnr(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    double error = 0; size_t count = 0;
    for (size_t i = 0; i < a.size(); i += 4)
        for (int c = 0; c < 3; ++c) { const double d = double(a[i + c]) - b[i + c]; error += d * d; ++count; }
    return error ? 10 * std::log10(255.0 * 255.0 / (error / count)) : 99;
}
// A photo-like picture: two gradients, a few hard edges and noise.
std::vector<uint8_t> picture(uint32_t width, uint32_t height) {
    std::vector<uint8_t> bgra(size_t{width} * height * 4);
    uint32_t seed = 12345;
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x) {
            seed = seed * 1664525 + 1013904223;
            const int noise = static_cast<int>((seed >> 24) % 9) - 4;
            uint8_t* p = bgra.data() + (size_t{y} * width + x) * 4;
            const bool block = (x / 37 + y / 23) % 5 == 0;
            p[2] = static_cast<uint8_t>(std::clamp(static_cast<int>(x * 255 / width) + noise, 0, 255));
            p[1] = static_cast<uint8_t>(std::clamp(static_cast<int>(y * 255 / height) + noise, 0, 255));
            p[0] = static_cast<uint8_t>(block ? 230 : 40 + (x ^ y) % 60);
            p[3] = 255;
        }
    return bgra;
}
struct Blob { size_t size = 0; };
struct BlobBytes { size_t operator()(const Blob& blob) const { return blob.size; } };

int sample(const wchar_t* in, const wchar_t* out) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ComPtr<IWICImagingFactory> wic;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.GetAddressOf()))) ||
        FAILED(wic->CreateDecoderFromFilename(in, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf())) ||
        FAILED(decoder->GetFrame(0, frame.GetAddressOf())) || FAILED(wic->CreateFormatConverter(converter.GetAddressOf())) ||
        FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return 200;
    UINT width = 0, height = 0; converter->GetSize(&width, &height);
    width -= width % 4; height -= height % 4;
    std::vector<uint8_t> pixels(size_t{width} * height * 4);
    const WICRect all{0, 0, static_cast<INT>(width), static_cast<INT>(height)};
    if (FAILED(converter->CopyPixels(&all, width * 4, static_cast<UINT>(pixels.size()), pixels.data()))) return 201;
    LARGE_INTEGER frequency{}, started{}, ended{};
    QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&started);
    std::vector<uint8_t> blocks;
    for (int run = 0; run < 5; ++run) blocks = block_compression::encodeBc1(pixels.data(), width, height);
    QueryPerformanceCounter(&ended);
    const double spent = (ended.QuadPart - started.QuadPart) * 1000.0 / frequency.QuadPart / 5;
    if (blocks.empty()) { std::cout << "sample has transparency: kept as BGRA\n"; return 0; }
    const auto decoded = block_compression::decodeBc1(blocks.data(), blocks.size(), width, height);
    std::cout << "sample " << width << 'x' << height << ": BC1 " << blocks.size() / 1024 << " KB (BGRA " << pixels.size() / 1024
              << " KB), PSNR " << psnr(pixels, decoded) << " dB, encoded in " << spent << " ms\n";
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> target;
    auto format = GUID_WICPixelFormat32bppBGRA;
    if (out && (FAILED(wic->CreateStream(stream.GetAddressOf())) || FAILED(stream->InitializeFromFilename(out, GENERIC_WRITE)) ||
        FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf())) || FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(target.GetAddressOf(), nullptr)) || FAILED(target->Initialize(nullptr)) || FAILED(target->SetSize(width, height)) ||
        FAILED(target->SetPixelFormat(&format)) ||
        FAILED(target->WritePixels(height, width * 4, static_cast<UINT>(decoded.size()), const_cast<BYTE*>(decoded.data()))) ||
        FAILED(target->Commit()) || FAILED(encoder->Commit()))) return 202;
    return 0;
}
} // namespace

int main() {
    wchar_t in[2048]{}, out[2048]{};
    if (GetEnvironmentVariableW(L"LAUNCHME_BC1_SAMPLE", in, 2048))
        return sample(in, GetEnvironmentVariableW(L"LAUNCHME_BC1_OUT", out, 2048) ? out : nullptr);

    // Encoded and decoded again, a photo-like picture keeps its look (a JPEG cover is ~35-40 dB).
    const auto image = picture(600, 900);
    const auto blocks = block_compression::encodeBc1(image.data(), 600, 900);
    if (blocks.size() != block_compression::bc1Bytes(600, 900) || blocks.size() != image.size() / 8) return 1;
    const auto decoded = block_compression::decodeBc1(blocks.data(), blocks.size(), 600, 900);
    if (decoded.size() != image.size()) return 2;
    const double quality = psnr(image, decoded);
    if (quality < 30) { std::cerr << "BC1 PSNR " << quality << " dB\n"; return 3; }
    // Every block in four-colour mode (no transparent texels), all opaque.
    for (size_t i = 0; i < blocks.size(); i += 8) {
        const uint16_t c0 = static_cast<uint16_t>(blocks[i] | blocks[i + 1] << 8), c1 = static_cast<uint16_t>(blocks[i + 2] | blocks[i + 3] << 8);
        if (c0 < c1) return 4;
    }
    for (size_t i = 3; i < decoded.size(); i += 4) if (decoded[i] != 255) return 5;
    // One colour: exact to the 5:6:5 grid.
    std::vector<uint8_t> flat(16 * 16 * 4);
    for (size_t i = 0; i < flat.size(); i += 4) { flat[i] = 0x10; flat[i + 1] = 0x80; flat[i + 2] = 0xF8; flat[i + 3] = 255; }
    const auto flat_blocks = block_compression::encodeBc1(flat.data(), 16, 16);
    const auto flat_back = block_compression::decodeBc1(flat_blocks.data(), flat_blocks.size(), 16, 16);
    if (flat_back.size() != flat.size() || psnr(flat, flat_back) < 40) return 6;
    // Not blocks: transparency, or a size that is not a multiple of four.
    auto translucent = image; translucent[3] = 254;
    if (!block_compression::encodeBc1(translucent.data(), 600, 900).empty()) return 7;
    if (!block_compression::encodeBc1(image.data(), 598, 900).empty()) return 8;
    if (!block_compression::decodeBc1(blocks.data(), blocks.size() - 8, 600, 900).empty()) return 9;

    // The cache given back behind a game: a zero budget drops every unpinned entry, oldest first.
    LruCache<Blob, BlobBytes> cache(100);
    cache.at("a").size = 30; cache.resize("a");
    cache.at("b").size = 30; cache.resize("b");
    cache.setBudget(0);
    const auto dropped = cache.trim([](const std::string& key) { return key == "b"; });
    if (dropped.size() != 1 || dropped[0] != "a" || cache.size() != 1 || cache.bytes() != 30) return 10;
    cache.setBudget(100);
    if (!cache.trim([](const std::string&) { return false; }).empty()) return 11;

    // This process's memory: something committed and resident; trimming leaves it working.
    const auto before = currentMemoryUsage();
    if (!before.private_bytes || !before.working_set) return 12;
    trimProcessMemory();
    const auto after = currentMemoryUsage();
    if (!after.private_bytes || after.working_set > before.working_set + (size_t{8} << 20)) return 13;
    std::cout << "BC1 " << quality << " dB; memory " << before.working_set / 1024 << " -> " << after.working_set / 1024 << " KB resident\n";
    return 0;
}
