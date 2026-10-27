#include "image_compact.hpp"
#include "text_crop.hpp"
#include "../platform/atomic_file.hpp"

#ifdef _WIN32
#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#endif

#include <algorithm>
#include <cwctype>
#include <fstream>

using Microsoft::WRL::ComPtr;

namespace {
struct Box { uint32_t width, height; };
// What the launcher draws at most (a 2560 px wide window, shelves, Flix, XMB, details).
Box limit(ImageRole role) {
    switch (role) {
    case ImageRole::background: return {2560, 1440};
    case ImageRole::landscape: return {1280, 720};
    case ImageRole::cover: return {1024, 1024};
    case ImageRole::poster: return {800, 1200};
    default: return {1280, 720}; // Logos.
    }
}

struct Com {
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~Com() { if (SUCCEEDED(result)) CoUninitialize(); }
};

std::wstring lower(std::wstring text) {
    for (auto& ch : text) ch = static_cast<wchar_t>(std::towlower(ch));
    return text;
}

std::vector<uint8_t> readFile(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() <= 0 || input.tellg() > 64 * 1024 * 1024) return {};
    std::vector<uint8_t> bytes(static_cast<size_t>(input.tellg()));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) return {};
    return bytes;
}
}

double compactScale(uint32_t width, uint32_t height, ImageRole role) {
    if (!width || !height) return 1.0;
    const auto box = limit(role);
    const double across = static_cast<double>(box.width) / width, down = static_cast<double>(box.height) / height;
    // A background is drawn cover-fit (it fills the screen and its sides are cut off), so it keeps
    // what still covers the box: a 3840×1240 hero stays whole, 2560 px wide it would cover a 16:9
    // screen only 827 px high and look soft. Everything else fits inside its box.
    return std::min(1.0, role == ImageRole::background ? std::max(across, down) : std::min(across, down));
}

