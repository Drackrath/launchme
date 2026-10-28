#include "image_codec.hpp"

#include <png.h>
#include <turbojpeg.h>
#include <webp/decode.h>

#include <algorithm>
#include <cmath>
#include <csetjmp>
#include <cstring>
#include <fstream>

// libpng (no gamma or colour-space conversion, like WIC), libjpeg-turbo's TurboJPEG API and
// libwebp. Scaling happens after decoding, in premultiplied alpha so transparent pixels do not
// bleed their colour into the edges of logos.
namespace image_codec {
namespace {
constexpr size_t max_file = 64 * 1024 * 1024;
constexpr uint32_t max_side = 65535;

Format sniff(std::span<const uint8_t> bytes) {
    static constexpr uint8_t png[]{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    if (bytes.size() >= 8 && std::equal(png, png + 8, bytes.begin())) return Format::png;
    if (bytes.size() >= 3 && bytes[0] == 0xff && bytes[1] == 0xd8 && bytes[2] == 0xff) return Format::jpeg;
    if (bytes.size() >= 12 && !std::memcmp(bytes.data(), "RIFF", 4) && !std::memcmp(bytes.data() + 8, "WEBP", 4)) return Format::webp;
    if (bytes.size() >= 26 && bytes[0] == 'B' && bytes[1] == 'M') return Format::other; // BMP.
    return Format::unknown;
}

// BMP, uncompressed (BI_RGB 1–32 bits, BI_BITFIELDS 16/32 bits) with a BITMAPINFOHEADER or later;
// like WIC, alpha only from an explicit alpha mask.
uint32_t little(std::span<const uint8_t> bytes, size_t at, int size) {
    uint32_t value = 0;
    for (int i = 0; i < size; ++i) value |= static_cast<uint32_t>(bytes[at + i]) << (8 * i);
    return value;
}
struct Bmp { Info info; uint32_t offset, header, bits, compression, colors; bool bottom_up; };
std::optional<Bmp> bmpHeader(std::span<const uint8_t> bytes) {
    if (bytes.size() < 54) return {};
    const uint32_t header = little(bytes, 14, 4);
    if (header < 40 || 14 + size_t{header} > bytes.size()) return {};
    const auto width = static_cast<int32_t>(little(bytes, 18, 4)), height = static_cast<int32_t>(little(bytes, 22, 4));
    const uint32_t bits = little(bytes, 28, 2), compression = little(bytes, 30, 4);
    if (width <= 0 || height == 0 || height == INT32_MIN || little(bytes, 26, 2) != 1) return {};
    if (!(compression == 0 && (bits == 1 || bits == 4 || bits == 8 || bits == 16 || bits == 24 || bits == 32)) &&
        !(compression == 3 && (bits == 16 || bits == 32))) return {};
    const uint32_t colors = bits <= 8 ? (little(bytes, 46, 4) ? little(bytes, 46, 4) : 1u << bits) : 0;
    return Bmp{{Format::other, static_cast<uint32_t>(width), static_cast<uint32_t>(height < 0 ? -height : height)},
               little(bytes, 10, 4), header, bits, compression, colors, height > 0};
}

std::optional<Image> decodeBmp(std::span<const uint8_t> bytes, const Fit& fit, std::optional<Size>& size) {
    const auto bmp = bmpHeader(bytes);
    if (!bmp || bmp->info.width > max_side || bmp->info.height > max_side || !(size = fit(bmp->info))) return {};
    const uint32_t width = bmp->info.width, height = bmp->info.height;
    const size_t stride = (size_t{width} * bmp->bits + 31) / 32 * 4;
    if (bmp->offset > bytes.size() || stride * height > bytes.size() - bmp->offset || (bmp->colors && bmp->colors > 256)) return {};
    // Masks: after a 40-byte header for BI_BITFIELDS, inside larger headers; 5:5:5 or 8:8:8 by default.
    uint32_t masks[4]{bmp->bits == 16 ? 0x7c00u : 0xff0000u, bmp->bits == 16 ? 0x3e0u : 0xff00u, bmp->bits == 16 ? 0x1fu : 0xffu, 0};
    if (bmp->compression == 3) {
        const size_t at = 14 + 40, count = bmp->header >= 56 ? 4 : 3;
        if (at + count * 4 > bytes.size()) return {};
        for (size_t i = 0; i < count; ++i) masks[i] = little(bytes, at + i * 4, 4);
    }
    const size_t palette = 14 + size_t{bmp->header};
    if (bmp->colors && palette + size_t{bmp->colors} * 4 > bytes.size()) return {};
    const auto channel = [](uint32_t pixel, uint32_t mask) -> uint8_t {
        if (!mask) return 255;
        int shift = 0; while (!(mask >> shift & 1)) ++shift;
        const uint32_t maximum = mask >> shift;
        return static_cast<uint8_t>(((pixel & mask) >> shift) * 255u / maximum);
    };
    Image image{Format::other, width, height, std::vector<uint8_t>(size_t{width} * height * 4)};
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* row = bytes.data() + bmp->offset + stride * (bmp->bottom_up ? height - 1 - y : y);
        uint8_t* out = image.pixels.data() + size_t{y} * width * 4;
        for (uint32_t x = 0; x < width; ++x, out += 4) {
            if (bmp->bits <= 8) {
                const uint32_t index = (row[x * bmp->bits / 8] >> (8 - bmp->bits - x * bmp->bits % 8)) & ((1u << bmp->bits) - 1);
                if (index >= bmp->colors) return {};
                std::copy_n(bytes.data() + palette + index * 4, 3, out); out[3] = 255;
            } else if (bmp->bits == 24) {
                std::copy_n(row + x * 3, 3, out); out[3] = 255;
            } else {
                const uint32_t pixel = bmp->bits == 16 ? little({row, stride}, x * 2, 2) : little({row, stride}, x * 4, 4);
                out[0] = channel(pixel, masks[2]); out[1] = channel(pixel, masks[1]); out[2] = channel(pixel, masks[0]);
                out[3] = masks[3] ? channel(pixel, masks[3]) : 255;
            }
        }
    }
    return image;
}

std::vector<uint8_t> readFile(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() <= 0 || static_cast<size_t>(input.tellg()) > max_file) return {};
    std::vector<uint8_t> bytes(static_cast<size_t>(input.tellg()));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) return {};
    return bytes;
}

