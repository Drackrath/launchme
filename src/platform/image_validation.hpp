#pragma once
#include "../artwork/image_codec.hpp"
#include <optional>
#include <span>
#include <vector>
#include <cstdint>

namespace image_validation {
struct Info {
    uint32_t width = 0, height = 0;
    bool png = false;
    size_t transparent = 0, opaque = 0;
    size_t pixels() const { return static_cast<size_t>(width) * height; }
    bool hasTransparency() const { return transparent > 0 && opaque > 0 && transparent * 100 >= pixels(); }
};
// Decode pixels, rather than trusting the URL, extension, or a PNG container whose actual alpha
// channel is fully opaque.
inline std::optional<Info> inspect(std::span<const uint8_t> bytes) {
    if (bytes.empty() || bytes.size() > 12 * 1024 * 1024) return {};
    const auto image = image_codec::decode(bytes, [](const image_codec::Info& stored) -> std::optional<image_codec::Size> {
        if (stored.width > 4096 || stored.height > 4096) return {};
        return image_codec::Size{stored.width, stored.height};
    });
    if (!image) return {};
    Info info{image->width, image->height, image->format == image_codec::Format::png};
    for (size_t i = 3; i < image->pixels.size(); i += 4) { if (!image->pixels[i]) ++info.transparent; if (image->pixels[i] == 255) ++info.opaque; }
    return info;
}
}
