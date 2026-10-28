// DirectWrite text layouts on SkParagraph. SkParagraph shapes, picks fonts, breaks lines and
// trims with "…"; this file adds what DirectWrite does differently: alignment within the maximum
// width (also without wrapping), trailing whitespace outside the aligned width, line spacing,
// paragraph alignment, trimming of text below the box at its last visible line, and positions
// counted in wchar_t units. The glyphs are gathered once per layout as text blobs, split where a
// drawing effect or an underline changes.
#include "dwrite_internal.hpp"

#include "include/core/SkFontMetrics.h"
#include "modules/skparagraph/include/ParagraphBuilder.h"
#include "modules/skparagraph/include/ParagraphStyle.h"
#include "modules/skparagraph/include/TextStyle.h"
#include "modules/skunicode/include/SkUnicode_icu.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lm_dwrite {
namespace {
constexpr float unbounded_width = 100000;

const sk_sp<SkUnicode>& threadUnicode() {
    thread_local const sk_sp<SkUnicode> unicode = SkUnicodes::ICU::Make();
    return unicode;
}

bool space(wchar_t c) { return c == L' ' || c == L'\t' || c == L'\n' || c == L'\r' || c == 0x2028 || c == 0x2029 || c == 0x00A0 || c == 0x3000; }
} // namespace

TextLayout::TextLayout(const FormatState& format, const WCHAR* text, UINT32 length, float maxWidth, float maxHeight)
    : FormatBase(format.font(), format.paragraph()), text_(text, length), max_width_(maxWidth), max_height_(maxHeight) {
    utf8_.reserve(length);
    offsets_.reserve(length + 1);
    utf16_.reserve(length + 1);
    uint32_t units = 0;
    for (UINT32 i = 0; i < length; ++i) {
        offsets_.push_back(static_cast<uint32_t>(utf8_.size()));
        utf16_.push_back(units);
        char32_t c = static_cast<char32_t>(text_[i]);
        // A UTF-16 pair (some of the launcher's strings carry them): one code point; its low half
        // is an empty character at the code point's start.
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < length && text_[i + 1] >= 0xDC00 && text_[i + 1] < 0xE000) {
            c = 0x10000 + ((c - 0xD800) << 10) + (static_cast<char32_t>(text_[i + 1]) - 0xDC00);
            offsets_.push_back(offsets_.back());
            utf16_.push_back(units);
            ++i;
        }
        if (c > 0x10FFFF || (c >= 0xD800 && c < 0xE000)) c = 0xFFFD;
        units += c >= 0x10000 ? 2 : 1;
        appendUtf8(utf8_, c ? c : 0x2060); // A NUL would end the text: an invisible word joiner instead.
    }
    offsets_.push_back(static_cast<uint32_t>(utf8_.size()));
    utf16_.push_back(units);
}

HRESULT TextLayout::create(const WCHAR* text, UINT32 length, IDWriteTextFormat* format, FLOAT maxWidth, FLOAT maxHeight,
                           ComPtr<TextLayout>& layout) {
    const FormatState* state = FormatState::from(format);
    if (!state || (!text && length) || !(maxWidth >= 0) || !(maxHeight >= 0)) return E_INVALIDARG;
    layout.Attach(new TextLayout(*state, text ? text : L"", length, maxWidth, maxHeight));
    return S_OK;
}

TextLayout* TextLayout::from(IDWriteTextLayout* layout) { return dynamic_cast<TextLayout*>(layout); }

// ---- Formatting ---------------------------------------------------------------------------

template <class Apply>
HRESULT TextLayout::style(DWRITE_TEXT_RANGE range, bool relayout, Apply apply) {
    const auto length = static_cast<UINT32>(text_.size());
    if (range.startPosition >= length || !range.length) return S_OK;
    const UINT32 end = range.startPosition + std::min(range.length, length - range.startPosition);
    if (styles_.empty()) styles_.assign(length, CharStyle{font_->size, font_->weight});
    for (UINT32 i = range.startPosition; i < end; ++i) apply(styles_[i]);
    if (relayout) dirty_ = true;
    else restyled_ = true;
    return S_OK;
}

HRESULT TextLayout::SetMaxWidth(FLOAT maxWidth) {
    if (!(maxWidth >= 0)) return E_INVALIDARG;
    max_width_ = maxWidth;
    dirty_ = true;
    return S_OK;
}