// PNG. libpng reports errors with longjmp, so the function holding setjmp owns no objects with
// destructors; its buffers belong to the caller.
struct PngInput { std::span<const uint8_t> bytes; size_t at = 0; };
void pngRead(png_structp png, png_bytep out, size_t size) {
    auto* input = static_cast<PngInput*>(png_get_io_ptr(png));
    if (size > input->bytes.size() - input->at) png_error(png, "truncated");
    std::memcpy(out, input->bytes.data() + input->at, size);
    input->at += size;
}
void pngFail(png_structp png, png_const_charp) { png_longjmp(png, 1); }
void pngWarn(png_structp, png_const_charp) {}

bool pngHeader(png_structp png, png_infop info, PngInput& input, Info& header) {
    if (setjmp(png_jmpbuf(png))) return false;
    png_set_read_fn(png, &input, pngRead);
    png_read_info(png, info);
    header = {Format::png, png_get_image_width(png, info), png_get_image_height(png, info)};
    return true;
}
bool pngPixels(png_structp png, png_infop info, uint32_t width, uint32_t height, std::vector<uint8_t>& pixels, std::vector<png_bytep>& rows) {
    if (setjmp(png_jmpbuf(png))) return false;
    // Everything to 8-bit BGRA: palettes, low bit depths and tRNS expanded, 16 bits rounded.
    png_set_expand(png);
    png_set_scale_16(png);
    png_set_gray_to_rgb(png);
    png_set_bgr(png);
    png_set_add_alpha(png, 0xff, PNG_FILLER_AFTER);
    png_set_interlace_handling(png);
    png_read_update_info(png, info);
    if (png_get_rowbytes(png, info) != size_t(width) * 4) return false;
    for (uint32_t y = 0; y < height; ++y) rows[y] = pixels.data() + size_t(y) * width * 4;
    png_read_image(png, rows.data());
    return true;
}

struct PngReader {
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, pngFail, pngWarn);
    png_infop info = png ? png_create_info_struct(png) : nullptr;
    ~PngReader() { png_destroy_read_struct(&png, info ? &info : nullptr, nullptr); }
};

