#include "../src/ui/text_layout_cache.hpp"
#include <d2d1.h>
#include <wincodec.h>
#include <iostream>

using Microsoft::WRL::ComPtr;
int main() {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;
    struct Apartment { ~Apartment() { CoUninitialize(); } } apartment;
    ComPtr<IDWriteFactory> write;
    ComPtr<ID2D1Factory> draw;
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(write.GetAddressOf()))) ||
        FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, draw.GetAddressOf())) ||
        FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.GetAddressOf())))) return 2;
    ComPtr<IDWriteTextFormat> format;
    if (FAILED(write->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                     DWRITE_FONT_STRETCH_NORMAL, 17, L"pl-PL", format.GetAddressOf()))) return 3;
    DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER};
    ComPtr<IDWriteInlineObject> sign;
    write->CreateEllipsisTrimmingSign(format.Get(), sign.GetAddressOf());
    format->SetTrimming(&trimming, sign.Get());
    TextLayoutCache cache;
    const std::wstring label = L"Zażółć gęślą jaźń — a longer title\nsecond line";
    const auto render = [&](bool cached, float x, float width) {
        ComPtr<IWICBitmap> image;
        ComPtr<ID2D1RenderTarget> target;
        ComPtr<ID2D1SolidColorBrush> ink;
        std::vector<BYTE> pixels(256 * 160 * 4);
        if (FAILED(wic->CreateBitmap(256, 160, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, image.GetAddressOf())) ||
            FAILED(draw->CreateWicBitmapRenderTarget(image.Get(), D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE), target.GetAddressOf())) ||
            FAILED(target->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1), ink.GetAddressOf()))) return std::vector<BYTE>{};
        const float y = 13.37f, height = 77.3f;
        target->BeginDraw(); target->Clear(D2D1::ColorF(0, 0, 0));
        if (cached) {
            auto* layout = cache.get(write.Get(), label, format.Get(), (x + width) - x, (y + height) - y);
            if (!layout) return std::vector<BYTE>{};
            target->DrawTextLayout(D2D1::Point2F(x, y), layout, ink.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        } else target->DrawTextW(label.c_str(), static_cast<UINT32>(label.size()), format.Get(), D2D1::RectF(x, y, x + width, y + height), ink.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        if (FAILED(target->EndDraw()) || FAILED(image->CopyPixels(nullptr, 256 * 4, static_cast<UINT>(pixels.size()), pixels.data()))) return std::vector<BYTE>{};
        return pixels;
    };
    for (auto wrapping : {DWRITE_WORD_WRAPPING_NO_WRAP, DWRITE_WORD_WRAPPING_WRAP})
        for (auto alignment : {DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_TEXT_ALIGNMENT_TRAILING})
            for (auto paragraph : {DWRITE_PARAGRAPH_ALIGNMENT_NEAR, DWRITE_PARAGRAPH_ALIGNMENT_CENTER})
                for (float x : {7.0f, 7.37f}) {
                    format->SetWordWrapping(wrapping); format->SetTextAlignment(alignment); format->SetParagraphAlignment(paragraph);
                    const auto original = render(false, x, 178.23f), cached = render(true, x, 178.23f);
                    if (original.empty() || cached != original) return 4;
                }
    auto* reused = cache.get(write.Get(), label, format.Get(), 100, 80);
    if (!reused || reused != cache.get(write.Get(), label, format.Get(), 100, 80)) return 5;
    format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, 30, 22);
    if (reused == cache.get(write.Get(), label, format.Get(), 100, 80)) return 6;
    // Variants: a configured layout (textLine's NEAR + centred override) is its own entry, set up
    // once, and never changes the plain entry of the same text; drawn, it matches a fresh layout.
    {
        int configured = 0;
        const auto centre = [&](IDWriteTextLayout* layout) {
            ++configured; layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER); layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        };
        format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_DEFAULT, 0, 0);
        format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        auto* plain = cache.get(write.Get(), label, format.Get(), 120, 40);
        auto* centred = cache.get(write.Get(), label, format.Get(), 120, 40, 17, centre);
        if (!plain || !centred || plain == centred || cache.get(write.Get(), label, format.Get(), 120, 40, 17, centre) != centred || configured != 1 ||
            plain->GetTextAlignment() != DWRITE_TEXT_ALIGNMENT_LEADING || centred->GetTextAlignment() != DWRITE_TEXT_ALIGNMENT_CENTER) return 10;
        ComPtr<IDWriteTextLayout> fresh;
        if (FAILED(write->CreateTextLayout(label.c_str(), static_cast<UINT32>(label.size()), format.Get(), 120, 40, fresh.GetAddressOf()))) return 11;
        centre(fresh.Get());
        DWRITE_TEXT_METRICS a{}, b{};
        if (FAILED(centred->GetMetrics(&a)) || FAILED(fresh->GetMetrics(&b)) || a.left != b.left || a.width != b.width || a.lineCount != b.lineCount) return 12;
    }
    for (size_t i = 0; i < TextLayoutCache::capacity + 32; ++i)
        if (!cache.get(write.Get(), std::to_wstring(i), format.Get(), 100, 80)) return 7;
    // Least recently used goes: the first of those is gone, the latest is still the same layout.
    if (auto* latest = cache.get(write.Get(), std::to_wstring(TextLayoutCache::capacity + 31), format.Get(), 100, 80);
        !latest || latest != cache.get(write.Get(), std::to_wstring(TextLayoutCache::capacity + 31), format.Get(), 100, 80)) return 13;
    if (cache.size() != TextLayoutCache::capacity ||
        cache.get(write.Get(), std::wstring(TextLayoutCache::max_characters + 1, L'a'), format.Get(), 100, 80)) return 8;
    cache.clear();
    if (cache.size()) return 9;
    std::cout << "Cached text is pixel-identical to DrawText; mutable formatting and bounded eviction passed\n";
}
