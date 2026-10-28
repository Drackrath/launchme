#pragma once
#include "settings_view.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <string>

// Add/edit panel of a user-added game (Settings → Library). One focus moves through
// the text fields, their "Wybierz…" buttons and the bottom buttons; geometry is shared
// by the renderer, pointer hit testing and the tests.
// A user-added game's panel shows its poster beside the fields (matched by the typed name) with
// "Zidentyfikuj grę" under it.
// The same panel edits how another game starts ("Opcje uruchamiania…" in the game's menu): a
// DRM-free game's file, options and working directory, a Steam game's launch options only.
namespace custom_game_ui {
enum class Item { name, file, file_browse, arguments, directory, directory_browse, identify, remove, cancel, save };
enum Field { name_field, file_field, arguments_field, directory_field, field_count };
enum class Target { custom, drm_free, steam };
// The poster beside the fields: nothing to look for (no name), being looked for, shown, none found.
enum class Poster { none, loading, shown, missing };

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
    bool confirm_remove = false; // "Usuń z biblioteki" pressed once; the second press removes.
    std::wstring error;
    // A user-added game: the ID a new game is saved under (so Zidentyfikuj grę can key it before
    // it is saved), the name the game's stored artwork belongs to (matched for or identified as),
    // whether a new game was identified (Anuluj drops what that stored), and its poster's state.
    std::string pending_id;
    std::wstring art_name;
    bool identified_new = false;
    Poster poster = Poster::none;
    Item identify_from = Item::name; // Left from "Zidentyfikuj grę" goes back where Right came from.
    bool editing() const { return !id.empty(); }
    bool launchOptions() const { return target != Target::custom; }
    bool posterShown() const { return target == Target::custom; }
    const std::string& gameId() const { return editing() ? id : pending_id; }
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
// Rows 0-3 hold the fields (1 and 3 with a browse button), row 4 the buttons; "Zidentyfikuj grę"
// stands apart in the poster's column (5).
inline int row(Item item) {
    switch (item) {
    case Item::name: return 0;
    case Item::file: case Item::file_browse: return 1;
    case Item::arguments: return 2;
    case Item::directory: case Item::directory_browse: return 3;
    case Item::identify: return 5;
    default: return 4;
    }
}
inline bool browseRow(int index) { return index == file_field || index == directory_field; }

struct Layout {
    float width, height;
    unsigned hidden = 0; // State::hidden(): left-out fields; the others close up.
    bool poster = false; // State::posterShown(): the poster column on the right.
    static constexpr float margin = 36, button_width = 176, gap = 16, poster_width = 200, poster_gap = 28;
    static Layout of(const State& state, float width, float height) { return {width, height, state.hidden(), state.posterShown()}; }
    bool shown(int index) const { return !(hidden & (1u << index)); }
    int slot(int index) const { // A field's row among the shown ones.
        int result = 0;
        for (int i = 0; i < index; ++i) result += shown(i);
        return result;
    }
    settings_ui::Rect panel() const {
        const float w = std::min(poster ? 1100.0f : 920.0f, width - 96), half = 326 - (field_count - slot(field_count)) * 52.0f;
        return {(width - w) * 0.5f, height * 0.5f - half, (width + w) * 0.5f, height * 0.5f + half};
    }
    float rowTop(int index) const { return panel().top + 96 + slot(index) * 104.0f; }
    float fieldsRight() const { return panel().right - margin - (poster ? poster_width + poster_gap : 0.0f); }
    settings_ui::Rect label(int index) const {
        const auto p = panel(); const float y = rowTop(index);
        return {p.left + margin, y, fieldsRight(), y + 28};
    }
    settings_ui::Rect input(int index) const {
        const auto p = panel(); const float y = rowTop(index) + 32;
        return {p.left + margin, y, fieldsRight() - (browseRow(index) ? button_width + gap : 0.0f), y + 56};
    }
    settings_ui::Rect browse(int index) const {
        const float y = rowTop(index) + 32;
        return {fieldsRight() - button_width, y + 6, fieldsRight(), y + 50};
    }
    // The poster (2:3) at the top of its column, "Zidentyfikuj grę" under it.
    settings_ui::Rect posterBox() const {
        const auto p = panel(); const float top = rowTop(0) + 4;
        return {p.right - margin - poster_width, top, p.right - margin, top + poster_width * 1.5f};
    }
    settings_ui::Rect identify() const {
        const auto box = posterBox();
        return {box.left, box.bottom + 16, box.right, box.bottom + 16 + settings_ui::Layout::button_height};
    }
    settings_ui::Rect error() const { const auto p = panel(); return {p.left + margin, p.bottom - 126, p.right - margin, p.bottom - 94}; }
    settings_ui::Rect button(Item item) const {
        const auto p = panel(); const float bottom = p.bottom - 30, top = bottom - settings_ui::Layout::button_height;
        // "Usuń z biblioteki" is longer than the other captions.
        if (item == Item::remove) return {p.left + margin, top, p.left + margin + button_width + 60, bottom};
        const float right = item == Item::save ? p.right - margin : p.right - margin - button_width - gap;
        return {right - button_width, top, right, bottom};
    }
    std::optional<Item> hit(float x, float y, bool editing) const {
        for (const auto item : {Item::name, Item::file, Item::arguments, Item::directory})
            if (shown(row(item)) && input(row(item)).contains(x, y)) return item;
        if (shown(file_field) && browse(file_field).contains(x, y)) return Item::file_browse;
        if (shown(directory_field) && browse(directory_field).contains(x, y)) return Item::directory_browse;
        if (poster && (identify().contains(x, y) || posterBox().contains(x, y))) return Item::identify;
        for (const auto item : {Item::remove, Item::cancel, Item::save})
            if ((item != Item::remove || editing) && button(item).contains(x, y)) return item;
        return std::nullopt;
    }
};
} // namespace custom_game_ui
