// DirectWrite's factory and text formats: a format is a font (fixed at creation, shared with the
// layouts made from it) and paragraph settings (copied into each layout).
#include "dwrite_internal.hpp"

#include <utility>

namespace lm_dwrite {
const FormatState* FormatState::from(IDWriteTextFormat* format) { return dynamic_cast<const FormatState*>(format); }

template <class Interface>
HRESULT FormatBase<Interface>::SetTextAlignment(DWRITE_TEXT_ALIGNMENT textAlignment) {
    if (static_cast<unsigned>(textAlignment) > DWRITE_TEXT_ALIGNMENT_JUSTIFIED) return E_INVALIDARG;
    paragraph_.alignment = textAlignment;
    edited();
    return S_OK;
}

template <class Interface>
HRESULT FormatBase<Interface>::SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT paragraphAlignment) {
    if (static_cast<unsigned>(paragraphAlignment) > DWRITE_PARAGRAPH_ALIGNMENT_CENTER) return E_INVALIDARG;
    paragraph_.vertical = paragraphAlignment;
    edited();
    return S_OK;
}

template <class Interface>
HRESULT FormatBase<Interface>::SetWordWrapping(DWRITE_WORD_WRAPPING wordWrapping) {
    if (static_cast<unsigned>(wordWrapping) > DWRITE_WORD_WRAPPING_CHARACTER) return E_INVALIDARG;
    paragraph_.wrapping = wordWrapping;
    edited();
    return S_OK;
}

template <class Interface>
HRESULT FormatBase<Interface>::SetTrimming(const DWRITE_TRIMMING* trimmingOptions, IDWriteInlineObject* trimmingSign) {
    if (!trimmingOptions || static_cast<unsigned>(trimmingOptions->granularity) > DWRITE_TRIMMING_GRANULARITY_WORD) return E_INVALIDARG;
    paragraph_.trimming = *trimmingOptions;
    paragraph_.sign = trimmingSign;
    edited();
    return S_OK;
}

template <class Interface>
HRESULT FormatBase<Interface>::SetLineSpacing(DWRITE_LINE_SPACING_METHOD lineSpacingMethod, FLOAT lineSpacing, FLOAT baseline) {
    if (static_cast<unsigned>(lineSpacingMethod) > DWRITE_LINE_SPACING_METHOD_PROPORTIONAL || !(lineSpacing >= 0)) return E_INVALIDARG;
    paragraph_.spacing_method = lineSpacingMethod;
    paragraph_.spacing = lineSpacing;
    paragraph_.baseline = baseline;
    edited();
    return S_OK;
}

template <class Interface>
HRESULT FormatBase<Interface>::GetTrimming(DWRITE_TRIMMING* trimmingOptions, IDWriteInlineObject** trimmingSign) {
    if (!trimmingOptions || !trimmingSign) return E_INVALIDARG;
    *trimmingOptions = paragraph_.trimming;
    return paragraph_.sign.CopyTo(trimmingSign);
}

template <class Interface>
HRESULT FormatBase<Interface>::GetLineSpacing(DWRITE_LINE_SPACING_METHOD* lineSpacingMethod, FLOAT* lineSpacing, FLOAT* baseline) {
    if (!lineSpacingMethod || !lineSpacing || !baseline) return E_INVALIDARG;
    *lineSpacingMethod = paragraph_.spacing_method;
    *lineSpacing = paragraph_.spacing;
    *baseline = paragraph_.baseline;
    return S_OK;
}

template <class Interface>
HRESULT FormatBase<Interface>::GetFontCollection(IDWriteFontCollection** fontCollection) {
    if (!fontCollection) return E_INVALIDARG;
    return font_->collection.CopyTo(fontCollection);
}

template <class Interface>
HRESULT FormatBase<Interface>::GetFontFamilyName(WCHAR* fontFamilyName, UINT32 nameSize) {
    if (!fontFamilyName) return E_INVALIDARG;
    const std::wstring& family = font_->family;
    if (nameSize <= family.size()) {
        if (nameSize) fontFamilyName[0] = 0;
        return E_NOT_SUFFICIENT_BUFFER;
    }
    std::wmemcpy(fontFamilyName, family.c_str(), family.size() + 1);
    return S_OK;
}

template class FormatBase<IDWriteTextFormat>;
template class FormatBase<IDWriteTextLayout1>;