HRESULT TextLayout::SetFontWeight(DWRITE_FONT_WEIGHT fontWeight, DWRITE_TEXT_RANGE textRange) {
    if (fontWeight < 1 || fontWeight > 999) return E_INVALIDARG;
    return style(textRange, true, [&](CharStyle& style) { style.weight = fontWeight; });
}

HRESULT TextLayout::SetFontSize(FLOAT fontSize, DWRITE_TEXT_RANGE textRange) {
    if (!(fontSize > 0)) return E_INVALIDARG;
    return style(textRange, true, [&](CharStyle& style) { style.size = fontSize; });
}

HRESULT TextLayout::SetUnderline(BOOL hasUnderline, DWRITE_TEXT_RANGE textRange) {
    return style(textRange, false, [&](CharStyle& style) { style.underline = hasUnderline != FALSE; });
}

HRESULT TextLayout::SetDrawingEffect(IUnknown* drawingEffect, DWRITE_TEXT_RANGE textRange) {
    return style(textRange, false, [&](CharStyle& style) { style.effect = drawingEffect; });
}

// SkParagraph spaces letters after each one: leading and trailing spacing become their sum.
HRESULT TextLayout::SetCharacterSpacing(FLOAT leadingSpacing, FLOAT trailingSpacing, FLOAT minimumAdvanceWidth, DWRITE_TEXT_RANGE textRange) {
    if (!(minimumAdvanceWidth >= 0)) return E_INVALIDARG;
    return style(textRange, true, [&](CharStyle& style) { style.spacing = leadingSpacing + trailingSpacing; });
}

// ---- Layout -------------------------------------------------------------------------------

UINT32 TextLayout::characterOfUtf8(size_t byte) const {
    const auto it = std::lower_bound(offsets_.begin(), offsets_.end(), static_cast<uint32_t>(byte));
    if (it != offsets_.end() && *it == byte) return static_cast<UINT32>(it - offsets_.begin());
    return static_cast<UINT32>(std::max<std::ptrdiff_t>(0, it - offsets_.begin() - 1));
}

UINT32 TextLayout::characterOfUtf16(size_t unit) const {
    const auto it = std::lower_bound(utf16_.begin(), utf16_.end(), static_cast<uint32_t>(unit));
    if (it != utf16_.end() && *it == unit) return static_cast<UINT32>(it - utf16_.begin());
    return static_cast<UINT32>(std::max<std::ptrdiff_t>(0, it - utf16_.begin() - 1));
}

float TextLayout::layoutWidth() const { return std::isfinite(max_width_) && max_width_ < unbounded_width ? max_width_ : unbounded_width; }

std::unique_ptr<textlayout::Paragraph> TextLayout::build(size_t max_lines, bool ellipsis) const {
    textlayout::TextStyle base;
    base.setFontFamilies({SkString(font_->resolved.c_str())});
    base.setFontSize(font_->size);
    base.setFontStyle(fontStyle(font_->weight, font_->style, font_->stretch));
    if (!font_->locale.empty()) base.setLocale(SkString(font_->locale.c_str()));
    base.setFontEdging(SkFont::Edging::kAntiAlias);
    base.setSubpixel(true);
    base.setFontHinting(SkFontHinting::kNone);
    textlayout::ParagraphStyle style;
    style.setTextStyle(base);
    style.setTextDirection(textlayout::TextDirection::kLtr);
    style.setTextAlign(paragraph_.alignment == DWRITE_TEXT_ALIGNMENT_JUSTIFIED ? textlayout::TextAlign::kJustify : textlayout::TextAlign::kLeft);
    style.turnHintingOff();
    style.setApplyRoundingHack(false);
    style.setReplaceTabCharacters(true);
    if (max_lines) style.setMaxLines(max_lines);
    if (ellipsis) style.setEllipsis(std::u16string(u"\u2026"));
    auto builder = textlayout::ParagraphBuilder::make(style, collection_, threadUnicode());
    builder->pushStyle(base);
    if (styles_.empty()) {
        builder->addText(utf8_.data(), utf8_.size());
    } else {
        const auto length = static_cast<UINT32>(text_.size());
        for (UINT32 first = 0; first < length;) {
            UINT32 last = first + 1;
            const CharStyle& own = styles_[first];
            while (last < length && styles_[last].size == own.size && styles_[last].weight == own.weight && styles_[last].spacing == own.spacing) ++last;
            textlayout::TextStyle part = base;
            part.setFontSize(own.size);
            part.setFontStyle(fontStyle(own.weight, font_->style, font_->stretch));
            part.setLetterSpacing(own.spacing);
            builder->pushStyle(part);
            builder->addText(utf8_.data() + offsets_[first], offsets_[last] - offsets_[first]);
            builder->pop();
            first = last;
        }
    }
    return builder->Build();
}

