#pragma once
#include <Windows.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <list>
#include <string>
#include <unordered_map>

// UI-thread cache for recurring labels (every desktop row, caption, count and icon glyph is
// drawn again each frame). Position and ink do not affect layout. Retains the format to prevent
// pointer reuse and includes all mutable base-format options in the key. `variant` separates
// callers that adjust the new layout (`configure`, run once): one variant's adjustments never
// reach another's entry. Hashed lookup, least recently used entries are dropped.
class TextLayoutCache {
public:
    static constexpr size_t capacity = 1024, max_characters = 512;
    IDWriteTextLayout* get(IDWriteFactory* factory, const std::wstring& text, IDWriteTextFormat* format,
                          float width, float height, uint32_t variant = 0,
                          const std::function<void(IDWriteTextLayout*)>& configure = {}) {
        if (!factory || !format || text.size() > max_characters ||
            !std::isfinite(width) || !std::isfinite(height) || width < 0 || height < 0) return nullptr;
        const auto style = Style::of(format);
        const auto hash = hashOf(text, format, width, height, variant);
        for (auto [at, end] = index_.equal_range(hash); at != end; ++at) {
            auto& entry = *at->second;
            if (entry.format.Get() == format && entry.width == width && entry.height == height && entry.variant == variant &&
                entry.style == style && entry.text == text) {
                if (at->second != entries_.begin()) entries_.splice(entries_.begin(), entries_, at->second);
                return entry.layout.Get();
            }
        }
        Entry entry;
        if (FAILED(factory->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()), format, width, height,
                                            entry.layout.GetAddressOf()))) return nullptr;
        if (configure) configure(entry.layout.Get());
        entry.text = text; entry.format = format; entry.style = style;
        entry.width = width; entry.height = height; entry.variant = variant; entry.hash = hash;
        entries_.push_front(std::move(entry));
        index_.emplace(hash, entries_.begin());
        if (entries_.size() > capacity) {
            const auto last = std::prev(entries_.end());
            for (auto [at, end] = index_.equal_range(last->hash); at != end; ++at)
                if (at->second == last) { index_.erase(at); break; }
            entries_.pop_back();
        }
        return entries_.front().layout.Get();
    }
    void clear() { index_.clear(); entries_.clear(); }
    size_t size() const { return entries_.size(); }
private:
    struct Style {
        DWRITE_TEXT_ALIGNMENT alignment{};
        DWRITE_PARAGRAPH_ALIGNMENT paragraph{};
        DWRITE_WORD_WRAPPING wrapping{};
        DWRITE_READING_DIRECTION reading{};
        DWRITE_FLOW_DIRECTION flow{};
        DWRITE_LINE_SPACING_METHOD spacing_method{};
        float spacing = 0, baseline = 0, tab = 0;
        DWRITE_TRIMMING trimming{};
        Microsoft::WRL::ComPtr<IDWriteInlineObject> sign;
        static Style of(IDWriteTextFormat* format) {
            Style result;
            result.alignment = format->GetTextAlignment(); result.paragraph = format->GetParagraphAlignment();
            result.wrapping = format->GetWordWrapping(); result.reading = format->GetReadingDirection();
            result.flow = format->GetFlowDirection(); result.tab = format->GetIncrementalTabStop();
            format->GetLineSpacing(&result.spacing_method, &result.spacing, &result.baseline);
            format->GetTrimming(&result.trimming, result.sign.GetAddressOf());
            return result;
        }
        bool operator==(const Style& other) const {
            return alignment == other.alignment && paragraph == other.paragraph && wrapping == other.wrapping &&
                reading == other.reading && flow == other.flow && spacing_method == other.spacing_method &&
                spacing == other.spacing && baseline == other.baseline && tab == other.tab &&
                trimming.granularity == other.trimming.granularity && trimming.delimiter == other.trimming.delimiter &&
                trimming.delimiterCount == other.trimming.delimiterCount && sign.Get() == other.sign.Get();
        }
    };
    struct Entry {
        std::wstring text;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        Style style;
        float width = 0, height = 0;
        uint32_t variant = 0;
        uint64_t hash = 0;
    };
    static uint64_t hashOf(const std::wstring& text, IDWriteTextFormat* format, float width, float height, uint32_t variant) {
        uint64_t hash = std::hash<std::wstring>{}(text);
        const auto mix = [&](uint64_t value) { hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2); };
        uint32_t w = 0, h = 0;
        std::memcpy(&w, &width, sizeof(w)); std::memcpy(&h, &height, sizeof(h));
        mix(reinterpret_cast<uintptr_t>(format)); mix(w); mix(h); mix(variant);
        return hash;
    }
    std::list<Entry> entries_; // Most recently used first.
    std::unordered_multimap<uint64_t, std::list<Entry>::iterator> index_;
};
