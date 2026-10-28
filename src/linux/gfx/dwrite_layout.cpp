// DirectWrite text layouts on Pango. Pango breaks lines, shapes and picks fonts; this file adds
// what DirectWrite does differently: alignment within the maximum width (also without wrapping),
// trailing whitespace outside the aligned width, line spacing, paragraph alignment, trimming of
// text below the box at its last visible line, and positions counted in wchar_t units.
#include "dwrite_internal.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lm_dwrite {
namespace {
constexpr float unit = 1.0f / PANGO_SCALE;
constexpr char sign[] = "\u2026";
constexpr uint32_t sign_bytes = sizeof(sign) - 1;

bool ellipsized(const PangoLayoutLine* line) {
    for (const GSList* run = line->runs; run; run = run->next)
        if (static_cast<const PangoGlyphItem*>(run->data)->item->analysis.flags & PANGO_ANALYSIS_FLAG_IS_ELLIPSIS) return true;
    return false;
}
} // namespace

TextLayout::TextLayout(const FormatState& format, const WCHAR* text, UINT32 length, float maxWidth, float maxHeight)
    : FormatBase(format.font(), format.paragraph()), text_(text, length), max_width_(maxWidth), max_height_(maxHeight) {
    utf8_.reserve(length);
    offsets_.reserve(length + 1);
    for (UINT32 i = 0; i < length; ++i) {
        offsets_.push_back(static_cast<uint32_t>(utf8_.size()));
        char32_t c = static_cast<char32_t>(text_[i]);
        // A UTF-16 pair (some of the launcher's strings carry them): one code point; its low half
        // is an empty character at the code point's start.
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < length && text_[i + 1] >= 0xDC00 && text_[i + 1] < 0xE000) {
            c = 0x10000 + ((c - 0xD800) << 10) + (static_cast<char32_t>(text_[i + 1]) - 0xDC00);
            offsets_.push_back(offsets_.back());
            ++i;
        }
        appendUtf8(utf8_, c ? c : 0x2060); // Pango's text ends at NUL: an invisible word joiner instead.
    }
    offsets_.push_back(static_cast<uint32_t>(utf8_.size()));
}