std::vector<uint8_t> compactImage(std::span<const uint8_t> bytes, ImageRole role) {
    // PNG and JPEG only: other files are left as they are.
    const bool png = bytes.size() > 8 && bytes[0] == 0x89 && bytes[1] == 'P';
    const bool jpeg = bytes.size() > 3 && bytes[0] == 0xff && bytes[1] == 0xd8;
    if ((!png && !jpeg) || bytes.size() > 64 * 1024 * 1024 || bytes.size() < 96 * 1024) return {};
    Com com;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> input;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateStream(&input)) ||
        FAILED(input->InitializeFromMemory(const_cast<BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size()))) ||
        FAILED(factory->CreateDecoderFromStream(input.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame))) return {};
    UINT width = 0, height = 0;
    if (FAILED(frame->GetSize(&width, &height)) || !width || !height || width > 16384 || height > 16384) return {};
    const double scale = compactScale(width, height, role);
    // A JPEG already within the limits gains nothing from a second encoding.
    if (jpeg && scale >= 1.0) return {};
    const UINT target_width = std::max<UINT>(1, static_cast<UINT>(width * scale + 0.5));
    const UINT target_height = std::max<UINT>(1, static_cast<UINT>(height * scale + 0.5));
    ComPtr<IWICBitmapSource> source = frame;
    if (scale < 1.0) {
        ComPtr<IWICBitmapScaler> scaler;
        if (FAILED(factory->CreateBitmapScaler(&scaler)) ||
            FAILED(scaler->Initialize(frame.Get(), target_width, target_height, WICBitmapInterpolationModeHighQualityCubic))) return {};
        source = scaler;
    }
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(source.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return {};
    std::vector<uint8_t> pixels(size_t(target_width) * target_height * 4);
    if (FAILED(converter->CopyPixels(nullptr, target_width * 4, static_cast<UINT>(pixels.size()), pixels.data()))) return {};
    bool transparent = false;
    for (size_t i = 3; i < pixels.size(); i += 4) if (pixels[i] < 250) { transparent = true; break; }
    ComPtr<IWICBitmap> bitmap;
    if (FAILED(factory->CreateBitmapFromMemory(target_width, target_height, GUID_WICPixelFormat32bppBGRA, target_width * 4,
                                               static_cast<UINT>(pixels.size()), pixels.data(), &bitmap))) return {};
    ComPtr<IStream> output;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &output))) return {};
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> target;
    ComPtr<IPropertyBag2> options;
    if (!output || FAILED(factory->CreateEncoder(transparent ? GUID_ContainerFormatPng : GUID_ContainerFormatJpeg, nullptr, &encoder)) ||
        FAILED(encoder->Initialize(output.Get(), WICBitmapEncoderNoCache)) || FAILED(encoder->CreateNewFrame(&target, &options))) return {};
    if (!transparent) {
        PROPBAG2 option{}; option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
        VARIANT value{}; value.vt = VT_R4; value.fltVal = 0.88f;
        options->Write(1, &option, &value);
    }
    if (FAILED(target->Initialize(options.Get())) || FAILED(target->SetSize(target_width, target_height))) return {};
    ComPtr<IWICBitmapSource> written = bitmap;
    if (!transparent) { // The JPEG encoder takes 24-bit BGR.
        ComPtr<IWICFormatConverter> opaque;
        if (FAILED(factory->CreateFormatConverter(&opaque)) ||
            FAILED(opaque->Initialize(bitmap.Get(), GUID_WICPixelFormat24bppBGR, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return {};
        written = opaque;
    }
    if (FAILED(target->WriteSource(written.Get(), nullptr)) || FAILED(target->Commit()) || FAILED(encoder->Commit())) return {};
    STATSTG stat{};
    if (FAILED(output->Stat(&stat, STATFLAG_NONAME)) || !stat.cbSize.QuadPart || stat.cbSize.QuadPart > bytes.size()) return {};
    std::vector<uint8_t> result(static_cast<size_t>(stat.cbSize.QuadPart));
    LARGE_INTEGER start{};
    ULONG read = 0;
    if (FAILED(output->Seek(start, STREAM_SEEK_SET, nullptr)) || FAILED(output->Read(result.data(), static_cast<ULONG>(result.size()), &read)) ||
        read != result.size()) return {};
    // Worth it only when clearly smaller.
    return result.size() < bytes.size() * 9 / 10 ? result : std::vector<uint8_t>{};
}

bool imageRoleFor(const std::filesystem::path& file, ImageRole& role) {
    if (lower(file.extension().wstring()) != L".img") return false;
    const auto path = lower(file.lexically_normal().wstring());
    const auto name = lower(file.filename().wstring());
    const auto has = [&](std::wstring_view part) { return path.find(part) != std::wstring::npos; };
    // Achievement icons, browser thumbnails and platform logos stay as they are.
    if (has(L"\\thumbnails\\") || has(L"\\achievements\\") || has(L"\\platforms\\") || has(L"\\avatars\\")) return false;
    if (has(L"\\logos\\") || has(L"\\logo\\") || name.starts_with(L"logo")) { role = ImageRole::logo; return true; }
    if (has(L"\\heroes\\") || has(L"\\hero\\") || has(L"\\backgrounds\\") || name.starts_with(L"hero")) { role = ImageRole::background; return true; }
    if (has(L"\\landscapes\\") || has(L"\\landscape\\") || name.starts_with(L"landscape")) { role = ImageRole::landscape; return true; }
    if (has(L"\\posters\\") || has(L"\\poster\\") || name.starts_with(L"poster")) { role = ImageRole::poster; return true; }
    if (has(L"\\covers\\") || has(L"\\cover\\") || name.starts_with(L"cover")) { role = ImageRole::cover; return true; }
    if (has(L"\\discs\\") || has(L"\\disc\\") || name.starts_with(L"disc")) { role = ImageRole::cover; return true; } // Square; alpha is preserved by the encoder.
    return false;
}

namespace { thread_local bool missing_images_only = false; }
MissingImagesOnly::MissingImagesOnly() : previous_(missing_images_only) { missing_images_only = true; }
MissingImagesOnly::~MissingImagesOnly() { missing_images_only = previous_; }
bool writeCachedImage(const std::filesystem::path& file, std::span<const uint8_t> bytes) {
    ImageRole role{};
    if (imageRoleFor(file, role))
        if (const auto compact = compactImage(bytes, role); !compact.empty()) return atomic_file::write(file, compact, !missing_images_only);
    return atomic_file::write(file, bytes, !missing_images_only);
}

size_t releaseShrunkenBackgrounds(const std::filesystem::path& root, const std::function<bool()>& keep_going) {
    size_t removed = 0;
    // Steam games' heroes\ and other stores' stores\<source>\<key>\hero.v2.img. Picks
    // (artwork\selections) and ES-DE copies (emulation\) are not downloaded again by themselves.
    for (const auto* folder : {L"heroes", L"stores"}) {
        std::error_code ec;
        for (auto item = std::filesystem::recursive_directory_iterator(root / folder, ec); !ec && item != std::filesystem::recursive_directory_iterator(); item.increment(ec)) {
            if (!keep_going()) return removed;
            std::error_code file;
            ImageRole role{};
            if (!item->is_regular_file(file) || !imageRoleFor(item->path(), role) || role != ImageRole::background) continue;
            const auto [width, height] = cropImageSize(item->path());
            if (width != 2560 || height <= 0 || height >= 1440 || manualTextCrop(item->path())) continue;
            auto focus = item->path(); focus.replace_extension(L".focus");
            if (std::filesystem::remove(item->path(), file)) { ++removed; std::filesystem::remove(focus, file); }
        }
    }
    return removed;
}

uint64_t compactImageCache(const std::filesystem::path& root, const std::function<bool()>& keep_going) {
    uint64_t saved = 0;
    for (const auto* folder : {L"heroes", L"logos", L"covers", L"posters", L"landscapes", L"stores", L"emulation", L"artwork"}) {
        std::error_code ec;
        for (auto item = std::filesystem::recursive_directory_iterator(root / folder, ec); !ec && item != std::filesystem::recursive_directory_iterator(); item.increment(ec)) {
            if (!keep_going()) return saved;
            std::error_code file;
            ImageRole role{};
            if (!item->is_regular_file(file) || !imageRoleFor(item->path(), role)) continue;
            const auto bytes = readFile(item->path());
            if (bytes.empty()) continue;
            const auto compact = compactImage(bytes, role);
            if (!compact.empty() && writeImageWithoutManualCrop(item->path(), compact)) saved += bytes.size() - compact.size();
        }
    }
    return saved;
}
