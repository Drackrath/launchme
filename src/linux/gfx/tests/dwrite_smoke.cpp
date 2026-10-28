// Smoke test of the DirectWrite port (dwrite_*.cpp), drawn through the Direct2D port: paragraphs
// in every alignment, wrapping and trimming mode, formatted ranges with brushes, hit-test
// selections, the Windows family aliases and transformed text, each frame written as a PNG
// (argv[1], default ".") to be looked at, plus measurements that must hold. The window layer is
// stubbed: a window target's frame goes to its PNG (frames named "_..." are only compared).
#include "../gfx_internal.hpp"
#include "../dwrite_internal.hpp" // resolveFamily, for the alias table.

#include <d2d1_2.h>
#include <d2d1_2helper.h>
#include <dwrite_3.h>
#include <wrl/client.h>

#include <cairo.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {
std::string output_directory = ".";
constexpr UINT32 width = 960, height = 640;
std::map<std::string, std::vector<uint8_t>> frames;
int failures = 0;

void check(bool condition, const char* what) {
    std::printf("%s: %s\n", condition ? "ok  " : "FAIL", what);
    if (!condition) ++failures;
}
bool near(float a, float b, float tolerance = 0.01f) { return std::fabs(a - b) <= tolerance; }
} // namespace

namespace lm_gfx {
// The scene's name stands in for the window.
void windowPixelSize(HWND, UINT32& w, UINT32& h) { w = width; h = height; }
bool presentWindow(HWND window, const uint8_t* pixels, UINT32 w, UINT32 h, UINT32 stride) {
    const std::string name = static_cast<const char*>(window);
    frames[name].assign(pixels, pixels + static_cast<size_t>(stride) * h);
    if (name[0] == '_') return true;
    cairo_surface_t* frame = cairo_image_surface_create_for_data(const_cast<uint8_t*>(pixels), CAIRO_FORMAT_ARGB32,
                                                                 static_cast<int>(w), static_cast<int>(h), static_cast<int>(stride));
    const std::string file = output_directory + "/" + name + ".png";
    const bool written = cairo_surface_write_to_png(frame, file.c_str()) == CAIRO_STATUS_SUCCESS;
    cairo_surface_destroy(frame);
    std::printf("wrote %s\n", file.c_str());
    return written;
}
} // namespace lm_gfx

namespace {
const wchar_t* const polish = L"Zażółć gęślą jaźń. Pchnąć w tę łódź jeża lub ośm skrzyń fig. Żółta grzywa źrebięcia "
                              L"łasi się do dłoni, a mgła nad stawem rzednie powoli o świcie.";

struct Scene {
    ComPtr<ID2D1HwndRenderTarget> window;
    ComPtr<ID2D1RenderTarget> target;
    ComPtr<ID2D1SolidColorBrush> solid;

    Scene(ID2D1Factory* factory, const char* name) {
        factory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(), D2D1::HwndRenderTargetProperties(const_cast<char*>(name), D2D1::SizeU(width, height)),
                                        window.GetAddressOf());
        window->QueryInterface(IID_PPV_ARGS(target.GetAddressOf()));
        target->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1), solid.GetAddressOf());
        target->BeginDraw();
        target->Clear(D2D1::ColorF(0.08f, 0.09f, 0.11f));
    }
    ~Scene() { check(SUCCEEDED(target->EndDraw()), "EndDraw"); }
    ID2D1SolidColorBrush* ink(D2D1_COLOR_F color) { solid->SetColor(color); return solid.Get(); }
    ComPtr<ID2D1SolidColorBrush> brush(D2D1_COLOR_F color) {
        ComPtr<ID2D1SolidColorBrush> created;
        target->CreateSolidColorBrush(color, created.GetAddressOf());
        return created;
    }
    // The layout box (dim) and the measured text box (accent), then the text.
    void draw(IDWriteTextLayout* layout, float x, float y, D2D1_COLOR_F color = D2D1::ColorF(0.94f, 0.94f, 0.96f)) {
        DWRITE_TEXT_METRICS m{};
        layout->GetMetrics(&m);
        target->DrawRectangle(D2D1::RectF(x, y, x + m.layoutWidth, y + m.layoutHeight), ink(D2D1::ColorF(1, 1, 1, 0.18f)), 1);
        target->DrawRectangle(D2D1::RectF(x + m.left, y + m.top, x + m.left + m.width, y + m.top + m.height), ink(D2D1::ColorF(0.3f, 0.6f, 1, 0.45f)), 1);
        target->DrawTextLayout(D2D1::Point2F(x, y), layout, ink(color));
    }
    void label(IDWriteTextFormat* format, const std::wstring& text, float x, float y, D2D1_COLOR_F color) {
        target->DrawText(text.c_str(), static_cast<UINT32>(text.size()), format, D2D1::RectF(x, y, x + 900, y + 30), ink(color));
    }
};

