#include "../src/ui/image_decoder.hpp"
#include "image_fixture.hpp"
#include "../src/ui/steelbook_material.hpp"

#include <chrono>
#include <iostream>
#include <cmath>

using Microsoft::WRL::ComPtr;

int main() {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;
    struct Com { ~Com() { CoUninitialize(); } } com;
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.GetAddressOf())))) return 2;

    // Pure decode: PBGRA size, ink bounds for a logo with a transparent left gutter.
    const auto logo = pngFixture(40, 64, 32, true);
    const auto decoded = decodeImage(wic.Get(), logo, {false, false, true});
    if (!decoded.valid() || decoded.width != 64 || decoded.height != 32 || !decoded.ink.valid || decoded.ink.left < 16) return 3;
        // Backgrounds keep what covers the screen (cover-fit): a 3840×1240 hero on a 1080p screen keeps
        // its height (3345×1080, not 1920×620 enlarged 1.75×); one already smaller stays as it is; a
        // 4K 16:9 picture covers 1920×1080 at that size.
        DecodeKind background{false, true, false};
        const auto hero = decodeImage(wic.Get(), pngFixture(90, 3840, 1240), background);
        if (!hero.valid() || hero.width != 3345 || hero.height != 1080) return 4;
        const auto small_hero = decodeImage(wic.Get(), pngFixture(90, 1920, 620), background);
        if (!small_hero.valid() || small_hero.width != 1920 || small_hero.height != 620) return 60;
        const auto screenshot = decodeImage(wic.Get(), pngFixture(90, 3840, 2160), background);
        if (!screenshot.valid() || screenshot.width != 1920 || screenshot.height != 1080) return 61;
        background.cover_width = 2560; background.cover_height = 1440; // A 1440p screen: the whole hero, within the 16 MiB output cap.
        const auto sharp = decodeImage(wic.Get(), pngFixture(90, 3840, 1240), background);
        if (!sharp.valid() || sharp.width < 3600 || sharp.pixels.size() > max_decoded_image_bytes || std::abs(double(sharp.width) / sharp.height - 3840.0 / 1240) > 0.01) return 62;
        if (coverFitSize(2400, 600, 1920, 1080) != std::pair<uint32_t, uint32_t>{2400, 600}) return 63;
    // Covers are capped by their shorter side (Fant, aspect kept); smaller images are untouched.
    const auto large = decodeImage(wic.Get(), pngFixture(70, 2048, 3072), {true, false, false, 1024});
    if (!large.valid() || large.width != 1024 || large.height != 1536) return 10;
    const auto poster_size = decodeImage(wic.Get(), pngFixture(70, 600, 900), {true, false, false, 1024});
    if (!poster_size.valid() || poster_size.width != 600 || poster_size.height != 900) return 11;
    const auto thumbnail = decodeImage(wic.Get(), pngFixture(70, 600, 900), {true, false, false, 256});
    if (!thumbnail.valid() || thumbnail.width != 256 || thumbnail.height != 384) return 12;
    // Poster accent stays within the renderer's lifted range (never near-black).
    const auto poster = decodeImage(wic.Get(), pngFixture(40, 96, 96), {true, false, false});
    for (const float channel : poster.accent) if (!poster.valid() || channel < 0.27f || channel > 1.0f) return 5;
    // The desktop grid's loose cell size: an image within 25% of it keeps its own size, a larger one
    // is scaled to it. A JPEG reduced by the codec's DCT scaling ends at the exact Fant size, with
    // the same dominant accent as the full decode.
    {
        DecodeKind cell{true, false, false, 512}; cell.loose_edge = true;
        const auto kept = decodeImage(wic.Get(), pngFixture(70, 600, 900), cell);
        if (!kept.valid() || kept.width != 600 || kept.height != 900) return 40;
        const auto strict = decodeImage(wic.Get(), pngFixture(70, 600, 900), {true, false, false, 512});
        if (!strict.valid() || strict.width != 512 || strict.height != 768) return 41;
        const auto jpeg = jpegFixture(1200, 1800, 3);
        DecodeKind tiny{true, false, false, 256}; tiny.loose_edge = true;
        const auto reduced = decodeImage(wic.Get(), jpeg, tiny), full = decodeImage(wic.Get(), jpeg, {true, false, false});
        if (!reduced.valid() || reduced.width != 256 || reduced.height != 384 || !full.valid() || full.width != 1200) return 42;
        for (int c = 0; c < 3; ++c) if (std::abs(reduced.accent[c] - full.accent[c]) > 0.08f) return 43;
    }
    // Corrupt data never produces pixels.
    const std::vector<uint8_t> garbage(512, 0x5a);
    if (decodeImage(wic.Get(), garbage, {}).valid()) return 6;

    // Large source/output limits apply to logos and extreme aspect ratios too.
    const auto canvas = std::make_shared<const std::vector<uint8_t>>(pngFixture(50, 4096, 4096));
    {
        const auto capped = decodeImage(wic.Get(), *canvas, {});
        if (!capped.valid() || capped.width != 2048 || capped.height != 2048 || capped.pixels.size() > max_decoded_image_bytes) return 13;
        const auto tall = decodeImage(wic.Get(), pngFixture(30, 1024, 8192), {false, false, false, 1024});
        if (!tall.valid() || tall.pixels.size() > max_decoded_image_bytes || std::abs(double(tall.height) / tall.width - 8.0) > 0.02) return 14;
        if (decodeImage(wic.Get(), pngFixture(20, 4097, 4096), {}).valid()) return 15;
        // Crop a small logo before shrinking its huge transparent canvas. Its
        // 128 source pixels must remain sharp rather than becoming 64 pixels.
        const auto gutter = decodeImage(wic.Get(), pngFixture(80, 4096, 2048, true, WICRect{2048, 1024, 128, 128}), {false, false, true});
        if (!gutter.valid() || !gutter.ink.valid || gutter.width > 256 || gutter.height > 256 ||
            gutter.ink.right - gutter.ink.left != 128 || gutter.ink.bottom - gutter.ink.top != 128) return 16;
    }

    // A real SGDB logo size: accepted as a logo, cropped and scaled into the existing output
    // budget. Ordinary artwork still uses its tighter source limit.
    {
        const auto bytes = pngFixture(180, 5929, 3859, true);
        const auto high_res_logo = decodeImage(wic.Get(), bytes, {false, false, true});
        if (!high_res_logo.valid() || !high_res_logo.ink.valid || high_res_logo.pixels.size() > max_decoded_image_bytes) return 22;
        if (decodeImage(wic.Get(), bytes, {}).valid()) return 23;
        if (decodeImage(wic.Get(), pngFixture(40, 8193, 4096, true), {false, false, true}).valid()) return 24;
    }

    // Foil is selected by hue/contrast, not just brightness. The worker produces a bounded
    // premultiplied tint/coverage map; the renderer lights it dynamically.
    {
        constexpr UINT w = 96, h = 128;
        const auto solid = [](uint8_t value, uint8_t alpha = 255) {
            std::vector<uint8_t> pixels(size_t(w) * h * 4, value);
            for (size_t i = 3; i < pixels.size(); i += 4) pixels[i] = alpha;
            return pixels;
        };
        // Near-black ink is matte for the foil but fully polished lacquer for the oval sheen;
        // other plain print is grainy matte ink.
        std::vector<uint8_t> gloss(size_t(w) * h), matte(gloss.size());
        const auto glossy = [&](uint8_t level) { return std::all_of(gloss.begin(), gloss.end(), [&](uint8_t v) { return v == level; }); };
        const auto unmatted = [&] { return std::all_of(matte.begin(), matte.end(), [](uint8_t v) { return v == 0; }); };
        for (const auto value : {uint8_t{0}, uint8_t{3}}) {
            auto black = solid(value); steelbook_material::bake(black, w, h, gloss, matte);
            if (std::any_of(black.begin(), black.end(), [](uint8_t v) { return v != 0; })) return 25;
            if (!glossy(255) || !unmatted()) return 34;
        }
        auto transparent = solid(0, 0); steelbook_material::bake(transparent, w, h, gloss, matte);
        if (std::any_of(transparent.begin(), transparent.end(), [](uint8_t v) { return v != 0; })) return 26;
        if (!glossy(0) || !unmatted()) return 35;
        auto mid = solid(128); steelbook_material::bake(mid, w, h, gloss, matte);
        if (std::any_of(mid.begin(), mid.end(), [](uint8_t v) { return v != 0; })) return 27;
        if (!glossy(0)) return 36; // Mid grey is neither foil nor polished lacquer...
        const auto [low, high] = std::minmax_element(matte.begin(), matte.end());
        if (*low < 100 || *high < 200 || *high - *low < 40) return 44; // ...but grainy matte print.
        auto colours = solid(24);
        const auto patch = [&](int l, int r, int t, int b, uint8_t red, uint8_t green, uint8_t blue) {
            for (int y = t; y < b; ++y) for (int x = l; x < r; ++x) {
                auto* p = colours.data() + (size_t(y) * w + x) * 4;
                p[0] = blue; p[1] = green; p[2] = red;
            }
        };
        patch(12, 44, 12, 116, 210, 50, 35);
        patch(60, 84, 20, 100, 35, 83, 195); // Almost equal luminance, a different pigment.
        steelbook_material::bake(colours, w, h);
        const auto* red = colours.data() + (size_t(60) * w + 28) * 4;
        const auto* blue = colours.data() + (size_t(60) * w + 72) * 4;
        if (red[3] < 220 || red[2] <= red[0] || blue[3] != 0) return 28;
        // The main background colour must work too; skin-like pale ink stays matte.
        auto ink_sheet = solid(0);
        for (size_t i = 0; i < ink_sheet.size(); i += 4) { ink_sheet[i] = 35; ink_sheet[i + 1] = 50; ink_sheet[i + 2] = 210; }
        const size_t pale = (size_t(60) * w + 40) * 4;
        ink_sheet[pale] = 192; ink_sheet[pale + 1] = 208; ink_sheet[pale + 2] = 238;
        steelbook_material::bake(ink_sheet, w, h);
        if (ink_sheet[3] < 220 || ink_sheet[pale + 3] != 0) return 33;
        auto silver = solid(0);
        for (UINT y = 18; y < h - 18; ++y) for (UINT x = 24; x < 42; ++x)
            for (int c = 0; c < 3; ++c) silver[(size_t(y) * w + x) * 4 + c] = 240;
        steelbook_material::bake(silver, w, h, gloss, matte);
        const auto* foil = silver.data() + (size_t(60) * w + 30) * 4;
        if (foil[3] < 220 || foil[0] != foil[1] || foil[1] != foil[2] || silver[3] != 0) return 31;
        if (gloss[size_t(60) * w + 30] != 0 || gloss[0] != 255) return 37; // Brushed foil does not mirror the oval.
        if (matte[size_t(60) * w + 30] > 16 || matte[0] != 0) return 45; // Neither foil nor lacquer is matte print.
        // High-contrast edges are embossed (direction towards the lighter ink); gentle shading is not.
        std::vector<uint8_t> relief(size_t(w) * h * 4);
        const auto halves = [&](uint8_t left, uint8_t right) {
            auto pixels = solid(left);
            for (UINT y = 0; y < h; ++y) for (UINT x = w / 2; x < w; ++x)
                for (int c = 0; c < 3; ++c) pixels[(size_t(y) * w + x) * 4 + c] = right;
            return pixels;
        };
        if (!steelbook_material::emboss(halves(10, 230), w, h, relief)) return 40;
        const auto* edge = relief.data() + (size_t(60) * w + w / 2) * 4;
        const auto* flat = relief.data() + (size_t(60) * w + 10) * 4;
        if (edge[2] < 230 || std::abs(int(edge[1]) - 128) > 2 || edge[0] != 128 || edge[3] != 255 || flat[2] != 128 || flat[1] != 128) return 41;
        if (!steelbook_material::emboss(halves(230, 10), w, h, relief) || relief[(size_t(60) * w + w / 2) * 4 + 2] > 26) return 43;
        if (steelbook_material::emboss(halves(100, 125), w, h, relief)) return 42;
        const auto material = decodeImage(wic.Get(), pngFixture(90, 1920, 1080), {false, false, false, 0, true});
        if (!material.valid() || material.width != 768 || material.height != 432) return 29;
        size_t covered = 0;
        for (size_t i = 0; i < material.pixels.size(); i += 4) {
            const auto* p = material.pixels.data() + i;
            covered += p[3] > 0;
            if (p[0] > p[3] || p[1] > p[3] || p[2] > p[3]) return 30;
        }
        if (!covered || covered > size_t(material.width) * material.height * 3 / 4) return 32;
        if (!material.gloss.empty()) return 38; // No dark ink in this fixture: no gloss bitmap at all.
        const auto dark = decodeImage(wic.Get(), pngFixture(0, 256, 256), {false, false, false, 0, true});
        if (!dark.valid() || dark.gloss.size() != size_t(dark.width) * dark.height) return 39;
    }

    // Background workers: every ticket completes, dropped tickets are skipped,
    // and the counters let the UI know when to upload.
    {
        ImageDecoder decoder;
        decoder.start(nullptr, 2);
        const auto bytes = std::make_shared<const std::vector<uint8_t>>(pngFixture(120, 256, 256));
        std::vector<std::shared_ptr<DecodeTicket>> kept;
        for (int i = 0; i < 16; ++i) kept.push_back(decoder.submit(bytes, {true, false, false}));
        for (int i = 0; i < 16; ++i) decoder.submit(bytes, {}); // Dropped immediately by the caller.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (decoder.completed() < 32 && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        if (decoder.completed() != 32 || decoder.pending() != 0) return 7;
        for (const auto& ticket : kept)
            if (!ticket->done.load() || !ticket->image.valid() || ticket->image.width != 256) return 8;
    }
    // Without workers the decode is inline and complete on return.
    {
        ImageDecoder inline_decoder;
        const auto ticket = inline_decoder.submit(std::make_shared<const std::vector<uint8_t>>(logo), {false, false, true});
        if (!ticket->done.load() || !ticket->image.valid() || inline_decoder.completed() != 1) return 9;
    }
    {
        // Completed output counts toward the budget until the UI consumes it.
        // Filling it pauses workers, releasing a ticket resumes them, and stop
        // must work even when callers keep every already completed result.
        std::vector<std::shared_ptr<DecodeTicket>> held;
        auto decoder = std::make_unique<ImageDecoder>();
        decoder->start(nullptr, 8); // Production caps codec concurrency to two.
        for (int i = 0; i < 6; ++i) held.push_back(decoder->submit(canvas, {}));
        const auto wait = [&](uint64_t count) {
            const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (decoder->completed() < count && std::chrono::steady_clock::now() < end) std::this_thread::sleep_for(std::chrono::milliseconds(2));
            return decoder->completed() == count;
        };
        if (!wait(4) || decoder->reservedBytes() != decoded_memory_budget || decoder->peakReservedBytes() > decoded_memory_budget) return 17;
        for (int i = 0; i < 4; ++i) if (!held[i]->done.load() || !held[i]->image.valid()) return 18;
        held[0].reset();
        if (!wait(5) || decoder->peakReservedBytes() > decoded_memory_budget) return 19;
        const auto started = std::chrono::steady_clock::now();
        decoder.reset();
        if (std::chrono::steady_clock::now() - started > std::chrono::seconds(2)) return 20;
        for (const auto& ticket : held) if (ticket && !ticket->done.load()) return 21;
    }
    std::cout << "WIC decode, hero cap, ink bounds, worker tickets and inline fallback passed\n";
    return 0;
}
