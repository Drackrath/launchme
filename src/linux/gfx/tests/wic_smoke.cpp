// Smoke test of the WIC and COM ports (wic_*.cpp, com_*.cpp): decodes, converts, scales, clips
// and encodes through the WIC API only, writes what it made as PNGs (argv[1], default ".") to be
// looked at, and checks the measurements that must hold.
#include <shlwapi.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <png.h>
#include <turbojpeg.h>
#include <webp/encode.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {
std::string output_directory = ".";
int failures = 0;

void check(bool condition, const char* what) {
    std::printf("%s: %s\n", condition ? "ok  " : "FAIL", what);
    if (!condition) ++failures;
}

// A colour gradient over a checkerboard; with alpha, a soft-edged opaque disc on a clear field
// and a half-transparent band down the left.
std::vector<BYTE> picture(UINT width, UINT height, bool alpha) {
    std::vector<BYTE> pixels(size_t{width} * height * 4);
    const double cx = width * 0.55, cy = height * 0.5, radius = std::min(width, height) * 0.42;
    for (UINT y = 0; y < height; ++y)
        for (UINT x = 0; x < width; ++x) {
            BYTE* p = pixels.data() + (size_t{y} * width + x) * 4;
            const bool dark = ((x / 16) + (y / 16)) % 2;
            const double u = double(x) / width, v = double(y) / height;
            p[0] = static_cast<BYTE>(255 * (1 - u) * (dark ? 0.7 : 1));
            p[1] = static_cast<BYTE>(255 * v * (dark ? 0.7 : 1));
            p[2] = static_cast<BYTE>(255 * u);
            const double distance = std::hypot(x - cx, y - cy);
            const double disc = std::clamp((radius - distance) / 6 + 0.5, 0.0, 1.0);
            p[3] = !alpha ? 255 : x < width / 8 ? 128 : static_cast<BYTE>(std::lround(255 * disc));
        }
    return pixels;
}

std::vector<BYTE> premultiplied(std::vector<BYTE> pixels) {
    for (size_t i = 0; i < pixels.size(); i += 4)
        for (int c = 0; c < 3; ++c) pixels[i + c] = static_cast<BYTE>((pixels[i + c] * pixels[i + 3] + 127) / 255);
    return pixels;
}

double psnr(const std::vector<BYTE>& a, const std::vector<BYTE>& b, int channels = 3) {
    if (a.size() != b.size() || a.empty()) return 0;
    double error = 0;
    size_t count = 0;
    for (size_t i = 0; i < a.size(); i += 4)
        for (int c = 0; c < channels; ++c, ++count) error += (double(a[i + c]) - b[i + c]) * (double(a[i + c]) - b[i + c]);
    return error ? 10 * std::log10(255.0 * 255.0 * count / error) : 99;
}

// The stream's bytes, read back as the launcher's fixtures do (Stat, Seek, Read).
std::vector<BYTE> contents(IStream* stream) {
    STATSTG statistics{};
    if (FAILED(stream->Stat(&statistics, STATFLAG_NONAME))) return {};
    std::vector<BYTE> bytes(static_cast<size_t>(statistics.cbSize.QuadPart));
    LARGE_INTEGER start{};
    ULONG read = 0;
    if (FAILED(stream->Seek(start, STREAM_SEEK_SET, nullptr)) || FAILED(stream->Read(bytes.data(), static_cast<ULONG>(bytes.size()), &read)) ||
        read != bytes.size()) return {};
    return bytes;
}

// `source` encoded into `stream`; quality < 0 leaves the encoder's default.
bool encodeInto(IWICImagingFactory* wic, IStream* stream, const GUID& container, IWICBitmapSource* source, WICPixelFormatGUID format, float quality = -1) {
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> options;
    if (FAILED(wic->CreateEncoder(container, nullptr, encoder.GetAddressOf())) || FAILED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(frame.GetAddressOf(), options.GetAddressOf()))) return false;
    if (quality >= 0) {
        PROPBAG2 option{};
        option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
        VARIANT value;
        VariantInit(&value);
        value.vt = VT_R4;
        value.fltVal = quality;
        if (FAILED(options->Write(1, &option, &value))) return false;
    }
    return SUCCEEDED(frame->Initialize(options.Get())) && SUCCEEDED(frame->SetPixelFormat(&format)) &&
           SUCCEEDED(frame->WriteSource(source, nullptr)) && SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
}

std::vector<BYTE> encode(IWICImagingFactory* wic, const GUID& container, IWICBitmapSource* source, float quality = -1) {
    ComPtr<IStream> stream;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, stream.GetAddressOf())) ||
        !encodeInto(wic, stream.Get(), container, source, container == GUID_ContainerFormatPng ? GUID_WICPixelFormat32bppBGRA : GUID_WICPixelFormat24bppBGR, quality))
        return {};
    return contents(stream.Get());
}

ComPtr<IWICBitmap> bitmap(IWICImagingFactory* wic, UINT width, UINT height, std::vector<BYTE>& pixels, const GUID& format = GUID_WICPixelFormat32bppBGRA) {
    ComPtr<IWICBitmap> result;
    wic->CreateBitmapFromMemory(width, height, format, width * 4, static_cast<UINT>(pixels.size()), pixels.data(), result.GetAddressOf());
    return result;
}

// The first frame of `bytes`, read in place.
ComPtr<IWICBitmapFrameDecode> decode(IWICImagingFactory* wic, std::vector<BYTE>& bytes, ComPtr<IWICBitmapDecoder>* keep = nullptr) {
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(wic->CreateStream(stream.GetAddressOf())) || FAILED(stream->InitializeFromMemory(bytes.data(), static_cast<DWORD>(bytes.size()))) ||
        FAILED(wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf())) ||
        FAILED(decoder->GetFrame(0, frame.GetAddressOf()))) return {};
    if (keep) *keep = decoder;
    return frame;
}

