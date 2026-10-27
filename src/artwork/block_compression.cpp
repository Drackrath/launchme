#include "block_compression.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace block_compression {
namespace {
struct Pixels { int r[16], g[16], b[16]; };
struct Block { uint16_t c0 = 0, c1 = 0; uint32_t indices = 0; int error = 0; };

int quantize(int value, int levels) { return (std::clamp(value, 0, 255) * levels + 127) / 255; }
uint16_t pack565(int r, int g, int b) { return static_cast<uint16_t>(quantize(r, 31) << 11 | quantize(g, 63) << 5 | quantize(b, 31)); }
void unpack565(uint16_t c, int& r, int& g, int& b) {
    r = (c >> 11) & 31; g = (c >> 5) & 63; b = c & 31;
    r = r << 3 | r >> 2; g = g << 2 | g >> 4; b = b << 3 | b >> 2;
}

// Four-colour mode (c0 > c1): the end colours and the two thirds between them, each pixel the
// nearest of the four by its place along the line between the ends (equal ends: three-colour
// mode, where index 0 is still c0).
Block fit(const Pixels& p, uint16_t c0, uint16_t c1) {
    Block block;
    if (c0 < c1) std::swap(c0, c1);
    block.c0 = c0; block.c1 = c1;
    int ar, ag, ab, br, bg, bb;
    unpack565(c0, ar, ag, ab); unpack565(c1, br, bg, bb);
    const int pr[4] = {ar, br, (2 * ar + br + 1) / 3, (ar + 2 * br + 1) / 3};
    const int pg[4] = {ag, bg, (2 * ag + bg + 1) / 3, (ag + 2 * bg + 1) / 3};
    const int pb[4] = {ab, bb, (2 * ab + bb + 1) / 3, (ab + 2 * bb + 1) / 3};
    const int dr = br - ar, dg = bg - ag, db = bb - ab;
    const int length = dr * dr + dg * dg + db * db;
    const bool line = c0 != c1 && length > 0;
    for (int i = 0; i < 16; ++i) {
        int k = 0;
        if (line) {
            const int t = ((p.r[i] - ar) * dr + (p.g[i] - ag) * dg + (p.b[i] - ab) * db) * 6;
            k = t < length ? 0 : t < 3 * length ? 2 : t < 5 * length ? 3 : 1;
        }
        const int er = p.r[i] - pr[k], eg = p.g[i] - pg[k], eb = p.b[i] - pb[k];
        block.indices |= static_cast<uint32_t>(k) << (2 * i);
        block.error += er * er + eg * eg + eb * eb;
    }
    return block;
}

// End colours that minimise the squared error for the indices chosen (least squares); false when
// every pixel took the same weight.
bool refine(const Pixels& p, const Block& block, uint16_t& c0, uint16_t& c1) {
    static constexpr float weight[4] = {1.0f, 0.0f, 2.0f / 3, 1.0f / 3};
    float aa = 0, bb = 0, ab = 0, ax[3]{}, bx[3]{};
    for (int i = 0; i < 16; ++i) {
        const float a = weight[(block.indices >> (2 * i)) & 3], b = 1 - a;
        aa += a * a; bb += b * b; ab += a * b;
        const float v[3] = {static_cast<float>(p.r[i]), static_cast<float>(p.g[i]), static_cast<float>(p.b[i])};
        for (int c = 0; c < 3; ++c) { ax[c] += v[c] * a; bx[c] += v[c] * b; }
    }
    const float det = aa * bb - ab * ab;
    if (std::abs(det) < 1e-4f) return false;
    int end0[3], end1[3];
    for (int c = 0; c < 3; ++c) {
        end0[c] = static_cast<int>(std::lround((ax[c] * bb - bx[c] * ab) / det));
        end1[c] = static_cast<int>(std::lround((bx[c] * aa - ax[c] * ab) / det));
    }
    c0 = pack565(end0[0], end0[1], end0[2]);
    c1 = pack565(end1[0], end1[1], end1[2]);
    return true;
}

// Real-time range fit (after J.M.P. van Waveren): the block's colour box, inset by a sixteenth, along
// the diagonal that follows how red and blue vary with green; one least-squares pass only where
// that fit is clearly off. Integer arithmetic: about the cost of decoding the picture.
Block encodeBlock(const Pixels& p) {
    int low[3] = {255, 255, 255}, high[3] = {0, 0, 0}, sum[3] = {0, 0, 0}, red_green = 0, blue_green = 0;
    for (int i = 0; i < 16; ++i) {
        const int r = p.r[i], g = p.g[i], b = p.b[i];
        low[0] = std::min(low[0], r); low[1] = std::min(low[1], g); low[2] = std::min(low[2], b);
        high[0] = std::max(high[0], r); high[1] = std::max(high[1], g); high[2] = std::max(high[2], b);
        sum[0] += r; sum[1] += g; sum[2] += b;
        red_green += r * g; blue_green += b * g;
    }
    if (low[0] == high[0] && low[1] == high[1] && low[2] == high[2]) { const auto c = pack565(low[0], low[1], low[2]); return fit(p, c, c); }
    red_green = red_green * 16 - sum[0] * sum[1]; // 256 × the covariances.
    blue_green = blue_green * 16 - sum[2] * sum[1];
    for (int c = 0; c < 3; ++c) { const int inset = (high[c] - low[c]) >> 4; low[c] += inset; high[c] -= inset; }
    if (red_green < 0) std::swap(low[0], high[0]);
    if (blue_green < 0) std::swap(low[2], high[2]);
    Block best = fit(p, pack565(high[0], high[1], high[2]), pack565(low[0], low[1], low[2]));
    if (best.error > 16 * 40) { // Several levels a pixel off.
        uint16_t c0 = 0, c1 = 0;
        if (refine(p, best, c0, c1)) {
            const auto candidate = fit(p, c0, c1);
            if (candidate.error < best.error) best = candidate;
        }
    }
    return best;
}
} // namespace

