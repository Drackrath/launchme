#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <vector>

// Still images for the artwork and network code: WIC on Windows (image_codec_wic.cpp), libpng,
// libjpeg-turbo and libwebp elsewhere (image_codec_linux.cpp). Pixels are 32-bit BGRA in packed
// rows (stride = width * 4), straight alpha unless premultiplied is asked for. Thread-safe; on
// Windows each call joins the thread's COM apartment (or starts a multithreaded one).
namespace image_codec {
// Only PNG and JPEG are told apart on Windows (the callers' rules need no more); WebP, GIF, BMP
// and the rest WIC decodes are `other`. Linux decodes PNG, JPEG, WebP and BMP (`other`).
enum class Format { unknown, png, jpeg, webp, other };
struct Size { uint32_t width = 0, height = 0; };
struct Info { Format format = Format::unknown; uint32_t width = 0, height = 0; };
struct Image {
    Format format = Format::unknown; // Of the encoded source.
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> pixels;     // BGRA, width * height * 4 bytes.
};
// WIC's Fant (area averaging) for checks and thumbnails; high-quality cubic for re-encoding.
enum class Filter { fant, cubic };
// The size to decode to, from the stored size; nullopt refuses the image before its pixels are
// decoded (size limits). Returning the stored size keeps it unscaled.
using Fit = std::function<std::optional<Size>(const Info&)>;
inline std::optional<Size> original(const Info& info) { return Size{info.width, info.height}; }

// The header only: format and size.
std::optional<Info> probe(std::span<const uint8_t> bytes);
std::optional<Info> probeFile(const std::filesystem::path& file);
// The first frame, scaled to fit's size. Nullopt for anything unreadable or truncated.
std::optional<Image> decode(std::span<const uint8_t> bytes, const Fit& fit = original, Filter filter = Filter::fant, bool premultiplied = false);
std::optional<Image> decodeFile(const std::filesystem::path& file, const Fit& fit = original, Filter filter = Filter::fant, bool premultiplied = false);
// Straight-alpha BGRA to an RGBA PNG, or to a 4:2:0 JPEG (alpha dropped) at quality 0–1 (WIC's
// ImageQuality). Empty on failure.
std::vector<uint8_t> encodePng(const Image& image);
std::vector<uint8_t> encodeJpeg(const Image& image, float quality);
} // namespace image_codec
