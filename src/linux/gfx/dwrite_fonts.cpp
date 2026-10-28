// DirectWrite fonts on Skia's fontconfig font manager: each thread's font collection, family
// names (Windows families the launcher asks for map to installed Linux stand-ins), the system and
// file-built collections, and font metrics in design units.
#include "dwrite_internal.hpp"

#include "include/core/SkFontMetrics.h"
#include "include/core/SkTypeface.h"
#include "include/ports/SkFontMgr_fontconfig.h"
#include "include/ports/SkFontScanner_FreeType.h"
#include "modules/skparagraph/include/TypefaceFontProvider.h"

#include <fontconfig/fontconfig.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>
#include <mutex>
#include <set>
#include <span>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace lm_dwrite {
namespace {
// ASCII case folding: family names DirectWrite compares ignoring case.
std::string fold(std::string name) {
    for (char& c : name) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return name;
}

// Windows families and their stand-ins, tried in order after the family itself. The last of
// each list is a fontconfig generic, which always resolves to some installed font.
constexpr const char* sans_names[] = {"Segoe UI", "Segoe UI Variable Display", "Segoe UI Variable", "Arial", "Calibri", "Tahoma", "Verdana"};
constexpr const char* sans_fallback[] = {"Inter", "Noto Sans", "Cantarell", "DejaVu Sans", "sans-serif"};
constexpr const char* print_names[] = {"Segoe Print"};
constexpr const char* print_fallback[] = {"Comic Neue", "Noto Sans", "sans-serif"};
constexpr const char* serif_names[] = {"Georgia", "Cambria", "Constantia", "Palatino Linotype", "Book Antiqua", "Gabriola"};
constexpr const char* serif_fallback[] = {"Noto Serif", "DejaVu Serif", "serif"};
constexpr const char* mono_names[] = {"Consolas"};
constexpr const char* mono_fallback[] = {"Noto Sans Mono", "DejaVu Sans Mono", "monospace"};
struct Alias { std::span<const char* const> names, fallback; };
constexpr Alias aliases[] = {{sans_names, sans_fallback}, {print_names, print_fallback}, {serif_names, serif_fallback},
                             {mono_names, mono_fallback}};
// A family DirectWrite does not know falls back to the system's UI font.
constexpr const Alias& default_alias = aliases[0];

const Alias* aliasOf(const std::string& folded) {
    for (const auto& alias : aliases)
        for (const char* name : alias.names)
            if (fold(name) == folded) return &alias;
    return nullptr;
}

bool generic(const char* family) {
    return !std::strcmp(family, "sans-serif") || !std::strcmp(family, "serif") || !std::strcmp(family, "monospace");
}

// Casefolded names of the installed (system, not application) font families.
bool installed(const std::string& folded) {
    static const auto families = [] {
        std::unordered_set<std::string> result;
        FcInit();
        if (const FcFontSet* fonts = FcConfigGetFonts(nullptr, FcSetSystem))
            for (int i = 0; i < fonts->nfont; ++i) {
                FcChar8* name = nullptr;
                for (int n = 0; FcPatternGetString(fonts->fonts[i], FC_FAMILY, n, &name) == FcResultMatch; ++n)
                    result.insert(fold(reinterpret_cast<const char*>(name)));
            }
        return result;
    }();
    return families.count(folded) != 0;
}

std::string resolveSystem(const std::string& family) {
    static std::mutex mutex;
    static std::unordered_map<std::string, std::string> resolved;
    const std::string key = fold(family);
    std::lock_guard lock(mutex);
    if (const auto found = resolved.find(key); found != resolved.end()) return found->second;
    std::string result = family;
    if (!installed(key)) {
        const Alias* alias = aliasOf(key);
        const auto fallback = (alias ? *alias : default_alias).fallback;
        const auto chosen = std::find_if(fallback.begin(), fallback.end(), [](const char* name) { return generic(name) || installed(fold(name)); });
        result = chosen != fallback.end() ? *chosen : fallback.back();
    }
    resolved.emplace(key, result);
    return result;
}

// fontconfig's fonts, shared by every thread (Skia's font managers are thread-safe).
const sk_sp<SkFontMgr>& fontManager() {
    static const sk_sp<SkFontMgr> manager = SkFontMgr_New_FontConfig(nullptr, SkFontScanner_Make_FreeType());
    return manager;
}

// Application fonts (files of IDWriteFontSet collections): their typefaces and family names.
struct AppFonts {
    std::mutex mutex;
    std::set<std::string> files;
    std::vector<sk_sp<SkTypeface>> typefaces;
    std::atomic<unsigned> generation{0};
};
AppFonts& appFonts() { static AppFonts fonts; return fonts; }

// Every family name a typeface answers to (typographic and legacy, every language).
std::vector<std::string> familiesOf(const SkTypeface& typeface) {
    std::vector<std::string> names;
    SkString name;
    typeface.getFamilyName(&name);
    if (!name.isEmpty()) names.emplace_back(name.c_str());
    if (SkTypeface::LocalizedStrings* strings = typeface.createFamilyNameIterator()) {
        SkTypeface::LocalizedString localized;
        while (strings->next(&localized))
            if (!localized.fString.isEmpty() && std::find(names.begin(), names.end(), localized.fString.c_str()) == names.end())
                names.emplace_back(localized.fString.c_str());
        strings->unref();
    }
    return names;
}

sk_sp<SkTypeface> matchTypeface(const std::string& family, SkFontStyle style) {
    {
        auto& fonts = appFonts();
        std::lock_guard lock(fonts.mutex);
        const std::string key = fold(family);
        sk_sp<SkTypeface> best;
        int best_distance = std::numeric_limits<int>::max();
        for (const auto& typeface : fonts.typefaces) {
            const auto names = familiesOf(*typeface);
            if (std::none_of(names.begin(), names.end(), [&](const std::string& name) { return fold(name) == key; })) continue;
            const SkFontStyle own = typeface->fontStyle();
            const int distance = std::abs(own.weight() - style.weight()) + (own.slant() != style.slant()) * 1000 + std::abs(own.width() - style.width()) * 100;
            if (distance < best_distance) { best = typeface; best_distance = distance; }
        }
        if (best) return best;
    }
    if (auto typeface = fontManager()->matchFamilyStyle(family.c_str(), style)) return typeface;
    return fontManager()->legacyMakeTypeface(nullptr, style);
}

// Design-unit metrics of the font matched for the family and style.
bool fontMetrics(const std::string& family, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STYLE style, DWRITE_FONT_STRETCH stretch,
                 DWRITE_FONT_METRICS& metrics) {
    const sk_sp<SkTypeface> typeface = matchTypeface(family, fontStyle(weight, style, stretch));
    const int units = typeface ? typeface->getUnitsPerEm() : 0;
    if (units <= 0) return false;
    const SkFont font(typeface, static_cast<SkScalar>(units));
    SkFontMetrics m;
    font.getMetrics(&m);
    const auto u16 = [](double value) { return static_cast<UINT16>(std::clamp<long>(std::lround(value), 0, std::numeric_limits<UINT16>::max())); };
    const auto i16 = [](double value) {
        return static_cast<INT16>(std::clamp<long>(std::lround(value), std::numeric_limits<INT16>::min(), std::numeric_limits<INT16>::max()));
    };
    metrics.designUnitsPerEm = u16(units);
    metrics.ascent = u16(-m.fAscent);
    metrics.descent = u16(m.fDescent);
    metrics.lineGap = i16(m.fLeading);
    metrics.capHeight = u16(std::abs(m.fCapHeight));
    metrics.xHeight = u16(std::abs(m.fXHeight));
    SkScalar value = 0;
    metrics.underlinePosition = i16(m.hasUnderlinePosition(&value) ? -value : -units / 10.0); // DirectWrite: negative below the baseline.
    metrics.underlineThickness = u16(m.hasUnderlineThickness(&value) ? value : units / 20.0);
    metrics.strikethroughPosition = i16(m.hasStrikeoutPosition(&value) ? -value : units / 4.0);
    metrics.strikethroughThickness = u16(m.hasStrikeoutThickness(&value) ? value : units / 20.0);
    return true;
}

class MatchedFont final : public lm_com::Object<IDWriteFont> {
public:
    explicit MatchedFont(const DWRITE_FONT_METRICS& metrics) : metrics_(metrics) {}
    void STDMETHODCALLTYPE GetMetrics(DWRITE_FONT_METRICS* metrics) override { if (metrics) *metrics = metrics_; }
private:
    DWRITE_FONT_METRICS metrics_;
};

// A collection's family, named as the font manager knows it.
class FontFamily final : public lm_com::Object<IDWriteFontFamily> {
public:
    explicit FontFamily(std::string family) : family_(std::move(family)) {}
    HRESULT STDMETHODCALLTYPE GetFirstMatchingFont(DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STRETCH stretch, DWRITE_FONT_STYLE style,
                                                   IDWriteFont** font) override {
        if (!font) return E_POINTER;
        *font = nullptr;
        DWRITE_FONT_METRICS metrics{};
        if (!fontMetrics(family_, weight, style, stretch, metrics)) return E_FAIL;
        *font = new MatchedFont(metrics);
        return S_OK;
    }
private:
    std::string family_;
};

// The system collection holds every installed family and the aliased Windows ones; a family
// gets its index when it is first found. A file-built collection holds its files' families.
class FontCollection final : public lm_com::Object<IDWriteFontCollection1> {
public:
    FontCollection(bool system, const std::vector<std::string>& families) : system_(system) {
        for (const auto& family : families) families_.push_back({family, fold(family)});
    }
    HRESULT STDMETHODCALLTYPE GetFontFamily(UINT32 index, IDWriteFontFamily** family) override {
        if (!family) return E_POINTER;
        *family = nullptr;
        std::string name;
        {
            std::lock_guard lock(mutex_);
            if (index >= families_.size()) return E_INVALIDARG;
            name = families_[index].name;
        }
        *family = new FontFamily(system_ ? resolveSystem(name) : name);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE FindFamilyName(const WCHAR* familyName, UINT32* index, BOOL* exists) override {
        if (!familyName || !index || !exists) return E_INVALIDARG;
        *index = std::numeric_limits<UINT32>::max();
        *exists = FALSE;
        const std::string name = toUtf8(familyName), key = fold(name);
        std::lock_guard lock(mutex_);
        const auto found = std::find_if(families_.begin(), families_.end(), [&](const Family& family) { return family.folded == key; });
        if (found != families_.end()) {
            *index = static_cast<UINT32>(found - families_.begin());
            *exists = TRUE;
        } else if (system_ && (installed(key) || aliasOf(key))) {
            families_.push_back({name, key});
            *index = static_cast<UINT32>(families_.size() - 1);
            *exists = TRUE;
        }
        return S_OK;
    }
    // A file-built collection's own family named `family` (any case); empty when it has none.
    std::string member(const std::string& family) const {
        if (system_) return {};
        const std::string key = fold(family);
        std::lock_guard lock(mutex_);
        for (const auto& own : families_)
            if (own.folded == key) return own.name;
        return {};
    }
private:
    struct Family { std::string name, folded; };
    const bool system_;
    mutable std::mutex mutex_;
    std::vector<Family> families_;
};

class FontFaceReference final : public lm_com::Object<IDWriteFontFaceReference> {
public:
    explicit FontFaceReference(std::string file) : path(std::move(file)) {}
    const std::string path;
};

class FontSet final : public lm_com::Object<IDWriteFontSet> {
public:
    explicit FontSet(std::vector<ComPtr<FontFaceReference>> list) : faces(std::move(list)) {}
    const std::vector<ComPtr<FontFaceReference>> faces;
};

class FontSetBuilder final : public lm_com::Object<IDWriteFontSetBuilder> {
public:
    HRESULT STDMETHODCALLTYPE AddFontFaceReference(IDWriteFontFaceReference* fontFaceReference) override {
        auto* own = dynamic_cast<FontFaceReference*>(fontFaceReference);
        if (!own) return E_INVALIDARG;
        faces_.emplace_back(own);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateFontSet(IDWriteFontSet** fontSet) override {
        if (!fontSet) return E_POINTER;
        *fontSet = new FontSet(faces_);
        return S_OK;
    }
private:
    std::vector<ComPtr<FontFaceReference>> faces_;
};
} // namespace

const sk_sp<textlayout::FontCollection>& threadFontCollection() {
    struct Current {
        sk_sp<textlayout::FontCollection> collection;
        unsigned generation = std::numeric_limits<unsigned>::max();
    };
    thread_local Current current;
    auto& fonts = appFonts();
    const unsigned generation = fonts.generation.load(std::memory_order_acquire);
    if (!current.collection || current.generation != generation) {
        auto collection = sk_make_sp<textlayout::FontCollection>();
        collection->setDefaultFontManager(fontManager());
        {
            std::lock_guard lock(fonts.mutex);
            if (!fonts.typefaces.empty()) {
                auto provider = sk_make_sp<textlayout::TypefaceFontProvider>();
                for (const auto& typeface : fonts.typefaces)
                    for (const auto& family : familiesOf(*typeface)) provider->registerTypeface(typeface, SkString(family.c_str()));
                collection->setAssetFontManager(std::move(provider));
            }
        }
        collection->enableFontFallback();
        current.collection = std::move(collection);
        current.generation = generation;
    }
    return current.collection;
}

SkFontStyle fontStyle(DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STYLE style, DWRITE_FONT_STRETCH stretch) {
    // DirectWrite's weights and widths are both OpenType's scales.
    const bool known = stretch >= DWRITE_FONT_STRETCH_ULTRA_CONDENSED && stretch <= DWRITE_FONT_STRETCH_ULTRA_EXPANDED;
    return SkFontStyle(std::clamp(static_cast<int>(weight), 100, 1000), known ? static_cast<int>(stretch) : SkFontStyle::kNormal_Width,
                       style == DWRITE_FONT_STYLE_ITALIC ? SkFontStyle::kItalic_Slant
                       : style == DWRITE_FONT_STYLE_OBLIQUE ? SkFontStyle::kOblique_Slant : SkFontStyle::kUpright_Slant);
}

void appendUtf8(std::string& out, char32_t c) {
    if (c > 0x10FFFF || (c >= 0xD800 && c < 0xE000)) c = 0xFFFD;
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | c >> 6);
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | c >> 12);
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | c >> 18);
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

