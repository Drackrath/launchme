// WIC's pixel formats: conversion between them (through 32-bit BGRA, straight or premultiplied),
// CopyPixels' shared checks and copies, and the scaler's resampling.
#include "wic_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lm_wic {
namespace {
BYTE multiply(unsigned colour, unsigned alpha) { return static_cast<BYTE>((colour * alpha + 127) / 255); }
BYTE divide(unsigned colour, unsigned alpha) { return alpha ? static_cast<BYTE>(std::min(255u, (colour * 255 + alpha / 2) / alpha)) : 0; }
// Grey as Rec. 709 luma of the stored (gamma-encoded) channels.
BYTE luma(unsigned b, unsigned g, unsigned r) { return static_cast<BYTE>((r * 54 + g * 183 + b * 19 + 128) >> 8); }

// To 32-bit BGRA, premultiplied or straight.
void toBgra(const Format& from, const BYTE* in, BYTE* out, UINT count, bool premultiplied) {
    switch (from.layout) {
    case Layout::bgra:
    case Layout::rgba: {
        const int red = from.layout == Layout::bgra ? 2 : 0, blue = 2 - red;
        const bool multiplying = premultiplied && !from.premultiplied, dividing = !premultiplied && from.premultiplied;
        for (UINT i = 0; i < count; ++i, in += 4, out += 4) {
            const BYTE b = in[blue], g = in[1], r = in[red], a = in[3];
            if (multiplying) { out[0] = multiply(b, a); out[1] = multiply(g, a); out[2] = multiply(r, a); }
            else if (dividing) { out[0] = divide(b, a); out[1] = divide(g, a); out[2] = divide(r, a); }
            else { out[0] = b; out[1] = g; out[2] = r; }
            out[3] = a;
        }
        break;
    }
    case Layout::bgrx:
        for (UINT i = 0; i < count; ++i, in += 4, out += 4) { out[0] = in[0]; out[1] = in[1]; out[2] = in[2]; out[3] = 255; }
        break;
    case Layout::bgr:
    case Layout::rgb: {
        const int red = from.layout == Layout::bgr ? 2 : 0, blue = 2 - red;
        for (UINT i = 0; i < count; ++i, in += 3, out += 4) { out[0] = in[blue]; out[1] = in[1]; out[2] = in[red]; out[3] = 255; }
        break;
    }
    case Layout::gray:
        for (UINT i = 0; i < count; ++i, ++in, out += 4) { out[0] = out[1] = out[2] = *in; out[3] = 255; }
        break;
    case Layout::alpha:
        for (UINT i = 0; i < count; ++i, ++in, out += 4) { out[0] = out[1] = out[2] = 0; out[3] = *in; }
        break;
    }
}

// From 32-bit BGRA. Formats without alpha take the colour channels as they are (premultiplied
// ones: the picture over black).
void fromBgra(const BYTE* in, bool premultiplied, const Format& to, BYTE* out, UINT count) {
    switch (to.layout) {
    case Layout::bgra:
    case Layout::rgba: {
        const int red = to.layout == Layout::bgra ? 2 : 0, blue = 2 - red;
        const bool multiplying = to.premultiplied && !premultiplied, dividing = !to.premultiplied && premultiplied;
        for (UINT i = 0; i < count; ++i, in += 4, out += 4) {
            const BYTE b = in[0], g = in[1], r = in[2], a = in[3];
            if (multiplying) { out[blue] = multiply(b, a); out[1] = multiply(g, a); out[red] = multiply(r, a); }
            else if (dividing) { out[blue] = divide(b, a); out[1] = divide(g, a); out[red] = divide(r, a); }
            else { out[blue] = b; out[1] = g; out[red] = r; }
            out[3] = a;
        }
        break;
    }
    case Layout::bgrx:
        for (UINT i = 0; i < count; ++i, in += 4, out += 4) { out[0] = in[0]; out[1] = in[1]; out[2] = in[2]; out[3] = 255; }
        break;
    case Layout::bgr:
    case Layout::rgb: {
        const int red = to.layout == Layout::bgr ? 2 : 0, blue = 2 - red;
        for (UINT i = 0; i < count; ++i, in += 4, out += 3) { out[blue] = in[0]; out[1] = in[1]; out[red] = in[2]; }
        break;
    }
    case Layout::gray:
        for (UINT i = 0; i < count; ++i, in += 4, ++out) *out = luma(in[0], in[1], in[2]);
        break;
    case Layout::alpha:
        for (UINT i = 0; i < count; ++i, in += 4, ++out) *out = in[3];
        break;
    }
}

// Resampling weights along one axis: output pixel x sums `count` input pixels from `first`, with
// the weights from `offset` on.
struct Tap { UINT first, count; size_t offset; };
struct Kernel { std::vector<Tap> taps; std::vector<float> weights; };
Kernel kernel(UINT from, UINT to, WICBitmapInterpolationMode mode) {
    Kernel result;
    result.taps.resize(to);
    const double scale = static_cast<double>(from) / to;
    const bool cubic = mode == WICBitmapInterpolationModeCubic || mode == WICBitmapInterpolationModeHighQualityCubic;
    for (UINT x = 0; x < to; ++x) {
        const double center = (x + 0.5) * scale;
        double begin, end;
        std::vector<double> weights;
        if (mode == WICBitmapInterpolationModeNearestNeighbor) {
            begin = std::floor(center); end = begin + 1;
            weights.push_back(1);
        } else if (mode == WICBitmapInterpolationModeFant && scale >= 1) {
            // The area the output pixel covers.
            begin = std::floor(x * scale); end = std::ceil((x + 1) * scale);
            for (double s = begin; s < end; ++s) weights.push_back(std::min(s + 1, (x + 1) * scale) - std::max(s, x * scale));
        } else {
            // Linear (Fant when enlarging) or Catmull-Rom; only high-quality cubic widens the
            // kernel when reducing, the others alias as WIC's do.
            const double width = mode == WICBitmapInterpolationModeHighQualityCubic ? std::max(1.0, scale) : 1.0;
            const double support = (cubic ? 2.0 : 1.0) * width;
            begin = std::floor(center - support); end = std::ceil(center + support);
            for (double s = begin; s < end; ++s) {
                const double t = std::abs((s + 0.5 - center) / width);
                weights.push_back(cubic ? (t < 1 ? 1.5 * t * t * t - 2.5 * t * t + 1 : t < 2 ? -0.5 * t * t * t + 2.5 * t * t - 4 * t + 2 : 0)
                                        : std::max(0.0, 1 - t));
            }
        }
        // Clamped to the edges: weights outside fold onto the border pixels.
        const int64_t low = std::clamp<int64_t>(static_cast<int64_t>(begin), 0, from - 1);
        const int64_t high = std::clamp<int64_t>(static_cast<int64_t>(end) - 1, 0, from - 1);
        Tap& tap = result.taps[x];
        tap = {static_cast<UINT>(low), static_cast<UINT>(high - low + 1), result.weights.size()};
        std::vector<double> folded(tap.count);
        double total = 0;
        for (size_t i = 0; i < weights.size(); ++i) {
            folded[static_cast<size_t>(std::clamp<int64_t>(static_cast<int64_t>(begin) + static_cast<int64_t>(i), low, high) - low)] += weights[i];
            total += weights[i];
        }
        for (const double w : folded) result.weights.push_back(static_cast<float>(total != 0 ? w / total : w));
    }
    return result;
}
} // namespace

