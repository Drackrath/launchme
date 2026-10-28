// DirectWrite fonts on fontconfig and HarfBuzz: the per-thread Pango context, family names
// (Windows families the launcher asks for map to installed Linux stand-ins), the system and
// file-built collections, and font metrics in design units.
#include "dwrite_internal.hpp"

#include <fontconfig/fontconfig.h>
#include <hb-ot.h>
#include <hb.h>

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
// Bumped when font files are registered: each thread then starts a new context (a new font map
// reads fontconfig's fonts again).
std::atomic<unsigned> font_generation{0};

struct ThreadContext {
    PangoContext* context = nullptr;
    unsigned generation = 0;
    ~ThreadContext() { if (context) g_object_unref(context); }
};

std::string fold(const std::string& name) {
    gchar* folded = g_utf8_casefold(name.c_str(), -1);
    std::string result(folded);
    g_free(folded);
    return result;
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
            if (!g_ascii_strcasecmp(name, folded.c_str())) return &alias;
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

// Design-unit metrics of the font fontconfig picks for the family and style.
bool fontMetrics(const std::string& family, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STYLE style, DWRITE_FONT_STRETCH stretch,
                 DWRITE_FONT_METRICS& metrics) {
    PangoFontDescription* description = describeFont(family, weight, style, stretch, 16);
    PangoFont* font = pango_context_load_font(threadContext(), description);
    pango_font_description_free(description);
    if (!font) return false;
    hb_font_t* sized = pango_font_get_hb_font(font);
    hb_face_t* face = sized ? hb_font_get_face(sized) : nullptr;
    if (face) {
        hb_font_t* units = hb_font_create(face); // Scaled to design units.
        const auto get = [&](hb_ot_metrics_tag_t tag) {
            hb_position_t value = 0;
            hb_ot_metrics_get_position(units, tag, &value);
            return value;
        };
        const auto u16 = [](long value) { return static_cast<UINT16>(std::clamp<long>(value, 0, std::numeric_limits<UINT16>::max())); };
        const auto i16 = [](long value) {
            return static_cast<INT16>(std::clamp<long>(value, std::numeric_limits<INT16>::min(), std::numeric_limits<INT16>::max()));
        };
        metrics.designUnitsPerEm = u16(hb_face_get_upem(face));
        metrics.ascent = u16(get(HB_OT_METRICS_TAG_HORIZONTAL_ASCENDER));
        metrics.descent = u16(-get(HB_OT_METRICS_TAG_HORIZONTAL_DESCENDER));
        metrics.lineGap = i16(get(HB_OT_METRICS_TAG_HORIZONTAL_LINE_GAP));
        metrics.capHeight = u16(get(HB_OT_METRICS_TAG_CAP_HEIGHT));
        metrics.xHeight = u16(get(HB_OT_METRICS_TAG_X_HEIGHT));
        metrics.underlinePosition = i16(get(HB_OT_METRICS_TAG_UNDERLINE_OFFSET));
        metrics.underlineThickness = u16(get(HB_OT_METRICS_TAG_UNDERLINE_SIZE));
        metrics.strikethroughPosition = i16(get(HB_OT_METRICS_TAG_STRIKEOUT_OFFSET));
        metrics.strikethroughThickness = u16(get(HB_OT_METRICS_TAG_STRIKEOUT_SIZE));
        hb_font_destroy(units);
    }
    g_object_unref(font);
    return face != nullptr;
}

class MatchedFont final : public lm_com::Object<IDWriteFont> {
public:
    explicit MatchedFont(const DWRITE_FONT_METRICS& metrics) : metrics_(metrics) {}
    void STDMETHODCALLTYPE GetMetrics(DWRITE_FONT_METRICS* metrics) override { if (metrics) *metrics = metrics_; }
private:
    DWRITE_FONT_METRICS metrics_;
};

// A collection's family, named as fontconfig knows it.
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

// The families fontconfig lists for a registered file (typographic and legacy names alike).
void addFamiliesOf(const std::string& path, std::vector<std::string>& families) {
    FcPattern* pattern = FcPatternCreate();
    FcPatternAddString(pattern, FC_FILE, reinterpret_cast<const FcChar8*>(path.c_str()));
    FcObjectSet* objects = FcObjectSetBuild(FC_FAMILY, nullptr);
    if (FcFontSet* fonts = FcFontList(nullptr, pattern, objects)) {
        for (int i = 0; i < fonts->nfont; ++i) {
            FcChar8* name = nullptr;
            for (int n = 0; FcPatternGetString(fonts->fonts[i], FC_FAMILY, n, &name) == FcResultMatch; ++n) {
                const std::string family(reinterpret_cast<const char*>(name));
                if (std::find(families.begin(), families.end(), family) == families.end()) families.push_back(family);
            }
        }
        FcFontSetDestroy(fonts);
    }
    FcObjectSetDestroy(objects);
    FcPatternDestroy(pattern);
}
} // namespace