std::vector<uint8_t> encodeBc1(const uint8_t* bgra, uint32_t width, uint32_t height) {
    if (!bgra || !width || !height || width % 4 || height % 4) return {};
    const size_t stride = static_cast<size_t>(width) * 4;
    for (size_t i = 3, end = stride * height; i < end; i += 4) if (bgra[i] != 255) return {};
    std::vector<uint8_t> blocks(bc1Bytes(width, height));
    uint8_t* out = blocks.data();
    Pixels pixels;
    for (uint32_t by = 0; by < height; by += 4)
        for (uint32_t bx = 0; bx < width; bx += 4) {
            for (int y = 0; y < 4; ++y) {
                const uint8_t* row = bgra + (by + y) * stride + static_cast<size_t>(bx) * 4;
                for (int x = 0; x < 4; ++x) { pixels.r[y * 4 + x] = row[x * 4 + 2]; pixels.g[y * 4 + x] = row[x * 4 + 1]; pixels.b[y * 4 + x] = row[x * 4]; }
            }
            const auto block = encodeBlock(pixels);
            out[0] = static_cast<uint8_t>(block.c0); out[1] = static_cast<uint8_t>(block.c0 >> 8);
            out[2] = static_cast<uint8_t>(block.c1); out[3] = static_cast<uint8_t>(block.c1 >> 8);
            std::memcpy(out + 4, &block.indices, 4); // Little-endian, as the format.
            out += 8;
        }
    return blocks;
}

std::vector<uint8_t> decodeBc1(const uint8_t* blocks, size_t size, uint32_t width, uint32_t height) {
    if (!blocks || !width || !height || width % 4 || height % 4 || size != bc1Bytes(width, height)) return {};
    std::vector<uint8_t> bgra(static_cast<size_t>(width) * height * 4);
    const uint8_t* in = blocks;
    for (uint32_t by = 0; by < height; by += 4)
        for (uint32_t bx = 0; bx < width; bx += 4, in += 8) {
            const uint16_t c0 = static_cast<uint16_t>(in[0] | in[1] << 8), c1 = static_cast<uint16_t>(in[2] | in[3] << 8);
            uint32_t indices = 0; std::memcpy(&indices, in + 4, 4);
            int r[4], g[4], b[4];
            uint8_t alpha[4] = {255, 255, 255, 255};
            unpack565(c0, r[0], g[0], b[0]); unpack565(c1, r[1], g[1], b[1]);
            if (c0 > c1) {
                r[2] = (2 * r[0] + r[1] + 1) / 3; g[2] = (2 * g[0] + g[1] + 1) / 3; b[2] = (2 * b[0] + b[1] + 1) / 3;
                r[3] = (r[0] + 2 * r[1] + 1) / 3; g[3] = (g[0] + 2 * g[1] + 1) / 3; b[3] = (b[0] + 2 * b[1] + 1) / 3;
            } else {
                r[2] = (r[0] + r[1] + 1) / 2; g[2] = (g[0] + g[1] + 1) / 2; b[2] = (b[0] + b[1] + 1) / 2;
                r[3] = g[3] = b[3] = 0; alpha[3] = 0;
            }
            for (int i = 0; i < 16; ++i) {
                const int k = (indices >> (2 * i)) & 3;
                uint8_t* out = bgra.data() + ((by + i / 4) * static_cast<size_t>(width) + bx + i % 4) * 4;
                out[0] = static_cast<uint8_t>(b[k]); out[1] = static_cast<uint8_t>(g[k]); out[2] = static_cast<uint8_t>(r[k]); out[3] = alpha[k];
            }
        }
    return bgra;
}
} // namespace block_compression