const Format* findFormat(const GUID& guid) {
    for (const auto& format : formats)
        if (format.guid == guid) return &format;
    return nullptr;
}

const Format* formatOf(IWICBitmapSource* source) {
    WICPixelFormatGUID guid{};
    return source && SUCCEEDED(source->GetPixelFormat(&guid)) ? findFormat(guid) : nullptr;
}

Image makeImage(const Format& format, UINT width, UINT height) {
    Image image{&format, width, height, (format.rowBytes(width) + 3) & ~size_t{3}, {}};
    image.bytes.resize(image.stride * height);
    return image;
}

void convertRow(const Format& from, const BYTE* in, const Format& to, BYTE* out, UINT count) {
    if (&from == &to) { std::memcpy(out, in, from.rowBytes(count)); return; }
    if (to.layout == Layout::bgra) { toBgra(from, in, out, count, to.premultiplied); return; }
    if (from.layout == Layout::bgra) { fromBgra(in, from.premultiplied, to, out, count); return; }
    // Through BGRA in pieces; formats without alpha keep the source's colours as they are.
    const bool premultiplied = to.alpha ? to.premultiplied : from.premultiplied;
    BYTE middle[256 * 4];
    for (UINT done = 0; done < count;) {
        const UINT part = std::min(count - done, 256u);
        toBgra(from, in + from.rowBytes(done), middle, part, premultiplied);
        fromBgra(middle, premultiplied, to, out + to.rowBytes(done), part);
        done += part;
    }
}

