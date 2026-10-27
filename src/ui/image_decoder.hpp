#pragma once
#include "../artwork/ink_bounds.hpp"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <utility>
#include <vector>

struct IWICImagingFactory;
struct DecodeMemoryBudget;
struct DecodeMemoryReservation;

inline constexpr size_t max_decoded_image_bytes = 16u * 1024 * 1024;
inline constexpr size_t decoded_memory_budget = 64u * 1024 * 1024;
inline constexpr uint64_t max_source_image_pixels = 16u * 1024 * 1024;
// SGDB logo exports can exceed 16 Mpix (e.g. Esoteric Ebb is 5929x3859).
// They pass through a bounded ink preview/crop and the same 16 MiB output cap.
inline constexpr uint64_t max_source_logo_pixels = 32u * 1024 * 1024;

using ImageBytes = std::shared_ptr<const std::vector<uint8_t>>;
// Encoded image shared by the launcher's artwork cache and the renderer (one buffer,
// no copy per bind); null means no image.
inline ImageBytes shareImage(std::vector<uint8_t> bytes) {
    return bytes.empty() ? ImageBytes{} : std::make_shared<const std::vector<uint8_t>>(std::move(bytes));
}
inline size_t imageSize(const ImageBytes& bytes) { return bytes ? bytes->size() : 0; }
// FNV-1a of encoded bytes, eight at a time: recognises a picture read from disk again (low memory).
inline uint64_t imageHash(const std::vector<uint8_t>& bytes) {
    uint64_t hash = 14695981039346656037ull;
    size_t i = 0;
    for (; i + 8 <= bytes.size(); i += 8) {
        uint64_t word = 0;
        for (int k = 0; k < 8; ++k) word |= static_cast<uint64_t>(bytes[i + k]) << (8 * k);
        hash = (hash ^ word) * 1099511628211ull;
    }
    for (; i < bytes.size(); ++i) hash = (hash ^ bytes[i]) * 1099511628211ull;
    return hash ^ bytes.size();
}
// How a background picture is shown: a game's own art, a console logo standing in for one
// (faint), or a random game's art behind a console tile (dimmed).
enum class HeroTone : uint8_t { full, logo, dimmed };

struct DecodeKind {
    bool accent = false;     // Dominant poster colour.
    // A background drawn cover-fit: downsampled only to what still covers cover_width × cover_height
    // (the screen), so its height is kept. A 3840×1240 hero on a 1080p screen becomes 3344×1080.
    bool cover_fit = false;
    bool ink = false;        // Ink bounds (transparent/matte gutters) of a logo.
    // >0: downsampled (Fant) so the shorter side is at most this many pixels. Covers are
    // drawn as square fills, so the shorter side is what reaches the screen.
    uint32_t short_edge = 0;
    bool steelbook = false; // Bounded colour-selected foil tint/coverage/relief map for Coverflow.
    // short_edge is a size wanted on screen rather than a limit: within 25% of it the image is
    // kept at its own size (a resample would cost more than the smaller upload saves).
    bool loose_edge = false;
    // Low memory mode on a GPU that samples BC1: the size is rounded to whole 4×4 blocks and an
    // opaque result is kept as BC1 blocks (DecodedImage::blocks) instead of BGRA pixels.
    bool block_compress = false;
    uint32_t cover_width = 1920, cover_height = 1080; // cover_fit's box, in pixels.
};
// The size a cover_fit image is decoded to: scaled down (never up) to the smallest size that still
// covers the box. Pure, for tests.
inline std::pair<uint32_t, uint32_t> coverFitSize(uint32_t width, uint32_t height, uint32_t box_width, uint32_t box_height) {
    if (!width || !height || !box_width || !box_height) return {width, height};
    const double scale = std::min(1.0, std::max(static_cast<double>(box_width) / width, static_cast<double>(box_height) / height));
    if (scale >= 1.0) return {width, height};
    return {std::max(1u, static_cast<uint32_t>(width * scale + 0.5)), std::max(1u, static_cast<uint32_t>(height * scale + 0.5))};
}