// The source's pixels converted to `format` (four bytes a pixel).
std::vector<BYTE> pixelsOf(IWICImagingFactory* wic, IWICBitmapSource* source, const GUID& format = GUID_WICPixelFormat32bppBGRA) {
    ComPtr<IWICFormatConverter> converter;
    UINT width = 0, height = 0;
    if (!source || FAILED(wic->CreateFormatConverter(converter.GetAddressOf())) ||
        FAILED(converter->Initialize(source, format, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) ||
        FAILED(converter->GetSize(&width, &height))) return {};
    std::vector<BYTE> pixels(size_t{width} * height * 4);
    if (FAILED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data()))) return {};
    return pixels;
}

// BGRA pixels as a PNG of another kind (PNG_FORMAT_BGR, PNG_FORMAT_GRAY).
std::vector<BYTE> pngOf(const std::vector<BYTE>& bgra, UINT width, UINT height, png_uint_32 format) {
    png_image png{};
    png.version = PNG_IMAGE_VERSION;
    png.width = width;
    png.height = height;
    png.format = format;
    const size_t channels = PNG_IMAGE_SAMPLE_CHANNELS(format);
    std::vector<BYTE> packed(size_t{width} * height * channels);
    for (size_t i = 0, j = 0; i < bgra.size(); i += 4, j += channels)
        if (channels == 1) packed[j] = static_cast<BYTE>((bgra[i] + bgra[i + 1] + bgra[i + 2]) / 3);
        else std::copy_n(&bgra[i], 3, &packed[j]);
    size_t size = 0;
    png_image_write_get_memory_size(png, size, 0, packed.data(), 0, nullptr);
    std::vector<BYTE> bytes(size);
    png_image_write_to_memory(&png, bytes.data(), &size, 0, packed.data(), 0, nullptr);
    bytes.resize(size);
    return bytes;
}

std::vector<BYTE> grayJpegOf(const std::vector<BYTE>& bgra, UINT width, UINT height) {
    tjhandle turbo = tj3Init(TJINIT_COMPRESS);
    unsigned char* buffer = nullptr;
    size_t size = 0;
    tj3Set(turbo, TJPARAM_QUALITY, 90);
    tj3Set(turbo, TJPARAM_SUBSAMP, TJSAMP_GRAY);
    tj3Compress8(turbo, bgra.data(), static_cast<int>(width), 0, static_cast<int>(height), TJPF_BGRA, &buffer, &size);
    std::vector<BYTE> bytes(buffer, buffer + size);
    tj3Free(buffer);
    tj3Destroy(turbo);
    return bytes;
}

GUID formatOf(IWICBitmapSource* source) {
    GUID format{};
    if (source) source->GetPixelFormat(&format);
    return format;
}

// As the renderer's UI captures: a PNG through IWICStream::InitializeFromFilename.
bool save(IWICImagingFactory* wic, IWICBitmapSource* source, const char* name) {
    const std::string file = output_directory + "/" + name;
    ComPtr<IWICStream> stream;
    const bool saved = source && SUCCEEDED(wic->CreateStream(stream.GetAddressOf())) &&
                       SUCCEEDED(stream->InitializeFromFilename(file.c_str(), GENERIC_WRITE)) &&
                       encodeInto(wic, stream.Get(), GUID_ContainerFormatPng, source, GUID_WICPixelFormat32bppBGRA);
    std::printf("%s %s\n", saved ? "wrote" : "FAILED to write", file.c_str());
    if (!saved) ++failures;
    return saved;
}

ComPtr<IWICBitmapScaler> scaler(IWICImagingFactory* wic, IWICBitmapSource* source, UINT width, UINT height, WICBitmapInterpolationMode mode) {
    ComPtr<IWICBitmapScaler> result;
    if (FAILED(wic->CreateBitmapScaler(result.GetAddressOf())) || FAILED(result->Initialize(source, width, height, mode))) return {};
    return result;
}

double milliseconds(std::chrono::steady_clock::time_point since) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - since).count();
}

// Placeholder class for the registry hook: creates the WIC factory.
HRESULT customClass(REFIID riid, void** object) { return CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, riid, object); }
constexpr CLSID custom_clsid{0x12345678, 0x1234, 0x1234, {1, 2, 3, 4, 5, 6, 7, 8}};

void comRuntime() {
    check(CoInitializeEx(nullptr, COINIT_MULTITHREADED) == S_OK, "CoInitializeEx: S_OK first");
    check(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED) == S_FALSE, "CoInitializeEx: S_FALSE again");
    CoUninitialize();
    CoUninitialize();
    check(CoInitializeEx(nullptr, COINIT_MULTITHREADED) == S_OK, "CoInitializeEx: S_OK after balanced CoUninitialize");
    ComPtr<IWICImagingFactory> factory;
    check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) && factory,
          "CoCreateInstance(CLSID_WICImagingFactory2)");
    ComPtr<IUnknown> unknown;
    check(CoCreateInstance(custom_clsid, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&unknown)) == REGDB_E_CLASSNOTREG && !unknown,
          "CoCreateInstance(unknown CLSID) = REGDB_E_CLASSNOTREG");
    lm_com::registerClass(custom_clsid, customClass);
    check(SUCCEEDED(CoCreateInstance(custom_clsid, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) && factory,
          "lm_com::registerClass makes a class creatable");
    ComPtr<IWICBitmapDecoder> wrong;
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wrong)) == E_NOINTERFACE && !wrong,
          "CoCreateInstance with an interface the class lacks = E_NOINTERFACE");
    void* memory = CoTaskMemAlloc(64);
    check(memory != nullptr, "CoTaskMemAlloc");
    CoTaskMemFree(memory);
}