HRESULT checkCopy(const WICRect* rect, UINT width, UINT height, const Format& format, UINT stride, UINT bufferSize, const BYTE* buffer,
                  WICRect& area) {
    area = rect ? *rect : WICRect{0, 0, static_cast<INT>(width), static_cast<INT>(height)};
    if (area.X < 0 || area.Y < 0 || area.Width < 0 || area.Height < 0 || static_cast<uint64_t>(area.X) + area.Width > width ||
        static_cast<uint64_t>(area.Y) + area.Height > height) return E_INVALIDARG;
    if (!area.Width || !area.Height) return S_OK;
    const size_t row = format.rowBytes(static_cast<UINT>(area.Width));
    if (!buffer || stride < row) return E_INVALIDARG;
    if (static_cast<uint64_t>(stride) * (area.Height - 1) + row > bufferSize) return WINCODEC_ERR_INSUFFICIENTBUFFER;
    return S_OK;
}

void copyView(const View& pixels, const WICRect& area, const Format& to, UINT stride, BYTE* out) {
    const size_t offset = pixels.format->rowBytes(static_cast<UINT>(area.X));
    for (INT y = 0; y < area.Height; ++y)
        convertRow(*pixels.format, pixels.data + (static_cast<size_t>(area.Y) + y) * pixels.stride + offset, to,
                   out + static_cast<size_t>(y) * stride, static_cast<UINT>(area.Width));
}

HRESULT copyConverted(IWICBitmapSource* source, const WICRect& area, const Format& to, UINT stride, BYTE* out) {
    if (!area.Width || !area.Height) return S_OK;
    if (auto* own = dynamic_cast<Source*>(source)) {
        View pixels;
        if (const HRESULT result = own->view(pixels); FAILED(result)) return result;
        if (pixels.data) { copyView(pixels, area, to, stride, out); return S_OK; }
    }
    const Format* from = formatOf(source);
    if (!from) return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    const auto width = static_cast<UINT>(area.Width);
    if (from == &to)
        return source->CopyPixels(&area, stride, static_cast<UINT>(size_t{stride} * (area.Height - 1) + to.rowBytes(width)), out);
    return lm_com::guarded([&] {
        // In bands of about a megabyte through a buffer in the source's format.
        const size_t row = from->rowBytes(width);
        const INT band = static_cast<INT>(std::clamp<size_t>((size_t{1} << 20) / row, 1, static_cast<size_t>(area.Height)));
        std::vector<BYTE> buffer(row * static_cast<size_t>(band));
        for (INT y = 0; y < area.Height; y += band) {
            const WICRect part{area.X, area.Y + y, area.Width, std::min(band, area.Height - y)};
            if (const HRESULT result = source->CopyPixels(&part, static_cast<UINT>(row), static_cast<UINT>(row * part.Height), buffer.data()); FAILED(result))
                return result;
            for (INT r = 0; r < part.Height; ++r)
                convertRow(*from, buffer.data() + row * r, to, out + static_cast<size_t>(y + r) * stride, width);
        }
        return S_OK;
    });
}