void TextLayout::layOut() {
    const auto& collection = threadFontCollection();
    if (collection != collection_) {
        collection_ = collection;
        dirty_ = true;
    }
    if (!dirty_) {
        if (restyled_ && laid_) collectRuns(*laid_);
        restyled_ = false;
        return;
    }
    dirty_ = restyled_ = false;
    const bool wraps = paragraph_.wrapping != DWRITE_WORD_WRAPPING_NO_WRAP, trims = paragraph_.trims();
    std::unique_ptr<textlayout::Paragraph> laid;
    if (!wraps) {
        // Each line at the width, trimmed when it is longer; untrimmed lines run on.
        const bool cut = trims && std::isfinite(max_width_) && max_width_ < unbounded_width;
        laid = build(0, cut);
        laid->layout(cut ? max_width_ : unbounded_width);
    } else {
        laid = build(0, false);
        laid->layout(layoutWidth());
        // With trimming, text below the box goes: the last line that fits (at least the first)
        // ends in the sign.
        if (trims && std::isfinite(max_height_) && laid->lineNumber() >= 2) {
            measure(*laid);
            size_t fit = 1;
            float bottom = lines_[0].height;
            while (fit < lines_.size() && bottom + lines_[fit].height <= max_height_ + 0.001f) bottom += lines_[fit++].height;
            if (fit < lines_.size()) {
                laid = build(fit, true);
                laid->layout(layoutWidth());
            }
        }
    }
    laid_ = std::move(laid);
    measure(*laid_);
    collectRuns(*laid_);
}

// SkParagraph always has a line, also for empty text.
void TextLayout::measure(textlayout::Paragraph& paragraph) {
    std::vector<textlayout::LineMetrics> metrics;
    paragraph.getLineMetrics(metrics);
    lines_.clear();
    const auto length = static_cast<UINT32>(text_.size());
    if (metrics.empty()) {
        textlayout::LineMetrics empty;
        const float size = font_->size;
        empty.fAscent = size * 0.9;
        empty.fDescent = size * 0.25;
        empty.fBaseline = empty.fAscent;
        empty.fEndIndex = empty.fEndExcludingWhitespaces = empty.fEndIncludingNewline = utf16_.back();
        metrics.push_back(empty);
    }
    float top = 0;
    for (size_t i = 0; i < metrics.size(); ++i) {
        const auto& m = metrics[i];
        Line line;
        const float ascent = static_cast<float>(m.fAscent), descent = static_cast<float>(m.fDescent), natural = ascent + descent;
        switch (paragraph_.spacing_method) {
        case DWRITE_LINE_SPACING_METHOD_UNIFORM: line.height = paragraph_.spacing; line.baseline = paragraph_.baseline; break;
        case DWRITE_LINE_SPACING_METHOD_PROPORTIONAL: line.height = natural * paragraph_.spacing; line.baseline = ascent * paragraph_.baseline; break;
        default: line.height = natural; line.baseline = ascent; break;
        }
        line.top = top;
        top += line.height;
        line.first = std::min(characterOfUtf16(m.fStartIndex), length);
        const UINT32 end_text = std::min(characterOfUtf16(m.fEndIndex), length);
        const UINT32 end_line = std::min(std::max(characterOfUtf16(m.fEndIncludingNewline), end_text), length);
        UINT32 end_kept = std::min(characterOfUtf16(m.fEndExcludingWhitespaces), end_text);
        while (end_kept > line.first && space(text_[end_kept - 1])) --end_kept;
        line.length = end_line > line.first ? end_line - line.first : 0;
        line.newline = end_line - end_text;
        line.trailing = end_line - std::max(end_kept, line.first); // DirectWrite counts the newline as whitespace.
        line.width = static_cast<float>(m.fWidth);
        line.full_width = line.width;
        if (end_kept < end_text) {
            // The trailing whitespace's extent (outside the aligned width).
            for (const auto& box : paragraph.getRectsForRange(utf16_[end_kept], utf16_[end_text], textlayout::RectHeightStyle::kTight,
                                                              textlayout::RectWidthStyle::kTight))
                line.full_width = std::max(line.full_width, box.rect.right() - static_cast<float>(m.fLeft));
        }
        line.sk_left = static_cast<float>(m.fLeft);
        line.sk_baseline = static_cast<float>(m.fBaseline);
        line.sk_top = line.sk_baseline - ascent;
        line.sk_bottom = line.sk_baseline + descent;
        lines_.push_back(line);
    }
    // Lines continue each other: a line ends where the next starts.
    for (size_t i = 0; i + 1 < lines_.size(); ++i)
        if (lines_[i + 1].first >= lines_[i].first) lines_[i].length = lines_[i + 1].first - lines_[i].first;
    shown_ = lines_.empty() ? 0 : std::min(length, lines_.back().first + lines_.back().length);
    if (paragraph.didExceedMaxLines() || (paragraph_.wrapping == DWRITE_WORD_WRAPPING_NO_WRAP && shown_ < length && paragraph_.trims()))
        lines_.back().trimmed = true;
    if (!lines_.empty() && paragraph_.trims() && shown_ < length) lines_.back().trimmed = true;

    float width = 0, full_width = 0, left = std::numeric_limits<float>::max();
    for (Line& line : lines_) {
        // Aligned within the maximum width without the trailing whitespace, as DirectWrite does.
        if (paragraph_.alignment == DWRITE_TEXT_ALIGNMENT_JUSTIFIED) {
            line.left = line.sk_left;
        } else {
            const float room = std::isfinite(max_width_) ? max_width_ - line.width : 0;
            line.left = paragraph_.alignment == DWRITE_TEXT_ALIGNMENT_TRAILING ? room : paragraph_.alignment == DWRITE_TEXT_ALIGNMENT_CENTER ? room / 2 : 0;
        }
        left = std::min(left, line.left);
        width = std::max(width, line.width);
        full_width = std::max(full_width, line.full_width);
    }
    const float room = std::isfinite(max_height_) ? max_height_ - top : 0;
    const float offset = paragraph_.vertical == DWRITE_PARAGRAPH_ALIGNMENT_FAR ? room : paragraph_.vertical == DWRITE_PARAGRAPH_ALIGNMENT_CENTER ? room / 2 : 0;
    metrics_ = {left, offset, width, full_width, top, max_width_, max_height_, 1, static_cast<UINT32>(lines_.size())};
}