ComPtr<IDWriteTextFormat> makeFormat(IDWriteFactory* write, const wchar_t* family, float size, DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL,
                                     DWRITE_FONT_STYLE style = DWRITE_FONT_STYLE_NORMAL, IDWriteFontCollection* collection = nullptr) {
    ComPtr<IDWriteTextFormat> format;
    write->CreateTextFormat(family, collection, weight, style, DWRITE_FONT_STRETCH_NORMAL, size, L"pl-PL", format.GetAddressOf());
    return format;
}

ComPtr<IDWriteTextLayout> makeLayout(IDWriteFactory* write, const std::wstring& text, IDWriteTextFormat* format, float w, float h) {
    ComPtr<IDWriteTextLayout> layout;
    write->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format, w, h, layout.GetAddressOf());
    return layout;
}

DWRITE_TEXT_METRICS metricsOf(IDWriteTextLayout* layout) {
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    return metrics;
}

std::vector<DWRITE_LINE_METRICS> linesOf(IDWriteTextLayout* layout) {
    UINT32 count = 0;
    layout->GetLineMetrics(nullptr, 0, &count);
    std::vector<DWRITE_LINE_METRICS> lines(count);
    if (count) layout->GetLineMetrics(lines.data(), count, &count);
    return lines;
}

std::vector<DWRITE_HIT_TEST_METRICS> hitsOf(IDWriteTextLayout* layout, UINT32 position, UINT32 length, float x = 0, float y = 0) {
    UINT32 count = 0;
    layout->HitTestTextRange(position, length, x, y, nullptr, 0, &count);
    std::vector<DWRITE_HIT_TEST_METRICS> hits(count);
    if (count) layout->HitTestTextRange(position, length, x, y, hits.data(), count, &count);
    return hits;
}

void print(const char* name, const DWRITE_TEXT_METRICS& m) {
    std::printf("  %-22s left %.2f top %.2f width %.2f (with whitespace %.2f) height %.2f, %u line(s) in %.1f x %.1f\n", name, m.left, m.top,
                m.width, m.widthIncludingTrailingWhitespace, m.height, m.lineCount, m.layoutWidth, m.layoutHeight);
}

ComPtr<IDWriteInlineObject> trim(IDWriteFactory* write, IDWriteTextFormat* format) {
    DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    ComPtr<IDWriteInlineObject> sign;
    write->CreateEllipsisTrimmingSign(format, sign.GetAddressOf());
    format->SetTrimming(&trimming, sign.Get());
    return sign;
}