void streams() {
    const char text[] = "launchme streams";
    ComPtr<IStream> memory;
    memory.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(text), sizeof(text)));
    char back[sizeof(text)]{};
    ULONG read = 0;
    check(memory && SUCCEEDED(memory->Read(back, sizeof(back), &read)) && read == sizeof(text) && !std::strcmp(back, text), "SHCreateMemStream reads its copy");
    check(memory->Read(back, 4, &read) == S_FALSE && read == 0, "Read at the end: S_FALSE, nothing");
    LARGE_INTEGER move{};
    move.QuadPart = -6;
    ULARGE_INTEGER position{};
    check(SUCCEEDED(memory->Seek(move, STREAM_SEEK_END, &position)) && position.QuadPart == sizeof(text) - 6, "Seek from the end");
    check(SUCCEEDED(memory->Write("XYZ", 3, nullptr)) && contents(memory.Get()).size() == sizeof(text), "Write inside keeps the size");

    const std::wstring file = std::wstring(output_directory.begin(), output_directory.end()) + L"/wic-stream.bin";
    ComPtr<IStream> written, again;
    check(SUCCEEDED(SHCreateStreamOnFileEx(file.c_str(), STGM_CREATE | STGM_WRITE | STGM_SHARE_EXCLUSIVE, 0, TRUE, nullptr, &written)) &&
          SUCCEEDED(written->Write(text, sizeof(text), nullptr)), "SHCreateStreamOnFileEx creates and writes");
    written.Reset();
    check(SHCreateStreamOnFileEx(file.c_str(), STGM_WRITE, 0, TRUE, nullptr, &again) == HRESULT_FROM_WIN32(80),
          "SHCreateStreamOnFileEx(create, no STGM_CREATE) fails when the file exists");
    STATSTG statistics{};
    check(SUCCEEDED(SHCreateStreamOnFileEx(file.c_str(), STGM_READ, 0, FALSE, nullptr, &again)) && SUCCEEDED(again->Stat(&statistics, STATFLAG_DEFAULT)) &&
          statistics.cbSize.QuadPart == sizeof(text) && statistics.pwcsName && file == statistics.pwcsName && contents(again.Get()).size() == sizeof(text),
          "SHCreateStreamOnFileEx reads it back, Stat names it");
    CoTaskMemFree(statistics.pwcsName);
    check(SHCreateStreamOnFileEx(L"/nonexistent/launchme", STGM_READ, 0, FALSE, nullptr, &again) == HRESULT_FROM_WIN32(2),
          "a missing file: HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)");
}

