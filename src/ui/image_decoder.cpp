#include "image_decoder.hpp"
#include "steelbook_material.hpp"
#include "texture_cache.hpp"
#include "../artwork/block_compression.hpp"

#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <tuple>

using Microsoft::WRL::ComPtr;

struct DecodeMemoryBudget {
    std::mutex mutex;
    std::condition_variable wake;
    size_t used = 0, peak = 0;
};
struct DecodeMemoryReservation {
    std::shared_ptr<DecodeMemoryBudget> budget;
    size_t bytes = 0;
    explicit DecodeMemoryReservation(std::shared_ptr<DecodeMemoryBudget> shared) : budget(std::move(shared)) {}
    void shrink(size_t next) {
        { std::lock_guard lock(budget->mutex); budget->used -= bytes - next; bytes = next; }
        budget->wake.notify_all();
    }
    ~DecodeMemoryReservation() { shrink(0); }
};

namespace {
// Pixel-area cap also covers very tall/narrow images. Rounding down cannot
// exceed the byte budget, and the aspect ratio is preserved to one pixel.
void capPixels(UINT& width, UINT& height, size_t pixels) {
    const auto area = static_cast<uint64_t>(width) * height;
    if (area <= pixels) return;
    const double scale = std::sqrt(static_cast<double>(pixels) / area);
    width = std::max(1u, static_cast<UINT>(width * scale));
    height = std::max(1u, static_cast<UINT>(height * scale));
}

// Find a conservative crop from a bounded preview before downsampling a huge
// canvas. A small sharp logo inside large gutters keeps its source resolution.
ComPtr<IWICBitmapClipper> cropLargeLogo(IWICImagingFactory* wic, IWICBitmapSource* frame, UINT& width, UINT& height) {
    UINT w = width, h = height;
    const double scale = std::min(1.0, 1024.0 / std::max(w, h));
    w = std::max(1u, static_cast<UINT>(w * scale)); h = std::max(1u, static_cast<UINT>(h * scale));
    ComPtr<IWICBitmapScaler> preview;
    ComPtr<IWICFormatConverter> converted;
    if (FAILED(wic->CreateBitmapScaler(preview.GetAddressOf())) ||
        FAILED(preview->Initialize(frame, w, h, WICBitmapInterpolationModeFant)) ||
        FAILED(wic->CreateFormatConverter(converted.GetAddressOf())) ||
        FAILED(converted->Initialize(preview.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return {};
    std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
    if (FAILED(converted->CopyPixels(nullptr, w * 4, static_cast<UINT>(pixels.size()), pixels.data()))) return {};
    const auto ink = findInkBounds(pixels, w, h, w * 4);
    if (!ink.valid) return {};
    // Two preview pixels of padding retain antialiased edges before the exact
    // final ink measurement. All coordinates remain inside the source image.
    const auto left = static_cast<UINT>(static_cast<uint64_t>(ink.left > 2 ? ink.left - 2 : 0) * width / w);
    const auto top = static_cast<UINT>(static_cast<uint64_t>(ink.top > 2 ? ink.top - 2 : 0) * height / h);
    const auto right = static_cast<UINT>((static_cast<uint64_t>(std::min(w, ink.right + 2)) * width + w - 1) / w);
    const auto bottom = static_cast<UINT>((static_cast<uint64_t>(std::min(h, ink.bottom + 2)) * height + h - 1) / h);
    const WICRect rect{static_cast<INT>(left), static_cast<INT>(top), static_cast<INT>(right - left), static_cast<INT>(bottom - top)};
    ComPtr<IWICBitmapClipper> clip;
    if (FAILED(wic->CreateBitmapClipper(clip.GetAddressOf())) || FAILED(clip->Initialize(frame, &rect))) return {};
    width = right - left; height = bottom - top;
    return clip;
}

// Dominant saturated hue of a 24x24 thumbnail; neutral when the poster is grey. The thumbnail is
// an area average of the decoded output (PBGRA), so the source is not decoded a second time.
void posterAccent(const std::vector<uint8_t>& decoded, UINT width, UINT height, float (&accent)[3]) {
    if (!width || !height || decoded.size() < static_cast<size_t>(width) * height * 4) return;
    std::array<uint8_t, 24 * 24 * 4> pixels{};
    for (UINT ty = 0; ty < 24; ++ty)
        for (UINT tx = 0; tx < 24; ++tx) {
            const UINT x0 = tx * width / 24, x1 = std::max(x0 + 1, (tx + 1) * width / 24);
            const UINT y0 = ty * height / 24, y1 = std::max(y0 + 1, (ty + 1) * height / 24);
            uint64_t sum[4]{};
            for (UINT y = y0; y < y1 && y < height; ++y) {
                const uint8_t* row = decoded.data() + (static_cast<size_t>(y) * width + x0) * 4;
                for (UINT x = x0; x < x1 && x < width; ++x, row += 4) { sum[0] += row[0]; sum[1] += row[1]; sum[2] += row[2]; sum[3] += row[3]; }
            }
            const uint64_t count = static_cast<uint64_t>(std::min(x1, width) - x0) * (std::min(y1, height) - y0);
            for (int c = 0; c < 4; ++c) pixels[(ty * 24 + tx) * 4 + c] = static_cast<uint8_t>(count ? (sum[c] + count / 2) / count : 0);
        }
    std::array<float, 12> scores{}, reds{}, greens{}, blues{};
    for (size_t i = 0; i < pixels.size(); i += 4) {
        if (pixels[i + 3] < 80) continue;
        const float b = pixels[i] / 255.0f, g = pixels[i + 1] / 255.0f, r = pixels[i + 2] / 255.0f;
        const float highest = std::max({r, g, b}), lowest = std::min({r, g, b});
        const float chroma = highest - lowest;
        if (chroma < 0.16f || highest < 0.18f) continue;
        float hue = 0;
        if (highest == r) hue = (g - b) / chroma;
        else if (highest == g) hue = 2 + (b - r) / chroma;
        else hue = 4 + (r - g) / chroma;
        if (hue < 0) hue += 6;
        const int bin = std::clamp(static_cast<int>(hue * 2), 0, 11);
        const float weight = chroma * chroma * highest;
        scores[bin] += weight;
        reds[bin] += r * weight;
        greens[bin] += g * weight;
        blues[bin] += b * weight;
    }
    int best = 0;
    float best_score = 0;
    for (int i = 0; i < 12; ++i) {
        const float score = scores[(i + 11) % 12] + scores[i] + scores[(i + 1) % 12];
        if (score > best_score) { best_score = score; best = i; }
    }
    if (best_score < 2.0f) return;
    const int previous = (best + 11) % 12, following = (best + 1) % 12;
    const float r = (reds[previous] + reds[best] + reds[following]) / best_score;
    const float g = (greens[previous] + greens[best] + greens[following]) / best_score;
    const float b = (blues[previous] + blues[best] + blues[following]) / best_score;
    const float largest = std::max({r, g, b});
    if (largest < 0.01f) return;
    accent[0] = 0.27f + 0.73f * r / largest;
    accent[1] = 0.27f + 0.73f * g / largest;
    accent[2] = 0.27f + 0.73f * b / largest;
}

// Inline fallback only; a cached factory would outlive the caller's COM apartment.
ComPtr<IWICImagingFactory> inlineFactory() {
    ComPtr<IWICImagingFactory> factory;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf()));
    return factory;
}
}

DecodedImage decodeImage(IWICImagingFactory* wic, std::span<const uint8_t> bytes, DecodeKind kind) try {
    DecodedImage result;
    if (!wic || bytes.empty() || bytes.size() > MAXDWORD) return result;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICBitmapScaler> scaler;
    ComPtr<IWICFormatConverter> converter;
    // WIC reads the caller's buffer in place; `bytes` outlives every object here.
    if (FAILED(wic->CreateStream(stream.GetAddressOf())) ||
        FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size()))) ||
        FAILED(wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf())) ||
        FAILED(decoder->GetFrame(0, frame.GetAddressOf()))) return result;
    UINT width = 0, height = 0;
    if (FAILED(frame->GetSize(&width, &height)) || !width || !height || width > 16384 || height > 16384) return result;
    if (static_cast<uint64_t>(width) * height > (kind.ink ? max_source_logo_pixels : max_source_image_pixels)) return result;
    IWICBitmapSource* source = frame.Get();
    ComPtr<IWICBitmapClipper> clip;
    if (kind.ink && static_cast<uint64_t>(width) * height > max_decoded_image_bytes / 4) {
        clip = cropLargeLogo(wic, frame.Get(), width, height);
        if (clip) source = clip.Get();
    }
    UINT target_width = width, target_height = height;
    if (kind.cover_fit) std::tie(target_width, target_height) = coverFitSize(width, height, kind.cover_width, kind.cover_height);
    // A loose cap (the desktop grid's cell size) leaves an image less than 25% larger alone:
    // resampling 600×900 to 512×768 costs the decoder more than it saves the upload.
    const uint32_t scale_above = kind.loose_edge ? kind.short_edge + kind.short_edge / 4 : kind.short_edge;
    if (kind.short_edge && std::min(target_width, target_height) > scale_above) {
        const double factor = static_cast<double>(kind.short_edge) / std::min(target_width, target_height);
        target_width = std::max(1U, static_cast<UINT>(target_width * factor + 0.5));
        target_height = std::max(1U, static_cast<UINT>(target_height * factor + 0.5));
    }
    capPixels(target_width, target_height, max_decoded_image_bytes / 4);
    if (kind.steelbook && std::max(target_width, target_height) > steelbook_material::max_edge) {
        const double scale = double(steelbook_material::max_edge) / std::max(target_width, target_height);
        target_width = std::max(1u, static_cast<UINT>(target_width * scale));
        target_height = std::max(1u, static_cast<UINT>(target_height * scale));
    }
    // Whole 4×4 blocks for BC1/BC3: at most 3 px less on a side (under 1% of a cover's shape).
    if (kind.block_compress && !kind.steelbook && target_width >= 4 && target_height >= 4) {
        target_width -= target_width % 4;
        target_height -= target_height % 4;
    }
    // A JPEG decoded at half, a quarter or an eighth of its size by the codec itself (its DCT
    // scaling) when the target is at most that large: the full image is never reconstructed.
    // The final size still comes from the Fant scaler below.
    ComPtr<IWICBitmap> reduced;
    if (source == frame.Get() && target_width * 2 <= width && target_height * 2 <= height) {
        ComPtr<IWICBitmapSourceTransform> transform;
        UINT reduced_width = width, reduced_height = height;
        while (reduced_width / 2 >= target_width && reduced_height / 2 >= target_height && reduced_width > width / 8) { reduced_width /= 2; reduced_height /= 2; }
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGR;
        ComPtr<IWICComponentInfo> info;
        ComPtr<IWICPixelFormatInfo> pixel_format;
        UINT bits = 0;
        if (SUCCEEDED(frame.As(&transform)) && SUCCEEDED(transform->GetClosestSize(&reduced_width, &reduced_height)) &&
            reduced_width >= target_width && reduced_height >= target_height && reduced_width < width &&
            SUCCEEDED(transform->GetClosestPixelFormat(&format)) && SUCCEEDED(wic->CreateComponentInfo(format, info.GetAddressOf())) &&
            SUCCEEDED(info.As(&pixel_format)) && SUCCEEDED(pixel_format->GetBitsPerPixel(&bits)) && bits && bits % 8 == 0) {
            const UINT stride = reduced_width * (bits / 8);
            std::vector<uint8_t> scaled(static_cast<size_t>(stride) * reduced_height);
            if (SUCCEEDED(transform->CopyPixels(nullptr, reduced_width, reduced_height, &format, WICBitmapTransformRotate0, stride,
                                                static_cast<UINT>(scaled.size()), scaled.data())) &&
                SUCCEEDED(wic->CreateBitmapFromMemory(reduced_width, reduced_height, format, stride, static_cast<UINT>(scaled.size()),
                                                      scaled.data(), reduced.GetAddressOf()))) {
                source = reduced.Get();
                width = reduced_width; height = reduced_height;
            }
        }
    }
    if (target_width != width || target_height != height) {
        if (FAILED(wic->CreateBitmapScaler(scaler.GetAddressOf())) ||
            FAILED(scaler->Initialize(source, target_width, target_height, WICBitmapInterpolationModeFant))) return result;
        source = scaler.Get();
        width = target_width; height = target_height;
    }
    if (FAILED(wic->CreateFormatConverter(converter.GetAddressOf())) ||
        FAILED(converter->Initialize(source, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)))
        return result;
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
    if (FAILED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data()))) return result;
    if (kind.ink) result.ink = findInkBounds(pixels, width, height, width * 4);
    if (kind.accent) posterAccent(pixels, width, height, result.accent);
    if (kind.steelbook) {
        std::vector<uint8_t> relief(pixels.size());
        if (steelbook_material::emboss(pixels, width, height, relief)) result.relief = std::move(relief);
        std::vector<uint8_t> gloss(static_cast<size_t>(width) * height), matte(gloss.size());
        steelbook_material::bake(pixels, width, height, gloss, matte);
        const auto any = [](const std::vector<uint8_t>& mask) { return std::any_of(mask.begin(), mask.end(), [](uint8_t value) { return value != 0; }); };
        if (any(gloss)) result.gloss = std::move(gloss);
        if (any(matte)) result.matte = std::move(matte);
    }
    result.width = width; result.height = height;
    if (kind.block_compress && !kind.steelbook) {
        result.blocks = block_compression::encodeBc1(pixels.data(), width, height);
        if (!result.blocks.empty()) result.block_format = block_compression::Format::bc1;
        else if (kind.block_alpha) { // Transparency (logos): BC3, a quarter of BGRA.
            result.blocks = block_compression::encodeBc3(pixels.data(), width, height);
            if (!result.blocks.empty()) result.block_format = block_compression::Format::bc3;
        }
    }
    if (result.blocks.empty()) result.pixels = std::move(pixels); // Not whole blocks, or transparent without block_alpha: as decoded.
    return result;
} catch (...) { return {}; } // Allocation/codec failures complete the ticket without terminating a worker.