namespace {
class TextFormat final : public FormatBase<IDWriteTextFormat> {
public:
    TextFormat(std::shared_ptr<const FontSpec> font, Paragraph paragraph) : FormatBase(std::move(font), std::move(paragraph)) {}
};

class EllipsisSign final : public lm_com::Object<IDWriteInlineObject> {};

class Factory final : public lm_com::Object<IDWriteFactory3> {
public:
    HRESULT STDMETHODCALLTYPE GetSystemFontCollection(IDWriteFontCollection** fontCollection, BOOL) override {
        if (!fontCollection) return E_INVALIDARG;
        *fontCollection = systemFontCollection();
        (*fontCollection)->AddRef();
        return S_OK;
    }

    // Succeeds for any family, as DirectWrite does: a missing one is drawn in a fallback font.
    HRESULT STDMETHODCALLTYPE CreateTextFormat(const WCHAR* fontFamilyName, IDWriteFontCollection* fontCollection,
                                               DWRITE_FONT_WEIGHT fontWeight, DWRITE_FONT_STYLE fontStyle, DWRITE_FONT_STRETCH fontStretch,
                                               FLOAT fontSize, const WCHAR* localeName, IDWriteTextFormat** textFormat) override {
        if (!textFormat) return E_INVALIDARG;
        *textFormat = nullptr;
        if (!fontFamilyName || !(fontSize > 0) || fontWeight < 1 || fontWeight > 999 ||
            static_cast<unsigned>(fontStyle) > DWRITE_FONT_STYLE_ITALIC || static_cast<unsigned>(fontStretch) > DWRITE_FONT_STRETCH_ULTRA_EXPANDED)
            return E_INVALIDARG;
        auto font = std::make_shared<FontSpec>();
        font->family = fontFamilyName;
        font->collection = fontCollection ? fontCollection : systemFontCollection();
        font->weight = fontWeight;
        font->style = fontStyle;
        font->stretch = fontStretch;
        font->size = fontSize;
        font->resolved = resolveFamily(font->family, font->collection.Get());
        font->locale = localeName ? toUtf8(localeName) : std::string{};
        *textFormat = new TextFormat(std::move(font), Paragraph{});
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CreateTextLayout(const WCHAR* string, UINT32 stringLength, IDWriteTextFormat* textFormat,
                                               FLOAT maxWidth, FLOAT maxHeight, IDWriteTextLayout** textLayout) override {
        if (!textLayout) return E_INVALIDARG;
        *textLayout = nullptr;
        ComPtr<TextLayout> layout;
        const HRESULT result = TextLayout::create(string, stringLength, textFormat, maxWidth, maxHeight, layout);
        if (SUCCEEDED(result)) *textLayout = layout.Detach();
        return result;
    }

    HRESULT STDMETHODCALLTYPE CreateEllipsisTrimmingSign(IDWriteTextFormat* textFormat, IDWriteInlineObject** trimmingSign) override {
        if (!trimmingSign) return E_INVALIDARG;
        *trimmingSign = nullptr;
        if (!FormatState::from(textFormat)) return E_INVALIDARG;
        *trimmingSign = new EllipsisSign;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CreateFontFaceReference(const WCHAR* filePath, const FILETIME*, UINT32, DWRITE_FONT_SIMULATIONS,
                                                      IDWriteFontFaceReference** fontFaceReference) override {
        if (!filePath) return E_INVALIDARG;
        return createFontFaceReference(toUtf8(filePath), fontFaceReference);
    }
    HRESULT STDMETHODCALLTYPE CreateFontFaceReference(const char* filePath, const FILETIME*, UINT32, DWRITE_FONT_SIMULATIONS,
                                                      IDWriteFontFaceReference** fontFaceReference) override {
        if (!filePath) return E_INVALIDARG;
        return createFontFaceReference(filePath, fontFaceReference);
    }
    HRESULT STDMETHODCALLTYPE CreateFontSetBuilder(IDWriteFontSetBuilder** fontSetBuilder) override {
        return createFontSetBuilder(fontSetBuilder);
    }
    HRESULT STDMETHODCALLTYPE CreateFontCollectionFromFontSet(IDWriteFontSet* fontSet, IDWriteFontCollection1** fontCollection) override {
        return createFontCollection(fontSet, fontCollection);
    }
};
} // namespace
} // namespace lm_dwrite

// Every factory is a new object; they share the system collection and the fonts registered.
HRESULT WINAPI DWriteCreateFactory(DWRITE_FACTORY_TYPE factoryType, REFIID iid, IUnknown** factory) {
    if (!factory) return E_POINTER;
    *factory = nullptr;
    if (static_cast<unsigned>(factoryType) > DWRITE_FACTORY_TYPE_ISOLATED) return E_INVALIDARG;
    lm_dwrite::ComPtr<lm_dwrite::Factory> created;
    created.Attach(new lm_dwrite::Factory);
    return created->QueryInterface(iid, reinterpret_cast<void**>(factory));
}