void pngAndConversions(IWICImagingFactory* wic) {
    constexpr UINT width = 320, height = 200;
    auto source = picture(width, height, true);
    const auto source_bitmap = bitmap(wic, width, height, source);
    auto png = encode(wic, GUID_ContainerFormatPng, source_bitmap.Get());
    check(png.size() > 8 && png[0] == 0x89 && png[1] == 'P', "PNG encoded into a CreateStreamOnHGlobal stream");
    ComPtr<IWICBitmapDecoder> decoder;
    const auto frame = decode(wic, png, &decoder);
    GUID container{};
    UINT frames = 0, w = 0, h = 0;
    check(frame && SUCCEEDED(decoder->GetContainerFormat(&container)) && container == GUID_ContainerFormatPng &&
          SUCCEEDED(decoder->GetFrameCount(&frames)) && frames == 1, "PNG decoder: container and frame count");
    check(frame && SUCCEEDED(frame->GetSize(&w, &h)) && w == width && h == height && formatOf(frame.Get()) == GUID_WICPixelFormat32bppBGRA,
          "PNG with alpha: its size, 32bppBGRA");
    std::vector<BYTE> copied(source.size());
    check(frame && SUCCEEDED(frame->CopyPixels(nullptr, width * 4, static_cast<UINT>(copied.size()), copied.data())) && copied == source,
          "PNG round trip: the same straight pixels");
    check(pixelsOf(wic, frame.Get(), GUID_WICPixelFormat32bppPBGRA) == premultiplied(source), "converter to 32bppPBGRA premultiplies");
    // A rectangle, rows straight into a larger stride.
    const WICRect rect{10, 20, 30, 40};
    std::vector<BYTE> part(64 * 4 * 40);
    bool same = frame && SUCCEEDED(frame->CopyPixels(&rect, 64 * 4, static_cast<UINT>(part.size()), part.data()));
    for (int y = 0; same && y < rect.Height; ++y)
        same = !std::memcmp(part.data() + y * 64 * 4, source.data() + ((rect.Y + y) * width + rect.X) * 4, rect.Width * 4);
    check(same, "CopyPixels of a rectangle with a wider stride");
    std::vector<BYTE> small(10);
    check(frame && frame->CopyPixels(&rect, 64 * 4, static_cast<UINT>(small.size()), small.data()) == WINCODEC_ERR_INSUFFICIENTBUFFER,
          "CopyPixels into too small a buffer: WINCODEC_ERR_INSUFFICIENTBUFFER");
    const WICRect outside{300, 0, 30, 10};
    check(frame && frame->CopyPixels(&outside, 30 * 4, 30 * 4 * 10, part.data()) == E_INVALIDARG, "CopyPixels outside the image: E_INVALIDARG");

    // Every format there and back: the opaque ones keep the colours, the alpha ones the pixels.
    auto opaque = picture(width, height, false);
    const auto opaque_bitmap = bitmap(wic, width, height, opaque);
    for (const GUID* format : {&GUID_WICPixelFormat32bppBGR, &GUID_WICPixelFormat24bppBGR, &GUID_WICPixelFormat24bppRGB, &GUID_WICPixelFormat32bppRGBA,
                               &GUID_WICPixelFormat32bppPRGBA, &GUID_WICPixelFormat32bppPBGRA}) {
        ComPtr<IWICFormatConverter> there;
        wic->CreateFormatConverter(there.GetAddressOf());
        there->Initialize(opaque_bitmap.Get(), *format, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);
        if (pixelsOf(wic, there.Get()) != opaque) { check(false, "format round trip of an opaque picture"); return; }
    }
    check(true, "opaque picture through 32bppBGR, 24bppBGR, 24bppRGB, 32bppRGBA, 32bppPRGBA, 32bppPBGRA and back");
    ComPtr<IWICFormatConverter> rgba;
    wic->CreateFormatConverter(rgba.GetAddressOf());
    rgba->Initialize(source_bitmap.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);
    const auto swapped = pixelsOf(wic, rgba.Get(), GUID_WICPixelFormat32bppRGBA);
    check(swapped.size() == source.size() && swapped[2] == source[0] && swapped[0] == source[2] && pixelsOf(wic, rgba.Get()) == source,
          "32bppRGBA swaps red and blue, and back");
    ComPtr<IWICFormatConverter> mask, back;
    wic->CreateFormatConverter(mask.GetAddressOf());
    wic->CreateFormatConverter(back.GetAddressOf());
    mask->Initialize(source_bitmap.Get(), GUID_WICPixelFormat8bppAlpha, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);
    back->Initialize(mask.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);
    std::vector<BYTE> from_mask(source.size());
    back->CopyPixels(nullptr, width * 4, static_cast<UINT>(from_mask.size()), from_mask.data());
    bool alpha_kept = true;
    for (size_t i = 0; i < source.size(); i += 4) alpha_kept = alpha_kept && from_mask[i + 3] == source[i + 3] && !from_mask[i];
    check(alpha_kept, "8bppAlpha keeps alpha (black colour)");

    // Frames report what the file stores. (This port's PNG encoder always writes RGBA, so the
    // other kinds come from libpng and TurboJPEG directly.)
    auto rgb_png = pngOf(opaque, width, height, PNG_FORMAT_BGR);
    auto gray_png = pngOf(opaque, width, height, PNG_FORMAT_GRAY);
    auto gray_jpeg = grayJpegOf(opaque, width, height);
    const auto rgb_frame = decode(wic, rgb_png), gray_frame = decode(wic, gray_png), gray_jpeg_frame = decode(wic, gray_jpeg);
    check(rgb_frame && formatOf(rgb_frame.Get()) == GUID_WICPixelFormat24bppBGR && pixelsOf(wic, rgb_frame.Get()) == opaque, "RGB PNG: 24bppBGR, exact");
    check(gray_frame && formatOf(gray_frame.Get()) == GUID_WICPixelFormat8bppGray, "grey PNG: 8bppGray");
    check(gray_jpeg_frame && formatOf(gray_jpeg_frame.Get()) == GUID_WICPixelFormat8bppGray, "grey JPEG: 8bppGray");
    // The scaler keeps the source's format; Fant from a transparent disc does not darken its edge.
    const auto fant = scaler(wic, frame.Get(), width / 3, height / 3, WICBitmapInterpolationModeFant);
    check(fant && formatOf(fant.Get()) == GUID_WICPixelFormat32bppBGRA, "scaler: the source's format");
    save(wic, frame.Get(), "wic-png-decoded.png");
    save(wic, fant.Get(), "wic-scale-fant-third.png");
}

