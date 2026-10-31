#include "block_compression.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

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

// The colour line along the block's principal axis (power iteration on its covariance, from the
// covariance's strongest column), its ends where the pixels' projections end, inset by a sixteenth
// as the range fit's. For blocks of several hues, where the box diagonal misses the line.
bool principalEnds(const Pixels& p, uint16_t& c0, uint16_t& c1) {
    float mean[3]{};
    for (int i = 0; i < 16; ++i) { mean[0] += static_cast<float>(p.r[i]); mean[1] += static_cast<float>(p.g[i]); mean[2] += static_cast<float>(p.b[i]); }
    for (auto& m : mean) m /= 16;
    float cov[3][3]{};
    for (int i = 0; i < 16; ++i) {
        const float d[3] = {p.r[i] - mean[0], p.g[i] - mean[1], p.b[i] - mean[2]};
        for (int a = 0; a < 3; ++a) for (int b = 0; b < 3; ++b) cov[a][b] += d[a] * d[b];
    }
    int strongest = 0;
    for (int a = 1; a < 3; ++a) if (cov[a][a] > cov[strongest][strongest]) strongest = a;
    float axis[3] = {cov[0][strongest], cov[1][strongest], cov[2][strongest]};
    for (int iteration = 0; iteration < 6; ++iteration) {
        float next[3]{};
        for (int a = 0; a < 3; ++a) for (int b = 0; b < 3; ++b) next[a] += cov[a][b] * axis[b];
        const float largest = std::max({std::abs(next[0]), std::abs(next[1]), std::abs(next[2])});
        if (largest < 1e-6f) return false;
        for (int a = 0; a < 3; ++a) axis[a] = next[a] / largest;
    }
    const float norm = axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2];
    if (norm < 1e-6f) return false;
    float low = std::numeric_limits<float>::max(), high = -low;
    for (int i = 0; i < 16; ++i) {
        const float t = ((p.r[i] - mean[0]) * axis[0] + (p.g[i] - mean[1]) * axis[1] + (p.b[i] - mean[2]) * axis[2]) / norm;
        low = std::min(low, t); high = std::max(high, t);
    }
    const float inset = (high - low) / 16;
    low += inset; high -= inset;
    const auto end = [&](float t) {
        return pack565(static_cast<int>(std::lround(mean[0] + axis[0] * t)), static_cast<int>(std::lround(mean[1] + axis[1] * t)),
                       static_cast<int>(std::lround(mean[2] + axis[2] * t)));
    };
    c0 = end(high); c1 = end(low);
    return true;
}

// The block's colour box, inset by a sixteenth, along the diagonal that follows how red and blue
// vary with green (after J.M.P. van Waveren's real-time fit); then up to two least-squares passes,
// and for a block still several levels a pixel off, its principal axis.
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
    for (int pass = 0; pass < 2 && best.error > 16 * 2; ++pass) {
        uint16_t c0 = 0, c1 = 0;
        if (!refine(p, best, c0, c1)) break;
        const auto candidate = fit(p, c0, c1);
        if (candidate.error >= best.error) break;
        best = candidate;
    }
    if (best.error > 16 * 48) {
        uint16_t c0 = 0, c1 = 0;
        if (principalEnds(p, c0, c1)) {
            auto candidate = fit(p, c0, c1);
            if (refine(p, candidate, c0, c1)) {
                const auto refined = fit(p, c0, c1);
                if (refined.error < candidate.error) candidate = refined;
            }
            if (candidate.error < best.error) best = candidate;
        }
    }
    return best;
}

void writeColour(const Block& block, uint8_t* out) {
    out[0] = static_cast<uint8_t>(block.c0); out[1] = static_cast<uint8_t>(block.c0 >> 8);
    out[2] = static_cast<uint8_t>(block.c1); out[3] = static_cast<uint8_t>(block.c1 >> 8);
    std::memcpy(out + 4, &block.indices, 4); // Little-endian, as the format.
}