// Wrapping, alignment, justification, trimming and paragraph alignment.
void paragraphs(ID2D1Factory* factory, IDWriteFactory* write) {
    Scene s(factory, "paragraphs");
    auto body = makeFormat(write, L"Segoe UI", 16);
    auto lineHeight = [&] { return linesOf(makeLayout(write, L"Hg", body.Get(), 100, 100).Get()).at(0).height; };
    const float line = lineHeight();

    auto leading = makeLayout(write, polish, body.Get(), 280, 220);
    const auto ml = metricsOf(leading.Get());
    print("wrapped leading", ml);
    check(ml.width <= 280 && ml.lineCount >= 4 && near(ml.left, 0) && near(ml.height, ml.lineCount * line), "wrap: lines within the width, heights add up");
    s.draw(leading.Get(), 20, 20);

    trim(write, body.Get());
    auto justified = makeLayout(write, polish, body.Get(), 280, line * 3 + 0.5f);
    justified->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_JUSTIFIED);
    const auto mj = metricsOf(justified.Get());
    const auto jl = linesOf(justified.Get());
    print("justified, 3 lines", mj);
    check(mj.lineCount == 3 && jl.back().isTrimmed && mj.height <= mj.layoutHeight, "wrapped trimming: the third line ends in the sign");
    check(near(mj.width, 280, 0.6f), "justified lines fill the width");
    s.draw(justified.Get(), 330, 20);

    auto centre = makeLayout(write, polish, body.Get(), 260, 220);
    centre->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    const auto mc = metricsOf(centre.Get());
    print("centred", mc);
    check(mc.left > 0 && mc.left + mc.width <= 260.01f, "centred lines inside the box");
    s.draw(centre.Get(), 650, 20);

    auto trailing = makeLayout(write, L"Trailing alignment: the right edge is straight.", body.Get(), 260, 60);
    trailing->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    const auto mt = metricsOf(trailing.Get());
    check(mt.lineCount == 2 && near(mt.left + mt.width, 260, 0.6f), "trailing: lines end at the right edge");
    s.draw(trailing.Get(), 650, 250);

    // Without wrapping: trimmed at the width, and still aligned within it.
    auto narrow = makeLayout(write, L"A single line far too long for its narrow box", body.Get(), 220, 30);
    narrow->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    const auto mn = metricsOf(narrow.Get());
    print("no wrap, trimmed", mn);
    check(mn.lineCount == 1 && mn.width <= 220.01f && linesOf(narrow.Get()).at(0).isTrimmed, "no wrap: one line trimmed with the sign");
    s.draw(narrow.Get(), 20, 330);

    DWRITE_TRIMMING none{DWRITE_TRIMMING_GRANULARITY_NONE, 0, 0};
    body->SetTrimming(&none, nullptr);
    auto unwrapped = makeLayout(write, L"Centred, not wrapped", body.Get(), 280, 30);
    unwrapped->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    unwrapped->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    const auto mu = metricsOf(unwrapped.Get());
    check(near(mu.left, (280 - mu.width) / 2), "no wrap: centred within the maximum width");
    s.draw(unwrapped.Get(), 20, 380);
    auto overflow = makeLayout(write, L"Trailing and wider than the box, not trimmed", body.Get(), 200, 30);
    overflow->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    overflow->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    const auto mo = metricsOf(overflow.Get());
    check(mo.left < 0 && near(mo.left + mo.width, 200), "no wrap: wider text overflows to the leading side");
    s.draw(overflow.Get(), 120, 430);

    // Paragraph alignment.
    auto middle = makeLayout(write, L"Centred vertically", body.Get(), 180, 110);
    middle->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    const auto mm = metricsOf(middle.Get());
    check(near(mm.top, (110 - mm.height) / 2), "paragraph centre");
    s.draw(middle.Get(), 330, 330);
    auto bottom = makeLayout(write, L"At the bottom", body.Get(), 180, 110);
    bottom->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_FAR);
    bottom->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    const auto mb = metricsOf(bottom.Get());
    check(near(mb.top, 110 - mb.height), "paragraph far");
    s.draw(bottom.Get(), 530, 330);

    // Several paragraphs trimmed below the box: the last visible line ends in the sign.
    trim(write, body.Get());
    auto stack = makeLayout(write, L"First paragraph.\nSecond paragraph, which wraps onto more lines.\nThird.\nFourth.", body.Get(), 200,
                            line * 2.5f);
    const auto ms = metricsOf(stack.Get());
    const auto sl = linesOf(stack.Get());
    print("paragraphs, 2 lines", ms);
    check(ms.lineCount == 2 && sl.back().isTrimmed && sl[0].newlineLength == 1, "paragraphs trimmed at the last line that fits");
    const auto past = hitsOf(stack.Get(), 60, 4);
    check(past.size() == 1 && past[0].isTrimmed, "hit test past the sign: trimmed");
    s.draw(stack.Get(), 730, 330);

    // Line metrics count wchar_t units: whitespace and newlines.
    auto counted = makeLayout(write, L"abc  \r\ndef\n", body.Get(), 300, 100);
    const auto cl = linesOf(counted.Get());
    check(cl.size() == 3 && cl[0].length == 7 && cl[0].trailingWhitespaceLength == 4 && cl[0].newlineLength == 2 && cl[1].length == 4 &&
              cl[1].newlineLength == 1 && cl[2].length == 0,
          "line metrics: lengths, trailing whitespace and newlines");
    const auto mw = metricsOf(counted.Get());
    check(mw.widthIncludingTrailingWhitespace > mw.width + 4, "trailing whitespace outside the width");

    auto label = makeFormat(write, L"Segoe UI", 13, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    s.label(label.Get(), L"wrap · justify + trim to 3 lines · centre / trailing · no wrap + trim · centred, overflowing · paragraph centre / far · paragraphs trimmed",
            20, 600, D2D1::ColorF(0.6f, 0.65f, 0.7f));
}