void scaling(IWICImagingFactory* wic) {
    constexpr UINT width = 480, height = 300;
    auto source = picture(width, height, true);
    auto png = encode(wic, GUID_ContainerFormatPng, bitmap(wic, width, height, source).Get());
    const auto frame = decode(wic, png);
    const struct { WICBitmapInterpolationMode mode; const char* name; } modes[]{
        {WICBitmapInterpolationModeNearestNeighbor, "wic-scale-nearest.png"}, {WICBitmapInterpolationModeLinear, "wic-scale-linear.png"},
        {WICBitmapInterpolationModeCubic, "wic-scale-cubic.png"}, {WICBitmapInterpolationModeFant, "wic-scale-fant.png"},
        {WICBitmapInterpolationModeHighQualityCubic, "wic-scale-hqcubic.png"}};
    for (const auto& mode : modes) {
        const auto scaled = scaler(wic, frame.Get(), 200, 125, mode.mode);
        save(wic, scaled.Get(), mode.name);
        // Row by row, as WIC's consumers read in strips: the same as at once.
        const auto whole = pixelsOf(wic, scaled.Get());
        std::vector<BYTE> rows(whole.size());
        bool same = !whole.empty();
        for (INT y = 0; same && y < 125; ++y) {
            const WICRect row{0, y, 200, 1};
            same = SUCCEEDED(scaled->CopyPixels(&row, 200 * 4, 200 * 4, rows.data() + y * 200 * 4));
        }
        if (!same || rows != whole) { check(false, mode.name); return; }
    }
    check(true, "every interpolation mode scales; rows copy as the whole");
    // The disc's opaque middle keeps its colour; outside, nothing (straight alpha 0).
    const auto fant = pixelsOf(wic, scaler(wic, frame.Get(), 240, 150, WICBitmapInterpolationModeFant).Get());
    const BYTE* middle = fant.data() + (75 * 240 + 132) * 4;
    const BYTE* original = source.data() + (150 * width + 264) * 4;
    check(fant.size() == 240 * 150 * 4 && middle[3] == 255 && std::abs(middle[0] - original[0]) <= 8 && std::abs(middle[2] - original[2]) <= 8,
          "Fant halving: the opaque middle keeps its colour");
    check(fant[(2 * 240 + 230) * 4 + 3] == 0, "Fant halving: the clear corner stays clear");
    // Fant at an exact half is the 2x2 average (premultiplied).
    const auto pre = premultiplied(source);
    const auto pre_fant = pixelsOf(wic, scaler(wic, frame.Get(), 240, 150, WICBitmapInterpolationModeFant).Get(), GUID_WICPixelFormat32bppPBGRA);
    int worst = 0;
    for (UINT y = 0; y < 150; ++y)
        for (UINT x = 0; x < 240; ++x)
            for (int c = 0; c < 4; ++c) {
                const auto at = [&](UINT sx, UINT sy) { return int(pre[(sy * width + sx) * 4 + c]); };
                const int average = (at(2 * x, 2 * y) + at(2 * x + 1, 2 * y) + at(2 * x, 2 * y + 1) + at(2 * x + 1, 2 * y + 1) + 2) / 4;
                worst = std::max(worst, std::abs(average - int(pre_fant[(y * 240 + x) * 4 + c])));
            }
    check(worst <= 2, "Fant at a half: the 2x2 premultiplied average");
    // Enlarging and a scaler over a scaler.
    const auto up = scaler(wic, frame.Get(), 960, 600, WICBitmapInterpolationModeFant);
    save(wic, up.Get(), "wic-scale-fant-double.png");
    const auto twice = scaler(wic, up.Get(), 120, 75, WICBitmapInterpolationModeHighQualityCubic);
    check(pixelsOf(wic, twice.Get()).size() == 120 * 75 * 4, "a scaler over a scaler");

    // Clipper: the rectangle's pixels; a scaler over it (as the launcher crops a large logo).
    ComPtr<IWICBitmapClipper> clip;
    const WICRect rect{100, 40, 200, 180};
    check(SUCCEEDED(wic->CreateBitmapClipper(clip.GetAddressOf())) && SUCCEEDED(clip->Initialize(frame.Get(), &rect)), "clipper");
    const auto clipped = pixelsOf(wic, clip.Get());
    bool same = clipped.size() == size_t{200} * 180 * 4;
    for (INT y = 0; same && y < rect.Height; ++y)
        same = !std::memcmp(clipped.data() + y * 200 * 4, source.data() + ((rect.Y + y) * width + rect.X) * 4, 200 * 4);
    check(same, "clipper: the rectangle's pixels");
    const WICRect inner{5, 6, 7, 8};
    std::vector<BYTE> few(7 * 8 * 4);
    same = SUCCEEDED(clip->CopyPixels(&inner, 7 * 4, static_cast<UINT>(few.size()), few.data()));
    for (INT y = 0; same && y < 8; ++y)
        same = !std::memcmp(few.data() + y * 7 * 4, source.data() + ((rect.Y + 6 + y) * width + rect.X + 5) * 4, 7 * 4);
    check(same, "clipper: a rectangle inside it");
    const WICRect too_big{400, 0, 100, 10};
    ComPtr<IWICBitmapClipper> bad;
    wic->CreateBitmapClipper(bad.GetAddressOf());
    check(bad->Initialize(frame.Get(), &too_big) == E_INVALIDARG, "clipper outside the source: E_INVALIDARG");
    save(wic, scaler(wic, clip.Get(), 100, 90, WICBitmapInterpolationModeFant).Get(), "wic-clip-scaled.png");

    // The renderer's grey picture: scaler, 8bppGray, 32bppPBGRA.
    ComPtr<IWICFormatConverter> gray, colour;
    wic->CreateFormatConverter(gray.GetAddressOf());
    wic->CreateFormatConverter(colour.GetAddressOf());
    const auto small = scaler(wic, frame.Get(), 240, 150, WICBitmapInterpolationModeFant);
    check(SUCCEEDED(gray->Initialize(small.Get(), GUID_WICPixelFormat8bppGray, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) &&
          SUCCEEDED(colour->Initialize(gray.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)),
          "scaler -> 8bppGray -> 32bppPBGRA");
    const auto grey = pixelsOf(wic, colour.Get(), GUID_WICPixelFormat32bppPBGRA);
    check(grey.size() == 240 * 150 * 4 && grey[0] == grey[1] && grey[1] == grey[2] && grey[3] == 255, "grey: equal channels, opaque");
    save(wic, colour.Get(), "wic-gray.png");
}

void jpeg(IWICImagingFactory* wic) {
    constexpr UINT width = 1600, height = 1000;
    auto source = picture(width, height, false);
    const auto source_bitmap = bitmap(wic, width, height, source);
    auto best = encode(wic, GUID_ContainerFormatJpeg, source_bitmap.Get(), 0.95f);
    const auto normal = encode(wic, GUID_ContainerFormatJpeg, source_bitmap.Get());
    const auto worst = encode(wic, GUID_ContainerFormatJpeg, source_bitmap.Get(), 0.3f);
    check(!best.empty() && best[0] == 0xff && best[1] == 0xd8, "JPEG encoded");
    std::printf("      JPEG sizes: quality 0.95 %zu, default (0.9) %zu, 0.3 %zu bytes\n", best.size(), normal.size(), worst.size());
    check(best.size() > normal.size() && normal.size() > worst.size(), "ImageQuality from the property bag changes the size");
    ComPtr<IWICBitmapDecoder> decoder;
    const auto frame = decode(wic, best, &decoder);
    GUID container{};
    check(frame && SUCCEEDED(decoder->GetContainerFormat(&container)) && container == GUID_ContainerFormatJpeg &&
          formatOf(frame.Get()) == GUID_WICPixelFormat24bppBGR, "JPEG: container, 24bppBGR");
    const double quality = psnr(pixelsOf(wic, frame.Get()), source);
    std::printf("      JPEG 0.95 PSNR %.1f dB\n", quality);
    check(quality > 33, "JPEG round trip looks like the picture");

    // Native reduction (DCT scaling) through IWICBitmapSourceTransform.
    ComPtr<IWICBitmapSourceTransform> transform;
    check(SUCCEEDED(frame.As(&transform)), "JPEG frame: IWICBitmapSourceTransform");
    UINT w = 390, h = 240;
    check(SUCCEEDED(transform->GetClosestSize(&w, &h)) && w == 400 && h == 250, "GetClosestSize(390x240) = 400x250 (1/4)");
    UINT big_w = 801, big_h = 10;
    check(SUCCEEDED(transform->GetClosestSize(&big_w, &big_h)) && big_w == width && big_h == height, "GetClosestSize past a half: the full size");
    UINT odd_w = 199, odd_h = 124;
    ComPtr<IWICBitmapDecoder> odd_decoder;
    auto odd_bytes = encode(wic, GUID_ContainerFormatJpeg, bitmap(wic, 1597, 999, source).Get());
    const auto odd = decode(wic, odd_bytes, &odd_decoder);
    ComPtr<IWICBitmapSourceTransform> odd_transform;
    check(odd && SUCCEEDED(odd.As(&odd_transform)) && SUCCEEDED(odd_transform->GetClosestSize(&odd_w, &odd_h)) && odd_w == 200 && odd_h == 125,
          "GetClosestSize rounds up as libjpeg-turbo scales (1597x999 / 8 = 200x125)");
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGR;
    ComPtr<IWICComponentInfo> info;
    ComPtr<IWICPixelFormatInfo> pixel_info;
    UINT bits = 0;
    check(SUCCEEDED(transform->GetClosestPixelFormat(&format)) && format == GUID_WICPixelFormat32bppBGR &&
          SUCCEEDED(wic->CreateComponentInfo(format, info.GetAddressOf())) && SUCCEEDED(info.As(&pixel_info)) &&
          SUCCEEDED(pixel_info->GetBitsPerPixel(&bits)) && bits == 32, "GetClosestPixelFormat keeps 32bppBGR; its info: 32 bits");
    std::vector<BYTE> reduced(size_t{w} * h * 4);
    auto started = std::chrono::steady_clock::now();
    const bool copied = SUCCEEDED(transform->CopyPixels(nullptr, w, h, &format, WICBitmapTransformRotate0, w * 4, static_cast<UINT>(reduced.size()), reduced.data()));
    const double reduced_ms = milliseconds(started);
    check(copied, "CopyPixels at 1/4 size");
    check(transform->CopyPixels(nullptr, 401, 250, &format, WICBitmapTransformRotate0, 401 * 4, 401 * 4 * 250, reduced.data()) == E_INVALIDARG,
          "CopyPixels at a size GetClosestSize does not give: E_INVALIDARG");
    ComPtr<IWICBitmap> reduced_bitmap;
    check(SUCCEEDED(wic->CreateBitmapFromMemory(w, h, format, w * 4, static_cast<UINT>(reduced.size()), reduced.data(), reduced_bitmap.GetAddressOf())),
          "CreateBitmapFromMemory (32bppBGR)");
    // Compared with decoding everything and scaling (a fresh frame: nothing decoded yet).
    const auto fresh = decode(wic, best);
    started = std::chrono::steady_clock::now();
    const auto full = pixelsOf(wic, scaler(wic, fresh.Get(), w, h, WICBitmapInterpolationModeFant).Get());
    const double full_ms = milliseconds(started);
    const double similar = psnr(pixelsOf(wic, reduced_bitmap.Get()), full);
    std::printf("      1/4 by DCT %.1f ms, full decode + Fant %.1f ms, PSNR between them %.1f dB\n", reduced_ms, full_ms, similar);
    check(similar > 30, "the DCT reduction looks like a Fant reduction");
    save(wic, reduced_bitmap.Get(), "wic-jpeg-quarter-dct.png");
    std::vector<BYTE> half(size_t{800} * 500 * 3);
    WICPixelFormatGUID bgr = GUID_WICPixelFormat24bppBGR;
    const WICRect corner{700, 400, 100, 100};
    check(SUCCEEDED(transform->CopyPixels(&corner, 800, 500, &bgr, WICBitmapTransformRotate0, 100 * 3, 100 * 3 * 100, half.data())),
          "CopyPixels at 1/2 size, a rectangle, 24bppBGR");
}

void webp(IWICImagingFactory* wic) {
    constexpr UINT width = 256, height = 160;
    auto source = picture(width, height, true);
    uint8_t* lossless = nullptr;
    const size_t lossless_size = WebPEncodeLosslessBGRA(source.data(), width, height, width * 4, &lossless);
    std::vector<BYTE> bytes(lossless, lossless + lossless_size);
    WebPFree(lossless);
    ComPtr<IWICBitmapDecoder> decoder;
    const auto frame = decode(wic, bytes, &decoder);
    GUID container{};
    check(frame && SUCCEEDED(decoder->GetContainerFormat(&container)) && container == GUID_ContainerFormatWebp &&
          formatOf(frame.Get()) == GUID_WICPixelFormat32bppBGRA, "WebP with alpha: container, 32bppBGRA");
    // Lossless keeps the visible pixels (libwebp may change the colour under alpha 0).
    const auto decoded = pixelsOf(wic, frame.Get(), GUID_WICPixelFormat32bppPBGRA);
    check(decoded == premultiplied(source), "lossless WebP decodes exactly (premultiplied)");
    auto opaque = picture(width, height, false);
    uint8_t* lossy = nullptr;
    const size_t lossy_size = WebPEncodeBGRA(opaque.data(), width, height, width * 4, 90, &lossy);
    std::vector<BYTE> lossy_bytes(lossy, lossy + lossy_size);
    WebPFree(lossy);
    const auto lossy_frame = decode(wic, lossy_bytes);
    check(lossy_frame && formatOf(lossy_frame.Get()) == GUID_WICPixelFormat24bppBGR && psnr(pixelsOf(wic, lossy_frame.Get()), opaque) > 30,
          "lossy WebP without alpha: 24bppBGR, looks like the picture");
    save(wic, frame.Get(), "wic-webp-decoded.png");
}

void filesAndFailures(IWICImagingFactory* wic) {
    constexpr UINT width = 64, height = 48;
    auto source = picture(width, height, true);
    const std::string file = output_directory + "/wic-file-roundtrip.png";
    save(wic, bitmap(wic, width, height, source).Get(), "wic-file-roundtrip.png");
    const std::wstring wide(file.begin(), file.end());
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    check(SUCCEEDED(wic->CreateDecoderFromFilename(wide.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf())) &&
          SUCCEEDED(decoder->GetFrame(0, frame.GetAddressOf())) && pixelsOf(wic, frame.Get()) == source, "CreateDecoderFromFilename: the saved PNG");
    ComPtr<IWICBitmapFrameDecode> second;
    check(decoder->GetFrame(1, second.GetAddressOf()) == WINCODEC_ERR_FRAMEMISSING, "GetFrame(1): WINCODEC_ERR_FRAMEMISSING");
    check(wic->CreateDecoderFromFilename(L"/nonexistent/launchme.png", nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.ReleaseAndGetAddressOf()) ==
          HRESULT_FROM_WIN32(2), "a missing file: HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)");
    // A file stream as the decoder's input (read whole, not in place).
    ComPtr<IStream> file_stream;
    check(SUCCEEDED(SHCreateStreamOnFileEx(wide.c_str(), STGM_READ, 0, FALSE, nullptr, &file_stream)) &&
          SUCCEEDED(wic->CreateDecoderFromStream(file_stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, decoder.ReleaseAndGetAddressOf())),
          "CreateDecoderFromStream on a file stream");

    // Encoding with WritePixels, as the low-memory UI test saves its screenshots.
    auto pixels = premultiplied(source);
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> target;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppPBGRA;
    const std::string written = output_directory + "/wic-writepixels.png";
    check(SUCCEEDED(wic->CreateStream(stream.GetAddressOf())) && SUCCEEDED(stream->InitializeFromFilename(written.c_str(), GENERIC_WRITE)) &&
          SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf())) &&
          SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) && SUCCEEDED(encoder->CreateNewFrame(target.GetAddressOf(), nullptr)) &&
          SUCCEEDED(target->Initialize(nullptr)) && SUCCEEDED(target->SetSize(width, height)) && SUCCEEDED(target->SetPixelFormat(&format)) &&
          format == GUID_WICPixelFormat32bppBGRA, "PNG frame: 32bppPBGRA asked, 32bppBGRA agreed");
    check(SUCCEEDED(target->WritePixels(20, width * 4, width * 4 * 20, pixels.data())) &&
          target->Commit() == WINCODEC_ERR_WRONGSTATE && SUCCEEDED(target->WritePixels(height - 20, width * 4, width * 4 * (height - 20), pixels.data() + width * 4 * 20)) &&
          target->WritePixels(1, width * 4, width * 4, pixels.data()) == WINCODEC_ERR_CODECTOOMANYSCANLINES &&
          SUCCEEDED(target->Commit()) && SUCCEEDED(encoder->Commit()) && encoder->Commit() == WINCODEC_ERR_WRONGSTATE,
          "WritePixels in two parts, Commit only when complete");
    stream.Reset();
    ComPtr<IWICBitmapDecoder> written_decoder;
    ComPtr<IWICBitmapFrameDecode> written_frame;
    const std::wstring written_wide(written.begin(), written.end());
    check(SUCCEEDED(wic->CreateDecoderFromFilename(written_wide.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, written_decoder.GetAddressOf())) &&
          SUCCEEDED(written_decoder->GetFrame(0, written_frame.GetAddressOf())) && pixelsOf(wic, written_frame.Get()) == pixels,
          "WritePixels' bytes as written (the agreed format is not converted)");

    // Unsupported and broken images.
    std::vector<BYTE> gif{'G', 'I', 'F', '8', '9', 'a', 1, 0, 1, 0, 0x80, 0, 0, 0, 0, 0, 0xff, 0xff, 0xff, ',', 0, 0, 0, 0, 1, 0, 1, 0, 0, 2, 2, 'D', 1, 0, ';'};
    ComPtr<IWICStream> gif_stream;
    wic->CreateStream(gif_stream.GetAddressOf());
    gif_stream->InitializeFromMemory(gif.data(), static_cast<DWORD>(gif.size()));
    check(wic->CreateDecoderFromStream(gif_stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, decoder.ReleaseAndGetAddressOf()) == WINCODEC_ERR_COMPONENTNOTFOUND,
          "GIF: WINCODEC_ERR_COMPONENTNOTFOUND");
    auto png = encode(wic, GUID_ContainerFormatPng, bitmap(wic, width, height, source).Get());
    png.resize(png.size() / 2);
    const auto truncated = decode(wic, png);
    std::vector<BYTE> sink(source.size());
    check(truncated && truncated->CopyPixels(nullptr, width * 4, static_cast<UINT>(sink.size()), sink.data()) == WINCODEC_ERR_BADIMAGE,
          "a truncated PNG: its header decodes, CopyPixels is WINCODEC_ERR_BADIMAGE");
    ComPtr<IWICBitmapEncoder> gif_encoder;
    check(wic->CreateEncoder(GUID_ContainerFormatWebp, nullptr, gif_encoder.GetAddressOf()) == WINCODEC_ERR_COMPONENTNOTFOUND,
          "no WebP encoder: WINCODEC_ERR_COMPONENTNOTFOUND");
    ComPtr<IWICFormatConverter> converter;
    wic->CreateFormatConverter(converter.GetAddressOf());
    UINT w = 0, h = 0;
    check(converter->GetSize(&w, &h) == WINCODEC_ERR_NOTINITIALIZED, "converter before Initialize: WINCODEC_ERR_NOTINITIALIZED");
}