std::optional<Info> probePng(std::span<const uint8_t> bytes) {
    PngReader reader;
    PngInput input{bytes};
    Info header;
    if (!reader.info || !pngHeader(reader.png, reader.info, input, header)) return {};
    return header;
}

std::optional<Image> decodePng(std::span<const uint8_t> bytes, const Fit& fit, std::optional<Size>& size) {
    PngReader reader;
    PngInput input{bytes};
    Info header;
    if (!reader.info || !pngHeader(reader.png, reader.info, input, header) || !header.width || !header.height ||
        header.width > max_side || header.height > max_side || !(size = fit(header))) return {};
    Image image{Format::png, header.width, header.height, std::vector<uint8_t>(size_t(header.width) * header.height * 4)};
    std::vector<png_bytep> rows(header.height);
    if (!pngPixels(reader.png, reader.info, header.width, header.height, image.pixels, rows)) return {};
    return image;
}

// JPEG.
struct Turbo {
    tjhandle handle;
    explicit Turbo(int kind) : handle(tj3Init(kind)) {}
    ~Turbo() { tj3Destroy(handle); }
};

std::optional<Info> probeJpeg(std::span<const uint8_t> bytes) {
    Turbo turbo(TJINIT_DECOMPRESS);
    if (!turbo.handle || tj3DecompressHeader(turbo.handle, bytes.data(), bytes.size()) != 0) return {};
    const int width = tj3Get(turbo.handle, TJPARAM_JPEGWIDTH), height = tj3Get(turbo.handle, TJPARAM_JPEGHEIGHT);
    if (width <= 0 || height <= 0) return {};
    return Info{Format::jpeg, static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
}

std::optional<Image> decodeJpeg(std::span<const uint8_t> bytes, const Fit& fit, std::optional<Size>& size) {
    Turbo turbo(TJINIT_DECOMPRESS);
    if (!turbo.handle || tj3DecompressHeader(turbo.handle, bytes.data(), bytes.size()) != 0) return {};
    const int width = tj3Get(turbo.handle, TJPARAM_JPEGWIDTH), height = tj3Get(turbo.handle, TJPARAM_JPEGHEIGHT);
    const Info header{Format::jpeg, static_cast<uint32_t>(std::max(width, 0)), static_cast<uint32_t>(std::max(height, 0))};
    if (width <= 0 || height <= 0 || header.width > max_side || header.height > max_side || !(size = fit(header))) return {};
    Image image{Format::jpeg, header.width, header.height, std::vector<uint8_t>(size_t(header.width) * header.height * 4)};
    // A truncated or damaged file is refused (TurboJPEG reports it as a warning).
    const int colorspace = tj3Get(turbo.handle, TJPARAM_COLORSPACE);
    if (colorspace != TJCS_CMYK && colorspace != TJCS_YCCK)
        return tj3Decompress8(turbo.handle, bytes.data(), bytes.size(), image.pixels.data(), 0, TJPF_BGRA) == 0 ? std::optional(std::move(image)) : std::nullopt;
    // CMYK as Photoshop writes it (inverted, Adobe marker), which TurboJPEG leaves to the caller.
    if (tj3Decompress8(turbo.handle, bytes.data(), bytes.size(), image.pixels.data(), 0, TJPF_CMYK) != 0) return {};
    for (size_t i = 0; i < image.pixels.size(); i += 4) {
        uint8_t* p = image.pixels.data() + i;
        const unsigned c = p[0], m = p[1], y = p[2], k = p[3];
        p[0] = static_cast<uint8_t>((y * k + 127) / 255);
        p[1] = static_cast<uint8_t>((m * k + 127) / 255);
        p[2] = static_cast<uint8_t>((c * k + 127) / 255);
        p[3] = 255;
    }
    return image;
}

// WebP: the first (only) frame; animations are refused.
std::optional<Info> probeWebp(std::span<const uint8_t> bytes) {
    WebPBitstreamFeatures features{};
    if (WebPGetFeatures(bytes.data(), bytes.size(), &features) != VP8_STATUS_OK || features.has_animation ||
        features.width <= 0 || features.height <= 0) return {};
    return Info{Format::webp, static_cast<uint32_t>(features.width), static_cast<uint32_t>(features.height)};
}

std::optional<Image> decodeWebp(std::span<const uint8_t> bytes, const Fit& fit, std::optional<Size>& size) {
    const auto header = probeWebp(bytes);
    if (!header || !(size = fit(*header))) return {};
    Image image{Format::webp, header->width, header->height, std::vector<uint8_t>(size_t(header->width) * header->height * 4)};
    if (!WebPDecodeBGRAInto(bytes.data(), bytes.size(), image.pixels.data(), image.pixels.size(), static_cast<int>(header->width * 4))) return {};
    return image;
}

// Scaling, separably, with weights per output pixel. Fant: the area each output pixel covers
// (bilinear when enlarging); cubic: Catmull-Rom widened by the reduction factor.
struct Tap { uint32_t first; std::vector<float> weights; };
std::vector<Tap> taps(uint32_t from, uint32_t to, Filter filter) {
    std::vector<Tap> result(to);
    const double scale = static_cast<double>(from) / to;
    for (uint32_t x = 0; x < to; ++x) {
        const double center = (x + 0.5) * scale;
        double begin, end;
        std::vector<double> weights;
        if (filter == Filter::fant && scale >= 1) {
            begin = std::floor(x * scale); end = std::ceil((x + 1) * scale);
            for (double s = begin; s < end; ++s) weights.push_back(std::min(s + 1, (x + 1) * scale) - std::max(s, x * scale));
        } else {
            const double width = std::max(1.0, scale), support = (filter == Filter::cubic ? 2.0 : 1.0) * width;
            begin = std::floor(center - support); end = std::ceil(center + support);
            for (double s = begin; s < end; ++s) {
                const double t = std::abs((s + 0.5 - center) / width);
                double w;
                if (filter == Filter::cubic) w = t < 1 ? 1.5 * t * t * t - 2.5 * t * t + 1 : t < 2 ? -0.5 * t * t * t + 2.5 * t * t - 4 * t + 2 : 0;
                else w = std::max(0.0, 1 - t);
                weights.push_back(w);
            }
        }
        // Clamp to the edges, folding outside weights onto the border pixels.
        Tap& tap = result[x];
        const int64_t low = std::clamp<int64_t>(static_cast<int64_t>(begin), 0, from - 1);
        const int64_t high = std::clamp<int64_t>(static_cast<int64_t>(end) - 1, 0, from - 1);
        tap.first = static_cast<uint32_t>(low);
        tap.weights.assign(static_cast<size_t>(high - low + 1), 0.0f);
        double total = 0;
        for (size_t i = 0; i < weights.size(); ++i) {
            const int64_t s = std::clamp<int64_t>(static_cast<int64_t>(begin) + static_cast<int64_t>(i), low, high);
            tap.weights[static_cast<size_t>(s - low)] += static_cast<float>(weights[i]);
            total += weights[i];
        }
        if (total != 0) for (auto& w : tap.weights) w = static_cast<float>(w / total);
    }
    return result;
}

Image scale(const Image& source, Size size, Filter filter) {
    const uint32_t w = source.width, h = source.height;
    // Premultiplied floats, rows first then columns.
    std::vector<float> premultiplied(size_t(w) * h * 4);
    for (size_t i = 0; i < premultiplied.size(); i += 4) {
        const float a = source.pixels[i + 3] / 255.0f;
        for (int c = 0; c < 3; ++c) premultiplied[i + c] = source.pixels[i + c] * a;
        premultiplied[i + 3] = source.pixels[i + 3];
    }
    const auto across = taps(w, size.width, filter), down = taps(h, size.height, filter);
    std::vector<float> rows(size_t(size.width) * h * 4);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < size.width; ++x) {
            float sum[4]{};
            const float* in = premultiplied.data() + (size_t(y) * w + across[x].first) * 4;
            for (size_t i = 0; i < across[x].weights.size(); ++i)
                for (int c = 0; c < 4; ++c) sum[c] += in[i * 4 + c] * across[x].weights[i];
            std::copy(sum, sum + 4, rows.data() + (size_t(y) * size.width + x) * 4);
        }
    Image result{source.format, size.width, size.height, std::vector<uint8_t>(size_t(size.width) * size.height * 4)};
    for (uint32_t y = 0; y < size.height; ++y)
        for (uint32_t x = 0; x < size.width; ++x) {
            float sum[4]{};
            for (size_t i = 0; i < down[y].weights.size(); ++i) {
                const float* in = rows.data() + ((down[y].first + i) * size.width + x) * 4;
                for (int c = 0; c < 4; ++c) sum[c] += in[c] * down[y].weights[i];
            }
            uint8_t* out = result.pixels.data() + (size_t(y) * size.width + x) * 4;
            const float alpha = std::clamp(sum[3], 0.0f, 255.0f);
            out[3] = static_cast<uint8_t>(alpha + 0.5f);
            for (int c = 0; c < 3; ++c)
                out[c] = out[3] ? static_cast<uint8_t>(std::clamp(sum[c] * 255.0f / alpha, 0.0f, 255.0f) + 0.5f) : 0;
        }
    return result;
}