std::string toUtf8(const std::wstring& text) {
    std::string result;
    result.reserve(text.size());
    for (const wchar_t c : text) appendUtf8(result, static_cast<char32_t>(c));
    return result;
}

std::string resolveFamily(const std::wstring& family, IDWriteFontCollection* collection) {
    const std::string name = toUtf8(family);
    if (const auto* own = dynamic_cast<const FontCollection*>(collection))
        if (std::string member = own->member(name); !member.empty()) return member;
    return resolveSystem(name);
}

IDWriteFontCollection* systemFontCollection() {
    static auto* collection = new FontCollection(true, {}); // Lives as long as the process.
    return collection;
}

HRESULT createFontFaceReference(std::string path, IDWriteFontFaceReference** reference) {
    if (!reference) return E_POINTER;
    *reference = nullptr;
    if (access(path.c_str(), R_OK) != 0) return E_INVALIDARG;
    *reference = new FontFaceReference(std::move(path));
    return S_OK;
}

HRESULT createFontSetBuilder(IDWriteFontSetBuilder** builder) {
    if (!builder) return E_POINTER;
    *builder = new FontSetBuilder;
    return S_OK;
}

// Every face of each file becomes an application font (once per file); the collection lists
// their families.
HRESULT createFontCollection(IDWriteFontSet* set, IDWriteFontCollection1** collection) {
    if (!collection) return E_POINTER;
    *collection = nullptr;
    const auto* own = dynamic_cast<const FontSet*>(set);
    if (!own) return E_INVALIDARG;
    std::vector<std::string> families;
    auto& fonts = appFonts();
    {
        std::lock_guard lock(fonts.mutex);
        bool added = false;
        for (const auto& face : own->faces) {
            const bool fresh = fonts.files.insert(face->path).second;
            for (int index = 0; index < 64; ++index) {
                sk_sp<SkTypeface> typeface = fontManager()->makeFromFile(face->path.c_str(), index);
                if (!typeface) break;
                for (auto& family : familiesOf(*typeface))
                    if (std::find(families.begin(), families.end(), family) == families.end()) families.push_back(std::move(family));
                if (fresh) { fonts.typefaces.push_back(std::move(typeface)); added = true; }
            }
        }
        if (added) fonts.generation.fetch_add(1, std::memory_order_release);
    }
    *collection = new FontCollection(false, families);
    return S_OK;
}
} // namespace lm_dwrite