void bitmaps(IWICImagingFactory* wic) {
    ComPtr<IWICBitmap> bitmap;
    constexpr UINT width = 100, height = 60;
    check(SUCCEEDED(wic->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, bitmap.GetAddressOf())), "CreateBitmap 32bppPBGRA");
    {
        // As Direct2D's WIC target writes its frame back (d2d_wic.cpp).
        const WICRect all{0, 0, static_cast<INT>(width), static_cast<INT>(height)};
        ComPtr<IWICBitmapLock> lock, other;
        UINT stride = 0, size = 0, w = 0, h = 0;
        BYTE* data = nullptr;
        check(SUCCEEDED(bitmap->Lock(&all, WICBitmapLockWrite, lock.GetAddressOf())) && SUCCEEDED(lock->GetStride(&stride)) &&
              SUCCEEDED(lock->GetDataPointer(&size, &data)) && SUCCEEDED(lock->GetSize(&w, &h)) && data && stride == width * 4 &&
              size >= stride * (height - 1) + width * 4 && w == width && h == height, "write lock: stride, data, size");
        for (UINT y = 0; y < height; ++y)
            for (UINT x = 0; x < width; ++x) { BYTE* p = data + y * stride + x * 4; p[0] = BYTE(x); p[1] = BYTE(y); p[2] = 0; p[3] = 255; }
        check(bitmap->Lock(&all, WICBitmapLockRead, other.GetAddressOf()) == WINCODEC_ERR_ALREADYLOCKED, "a second lock while writing: refused");
    }
    ComPtr<IWICBitmapLock> first, second;
    const WICRect corner{50, 30, 10, 10};
    UINT size = 0;
    BYTE* data = nullptr;
    check(SUCCEEDED(bitmap->Lock(&corner, WICBitmapLockRead, first.GetAddressOf())) && SUCCEEDED(bitmap->Lock(nullptr, WICBitmapLockRead, second.GetAddressOf())) &&
          SUCCEEDED(first->GetDataPointer(&size, &data)) && data[0] == 50 && data[1] == 30, "read locks share; a lock of a rectangle points into it");
    std::vector<BYTE> pixels(size_t{width} * height * 4);
    check(SUCCEEDED(bitmap->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data())) && pixels[(10 * width + 20) * 4] == 20 &&
          pixels[(10 * width + 20) * 4 + 1] == 10, "CopyPixels reads what the lock wrote");
    const std::pair<const GUID*, UINT> expected[]{{&GUID_WICPixelFormat8bppGray, 8}, {&GUID_WICPixelFormat24bppRGB, 24}, {&GUID_WICPixelFormat32bppPRGBA, 32}};
    for (const auto& [format, bits] : expected) {
        ComPtr<IWICComponentInfo> info;
        ComPtr<IWICPixelFormatInfo> pixel;
        UINT value = 0;
        if (FAILED(wic->CreateComponentInfo(*format, info.GetAddressOf())) || FAILED(info.As(&pixel)) || FAILED(pixel->GetBitsPerPixel(&value)) || value != bits) {
            check(false, "IWICPixelFormatInfo::GetBitsPerPixel"); return;
        }
    }
    check(true, "IWICPixelFormatInfo::GetBitsPerPixel");
    ComPtr<IWICComponentInfo> none;
    check(wic->CreateComponentInfo(GUID_ContainerFormatPng, none.GetAddressOf()) == WINCODEC_ERR_COMPONENTNOTFOUND, "CreateComponentInfo(not a format)");
}

