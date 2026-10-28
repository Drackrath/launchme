#pragma once
// Linux only: the DirectWrite port's private types, shared by dwrite_*.cpp. Pango lays text out
// (HarfBuzz shapes it, fontconfig picks fonts and fallbacks) in a per-thread PangoContext that
// measures unhinted, unrounded positions in DIPs, so a layout measures the same under any
// transform it is drawn with. Pango works in UTF-8; the API counts wchar_t (UTF-32) units.
#include "gfx_internal.hpp"
#include "../win32/dwrite_3.h"
#include "../win32/wrl/client.h"

#include <pango/pangocairo.h>

#include <memory>
#include <string>
#include <vector>

namespace lm_dwrite {
using Microsoft::WRL::ComPtr;

// ---- Pango and fonts (dwrite_fonts.cpp) ---------------------------------------------------

// The calling thread's context (Pango objects are not thread-safe). Replaced when font files
// are registered; layouts built in an older context lay themselves out again.
PangoContext* threadContext();

// The fontconfig family to ask for `family` (DirectWrite matches family names ignoring case):
// the collection's own family, an installed one, or the Linux stand-in for a Windows family.
std::string resolveFamily(const std::wstring& family, IDWriteFontCollection* collection);
// The system collection (not AddRef'd): installed families and the aliased Windows ones.
IDWriteFontCollection* systemFontCollection();
PangoFontDescription* describeFont(const std::string& family, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STYLE style,
                                   DWRITE_FONT_STRETCH stretch, float size);

// IDWriteFactory3's font sets: files registered with fontconfig as application fonts (every
// face of a file).
HRESULT createFontFaceReference(std::string path, IDWriteFontFaceReference** reference);
HRESULT createFontSetBuilder(IDWriteFontSetBuilder** builder);
HRESULT createFontCollection(IDWriteFontSet* set, IDWriteFontCollection1** collection);

// UTF-8 of a code point; surrogates and values past U+10FFFF become U+FFFD.
void appendUtf8(std::string& out, char32_t c);
std::string toUtf8(const std::wstring& text);

// ---- Formats (dwrite_factory.cpp) ---------------------------------------------------------

struct DescriptionDeleter { void operator()(PangoFontDescription* d) const { pango_font_description_free(d); } };

// A format's font, fixed at CreateTextFormat and shared by the layouts made from it.
struct FontSpec {
    std::wstring family;
    ComPtr<IDWriteFontCollection> collection;
    DWRITE_FONT_WEIGHT weight;
    DWRITE_FONT_STYLE style;
    DWRITE_FONT_STRETCH stretch;
    float size;
    PangoLanguage* language; // Null for an empty locale.
    std::unique_ptr<PangoFontDescription, DescriptionDeleter> description;
};

struct Paragraph {
    DWRITE_TEXT_ALIGNMENT alignment = DWRITE_TEXT_ALIGNMENT_LEADING;
    DWRITE_PARAGRAPH_ALIGNMENT vertical = DWRITE_PARAGRAPH_ALIGNMENT_NEAR;
    DWRITE_WORD_WRAPPING wrapping = DWRITE_WORD_WRAPPING_WRAP;
    DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_NONE, 0, 0};
    ComPtr<IDWriteInlineObject> sign;
    DWRITE_LINE_SPACING_METHOD spacing_method = DWRITE_LINE_SPACING_METHOD_DEFAULT;
    float spacing = 0, baseline = 0;
    // Trims overflowing text with "…". Without a sign DirectWrite cuts it unmarked; this port
    // leaves that text to the draw call's clip.
    bool trims() const { return trimming.granularity != DWRITE_TRIMMING_GRANULARITY_NONE && sign; }
};

// What a format (or a layout, which is one) carries; reachable from any IDWriteTextFormat.
class FormatState {
public:
    static const FormatState* from(IDWriteTextFormat* format);
    const std::shared_ptr<const FontSpec>& font() const { return font_; }
    const Paragraph& paragraph() const { return paragraph_; }
protected:
    FormatState(std::shared_ptr<const FontSpec> font, Paragraph paragraph) : font_(std::move(font)), paragraph_(std::move(paragraph)) {}
    std::shared_ptr<const FontSpec> font_;
    Paragraph paragraph_;
};