ImageDecoder::ImageDecoder() : budget_(std::make_shared<DecodeMemoryBudget>()) {}
ImageDecoder::~ImageDecoder() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        for (auto& job : jobs_) {
            job.ticket->done.store(true, std::memory_order_release);
            completed_.fetch_add(1); pending_.fetch_sub(1);
        }
        jobs_.clear();
    }
    wake_.notify_all();
    budget_->wake.notify_all();
    for (auto& worker : workers_) worker.join();
}

void ImageDecoder::start(HWND notify, unsigned threads) {
    std::lock_guard lock(mutex_);
    if (!workers_.empty()) return;
    notify_ = notify;
    try {
        // At most two codecs run together; ready output has a separate shared
        // budget. Reserve ownership before creating any joinable thread.
        threads = std::min(threads, 2u);
        workers_.reserve(threads);
        for (unsigned i = 0; i < threads; ++i) workers_.emplace_back([this] { run(); });
    } catch (...) { /* Fewer (or no) workers: submit() decodes inline. */ }
}

void ImageDecoder::useTextureCache(const std::filesystem::path& folder, uint64_t budget_bytes) {
    std::lock_guard lock(mutex_);
    textures_ = folder.empty() ? nullptr : std::make_shared<TextureCache>(folder, budget_bytes);
}

