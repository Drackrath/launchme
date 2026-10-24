#pragma once

#include <algorithm>

// Geometry of the XMB home layout, shared by animation targets, hit testing and labels.
// The focused column is pinned at anchor_x; the top row scrolls horizontally and the
// focused column's games are listed downward (earlier rows move above the tile row).
namespace xmb_layout {
constexpr float anchor_x = 150, row_center = 205;
constexpr float tile = 112, focus_tile = 150, gap = 22;
constexpr float item = 88, focus_item = 124, item_gap = 16;
constexpr float caption_top = row_center + focus_tile * 0.5f + 10;
constexpr float list_top = row_center + focus_tile * 0.5f + 58;
constexpr float above_bottom = row_center - focus_tile * 0.5f - 26;
constexpr float label_left = anchor_x + focus_tile * 0.5f + focus_item * 0.5f + 24;
constexpr float poster_aspect = 2.0f / 3.0f;
inline float sizeScale(int size) { return 0.8f + std::clamp(size, 0, 2) * 0.2f; }
inline float artHeight(float width, bool posters) { return posters ? width / poster_aspect : width; }
inline float rowOffset(bool posters, float scale = 1) { return (artHeight(focus_tile, posters) * scale - focus_tile) * 0.5f; }

inline float tileSize(bool focused, float scale = 1) { return (focused ? focus_tile : tile) * scale; }
inline float tileLeft(int offset, float scale = 1) {
    if (offset == 0) return anchor_x;
    if (offset > 0) return anchor_x + (focus_tile + gap + (offset - 1) * (tile + gap)) * scale;
    return anchor_x + offset * (tile + gap) * scale;
}
inline float tileTop(float size) { return row_center - size * 0.5f; }
inline float itemSize(bool focused, float scale = 1) { return (focused ? focus_item : item) * scale; }
inline float itemLeft(float size, float scale = 1) { return anchor_x + (focus_tile * scale - size) * 0.5f; }
inline float labelLeft(float scale = 1) { return anchor_x + (focus_tile + focus_item) * scale * 0.5f + 24; }
// focus_row < 0 previews the column under a focused tile.
// A column belongs to a square collection/console tile, even when its games are posters.
inline float itemTop(int row, int focus_row, bool posters = false, float scale = 1) {
    const float top = list_top + rowOffset(false, scale), height = artHeight(item, posters) * scale;
    const float spacing = item_gap * scale;
    if (focus_row < 0) return top + row * (height + spacing);
    const int offset = row - focus_row;
    if (offset == 0) return top;
    if (offset > 0) return top + artHeight(focus_item, posters) * scale + spacing + (offset - 1) * (height + spacing);
    return above_bottom - rowOffset(false, scale) - height + (offset + 1) * (height + spacing);
}
// Enough rows around the focus to cover a tall window, without binding a whole collection.
inline int firstVisibleRow(int focus_row) { return std::max(0, focus_row - 2); }
inline int lastVisibleRow(int focus_row, float height, bool posters = false, float scale = 1) {
    return std::max(0, focus_row) + static_cast<int>((height - list_top - rowOffset(false, scale)) / ((artHeight(item, posters) + item_gap) * scale)) + 2;
}
} // namespace xmb_layout