// IDWriteTextFormat's methods, for formats and layouts; `changed` follows each paragraph setter.
template <class Interface>
class FormatBase : public lm_com::Object<Interface>, public FormatState {
public:
    HRESULT STDMETHODCALLTYPE SetTextAlignment(DWRITE_TEXT_ALIGNMENT textAlignment) override;
    HRESULT STDMETHODCALLTYPE SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT paragraphAlignment) override;
    HRESULT STDMETHODCALLTYPE SetWordWrapping(DWRITE_WORD_WRAPPING wordWrapping) override;
    HRESULT STDMETHODCALLTYPE SetTrimming(const DWRITE_TRIMMING* trimmingOptions, IDWriteInlineObject* trimmingSign) override;
    HRESULT STDMETHODCALLTYPE SetLineSpacing(DWRITE_LINE_SPACING_METHOD lineSpacingMethod, FLOAT lineSpacing, FLOAT baseline) override;
    DWRITE_TEXT_ALIGNMENT STDMETHODCALLTYPE GetTextAlignment() override { return paragraph_.alignment; }
    DWRITE_PARAGRAPH_ALIGNMENT STDMETHODCALLTYPE GetParagraphAlignment() override { return paragraph_.vertical; }
    DWRITE_WORD_WRAPPING STDMETHODCALLTYPE GetWordWrapping() override { return paragraph_.wrapping; }
    DWRITE_READING_DIRECTION STDMETHODCALLTYPE GetReadingDirection() override { return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT; }
    DWRITE_FLOW_DIRECTION STDMETHODCALLTYPE GetFlowDirection() override { return DWRITE_FLOW_DIRECTION_TOP_TO_BOTTOM; }
    FLOAT STDMETHODCALLTYPE GetIncrementalTabStop() override { return font_->size * 4; }
    HRESULT STDMETHODCALLTYPE GetTrimming(DWRITE_TRIMMING* trimmingOptions, IDWriteInlineObject** trimmingSign) override;
    HRESULT STDMETHODCALLTYPE GetLineSpacing(DWRITE_LINE_SPACING_METHOD* lineSpacingMethod, FLOAT* lineSpacing, FLOAT* baseline) override;
    HRESULT STDMETHODCALLTYPE GetFontCollection(IDWriteFontCollection** fontCollection) override;
    HRESULT STDMETHODCALLTYPE GetFontFamilyName(WCHAR* fontFamilyName, UINT32 nameSize) override;
    DWRITE_FONT_WEIGHT STDMETHODCALLTYPE GetFontWeight() override { return font_->weight; }
    DWRITE_FONT_STYLE STDMETHODCALLTYPE GetFontStyle() override { return font_->style; }
    DWRITE_FONT_STRETCH STDMETHODCALLTYPE GetFontStretch() override { return font_->stretch; }
    FLOAT STDMETHODCALLTYPE GetFontSize() override { return font_->size; }
protected:
    using FormatState::FormatState;
    virtual void changed() {}
};
extern template class FormatBase<IDWriteTextFormat>;
extern template class FormatBase<IDWriteTextLayout1>;

// ---- Layouts (dwrite_layout.cpp, drawn by dwrite_render.cpp) -------------------------------

// A character's formatting beyond the format's (SetFontWeight, SetFontSize, SetUnderline,
// SetDrawingEffect, SetCharacterSpacing).
struct CharStyle {
    float size = 0;
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
    float spacing = 0; // Leading plus trailing spacing.
    bool underline = false;
    ComPtr<IUnknown> effect;
    bool operator==(const CharStyle& other) const {
        return size == other.size && weight == other.weight && spacing == other.spacing && underline == other.underline &&
               effect.Get() == other.effect.Get();
    }
};

class TextLayout final : public FormatBase<IDWriteTextLayout1> {
public:
    static HRESULT create(const WCHAR* text, UINT32 length, IDWriteTextFormat* format, FLOAT maxWidth, FLOAT maxHeight,
                          ComPtr<TextLayout>& layout);
    static TextLayout* from(IDWriteTextLayout* layout);