// Workers decode at once, each with its own factory and objects, as ImageDecoder's do.
void threads() {
    constexpr UINT width = 600, height = 900;
    auto source = picture(width, height, false);
    ComPtr<IWICImagingFactory> wic;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
    auto bytes = encode(wic.Get(), GUID_ContainerFormatJpeg, bitmap(wic.Get(), width, height, source).Get());
    const auto pipeline = [&](IWICImagingFactory* factory) {
        const auto frame = decode(factory, bytes);
        return pixelsOf(factory, scaler(factory, frame.Get(), 512, 768, WICBitmapInterpolationModeFant).Get(), GUID_WICPixelFormat32bppPBGRA);
    };
    const auto started = std::chrono::steady_clock::now();
    const auto reference = pipeline(wic.Get());
    std::printf("      600x900 JPEG -> 512x768 PBGRA (Fant): %.1f ms\n", milliseconds(started));
    std::vector<std::thread> workers;
    std::vector<int> mismatches(4);
    for (int t = 0; t < 4; ++t)
        workers.emplace_back([&, t] {
            const HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            ComPtr<IWICImagingFactory> own;
            CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&own));
            for (int i = 0; i < 10; ++i) mismatches[t] += pipeline(own.Get()) != reference;
            own.Reset();
            if (apartment == S_OK) CoUninitialize(); else ++mismatches[t];
        });
    for (auto& worker : workers) worker.join();
    check(!reference.empty() && mismatches == std::vector<int>(4, 0), "four threads decode and scale the same pixels");
}
} // namespace

int main(int argc, char** argv) {
    if (argc > 1) output_directory = argv[1];
    comRuntime();
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.GetAddressOf())))) return 1;
    streams();
    pngAndConversions(wic.Get());
    scaling(wic.Get());
    jpeg(wic.Get());
    webp(wic.Get());
    filesAndFailures(wic.Get());
    bitmaps(wic.Get());
    threads();
    wic.Reset();
    CoUninitialize();
    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "passed", failures);
    return failures ? 1 : 0;
}
