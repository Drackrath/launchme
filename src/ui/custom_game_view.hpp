#pragma once
#include "settings_view.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <string>

// Add/edit panel of a user-added game (Settings → Library). One focus moves through
// the text fields, their "Wybierz…" buttons and the bottom buttons; geometry is shared
// by the renderer, pointer hit testing and the tests.
// The same panel edits how another game starts ("Opcje uruchamiania…" in the game's menu): a
// DRM-free game's file, options and working directory, a Steam game's launch options only.
namespace custom_game_ui {
enum class Item { name, file, file_browse, arguments, directory, directory_browse, remove, cancel, save };
enum Field { name_field, file_field, arguments_field, directory_field, field_count };
enum class Target { custom, drm_free, steam };

struct State {
    bool open = false;
    std::string id; // Empty while adding a game.
    Target target = Target::custom;
    std::string key;    // drm_free: its catalog entry ID; steam: the game's gameKey.
    std::wstring title; // The edited game's name (launch options).
    std::array<std::wstring, field_count> fields;
    Item focus = Item::name;
    size_t caret = 0;            // In the focused text field.
    bool selected = false;       // Ctrl+A: the next edit replaces the whole field.
    bool confirm_remove = false; // "Usuń" pressed once; the second press removes.
    std::wstring error;
    bool editing() const { return !id.empty(); }
    bool launchOptions() const { return target != Target::custom; }
    bool shows(int index) const {
        return target == Target::custom || (target == Target::drm_free ? index != name_field : index == arguments_field);
    }
    unsigned hidden() const { // Bit per field the panel leaves out.
        unsigned mask = 0;
        for (int index = 0; index < field_count; ++index) if (!shows(index)) mask |= 1u << index;
        return mask;
    }
};

// Text field behind an item, -1 for buttons.
inline int field(Item item) {
    switch (item) {
    case Item::name: return name_field;
    case Item::file: return file_field;
    case Item::arguments: return arguments_field;
    case Item::directory: return directory_field;
    default: return -1;
    }
}
// Rows 0-3 hold the fields (1 and 3 with a browse button), row 4 the buttons.
inline int row(Item item) {
    switch (item) {
    case Item::name: return 0;
    case Item::file: case Item::file_browse: return 1;
    case Item::arguments: return 2;
    case Item::directory: case Item::directory_browse: return 3;
    default: return 4;
    }
}
inline bool browseRow(int index) { return index == file_field || index == directory_field; }

struct Layout {
    float width, height;
    unsigned hidden = 0; // State::hidden(): left-out fields; the others close up.
    static constexpr float margin = 36, button_width = 176, gap = 16;
    bool shown(int index) const { return !(hidden & (1u << index)); }
    int slot(int index) const { // A field's row among the shown ones.
        int result = 0;
        for (int i = 0; i < index; ++i) result += shown(i);
        return result;
    }
    settings_ui::Rect panel() const {
        const float w = std::min(920.0f, width - 96), half = 326 - (field_count - slot(field_count)) * 52.0f;
        return {(width - w) * 0.5f, height * 0.5f - half, (width + w) * 0.5f, height * 0.5f + half};
    }
    float rowTop(int index) const { return panel().top + 96 + slot(index) * 104.0f; }
    settings_ui::Rect label(int index) const {
        const auto p = panel(); const float y = rowTop(index);
        return {p.left + margin, y, p.right - margin, y + 28};
    }
    settings_ui::Rect input(int index) const {
        const auto p = panel(); const float y = rowTop(index) + 32;
        return {p.left + margin, y, p.right - margin - (browseRow(index) ? button_width + gap : 0.0f), y + 56};
    }
    settings_ui::Rect browse(int index) const {
        const auto p = panel(); const float y = rowTop(index) + 32;
        return {p.right - margin - button_width, y + 6, p.right - margin, y + 50};
    }
    settings_ui::Rect error() const { const auto p = panel(); return {p.left + margin, p.bottom - 126, p.right - margin, p.bottom - 94}; }
    settings_ui::Rect button(Item item) const {
        const auto p = panel(); const float bottom = p.bottom - 30, top = bottom - settings_ui::Layout::button_height;
        if (item == Item::remove) return {p.left + margin, top, p.left + margin + button_width, bottom};
        const float right = item == Item::save ? p.right - margin : p.right - margin - button_width - gap;
        return {right - button_width, top, right, bottom};
    }
    std::optional<Item> hit(float x, float y, bool editing) const {
        for (const auto item : {Item::name, Item::file, Item::arguments, Item::directory})
            if (shown(row(item)) && input(row(item)).contains(x, y)) return item;
        if (shown(file_field) && browse(file_field).contains(x, y)) return Item::file_browse;
        if (shown(directory_field) && browse(directory_field).contains(x, y)) return Item::directory_browse;
        for (const auto item : {Item::remove, Item::cancel, Item::save})
            if ((item != Item::remove || editing) && button(item).contains(x, y)) return item;
        return std::nullopt;
    }
};
} // namespace custom_game_ui