void premultiply(Image& image) {
    for (size_t i = 0; i < image.pixels.size(); i += 4) {
        const unsigned a = image.pixels[i + 3];
        for (int c = 0; c < 3; ++c) image.pixels[i + c] = static_cast<uint8_t>((image.pixels[i + c] * a + 127) / 255);
    }
}

bool valid(const Image& image) {
    return image.width && image.height && image.width <= max_side && image.height <= max_side &&
        image.pixels.size() == size_t(image.width) * image.height * 4;
}
} // namespace

std::optional<Info> probe(std::span<const uint8_t> bytes) {
    switch (sniff(bytes)) {
    case Format::png: return probePng(bytes);
    case Format::jpeg: return probeJpeg(bytes);
    case Format::webp: return probeWebp(bytes);
    case Format::other: if (const auto bmp = bmpHeader(bytes)) return bmp->info; return {};
    default: return {};
    }
}

std::optional<Info> probeFile(const std::filesystem::path& file) { return probe(readFile(file)); }

std::optional<Image> decode(std::span<const uint8_t> bytes, const Fit& fit, Filter filter, bool premultiplied) {
    std::optional<Size> size;
    std::optional<Image> image;
    switch (sniff(bytes)) {
    case Format::png: image = decodePng(bytes, fit, size); break;
    case Format::jpeg: image = decodeJpeg(bytes, fit, size); break;
    case Format::webp: image = decodeWebp(bytes, fit, size); break;
    case Format::other: image = decodeBmp(bytes, fit, size); break;
    default: return {};
    }
    if (!image || !size || !size->width || !size->height) return {};
    if (size->width != image->width || size->height != image->height) {
        if (size->width > max_side || size->height > max_side) return {};
        image = scale(*image, *size, filter);
    }
    if (premultiplied) premultiply(*image);
    return image;
}