struct DecodedImage {
    DecodedImage() = default;
    DecodedImage(DecodedImage&&) noexcept = default;
    DecodedImage& operator=(DecodedImage&&) noexcept = default;
    DecodedImage(const DecodedImage&) = delete;
    DecodedImage& operator=(const DecodedImage&) = delete;
    uint32_t width = 0, height = 0;
    // Declared before pixels so their storage is freed before returning the
    // decoder's reservation. Completed tickets keep it until upload/discard.
    std::shared_ptr<DecodeMemoryReservation> reservation;
    std::vector<uint8_t> pixels;             // 32bpp premultiplied BGRA, stride = width * 4; empty when `blocks` holds the image.
    std::vector<uint8_t> blocks;             // DecodeKind::block_compress: BC1 blocks (block_compression.hpp), else empty.
    std::vector<uint8_t> gloss;              // Steelbook only: A8 polished dark ink, stride = width; empty if none.
    std::vector<uint8_t> matte;              // Steelbook only: A8 grainy matte print, stride = width; empty if none.
    std::vector<uint8_t> relief;             // Steelbook only: opaque BGRA edge relief (steelbook_material::emboss); empty if none.
    float accent[3]{0.94f, 0.96f, 0.97f};   // Poster accent; the neutral fallback otherwise.
    InkBounds ink;                           // Logos only.
    bool valid() const {
        return width && height && (pixels.size() == static_cast<size_t>(width) * height * 4 ||
                                   (pixels.empty() && !blocks.empty() && blocks.size() == static_cast<size_t>(width / 4) * (height / 4) * 8));
    }
};

// Pure WIC decode, callable on any thread that owns `wic`. Returns an invalid
// image for corrupt/unsupported data, dimensions beyond 16384 px or sources
// exceeding 16 Mpix (32 Mpix for logos). Output is at most 16 MiB; large logos are cropped first.
DecodedImage decodeImage(IWICImagingFactory* wic, std::span<const uint8_t> bytes, DecodeKind kind);

// Result slot shared by the renderer (owner) and a decoder worker. A slot the
// renderer dropped before decoding started is skipped, not decoded.
struct DecodeTicket {
    std::atomic_bool done{false};
    // Lower decodes (and uploads) first; equal priorities keep submission order. The renderer
    // ranks grid artwork by distance from the view while it waits (Renderer::rankArt).
    std::atomic_int priority{0};
    DecodedImage image; // Written by the worker before `done`, read by the UI after it.
};

// Decoding a poster costs ~2 ms and a 4K hero far more; neither belongs on the
// UI thread. Workers invalidate the window when a result is ready, so an idle
// frame loop still uploads it promptly.
class ImageDecoder {
public:
    ImageDecoder();
    ~ImageDecoder();
    ImageDecoder(const ImageDecoder&) = delete;
    ImageDecoder& operator=(const ImageDecoder&) = delete;

    void start(HWND notify, unsigned threads = 2);
    // Without started workers (tests, failed start) the image is decoded inline.
    std::shared_ptr<DecodeTicket> submit(ImageBytes bytes, DecodeKind kind);
    uint64_t completed() const { return completed_.load(std::memory_order_acquire); }
    size_t pending() const { return pending_.load(std::memory_order_acquire); }
    size_t reservedBytes() const;
    size_t peakReservedBytes() const;
    // Total WIC decode time of finished jobs (all threads), for the benchmark.
    double decodeMilliseconds() const { return decode_us_.load(std::memory_order_relaxed) / 1000.0; }

private:
    struct Job { ImageBytes bytes; DecodeKind kind; std::shared_ptr<DecodeTicket> ticket; };
    void run();
    void decode(Job& job, IWICImagingFactory* wic, bool wait);

    HWND notify_ = nullptr;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Job> jobs_;
    std::vector<std::thread> workers_;
    std::atomic_bool stopping_{false};
    std::shared_ptr<DecodeMemoryBudget> budget_;
    std::atomic_uint64_t completed_{0};
    std::atomic_size_t pending_{0};
    std::atomic_uint64_t decode_us_{0};
};
