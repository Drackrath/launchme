#pragma once
#include <cstdint>
#include <vector>

// Block compression of decoded artwork for the GPU (AppSettings::texture_compression): one block per
// 4×4 pixels, row by row.
// - BC1 (DXT1), opaque pictures: 4 bits a pixel instead of 32. Two 5:6:5 end colours (the first the
//   larger, so every block is in four-colour mode) and 2-bit indices, the first pixel in the lowest bits.
// - BC3 (DXT5), pictures with alpha (logos): 8 bits a pixel. An alpha block (two end values and 3-bit
//   indices) followed by a BC1 colour block of the premultiplied colours (always four-colour).
// The encoder fits each block's colour line (its range, then a least-squares refinement and, for a block
// still far off, its principal axis): better than a plain range fit on gradients, at about twice its cost;
// the results are kept on disk (src/ui/texture_cache.hpp), so a picture is compressed once.
namespace block_compression {
enum class Format : uint8_t { none = 0, bc1 = 1, bc3 = 3 };
// Of premultiplied 32bpp BGRA pixels (stride width * 4). Empty when width or height is not a
// multiple of 4 or any pixel is not fully opaque.
std::vector<uint8_t> encodeBc1(const uint8_t* bgra, uint32_t width, uint32_t height);
// Of premultiplied 32bpp BGRA pixels with any alpha. Empty when width or height is not a multiple of 4.
std::vector<uint8_t> encodeBc3(const uint8_t* bgra, uint32_t width, uint32_t height);
// The pixels a GPU samples from such blocks (premultiplied BGRA); empty for a size that does not match.
std::vector<uint8_t> decodeBc1(const uint8_t* blocks, size_t size, uint32_t width, uint32_t height);
std::vector<uint8_t> decodeBc3(const uint8_t* blocks, size_t size, uint32_t width, uint32_t height);
inline size_t bc1Bytes(uint32_t width, uint32_t height) { return static_cast<size_t>(width / 4) * (height / 4) * 8; }
inline size_t bc3Bytes(uint32_t width, uint32_t height) { return static_cast<size_t>(width / 4) * (height / 4) * 16; }
inline size_t blockBytes(Format format, uint32_t width, uint32_t height) {
    return format == Format::bc1 ? bc1Bytes(width, height) : format == Format::bc3 ? bc3Bytes(width, height) : 0;
}
} // namespace block_compression