TextLayout::~TextLayout() {
    if (pango_) g_object_unref(pango_);
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

// Pango spaces letters evenly on both sides: leading and trailing spacing become their sum.
HRESULT TextLayout::SetCharacterSpacing(FLOAT leadingSpacing, FLOAT trailingSpacing, FLOAT minimumAdvanceWidth, DWRITE_TEXT_RANGE textRange) {
    if (!(minimumAdvanceWidth >= 0)) return E_INVALIDARG;
    return style(textRange, true, [&](CharStyle& style) { style.spacing = leadingSpacing + trailingSpacing; });
}

// ---- Layout -------------------------------------------------------------------------------

UINT32 TextLayout::characterAt(uint32_t byte) const {
    if (byte >= cut_) return shown_;
    return static_cast<UINT32>(std::lower_bound(offsets_.begin(), offsets_.end(), byte) - offsets_.begin());
}

const CharStyle* TextLayout::styleAt(uint32_t byte) const {
    if (styles_.empty() || !shown_) return nullptr;
    return &styles_[std::min(characterAt(byte), shown_ - 1)]; // The sign continues the text before it.
}

PangoAttrList* TextLayout::attributes(bool cut) const {
    PangoAttrList* list = pango_attr_list_new();
    const uint32_t end = cut ? cut_ + sign_bytes : cut_;
    const auto add = [&](PangoAttribute* attribute, uint32_t from, uint32_t to) {
        attribute->start_index = from;
        attribute->end_index = to;
        pango_attr_list_insert(list, attribute);
    };
    if (font_->language) add(pango_attr_language_new(font_->language), 0, end);
    if (styles_.empty()) return list;
    for (UINT32 first = 0; first < shown_;) {
        UINT32 last = first + 1;
        while (last < shown_ && styles_[last] == styles_[first]) ++last;
        const uint32_t from = offsets_[first], to = last == shown_ ? end : offsets_[last];
        const CharStyle& style = styles_[first];
        if (style.size != font_->size) add(pango_attr_size_new_absolute(static_cast<int>(std::lround(style.size * PANGO_SCALE))), from, to);
        if (style.weight != font_->weight) add(pango_attr_weight_new(static_cast<PangoWeight>(style.weight)), from, to);
        if (style.spacing != 0) add(pango_attr_letter_spacing_new(static_cast<int>(std::lround(style.spacing * PANGO_SCALE))), from, to);
        first = last;
    }
    return list;
}

void TextLayout::configure(bool cut, int height) {
    const Paragraph& paragraph = paragraph_;
    const bool wraps = paragraph.wrapping != DWRITE_WORD_WRAPPING_NO_WRAP, trims = paragraph.trims();
    if (cut) {
        const std::string text = utf8_.substr(0, cut_) + sign;
        pango_layout_set_text(pango_, text.data(), static_cast<int>(text.size()));
    } else {
        pango_layout_set_text(pango_, utf8_.data(), static_cast<int>(utf8_.size()));
    }
    pango_layout_set_font_description(pango_, font_->description.get());
    PangoAttrList* list = attributes(cut);
    pango_layout_set_attributes(pango_, list);
    pango_attr_list_unref(list);
    // Pango wraps and trims within the width; the alignment is placed by measure().
    const double width = static_cast<double>(max_width_) * PANGO_SCALE;
    pango_layout_set_width(pango_, (wraps || trims) && width < 1e9 ? static_cast<int>(std::lround(width)) : -1);
    pango_layout_set_wrap(pango_, paragraph.wrapping == DWRITE_WORD_WRAPPING_WHOLE_WORD  ? PANGO_WRAP_WORD
                                  : paragraph.wrapping == DWRITE_WORD_WRAPPING_CHARACTER ? PANGO_WRAP_CHAR
                                                                                         : PANGO_WRAP_WORD_CHAR);
    pango_layout_set_justify(pango_, paragraph.alignment == DWRITE_TEXT_ALIGNMENT_JUSTIFIED);
    // Unwrapped text trims each line at the width; wrapped text only at its last visible line.
    pango_layout_set_ellipsize(pango_, trims && (!wraps || cut) ? PANGO_ELLIPSIZE_END : PANGO_ELLIPSIZE_NONE);
    pango_layout_set_height(pango_, wraps ? height : -1);
    if (utf8_.find('\t') != std::string::npos) {
        PangoTabArray* tabs = pango_tab_array_new_with_positions(1, FALSE, PANGO_TAB_LEFT,
                                                                 static_cast<int>(std::lround(GetIncrementalTabStop() * PANGO_SCALE)));
        pango_layout_set_tabs(pango_, tabs);
        pango_tab_array_free(tabs);
    }
}

// Pango always has a line, also for empty text.
void TextLayout::measure() {
    lines_.clear();
    float top = 0;
    for (const GSList* item = pango_layout_get_lines_readonly(pango_); item; item = item->next) {
        Line line;
        line.pango = static_cast<PangoLayoutLine*>(item->data);
        PangoRectangle logical;
        pango_layout_line_get_extents(line.pango, nullptr, &logical);
        const float natural = logical.height * unit, ascent = -logical.y * unit;
        switch (paragraph_.spacing_method) {
        case DWRITE_LINE_SPACING_METHOD_UNIFORM: line.height = paragraph_.spacing; line.baseline = paragraph_.baseline; break;
        case DWRITE_LINE_SPACING_METHOD_PROPORTIONAL: line.height = natural * paragraph_.spacing; line.baseline = ascent * paragraph_.baseline; break;
        default: line.height = natural; line.baseline = ascent; break;
        }
        line.pango_height = logical.height;
        line.full_width = (logical.x + logical.width) * unit;
        line.top = top;
        top += line.height;
        line.first = characterAt(line.pango->start_index);
        line.trimmed = ellipsized(line.pango);
        lines_.push_back(line);
    }
    std::vector<Segment> whitespace;
    float width = 0, full_width = 0, left = std::numeric_limits<float>::max();
    for (size_t i = 0; i < lines_.size(); ++i) {
        Line& line = lines_[i];
        const UINT32 next = i + 1 < lines_.size() ? lines_[i + 1].first : shown_;
        const UINT32 content = std::clamp(characterAt(line.pango->start_index + line.pango->length), line.first, next);
        line.length = next - line.first;
        line.newline = next - content;
        UINT32 kept = content;
        while (kept > line.first && g_unichar_isspace(static_cast<gunichar>(text_[kept - 1]))) --kept;
        line.trailing = content - kept + line.newline; // DirectWrite counts the newline as whitespace.
        line.width = line.full_width;
        if (kept < content && !line.trimmed) {
            whitespace.clear();
            segments(line, kept, content, whitespace);
            for (const Segment& part : whitespace) line.width -= (part.x1 - part.x0) * unit;
            line.width = std::max(0.0f, line.width);
        }
        // Aligned within the maximum width without the trailing whitespace, as DirectWrite does.
        const float room = std::isfinite(max_width_) ? max_width_ - line.width : 0;
        line.left = paragraph_.alignment == DWRITE_TEXT_ALIGNMENT_TRAILING ? room : paragraph_.alignment == DWRITE_TEXT_ALIGNMENT_CENTER ? room / 2 : 0;
        left = std::min(left, line.left);
        width = std::max(width, line.width);
        full_width = std::max(full_width, line.full_width);
    }
    const float room = std::isfinite(max_height_) ? max_height_ - top : 0;
    const float offset = paragraph_.vertical == DWRITE_PARAGRAPH_ALIGNMENT_FAR ? room : paragraph_.vertical == DWRITE_PARAGRAPH_ALIGNMENT_CENTER ? room / 2 : 0;
    metrics_ = {left, offset, width, full_width, top, max_width_, max_height_, 1, static_cast<UINT32>(lines_.size())};
}

void TextLayout::layOut() {
    PangoContext* context = threadContext();
    if (context != context_) {
        if (pango_) g_object_unref(pango_);
        pango_ = pango_layout_new(context);
        pango_layout_set_auto_dir(pango_, FALSE);
        context_ = context;
        dirty_ = true;
    }
    if (!dirty_) return;
    dirty_ = false;
    cut_ = static_cast<uint32_t>(utf8_.size());
    shown_ = static_cast<UINT32>(text_.size());
    configure(false, -1);
    measure();
    // With trimming, text below the box goes: the last line that fits (at least the first) ends
    // in the sign, after its last visible character.
    if (!paragraph_.trims() || !std::isfinite(max_height_) || lines_.size() < 2) return;
    size_t fit = 1;
    float bottom = lines_[0].height;
    int height = lines_[0].pango_height;
    while (fit < lines_.size() && bottom + lines_[fit].height <= max_height_ + 0.001f) {
        bottom += lines_[fit].height;
        height += lines_[fit].pango_height;
        ++fit;
    }
    if (fit == lines_.size()) return;
    const Line& last = lines_[fit - 1];
    shown_ = last.first + last.length - last.trailing;
    cut_ = offsets_[shown_];
    configure(true, height + PANGO_SCALE / 2);
    measure();
    lines_.back().trimmed = true;
}

// ---- Measuring ----------------------------------------------------------------------------

void TextLayout::segments(const Line& line, UINT32 first, UINT32 end, std::vector<Segment>& out) const {
    const size_t start = out.size();
    const UINT32 content = line.first + line.length - line.newline;
    const uint32_t from = offsets_[std::min(first, shown_)], to = offsets_[std::min(std::min(end, content), shown_)];
    const char* text = pango_layout_get_text(pango_);
    int x = 0;
    for (const GSList* item = line.pango->runs; item; item = item->next) {
        auto* run = static_cast<PangoGlyphItem*>(item->data);
        PangoItem* piece = run->item;
        const int width = pango_glyph_string_get_width(run->glyphs);
        const uint32_t begin = static_cast<uint32_t>(piece->offset), stop = begin + static_cast<uint32_t>(piece->length);
        const uint32_t a = std::max(from, begin), b = std::min(to, stop);
        if (a < b) {
            const UINT32 level = piece->analysis.level, at = characterAt(a), length = characterAt(b) - at;
            if (piece->analysis.flags & PANGO_ANALYSIS_FLAG_IS_ELLIPSIS) {
                out.push_back({at, length, x, x + width, level, true}); // Characters the sign stands for.
            } else {
                int x0 = 0, x1 = level % 2 ? 0 : width;
                pango_glyph_string_index_to_x(run->glyphs, text + begin, piece->length, &piece->analysis, static_cast<int>(a - begin), FALSE, &x0);
                if (b < stop)
                    pango_glyph_string_index_to_x(run->glyphs, text + begin, piece->length, &piece->analysis, static_cast<int>(b - begin), FALSE, &x1);
                out.push_back({at, length, x + std::min(x0, x1), x + std::max(x0, x1), level, false});
            }
        }
        x += width;
    }
    // The newline: no width, at the line's end.
    if (end > content && first < line.first + line.length) {
        const UINT32 at = std::max(first, content);
        out.push_back({at, std::min(end, line.first + line.length) - at, x, x, 0, false});
    }
    // Pieces continuing each other (runs split by font fallback) are one rectangle.
    size_t kept = start;
    for (size_t i = start; i < out.size(); ++i) {
        if (kept > start) {
            Segment& previous = out[kept - 1];
            const Segment& next = out[i];
            if (previous.first + previous.length == next.first && previous.x1 == next.x0 && previous.level == next.level &&
                previous.trimmed == next.trimmed) {
                previous.length += next.length;
                previous.x1 = next.x1;
                continue;
            }
        }
        out[kept++] = out[i];
    }
    out.resize(kept);
}

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

// One rectangle per line (per direction run) of the range; characters trimmed below the box
// are one trimmed, empty rectangle after the sign.
HRESULT TextLayout::HitTestTextRange(UINT32 textPosition, UINT32 textLength, FLOAT originX, FLOAT originY,
                                     DWRITE_HIT_TEST_METRICS* hitTestMetrics, UINT32 maxHitTestMetricsCount, UINT32* actualHitTestMetricsCount) {
    if (!actualHitTestMetricsCount || (maxHitTestMetricsCount && !hitTestMetrics)) return E_INVALIDARG;
    *actualHitTestMetricsCount = 0;
    layOut();
    const UINT32 end = textPosition + std::min(textLength, std::numeric_limits<UINT32>::max() - textPosition);
    std::vector<DWRITE_HIT_TEST_METRICS> hits;
    std::vector<Segment> parts;
    for (const Line& line : lines_) {
        const UINT32 first = std::max(textPosition, line.first), last = std::min(end, line.first + line.length);
        if (first >= last) continue;
        parts.clear();
        segments(line, first, last, parts);
        const float x = originX + line.left, y = originY + metrics_.top + line.top;
        for (const Segment& part : parts)
            hits.push_back({part.first, part.length, x + part.x0 * unit, y, (part.x1 - part.x0) * unit, line.height, part.level, TRUE, part.trimmed});
    }
    const auto length = static_cast<UINT32>(text_.size());
    if (end > shown_ && textPosition < length) {
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