// SkParagraph's glyph runs moved to this layout's line places, cut where the drawing style changes.
void TextLayout::collectRuns(textlayout::Paragraph& paragraph) {
    runs_.clear();
    const auto length = static_cast<UINT32>(text_.size());
    const auto styleAt = [&](uint32_t byte, int line) -> UINT32 {
        if (styles_.empty() || !length) return 0;
        UINT32 at = byte <= utf8_.size() ? characterOfUtf8(byte) : length;
        // The sign (and anything past the shown text) continues the text before it.
        const UINT32 line_end = line < static_cast<int>(lines_.size()) ? lines_[line].first + lines_[line].length : shown_;
        if (at >= std::max<UINT32>(1, std::min(shown_, line_end)) || at >= length) at = std::min(shown_, line_end) ? std::min(shown_, line_end) - 1 : 0;
        return std::min(at, length - 1);
    };
    const auto sameInk = [&](UINT32 a, UINT32 b) {
        return styles_.empty() || (styles_[a].effect.Get() == styles_[b].effect.Get() && styles_[a].underline == styles_[b].underline);
    };
    paragraph.visit([&](int number, const textlayout::Paragraph::VisitorInfo* info) {
        if (!info || number < 0 || number >= static_cast<int>(lines_.size()) || info->count <= 0) return;
        const Line& line = lines_[number];
        const float dx = line.left - line.sk_left, dy = metrics_.top + line.top + line.baseline - line.sk_baseline;
        for (int i = 0; i < info->count;) {
            const UINT32 style = styleAt(info->utf8Starts[i], number);
            int j = i + 1;
            while (j < info->count && sameInk(styleAt(info->utf8Starts[j], number), style)) ++j;
            SkTextBlobBuilder builder;
            const auto& buffer = builder.allocRunPos(info->font, j - i);
            for (int k = i; k < j; ++k) {
                buffer.glyphs[k - i] = info->glyphs[k];
                buffer.points()[k - i] = {info->origin.x() + info->positions[k].x() + dx, info->origin.y() + info->positions[k].y() + dy};
            }
            Run run{builder.make(), style, !styles_.empty() && styles_[style].underline, {}};
            if (run.underline) {
                SkFontMetrics font;
                info->font.getMetrics(&font);
                SkScalar position = 0, thickness = 0;
                if (!font.hasUnderlinePosition(&position)) position = info->font.getSize() * 0.1f;
                if (!font.hasUnderlineThickness(&thickness) || thickness <= 0) thickness = std::max(1.0f, info->font.getSize() / 16);
                SkScalar last = 0;
                info->font.getWidths({&info->glyphs[j - 1], 1}, {&last, 1});
                const float x0 = info->origin.x() + info->positions[i].x() + dx;
                const float x1 = info->origin.x() + info->positions[j - 1].x() + dx + last;
                const float y = info->origin.y() + dy + position;
                run.underline_rect = SkRect::MakeLTRB(x0, y, x1, y + thickness);
            }
            if (run.blob) runs_.push_back(std::move(run));
            i = j;
        }
    });
}