std::shared_ptr<DecodeTicket> ImageDecoder::submit(ImageBytes bytes, DecodeKind kind) {
    auto ticket = std::make_shared<DecodeTicket>();
    if (!bytes || bytes->empty()) { ticket->done.store(true, std::memory_order_release); return ticket; }
    std::shared_ptr<TextureCache> textures;
    {
        std::lock_guard lock(mutex_);
        if (!workers_.empty() && !stopping_) {
            jobs_.push_back({std::move(bytes), kind, ticket});
            pending_.fetch_add(1, std::memory_order_release);
            wake_.notify_one();
            return ticket;
        }
        textures = textures_;
    }
    Job job{std::move(bytes), kind, ticket};
    // Never wait for memory on the UI thread: it may own the tickets whose
    // upload would release it. Inline users get a completed invalid ticket.
    decode(job, inlineFactory().Get(), false, textures.get());
    ticket->done.store(true, std::memory_order_release);
    completed_.fetch_add(1, std::memory_order_acq_rel);
    return ticket;
}

size_t ImageDecoder::reservedBytes() const { std::lock_guard lock(budget_->mutex); return budget_->used; }
size_t ImageDecoder::peakReservedBytes() const { std::lock_guard lock(budget_->mutex); return budget_->peak; }

void ImageDecoder::decode(Job& job, IWICImagingFactory* wic, bool wait, TextureCache* textures) try {
    auto reservation = std::make_shared<DecodeMemoryReservation>(budget_);
    {
        std::unique_lock lock(budget_->mutex);
        const auto cancelled = [&] { return stopping_.load() || job.ticket.use_count() == 1; };
        while (budget_->used > decoded_memory_budget - max_decoded_image_bytes && !cancelled()) {
            if (!wait) return;
            // Dropping a queued ticket has no reservation to signal. Poll it
            // while waiting; shutdown and released output signal immediately.
            budget_->wake.wait_for(lock, std::chrono::milliseconds(25));
        }
        if (cancelled()) return;
        reservation->bytes = max_decoded_image_bytes;
        budget_->used += reservation->bytes; budget_->peak = std::max(budget_->peak, budget_->used);
    }
    const auto started = std::chrono::steady_clock::now();
    // Block-compressed before (this picture for this use): read back instead of decoded and compressed.
    const bool cached = textures && job.kind.block_compress && !job.kind.steelbook;
    const uint64_t key = cached ? TextureCache::key(*job.bytes, job.kind) : 0;
    DecodedImage image;
    if (!cached || !textures->load(key, image) || !image.valid()) {
        image = decodeImage(wic, *job.bytes, job.kind);
        if (cached && !image.blocks.empty()) textures->store(key, image);
    }
    decode_us_.fetch_add(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count()), std::memory_order_relaxed);
    if (image.valid()) {
        reservation->shrink(image.pixels.size() + image.blocks.size() + image.gloss.size() + image.matte.size() + image.relief.size());
        image.reservation = std::move(reservation);
    }
    job.ticket->image = std::move(image);
} catch (...) { /* The caller still publishes completion and keeps its counters consistent. */ }