HRESULT viewOf(IWICBitmapSource* source, const Format& format, Image& image, View& pixels) {
    pixels = {};
    if (auto* own = dynamic_cast<Source*>(source)) {
        if (const HRESULT result = own->view(pixels); FAILED(result) || pixels.data) return result;
    }
    UINT width = 0, height = 0;
    if (const HRESULT result = source->GetSize(&width, &height); FAILED(result)) return result;
    return lm_com::guarded([&] {
        image = makeImage(format, width, height);
        const HRESULT result = copyConverted(source, {0, 0, static_cast<INT>(width), static_cast<INT>(height)}, format,
                                             static_cast<UINT>(image.stride), image.bytes.data());
        if (SUCCEEDED(result)) pixels = image.view();
        return result;
    });
}

Image scale(const View& source, UINT width, UINT height, WICBitmapInterpolationMode mode, bool premultiplied) {
    const UINT w = source.width, h = source.height;
    const Kernel across = kernel(w, width, mode), down = kernel(h, height, mode);
    const bool straight = !source.format->premultiplied;
    // Source rows scaled across, into premultiplied floats, in a ring that holds as many as one
    // output row sums (the taps move down monotonically).
    UINT window = 1;
    for (const Tap& tap : down.taps) window = std::max(window, tap.count);
    const size_t row_floats = size_t{width} * 4;
    std::vector<BYTE> bgra(size_t{w} * 4);
    std::vector<float> line(size_t{w} * 4), ring(row_floats * window);
    const auto across_row = [&](UINT y) {
        toBgra(*source.format, source.data + y * source.stride, bgra.data(), w, !straight);
        for (size_t i = 0; i < line.size(); i += 4) {
            const float a = bgra[i + 3], factor = straight ? a * (1 / 255.0f) : 1.0f;
            line[i] = bgra[i] * factor; line[i + 1] = bgra[i + 1] * factor; line[i + 2] = bgra[i + 2] * factor; line[i + 3] = a;
        }
        float* out = ring.data() + (y % window) * row_floats;
        for (const Tap& tap : across.taps) {
            const float* in = line.data() + size_t{tap.first} * 4;
            const float* weight = across.weights.data() + tap.offset;
            float b = 0, g = 0, r = 0, a = 0;
            for (UINT i = 0; i < tap.count; ++i, in += 4) { b += in[0] * weight[i]; g += in[1] * weight[i]; r += in[2] * weight[i]; a += in[3] * weight[i]; }
            out[0] = b; out[1] = g; out[2] = r; out[3] = a;
            out += 4;
        }
    };
    // Then down, a whole output row at a time.
    Image result = makeImage(premultiplied ? format_pbgra : format_bgra, width, height);
    std::vector<float> sums(row_floats);
    UINT ready = 0; // Source rows scaled across so far.
    for (UINT y = 0; y < height; ++y) {
        const Tap& tap = down.taps[y];
        for (; ready < tap.first + tap.count; ++ready) across_row(ready);
        std::fill(sums.begin(), sums.end(), 0.0f);
        for (UINT i = 0; i < tap.count; ++i) {
            const float weight = down.weights[tap.offset + i];
            const float* in = ring.data() + ((tap.first + i) % window) * row_floats;
            for (size_t j = 0; j < row_floats; ++j) sums[j] += in[j] * weight;
        }
        BYTE* out = result.bytes.data() + y * result.stride;
        for (size_t i = 0; i < sums.size(); i += 4, out += 4) {
            const float alpha = std::clamp(sums[i + 3], 0.0f, 255.0f);
            out[3] = static_cast<BYTE>(alpha + 0.5f);
            if (premultiplied)
                for (int c = 0; c < 3; ++c) out[c] = static_cast<BYTE>(std::clamp(sums[i + c], 0.0f, alpha) + 0.5f);
            else {
                const float unpremultiply = out[3] ? 255.0f / alpha : 0.0f;
                for (int c = 0; c < 3; ++c) out[c] = static_cast<BYTE>(std::clamp(sums[i + c] * unpremultiply, 0.0f, 255.0f) + 0.5f);
            }
        }
    }
    return result;
}
} // namespace lm_wic
