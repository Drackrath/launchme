#pragma once
#include <cstdint>
#include <vector>

// BC1 (DXT1) block compression of decoded artwork: 4 bits a pixel on the GPU instead of 32
// (AppSettings::low_memory). Opaque images only; one 8-byte block per 4×4 pixels, row by row,
// two 5:6:5 end colours (the first the larger, so every block is in four-colour mode) and
// 2-bit indices, the first pixel in the lowest bits.
namespace block_compression {
// Of premultiplied 32bpp BGRA pixels (stride width * 4). Empty when width or height is not a
// multiple of 4 or any pixel is not fully opaque.
std::vector<uint8_t> encodeBc1(const uint8_t* bgra, uint32_t width, uint32_t height);
// The pixels a GPU samples from such blocks (BGRA, opaque); empty for a size that does not match.
std::vector<uint8_t> decodeBc1(const uint8_t* blocks, size_t size, uint32_t width, uint32_t height);
inline size_t bc1Bytes(uint32_t width, uint32_t height) { return static_cast<size_t>(width / 4) * (height / 4) * 8; }
} // namespace block_compression