// Formatted ranges, drawing effects, emoji, surrogate pairs and hit-test selections.
void rich(ID2D1Factory* factory, IDWriteFactory* write) {
    Scene s(factory, "rich");
    auto body = makeFormat(write, L"Segoe UI", 22);
    body->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    const std::wstring weights = L"Light Normal Medium SemiBold Bold Black";
    auto weighted = makeLayout(write, weights, body.Get(), 900, 40);
    const DWRITE_FONT_WEIGHT steps[] = {DWRITE_FONT_WEIGHT_LIGHT, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_WEIGHT_MEDIUM,
                                        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_WEIGHT_BLACK};
    const float plain = metricsOf(weighted.Get()).width;
    for (UINT32 at = 0, word = 0; at < weights.size(); ++word) {
        UINT32 end = at;
        while (end < weights.size() && weights[end] != L' ') ++end;
        weighted->SetFontWeight(steps[word], {at, end - at});
        at = end + 1;
    }
    check(metricsOf(weighted.Get()).width > plain, "bold ranges are wider");
    s.draw(weighted.Get(), 20, 20);

    const std::wstring colours = L"Red, green and a gradient over these words, then an underlined link.";
    auto coloured = makeLayout(write, colours, body.Get(), 900, 40);
    auto red = s.brush(D2D1::ColorF(1, 0.3f, 0.3f)), green = s.brush(D2D1::ColorF(0.3f, 0.95f, 0.4f)), link = s.brush(D2D1::ColorF(0.4f, 0.7f, 1));
    ComPtr<ID2D1GradientStopCollection> stops;
    const D2D1_GRADIENT_STOP gradient[] = {{0, D2D1::ColorF(1, 0.8f, 0.2f)}, {1, D2D1::ColorF(0.8f, 0.3f, 1)}};
    s.target->CreateGradientStopCollection(gradient, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, stops.GetAddressOf());
    ComPtr<ID2D1LinearGradientBrush> sweep;
    s.target->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({160, 0}, {420, 0}), stops.Get(), sweep.GetAddressOf());
    const auto find = [&](const std::wstring& text, const wchar_t* part) {
        return DWRITE_TEXT_RANGE{static_cast<UINT32>(text.find(part)), static_cast<UINT32>(std::wcslen(part))};
    };
    coloured->SetDrawingEffect(red.Get(), find(colours, L"Red"));
    coloured->SetDrawingEffect(green.Get(), find(colours, L"green"));
    coloured->SetDrawingEffect(sweep.Get(), find(colours, L"a gradient over these words"));
    coloured->SetDrawingEffect(link.Get(), find(colours, L"underlined link"));
    coloured->SetUnderline(TRUE, find(colours, L"underlined link"));
    s.draw(coloured.Get(), 20, 70);

    const std::wstring sizes = L"small BIG small, then Ż";
    auto sized = makeLayout(write, sizes, body.Get(), 900, 60);
    sized->SetFontSize(40, find(sizes, L"BIG"));
    sized->SetFontSize(48, find(sizes, L"Ż"));
    check(linesOf(sized.Get()).at(0).height > 40, "a larger range makes the line taller");
    s.draw(sized.Get(), 20, 120);

    // Character spacing (IDWriteTextLayout1), as the launcher's capitals use it.
    const std::wstring caps = L"CHARACTER SPACING 1.1";
    auto spaced = makeLayout(write, caps, body.Get(), 900, 40);
    const float tight = metricsOf(spaced.Get()).width;
    ComPtr<IDWriteTextLayout1> spacing;
    check(SUCCEEDED(spaced.As(&spacing)) && SUCCEEDED(spacing->SetCharacterSpacing(0, 1.1f, 0, {0, static_cast<UINT32>(caps.size())})),
          "IDWriteTextLayout1::SetCharacterSpacing");
    const float loose = metricsOf(spaced.Get()).width;
    std::printf("  spacing: %.2f -> %.2f (DirectWrite adds %.2f)\n", tight, loose, 1.1f * caps.size());
    check(near(loose - tight, 1.1f * (caps.size() - 1), 1.2f), "character spacing widens by about 1.1 per letter");
    s.draw(spaced.Get(), 20, 190);

    // Emoji, symbols and CJK through font fallback; UTF-16 pairs count as two positions.
    const std::wstring emoji = L"Emoji 😀🎮🚀 ✓ ★ ♥ → 日本語 한국어";
    auto colourful = makeLayout(write, emoji, body.Get(), 900, 40);
    check(metricsOf(colourful.Get()).lineCount == 1, "emoji and CJK in one line");
    s.draw(colourful.Get(), 20, 240);
    const std::wstring pair = std::wstring(L"pair: a") + L'\xD83D' + L'\xDE00' + L"b";
    auto paired = makeLayout(write, pair, body.Get(), 900, 40);
    const auto b = hitsOf(paired.Get(), static_cast<UINT32>(pair.size() - 1), 1), smile = hitsOf(paired.Get(), 7, 2);
    check(b.size() == 1 && b[0].textPosition == pair.size() - 1 && smile.size() == 1 && smile[0].width > 10 && near(smile[0].left + smile[0].width, b[0].left),
          "a UTF-16 pair is one glyph at two positions");
    s.draw(paired.Get(), 560, 240);

    // Right-to-left words in a left-to-right paragraph: their hit rectangle is one bidi run.
    const std::wstring mixed = L"Hebrew שלום עולם and Arabic مرحبا";
    auto bidi = makeLayout(write, mixed, body.Get(), 600, 40);
    const auto hebrew = hitsOf(bidi.Get(), 7, 9);
    check(hebrew.size() == 1 && hebrew[0].bidiLevel == 1 && hebrew[0].length == 9 && hebrew[0].width > 40, "right-to-left run hit-tested");
    s.draw(bidi.Get(), 560, 300);
    auto empty = makeLayout(write, L"", body.Get(), 100, 40);
    const auto me = metricsOf(empty.Get());
    check(me.lineCount == 1 && me.height > 20 && me.width == 0, "empty text: one empty line of the font's height");

    // A text field: selection behind the text, caret after it (positions in wchar_t units).
    auto field = makeFormat(write, L"Segoe UI", 34);
    const std::wstring typed = L"Zażółć gęślą jaźń";
    auto input = makeLayout(write, typed, field.Get(), 500, 50);
    const auto selection = hitsOf(input.Get(), 7, 5, 20, 300);
    check(selection.size() == 1 && selection[0].textPosition == 7 && selection[0].length == 5, "selection: one rectangle for \"gęślą\"");
    for (const auto& hit : selection)
        s.target->FillRectangle(D2D1::RectF(hit.left, hit.top, hit.left + hit.width, hit.top + hit.height), s.ink(D2D1::ColorF(0.25f, 0.45f, 0.9f, 0.6f)));
    const auto caret = hitsOf(input.Get(), static_cast<UINT32>(typed.size() - 1), 1, 20, 300);
    if (!caret.empty())
        s.target->FillRectangle(D2D1::RectF(caret[0].left + caret[0].width, caret[0].top + 4, caret[0].left + caret[0].width + 2, caret[0].top + caret[0].height - 4),
                                s.ink(D2D1::ColorF(1, 1, 1)));
    s.draw(input.Get(), 20, 300);

    // A selection across wrapped lines: a rectangle per line, all inside the measured text.
    auto paragraph = makeFormat(write, L"Segoe UI", 18);
    auto wrapped = makeLayout(write, polish, paragraph.Get(), 420, 200);
    const auto m = metricsOf(wrapped.Get());
    UINT32 needed = 0;
    const HRESULT small = wrapped->HitTestTextRange(10, 70, 0, 0, nullptr, 0, &needed);
    check(small == E_NOT_SUFFICIENT_BUFFER && needed >= 2, "hit test: E_NOT_SUFFICIENT_BUFFER with the count needed");
    const auto spans = hitsOf(wrapped.Get(), 10, 70, 20, 380);
    bool inside = !spans.empty();
    UINT32 covered = 0;
    for (const auto& hit : spans) {
        inside = inside && hit.left >= 20 + m.left - 0.01f && hit.left + hit.width <= 20 + m.left + m.widthIncludingTrailingWhitespace + 0.01f &&
                 hit.top >= 380 + m.top - 0.01f && hit.top + hit.height <= 380 + m.top + m.height + 0.01f;
        covered += hit.length;
        s.target->FillRectangle(D2D1::RectF(hit.left, hit.top, hit.left + hit.width, hit.top + hit.height), s.ink(D2D1::ColorF(0.9f, 0.6f, 0.2f, 0.45f)));
    }
    check(inside && covered == 70, "selection rectangles cover the range inside the text");
    s.draw(wrapped.Get(), 20, 380);
}