    HRESULT STDMETHODCALLTYPE SetMaxWidth(FLOAT maxWidth) override;
    HRESULT STDMETHODCALLTYPE SetFontWeight(DWRITE_FONT_WEIGHT fontWeight, DWRITE_TEXT_RANGE textRange) override;
    HRESULT STDMETHODCALLTYPE SetFontSize(FLOAT fontSize, DWRITE_TEXT_RANGE textRange) override;
    HRESULT STDMETHODCALLTYPE SetUnderline(BOOL hasUnderline, DWRITE_TEXT_RANGE textRange) override;
    HRESULT STDMETHODCALLTYPE SetDrawingEffect(IUnknown* drawingEffect, DWRITE_TEXT_RANGE textRange) override;
    HRESULT STDMETHODCALLTYPE SetCharacterSpacing(FLOAT leadingSpacing, FLOAT trailingSpacing, FLOAT minimumAdvanceWidth,
                                                  DWRITE_TEXT_RANGE textRange) override;
    HRESULT STDMETHODCALLTYPE GetLineMetrics(DWRITE_LINE_METRICS* lineMetrics, UINT32 maxLineCount, UINT32* actualLineCount) override;
    HRESULT STDMETHODCALLTYPE GetMetrics(DWRITE_TEXT_METRICS* textMetrics) override;
    HRESULT STDMETHODCALLTYPE HitTestTextRange(UINT32 textPosition, UINT32 textLength, FLOAT originX, FLOAT originY,
                                               DWRITE_HIT_TEST_METRICS* hitTestMetrics, UINT32 maxHitTestMetricsCount,
                                               UINT32* actualHitTestMetricsCount) override;

    // Draws the layout with its box's top-left corner at (x, y), in the context's current source.
    void draw(cairo_t* context, float x, float y);

private:
    // A laid-out line. Positions are DIPs; `left` is where the line starts in the layout box
    // (its alignment), `top` is below the text's first line (before paragraph alignment).
    struct Line {
        PangoLayoutLine* pango = nullptr; // Owned by pango_.
        UINT32 first = 0, length = 0, trailing = 0, newline = 0; // Characters.
        int pango_height = 0;                                    // Pango's own line height (units).
        float left = 0, top = 0, height = 0, baseline = 0;
        float width = 0, full_width = 0; // Without and with trailing whitespace.
        bool trimmed = false;
    };
    // Part of a line covering characters [first, first + length) from x0 to x1 (Pango units
    // from the line's start).
    struct Segment {
        UINT32 first, length;
        int x0, x1;
        UINT32 level;
        bool trimmed;
    };

    TextLayout(const FormatState& format, const WCHAR* text, UINT32 length, float maxWidth, float maxHeight);
    ~TextLayout() override;
    void changed() override { dirty_ = true; }

    // Applies `apply` to each character of the range; `relayout` when it changes the layout.
    template <class Apply> HRESULT style(DWRITE_TEXT_RANGE range, bool relayout, Apply apply);
    // Lays the text out in the calling thread's context when anything changed.
    void layOut();
    // Sets the Pango layout up: the whole text, or (cut) the text up to cut_ and the sign.
    void configure(bool cut, int height);
    PangoAttrList* attributes(bool cut) const;
    void measure();
    void segments(const Line& line, UINT32 first, UINT32 end, std::vector<Segment>& out) const;
    UINT32 characterAt(uint32_t byte) const;
    const CharStyle* styleAt(uint32_t byte) const;
    void drawRun(cairo_t* context, PangoGlyphItem* run, double x, double baseline, cairo_pattern_t* paint, cairo_antialias_t antialias,
                 cairo_scaled_font_t*& installed) const;

    std::wstring text_;
    std::string utf8_;
    std::vector<uint32_t> offsets_; // UTF-8 offset of each character, then the end.
    float max_width_, max_height_;
    std::vector<CharStyle> styles_; // One per character; empty until a range setter runs.

    PangoContext* context_ = nullptr; // The thread context pango_ was laid out in.
    PangoLayout* pango_ = nullptr;
    bool dirty_ = true;
    uint32_t cut_ = 0; // Bytes of utf8_ laid out (followed by the sign when trimmed below the box).
    UINT32 shown_ = 0; // Characters laid out.
    std::vector<Line> lines_;
    DWRITE_TEXT_METRICS metrics_{};
};
} // namespace lm_dwrite