std::optional<Image> decodeFile(const std::filesystem::path& file, const Fit& fit, Filter filter, bool premultiplied) {
    return decode(readFile(file), fit, filter, premultiplied);
}

std::vector<uint8_t> encodePng(const Image& image) {
    if (!valid(image)) return {};
    png_image png{};
    png.version = PNG_IMAGE_VERSION;
    png.width = image.width;
    png.height = image.height;
    png.format = PNG_FORMAT_BGRA;
    size_t bytes = 0;
    if (!png_image_write_get_memory_size(png, bytes, 0, image.pixels.data(), 0, nullptr)) { png_image_free(&png); return {}; }
    std::vector<uint8_t> result(bytes);
    if (!png_image_write_to_memory(&png, result.data(), &bytes, 0, image.pixels.data(), 0, nullptr)) { png_image_free(&png); return {}; }
    result.resize(bytes);
    return result;
}

std::vector<uint8_t> encodeJpeg(const Image& image, float quality) {
    if (!valid(image)) return {};
    Turbo turbo(TJINIT_COMPRESS);
    unsigned char* buffer = nullptr;
    size_t size = 0;
    if (!turbo.handle || tj3Set(turbo.handle, TJPARAM_QUALITY, std::clamp(static_cast<int>(std::lround(quality * 100)), 1, 100)) != 0 ||
        tj3Set(turbo.handle, TJPARAM_SUBSAMP, TJSAMP_420) != 0 ||
        tj3Compress8(turbo.handle, image.pixels.data(), static_cast<int>(image.width), 0, static_cast<int>(image.height), TJPF_BGRA, &buffer, &size) != 0) {
        tj3Free(buffer);
        return {};
    }
    std::vector<uint8_t> result(buffer, buffer + size);
    tj3Free(buffer);
    return result;
}
} // namespace image_codec