// The eight values an alpha block's ends give: a0 > a1 interpolates six between them; otherwise four,
// then 0 and 255.
void alphaLevels(int a0, int a1, int (&level)[8]) {
    level[0] = a0; level[1] = a1;
    if (a0 > a1) for (int k = 0; k < 6; ++k) level[2 + k] = ((6 - k) * a0 + (1 + k) * a1 + 3) / 7;
    else {
        for (int k = 0; k < 4; ++k) level[2 + k] = ((4 - k) * a0 + (1 + k) * a1 + 2) / 5;
        level[6] = 0; level[7] = 255;
    }
}

// Each pixel's nearest level; returns the squared error and fills the 48 index bits.
int alphaIndices(const uint8_t (&alpha)[16], const int (&level)[8], uint64_t& bits) {
    int error = 0;
    bits = 0;
    for (int i = 0; i < 16; ++i) {
        int best = 0, best_error = std::numeric_limits<int>::max();
        for (int k = 0; k < 8; ++k) {
            const int d = alpha[i] - level[k];
            if (d * d < best_error) { best_error = d * d; best = k; }
        }
        error += best_error;
        bits |= static_cast<uint64_t>(best) << (3 * i);
    }
    return error;
}

// An alpha block (BC3's first 8 bytes, BC4's layout): the better of the two modes.
void encodeAlpha(const uint8_t (&alpha)[16], uint8_t* out) {
    int low = 255, high = 0, inner_low = 255, inner_high = 0;
    for (const uint8_t a : alpha) {
        low = std::min<int>(low, a); high = std::max<int>(high, a);
        if (a != 0 && a != 255) { inner_low = std::min<int>(inner_low, a); inner_high = std::max<int>(inner_high, a); }
    }
    int a0 = high, a1 = low;
    uint64_t bits = 0;
    int level[8];
    if (high == low) { a0 = a1 = high; alphaLevels(a0, a1, level); alphaIndices(alpha, level, bits); }
    else {
        alphaLevels(high, low, level);
        const int eight = alphaIndices(alpha, level, bits);
        // Six levels between the values other than 0 and 255, which the mode holds exactly.
        if (inner_low > inner_high) { inner_low = inner_high = low == 0 ? high : low; }
        uint64_t six_bits = 0;
        int six_level[8];
        alphaLevels(inner_low, inner_high, six_level);
        const int six = alphaIndices(alpha, six_level, six_bits);
        if (six < eight) { a0 = inner_low; a1 = inner_high; bits = six_bits; }
    }
    out[0] = static_cast<uint8_t>(a0); out[1] = static_cast<uint8_t>(a1);
    for (int i = 0; i < 6; ++i) out[2 + i] = static_cast<uint8_t>(bits >> (8 * i));
}

void decodeColour(const uint8_t* in, bool always_four, uint8_t* bgra, size_t stride, bool keep_alpha) {
    const uint16_t c0 = static_cast<uint16_t>(in[0] | in[1] << 8), c1 = static_cast<uint16_t>(in[2] | in[3] << 8);
    uint32_t indices = 0; std::memcpy(&indices, in + 4, 4);
    int r[4], g[4], b[4];
    uint8_t alpha[4] = {255, 255, 255, 255};
    unpack565(c0, r[0], g[0], b[0]); unpack565(c1, r[1], g[1], b[1]);
    if (always_four || c0 > c1) {
        r[2] = (2 * r[0] + r[1] + 1) / 3; g[2] = (2 * g[0] + g[1] + 1) / 3; b[2] = (2 * b[0] + b[1] + 1) / 3;
        r[3] = (r[0] + 2 * r[1] + 1) / 3; g[3] = (g[0] + 2 * g[1] + 1) / 3; b[3] = (b[0] + 2 * b[1] + 1) / 3;
    } else {
        r[2] = (r[0] + r[1] + 1) / 2; g[2] = (g[0] + g[1] + 1) / 2; b[2] = (b[0] + b[1] + 1) / 2;
        r[3] = g[3] = b[3] = 0; alpha[3] = 0;
    }
    for (int i = 0; i < 16; ++i) {
        const int k = (indices >> (2 * i)) & 3;
        uint8_t* out = bgra + (i / 4) * stride + static_cast<size_t>(i % 4) * 4;
        out[0] = static_cast<uint8_t>(b[k]); out[1] = static_cast<uint8_t>(g[k]); out[2] = static_cast<uint8_t>(r[k]);
        if (!keep_alpha) out[3] = alpha[k];
    }
}