// The Windows families the launcher asks for, what they resolve to, and their text.
void families(ID2D1Factory* factory, IDWriteFactory* write) {
    Scene s(factory, "families");
    ComPtr<IDWriteFontCollection> system;
    write->GetSystemFontCollection(system.GetAddressOf());
    auto caption = makeFormat(write, L"Segoe UI", 14);
    caption->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    const wchar_t* const names[] = {L"Segoe UI", L"Segoe UI Variable Display", L"Arial", L"Calibri", L"Tahoma", L"Verdana", L"Segoe Print",
                                    L"Georgia", L"Cambria", L"Constantia", L"Palatino Linotype", L"Book Antiqua", L"Gabriola", L"Consolas",
                                    L"Segoe MDL2 Assets", L"Segoe Fluent Icons", L"Noto Sans", L"Missing Family"};
    float y = 14;
    for (const auto* name : names) {
        UINT32 index = 0;
        BOOL exists = FALSE;
        system->FindFamilyName(name, &index, &exists);
        const std::string resolved = lm_dwrite::resolveFamily(name, system.Get());
        std::printf("  %-26ls exists %d -> %s\n", name, exists, resolved.c_str());
        s.label(caption.Get(), std::wstring(name) + (exists ? L"" : L"  (not found)"), 20, y + 6, exists ? D2D1::ColorF(0.7f, 0.75f, 0.8f) : D2D1::ColorF(0.9f, 0.4f, 0.4f));
        s.label(caption.Get(), std::wstring(resolved.begin(), resolved.end()), 230, y + 6, D2D1::ColorF(0.5f, 0.55f, 0.6f));
        auto sample = makeFormat(write, name, 22, DWRITE_FONT_WEIGHT_NORMAL, std::wcsstr(name, L"Constantia") ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL);
        check(sample != nullptr, "CreateTextFormat succeeds for any family");
        if (sample) s.label(sample.Get(), L"Zażółć gęślą jaźń 0123 — Ag", 420, y, D2D1::ColorF(0.95f, 0.95f, 0.97f));
        y += 34;
    }
    UINT32 index = 0;
    BOOL exists = FALSE;
    system->FindFamilyName(L"segoe ui", &index, &exists);
    check(exists, "FindFamilyName ignores case");
    system->FindFamilyName(L"Segoe MDL2 Assets", &index, &exists);
    check(!exists, "FindFamilyName: no icon font is claimed");
    system->FindFamilyName(L"Definitely Missing", &index, &exists);
    check(!exists, "FindFamilyName: a missing family does not exist");

    // Font metrics through the collection, as Renderer::typeMetrics reads the cap height.
    auto format = makeFormat(write, L"Segoe UI Variable Display", 20, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    ComPtr<IDWriteFontCollection> collection;
    ComPtr<IDWriteFontFamily> family;
    ComPtr<IDWriteFont> font;
    wchar_t name[64]{};
    DWRITE_FONT_METRICS metrics{};
    if (SUCCEEDED(format->GetFontCollection(collection.GetAddressOf())) && SUCCEEDED(format->GetFontFamilyName(name, 64)) &&
        SUCCEEDED(collection->FindFamilyName(name, &index, &exists)) && exists && SUCCEEDED(collection->GetFontFamily(index, family.GetAddressOf())) &&
        SUCCEEDED(family->GetFirstMatchingFont(format->GetFontWeight(), format->GetFontStretch(), format->GetFontStyle(), font.GetAddressOf())))
        font->GetMetrics(&metrics);
    std::printf("  font metrics: %u units/em, ascent %u, descent %u, gap %d, cap %u, x %u\n", metrics.designUnitsPerEm, metrics.ascent, metrics.descent,
                metrics.lineGap, metrics.capHeight, metrics.xHeight);
    check(metrics.designUnitsPerEm && metrics.capHeight > metrics.designUnitsPerEm / 2 && metrics.ascent > metrics.capHeight, "font metrics in design units");
}

// The same layout under scales and a rotation: measured once, drawn at each transform.
void transforms(ID2D1Factory* factory, IDWriteFactory* write) {
    Scene s(factory, "transforms");
    auto format = makeFormat(write, L"Segoe UI", 15);
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    auto layout = makeLayout(write, L"Measured once — Zażółć gęślą jaźń ✓", format.Get(), 400, 30);
    const auto m = metricsOf(layout.Get());
    const float scales[] = {1, 1.37f, 2, 3.1f};
    float y = 20;
    for (float scale : scales) {
        s.target->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale) * D2D1::Matrix3x2F::Translation(20, y));
        s.target->DrawRectangle(D2D1::RectF(0, 0, m.widthIncludingTrailingWhitespace, m.height), s.ink(D2D1::ColorF(0.3f, 0.6f, 1, 0.5f)), 1 / scale);
        s.target->DrawTextLayout(D2D1::Point2F(0, 0), layout.Get(), s.ink(D2D1::ColorF(1, 1, 1)));
        y += m.height * scale + 12;
    }
    s.target->SetTransform(D2D1::Matrix3x2F::Rotation(-12, {600, 520}) * D2D1::Matrix3x2F::Translation(0, 0));
    s.target->DrawRectangle(D2D1::RectF(420, 500, 420 + m.widthIncludingTrailingWhitespace * 1.2f, 500 + m.height * 1.2f), s.ink(D2D1::ColorF(0.3f, 0.6f, 1, 0.5f)), 1);
    s.target->SetTransform(D2D1::Matrix3x2F::Scale(1.2f, 1.2f) * D2D1::Matrix3x2F::Translation(420, 500) * D2D1::Matrix3x2F::Rotation(-12, {600, 520}));
    s.target->DrawTextLayout(D2D1::Point2F(0, 0), layout.Get(), s.ink(D2D1::ColorF(1, 0.85f, 0.5f)));
    s.target->SetTransform(D2D1::IdentityMatrix());
}

