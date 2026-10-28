// Drawing DirectWrite layouts with Skia: the layout's text blobs in the target's paint, ranges
// with a drawing effect in that brush's paint, underlines as rectangles. DrawText keeps the
// layouts it made (per thread, by format, text and box), so the same text drawn every frame is
// shaped once and its blobs stay in the GPU's text cache.
#include "dwrite_internal.hpp"

#include "include/core/SkCanvas.h"

#include <algorithm>
#include <list>
#include <unordered_map>

namespace lm_dwrite {
void TextLayout::draw(SkCanvas* canvas, float x, float y, const SkPaint& paint) {
    layOut();
    for (const Run& run : runs_) {
        const CharStyle* style = styles_.empty() ? nullptr : &styles_[run.style];
        SkPaint ink = paint;
        if (style && style->effect && !lm_gfx::brushPaint(style->effect.Get(), canvas, ink)) ink = paint;
        canvas->drawTextBlob(run.blob, x, y, ink);
        if (run.underline) canvas->drawRect(run.underline_rect.makeOffset(x, y), ink);
    }
}

namespace {
// DrawText's layouts: the most recently used 512 on each thread.
class LayoutCache {
public:
    ComPtr<TextLayout> get(const wchar_t* text, UINT32 length, IDWriteTextFormat* format, float width, float height) {
        const FormatState* state = FormatState::from(format);
        if (!state) return {};
        Key key{state->font().get(), state->revision(), std::wstring(text, length), width, height};
        if (const auto found = map_.find(key); found != map_.end()) {
            order_.splice(order_.begin(), order_, found->second.second);
            return found->second.first;
        }
        ComPtr<TextLayout> layout;
        if (FAILED(TextLayout::create(text, length, format, width, height, layout))) return {};
        order_.push_front(key);
        map_.emplace(std::move(key), std::make_pair(layout, order_.begin()));
        if (map_.size() > 512) {
            map_.erase(order_.back());
            order_.pop_back();
        }
        return layout;
    }
private:
    // A format's font spec is its own (and lives while a cached layout holds it): with the
    // format's revision it names the format's settings.
    struct Key {
        const FontSpec* format;
        uint64_t revision;
        std::wstring text;
        float width, height;
        bool operator==(const Key& other) const {
            return format == other.format && revision == other.revision && width == other.width && height == other.height && text == other.text;
        }
    };
    struct Hash {
        size_t operator()(const Key& key) const {
            size_t hash = std::hash<std::wstring>()(key.text) ^ std::hash<const void*>()(key.format);
            hash = hash * 31 + std::hash<uint64_t>()(key.revision);
            hash = hash * 31 + std::hash<float>()(key.width);
            return hash * 31 + std::hash<float>()(key.height);
        }
    };
    std::list<Key> order_;
    std::unordered_map<Key, std::pair<ComPtr<TextLayout>, std::list<Key>::iterator>, Hash> map_;
};
} // namespace
} // namespace lm_dwrite

namespace lm_gfx {
void drawTextLayout(SkCanvas* canvas, IDWriteTextLayout* layout, float x, float y, const SkPaint& paint, bool) {
    if (auto* own = lm_dwrite::TextLayout::from(layout)) own->draw(canvas, x, y, paint);
}

// The same layout and clip as a text layout of the box's size drawn at its corner with
// D2D1_DRAW_TEXT_OPTIONS_CLIP, so both draw the same pixels.
void drawText(SkCanvas* canvas, const wchar_t* text, UINT32 length, IDWriteTextFormat* format, float left, float top, float right,
              float bottom, bool clip, const SkPaint& paint, bool) {
    thread_local lm_dwrite::LayoutCache cache;
    const float width = std::max(0.0f, right - left), height = std::max(0.0f, bottom - top);
    const auto layout = cache.get(text, length, format, width, height);
    if (!layout) return;
    const int restore_to = canvas->save();
    if (clip) canvas->clipRect(SkRect::MakeXYWH(left, top, width, height), true);
    layout->draw(canvas, left, top, paint);
    canvas->restoreToCount(restore_to);
}
} // namespace lm_gfx