void ImageDecoder::run() {
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ComPtr<IWICImagingFactory> wic;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.GetAddressOf()));
    while (true) {
        Job job;
        std::shared_ptr<TextureCache> textures;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [&] { return stopping_ || !jobs_.empty(); });
            if (stopping_) break;
            textures = textures_;
            // The most wanted first (lowest priority, then the oldest): after a fast scroll the rows
            // now in view do not wait behind the ones passed on the way.
            auto next = jobs_.begin();
            int best = next->ticket->priority.load(std::memory_order_relaxed);
            for (auto at = std::next(jobs_.begin()); at != jobs_.end() && best > 0; ++at)
                if (const int priority = at->ticket->priority.load(std::memory_order_relaxed); priority < best) { best = priority; next = at; }
            job = std::move(*next);
            jobs_.erase(next);
        }
        // The renderer replaced or dropped this slot while it was queued.
        if (job.ticket.use_count() > 1) {
            decode(job, wic.Get(), true, textures.get());
        }
        job.ticket->done.store(true, std::memory_order_release);
        completed_.fetch_add(1, std::memory_order_acq_rel);
        pending_.fetch_sub(1, std::memory_order_acq_rel);
        if (notify_) InvalidateRect(notify_, nullptr, FALSE);
    }
    wic.Reset();
    if (SUCCEEDED(apartment)) CoUninitialize();
}