template <class Encode> std::vector<uint8_t> encodeBlocks(const uint8_t* bgra, uint32_t width, uint32_t height, size_t block_size, Encode encode) {
    std::vector<uint8_t> blocks(static_cast<size_t>(width / 4) * (height / 4) * block_size);
    const size_t stride = static_cast<size_t>(width) * 4;
    uint8_t* out = blocks.data();
    Pixels pixels;
    uint8_t alpha[16];
    for (uint32_t by = 0; by < height; by += 4)
        for (uint32_t bx = 0; bx < width; bx += 4, out += block_size) {
            for (int y = 0; y < 4; ++y) {
                const uint8_t* row = bgra + (by + y) * stride + static_cast<size_t>(bx) * 4;
                for (int x = 0; x < 4; ++x) {
                    const int i = y * 4 + x;
                    pixels.r[i] = row[x * 4 + 2]; pixels.g[i] = row[x * 4 + 1]; pixels.b[i] = row[x * 4]; alpha[i] = row[x * 4 + 3];
                }
            }
            encode(pixels, alpha, out);
        }
    return blocks;
}
} // namespace

std::vector<uint8_t> encodeBc1(const uint8_t* bgra, uint32_t width, uint32_t height) {
    if (!bgra || !width || !height || width % 4 || height % 4) return {};
    const size_t stride = static_cast<size_t>(width) * 4;
    for (size_t i = 3, end = stride * height; i < end; i += 4) if (bgra[i] != 255) return {};
    return encodeBlocks(bgra, width, height, 8, [](const Pixels& p, const uint8_t (&)[16], uint8_t* out) { writeColour(encodeBlock(p), out); });
}

std::vector<uint8_t> encodeBc3(const uint8_t* bgra, uint32_t width, uint32_t height) {
    if (!bgra || !width || !height || width % 4 || height % 4) return {};
    return encodeBlocks(bgra, width, height, 16, [](const Pixels& p, const uint8_t (&alpha)[16], uint8_t* out) {
        encodeAlpha(alpha, out);
        writeColour(encodeBlock(p), out + 8);
    });
}

std::vector<uint8_t> decodeBc1(const uint8_t* blocks, size_t size, uint32_t width, uint32_t height) {
    if (!blocks || !width || !height || width % 4 || height % 4 || size != bc1Bytes(width, height)) return {};
    std::vector<uint8_t> bgra(static_cast<size_t>(width) * height * 4);
    const size_t stride = static_cast<size_t>(width) * 4;
    const uint8_t* in = blocks;
    for (uint32_t by = 0; by < height; by += 4)
        for (uint32_t bx = 0; bx < width; bx += 4, in += 8)
            decodeColour(in, false, bgra.data() + by * stride + static_cast<size_t>(bx) * 4, stride, false);
    return bgra;
}

std::vector<uint8_t> decodeBc3(const uint8_t* blocks, size_t size, uint32_t width, uint32_t height) {
    if (!blocks || !width || !height || width % 4 || height % 4 || size != bc3Bytes(width, height)) return {};
    std::vector<uint8_t> bgra(static_cast<size_t>(width) * height * 4);
    const size_t stride = static_cast<size_t>(width) * 4;
    const uint8_t* in = blocks;
    for (uint32_t by = 0; by < height; by += 4)
        for (uint32_t bx = 0; bx < width; bx += 4, in += 16) {
            uint8_t* corner = bgra.data() + by * stride + static_cast<size_t>(bx) * 4;
            int level[8];
            alphaLevels(in[0], in[1], level);
            uint64_t bits = 0;
            for (int i = 0; i < 6; ++i) bits |= static_cast<uint64_t>(in[2 + i]) << (8 * i);
            for (int i = 0; i < 16; ++i) corner[(i / 4) * stride + static_cast<size_t>(i % 4) * 4 + 3] = static_cast<uint8_t>(level[(bits >> (3 * i)) & 7]);
            decodeColour(in + 8, true, corner, stride, true);
        }
    return bgra;
}
} // namespace block_compression