PangoContext* threadContext() {
    thread_local ThreadContext current;
    const unsigned generation = font_generation.load(std::memory_order_acquire);
    if (!current.context || current.generation != generation) {
        PangoFontMap* map = pango_cairo_font_map_new();
        PangoContext* context = pango_font_map_create_context(map);
        g_object_unref(map); // The context holds it.
        // Absolute sizes in DIPs; outlines and advances unhinted, positions unrounded: widths
        // measured here are the widths drawn at any scale.
        pango_cairo_context_set_resolution(context, 96);
        cairo_font_options_t* options = cairo_font_options_create();
        cairo_font_options_set_antialias(options, CAIRO_ANTIALIAS_GRAY);
        cairo_font_options_set_hint_style(options, CAIRO_HINT_STYLE_NONE);
        cairo_font_options_set_hint_metrics(options, CAIRO_HINT_METRICS_OFF);
        pango_cairo_context_set_font_options(context, options);
        cairo_font_options_destroy(options);
        pango_context_set_round_glyph_positions(context, FALSE);
        pango_context_set_base_dir(context, PANGO_DIRECTION_LTR);
        if (current.context) g_object_unref(current.context);
        current.context = context;
        current.generation = generation;
    }
    return current.context;
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

PangoFontDescription* describeFont(const std::string& family, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STYLE style,
                                   DWRITE_FONT_STRETCH stretch, float size) {
    PangoFontDescription* description = pango_font_description_new();
    pango_font_description_set_family(description, family.c_str());
    // DirectWrite's and Pango's weights are both the OpenType scale.
    pango_font_description_set_weight(description, static_cast<PangoWeight>(std::clamp(static_cast<int>(weight), 100, 1000)));
    pango_font_description_set_style(description, style == DWRITE_FONT_STYLE_ITALIC    ? PANGO_STYLE_ITALIC
                                                  : style == DWRITE_FONT_STYLE_OBLIQUE ? PANGO_STYLE_OBLIQUE
                                                                                       : PANGO_STYLE_NORMAL);
    const bool known = stretch >= DWRITE_FONT_STRETCH_ULTRA_CONDENSED && stretch <= DWRITE_FONT_STRETCH_ULTRA_EXPANDED;
    pango_font_description_set_stretch(description, known ? static_cast<PangoStretch>(stretch - 1) : PANGO_STRETCH_NORMAL);
    pango_font_description_set_absolute_size(description, size * PANGO_SCALE);
    return description;
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

HRESULT createFontCollection(IDWriteFontSet* set, IDWriteFontCollection1** collection) {
    if (!collection) return E_POINTER;
    *collection = nullptr;
    const auto* own = dynamic_cast<const FontSet*>(set);
    if (!own) return E_INVALIDARG;
    std::vector<std::string> families;
    {
        static std::mutex mutex;
        static std::set<std::string> registered;
        std::lock_guard lock(mutex);
        bool added = false;
        for (const auto& face : own->faces) {
            if (!registered.count(face->path)) {
                if (!FcConfigAppFontAddFile(nullptr, reinterpret_cast<const FcChar8*>(face->path.c_str()))) continue;
                registered.insert(face->path);
                added = true;
            }
            addFamiliesOf(face->path, families);
        }
        if (added) font_generation.fetch_add(1, std::memory_order_release);
    }
    *collection = new FontCollection(false, families);
    return S_OK;
}
} // namespace lm_dwrite