// DrawText and a layout of the box's size drawn with CLIP are the same pixels (as
// tests/text_layout_cache_tests.cpp requires of the launcher's layout cache).
void identity(ID2D1Factory* factory, IDWriteFactory* write) {
    auto format = makeFormat(write, L"Segoe UI", 17);
    trim(write, format.Get());
    const std::wstring label = L"Zażółć gęślą jaźń — a longer title\nsecond line";
    bool same = true;
    for (auto wrapping : {DWRITE_WORD_WRAPPING_NO_WRAP, DWRITE_WORD_WRAPPING_WRAP})
        for (auto alignment : {DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_TEXT_ALIGNMENT_TRAILING})
            for (auto paragraph : {DWRITE_PARAGRAPH_ALIGNMENT_NEAR, DWRITE_PARAGRAPH_ALIGNMENT_CENTER})
                for (float x : {7.0f, 7.37f}) {
                    format->SetWordWrapping(wrapping);
                    format->SetTextAlignment(alignment);
                    format->SetParagraphAlignment(paragraph);
                    const float y = 13.37f, w = 178.23f, h = 77.3f;
                    {
                        Scene s(factory, "_text");
                        s.target->DrawText(label.c_str(), static_cast<UINT32>(label.size()), format.Get(), D2D1::RectF(x, y, x + w, y + h), s.ink(D2D1::ColorF(1, 1, 1)),
                                           D2D1_DRAW_TEXT_OPTIONS_CLIP);
                    }
                    {
                        Scene s(factory, "_layout");
                        auto layout = makeLayout(write, label, format.Get(), (x + w) - x, (y + h) - y);
                        s.target->DrawTextLayout(D2D1::Point2F(x, y), layout.Get(), s.ink(D2D1::ColorF(1, 1, 1)), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                    }
                    same = same && frames["_text"] == frames["_layout"];
                }
    check(same, "DrawText and DrawTextLayout with CLIP draw the same pixels");
}

// Layouts measured on another thread, and one moved between threads.
void threads(IDWriteFactory* write) {
    auto format = makeFormat(write, L"Segoe UI", 15);
    const auto widths = [&] {
        std::vector<float> result;
        for (int i = 0; i < 400; ++i) result.push_back(metricsOf(makeLayout(write, L"Label " + std::to_wstring(i) + L" — ąę", format.Get(), 300, 30).Get()).width);
        return result;
    };
    std::vector<float> worker_widths;
    std::thread worker([&] { worker_widths = widths(); });
    const auto main_widths = widths();
    worker.join();
    check(main_widths == worker_widths, "layouts measured on two threads at once agree");
    auto moved = makeLayout(write, polish, format.Get(), 300, 400);
    const auto here = metricsOf(moved.Get());
    DWRITE_TEXT_METRICS there{};
    std::thread([&] { there = metricsOf(moved.Get()); }).join();
    check(here.width == there.width && here.lineCount == there.lineCount, "a layout measured on another thread lays out the same");
}

// IDWriteFactory3: a collection built from a font file, as Renderer::loadLocalFonts does.
void fontFiles(IDWriteFactory* write) {
    ComPtr<IDWriteFactory3> modern;
    ComPtr<IDWriteFontSetBuilder> builder;
    ComPtr<IDWriteFontFaceReference> reference;
    ComPtr<IDWriteFontSet> set;
    ComPtr<IDWriteFontCollection1> collection;
    const std::filesystem::path file = "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf";
    auto format = makeFormat(write, L"Segoe UI", 15);
    auto before = makeLayout(write, polish, format.Get(), 300, 400);
    const float width = metricsOf(before.Get()).width;
    const bool built = SUCCEEDED(write->QueryInterface(IID_PPV_ARGS(modern.GetAddressOf()))) &&
                       SUCCEEDED(modern->CreateFontSetBuilder(builder.GetAddressOf())) &&
                       SUCCEEDED(modern->CreateFontFaceReference(file.c_str(), nullptr, 0, DWRITE_FONT_SIMULATIONS_NONE, reference.GetAddressOf())) &&
                       SUCCEEDED(builder->AddFontFaceReference(reference.Get())) && SUCCEEDED(builder->CreateFontSet(set.GetAddressOf())) &&
                       SUCCEEDED(modern->CreateFontCollectionFromFontSet(set.Get(), collection.GetAddressOf()));
    check(built, "font set from a file");
    if (!built) return;
    UINT32 index = 0;
    BOOL exists = FALSE, other = TRUE;
    collection->FindFamilyName(L"DejaVu Serif", &index, &exists);
    collection->FindFamilyName(L"Noto Sans", &index, &other);
    check(exists && !other, "the file's collection holds its family only");
    auto own = makeFormat(write, L"DejaVu Serif", 15, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, collection.Get());
    check(own && metricsOf(makeLayout(write, polish, own.Get(), 300, 400).Get()).width > 0, "text in the collection's family");
    check(metricsOf(before.Get()).width == width, "earlier layouts measure the same after fonts are added");
}

// Layouts made, measured and drawn per frame, as the renderer's paint paths do.
void timing(ID2D1Factory* factory, IDWriteFactory* write) {
    auto format = makeFormat(write, L"Segoe UI", 15, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    trim(write, format.Get());
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    std::vector<std::wstring> labels;
    for (int i = 0; i < 2000; ++i) labels.push_back(L"Gra numer " + std::to_wstring(i) + (i % 3 ? L" — żółw" : L" Settings"));
    for (int round = 0; round < 2; ++round) {
        Scene s(factory, "_timing");
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 2000; ++i) {
            ComPtr<IDWriteTextLayout> layout;
            write->CreateTextLayout(labels[i].c_str(), static_cast<UINT32>(labels[i].size()), format.Get(), 220, 24, layout.GetAddressOf());
            DWRITE_TEXT_METRICS metrics{};
            layout->GetMetrics(&metrics);
            s.target->DrawTextLayout(D2D1::Point2F(static_cast<float>(i % 4) * 230, static_cast<float>(i / 4 % 26) * 24), layout.Get(), s.ink(D2D1::ColorF(1, 1, 1)));
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::printf("  2000 layouts created, measured and drawn: %.1f ms%s\n", ms, round ? "" : " (cold)");
        if (round) check(ms < 50, "2000 short layouts well under 50 ms");
    }
}
} // namespace

int main(int argc, char** argv) {
    if (argc > 1) output_directory = argv[1];
    ComPtr<ID2D1Factory> factory;
    ComPtr<IDWriteFactory> write;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf())) ||
        FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(write.GetAddressOf()))))
        return 1;
    paragraphs(factory.Get(), write.Get());
    rich(factory.Get(), write.Get());
    families(factory.Get(), write.Get());
    transforms(factory.Get(), write.Get());
    identity(factory.Get(), write.Get());
    threads(write.Get());
    fontFiles(write.Get());
    timing(factory.Get(), write.Get());
    std::printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