int TextLayout::lineAtSkiaY(float y) const {
    for (size_t i = 0; i < lines_.size(); ++i)
        if (y < lines_[i].sk_bottom || i + 1 == lines_.size()) return static_cast<int>(i);
    return 0;
}

// ---- Measuring ----------------------------------------------------------------------------

HRESULT TextLayout::GetMetrics(DWRITE_TEXT_METRICS* textMetrics) {
    if (!textMetrics) return E_INVALIDARG;
    layOut();
    *textMetrics = metrics_;
    return S_OK;
}

HRESULT TextLayout::GetLineMetrics(DWRITE_LINE_METRICS* lineMetrics, UINT32 maxLineCount, UINT32* actualLineCount) {
    if (!actualLineCount || (maxLineCount && !lineMetrics)) return E_INVALIDARG;
    layOut();
    const auto count = static_cast<UINT32>(lines_.size());
    *actualLineCount = count;
    for (UINT32 i = 0; i < std::min(count, maxLineCount); ++i) {
        const Line& line = lines_[i];
        lineMetrics[i] = {line.length, line.trailing, line.newline, line.height, line.baseline, line.trimmed};
    }
    return maxLineCount < count ? E_NOT_SUFFICIENT_BUFFER : S_OK;
}

// One rectangle per line of the range; characters trimmed below the box (or past the sign) are
// one trimmed, empty rectangle after the line's end.
HRESULT TextLayout::HitTestTextRange(UINT32 textPosition, UINT32 textLength, FLOAT originX, FLOAT originY,
                                     DWRITE_HIT_TEST_METRICS* hitTestMetrics, UINT32 maxHitTestMetricsCount, UINT32* actualHitTestMetricsCount) {
    if (!actualHitTestMetricsCount || (maxHitTestMetricsCount && !hitTestMetrics)) return E_INVALIDARG;
    *actualHitTestMetricsCount = 0;
    layOut();
    const UINT32 end = textPosition + std::min(textLength, std::numeric_limits<UINT32>::max() - textPosition);
    std::vector<DWRITE_HIT_TEST_METRICS> hits;
    for (size_t index = 0; index < lines_.size(); ++index) {
        const Line& line = lines_[index];
        const UINT32 first = std::max(textPosition, line.first), last = std::min({end, line.first + line.length, shown_});
        if (first >= last) continue;
        const float x = originX + line.left - line.sk_left, y = originY + metrics_.top + line.top;
        // The characters' boxes on this line, as one rectangle.
        float left = std::numeric_limits<float>::max(), right = -std::numeric_limits<float>::max();
        for (const auto& box : laid_->getRectsForRange(utf16_[first], utf16_[last], textlayout::RectHeightStyle::kMax,
                                                            textlayout::RectWidthStyle::kTight)) {
            if (lineAtSkiaY(box.rect.centerY()) != static_cast<int>(index)) continue;
            left = std::min(left, box.rect.left());
            right = std::max(right, box.rect.right());
        }
        if (left > right) left = right = line.sk_left + (first >= line.first + line.length - line.trailing ? line.full_width : 0);
        hits.push_back({first, last - first, x + left, y, right - left, line.height, 0, TRUE, FALSE});
    }
    const auto length = static_cast<UINT32>(text_.size());
    if (end > shown_ && textPosition < length && !lines_.empty()) {
        const Line& line = lines_.back();
        const UINT32 first = std::max(textPosition, shown_);
        hits.push_back({first, std::min(end, length) - first, originX + line.left + line.full_width, originY + metrics_.top + line.top, 0,
                        line.height, 0, TRUE, TRUE});
    }
    *actualHitTestMetricsCount = static_cast<UINT32>(hits.size());
    if (hits.size() > maxHitTestMetricsCount) return E_NOT_SUFFICIENT_BUFFER;
    std::copy(hits.begin(), hits.end(), hitTestMetrics);
    return S_OK;
}
} // namespace lm_dwrite
