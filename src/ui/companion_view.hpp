#pragma once
// Towarzyszka: her home tile (her close-up picture and a speech bubble; where each layout puts
// it: home_widgets.hpp) and her chat screen. Geometry here is shared by drawing
// (companion_renderer.cpp), clicks and keyboard/pad focus (src/app/companion_input.cpp).
#include "settings_view.hpp"
#include "../net/companion_characters.hpp"
#include "avatar.hpp"
#include "spring.hpp"
#include "companion_feed_view.hpp"
#include "../net/companion_feelings.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace companion_ui {
using settings_ui::Rect;

// The game roulette in the log (src/app/companion_roulette.cpp): titles pass through a slot
// window, slow down and stop on the last one.
struct Roulette {
    std::vector<std::wstring> reel; // The last title is the pick.
    std::string key;                // gameKey() of the pick.
    float age = 0;                  // Seconds since the spin started.
    static constexpr float duration = 3.2f;
    bool landed() const { return age >= duration; }
    // Reel position in titles (fractional), easing out to reel.size() - 1.
    float position() const {
        if (reel.empty()) return 0;
        const float t = std::clamp(age / duration, 0.0f, 1.0f);
        const float eased = 1 - (1 - t) * (1 - t) * (1 - t) * (1 - t);
        return eased * static_cast<float>(reel.size() - 1);
    }
};
struct Message {
    bool user = false;
    std::wstring text;
    std::vector<companion::Segment> emotions; // Her messages: the emotion along the text.
    bool error = false;                        // A failed request, said in her voice.
    std::optional<Roulette> roulette;          // A roulette card instead of text.
    bool photo = false;                        // Her picture instead of text (CompanionView::photo).
    companion::Look speaker{}; // Who said it (signed with her name and colour).
    std::vector<companion::GameMention> mentions;
    bool free = false;                         // A session remark in the chat: never counted towards message_cap.
};
// Each companion's colour: her name over her messages, her bubbles' rim, her light in the chat.
struct Tint { float r, g, b; };
// (character.txt's color=).
inline Tint tint(companion::Look look) {
    const auto* character = companion::character(look);
    return character ? Tint{character->r, character->g, character->b} : Tint{0.8f, 0.8f, 0.8f};
}
// The chat's "…" menu: call another companion in, say goodbye to her, start the chat afresh, show
// where each answer's time went, or (gamification on) reset her sympathy after a dialog.
// "Zmień strój" turns the menu into her costumes (Kind::wear, `value` its slot; locked ones disabled).
// "Historia czatu" turns it into her older conversations (Kind::archive, `value` its index in
// CompanionState::history). Kind::clear ends the conversation, or starts a new one once it ended.
// Kind::photo enlarges her photo of this chat (as a click on it does).
struct MenuItem {
    enum class Kind { invite, farewell, clear, diagnostics, reset_affinity, costumes, wear, history, archive, photo, change, mind, relationship, relation_pick, backup, video };
    Kind kind = Kind::clear;
    std::wstring label;
    bool enabled = true;
    int value = 0;
    std::wstring detail; // Right-aligned and dimmed: "od poziomu 10", "✓".
};
// Her picture in the log: at most this wide, as tall as its proportions make it (capped).
inline constexpr float photo_width = 300, photo_max_height = 420;
// A suggestion under her last message: a prompt to send, a game to start (its gameKey) or the roulette.
struct Chip {
    enum class Kind { prompt, game, roulette };
    Kind kind = Kind::prompt;
    std::wstring label;
    std::wstring prompt; // Kind::prompt: sent as the player's message.
    std::string game;    // Kind::game: gameKey() of the game it starts.
};
// Focus: the chips are 0…n−1; the "…" menu's rows are menuFocus(0…).
inline constexpr int focus_back = -2, focus_input = -1, focus_send = -3, focus_more = -4, no_hit = -100;
// The pointer over her photo in the log (hover only; Renderer::companionPhoto() knows where it is).
inline constexpr int photo_hit = -5, focus_feed = -6;
inline constexpr int mind_delete_hit = -7;
// "Umysł i pamięć": Pokaż/Ukryj of a hidden thought (its button or its masked text), and the live
// thought bar under the sphere (a click shows or masks what she is thinking right now).
inline constexpr int mind_reveal_hit = -8, mind_live_hit = -9;
enum class MindFilter { all, feelings, memories, thoughts, reflections, life, games, active };
inline constexpr int mind_filter_count = 8, mind_filter_hit_base = -110;
inline const wchar_t* mindFilterLabel(MindFilter filter) {
    static constexpr std::array<const wchar_t*, mind_filter_count> names{trKey(L"Wszystko"), trKey(L"Uczucia"), trKey(L"Pamięć"), trKey(L"Myśli"),
        trKey(L"Wnioski"), trKey(L"Życie"), trKey(L"Gry"), trKey(L"Aktywne")};
    return tr(names[std::clamp(static_cast<int>(filter), 0, mind_filter_count - 1)]);
}
inline constexpr int menu_focus_base = -20;
inline constexpr int menuFocus(int row) { return menu_focus_base - row; }
inline constexpr int menuRowOf(int focus) { return focus <= menu_focus_base && focus > no_hit ? menu_focus_base - focus : -1; }
// The reset dialog's buttons: 0 resets (red), 1 cancels.
inline constexpr int confirm_reset = 0, confirm_cancel = 1;
inline constexpr int confirm_focus_base = -60;
inline constexpr int confirmFocus(int button) { return confirm_focus_base - button; }
inline constexpr int confirmButtonOf(int focus) { return focus == confirmFocus(confirm_reset) ? confirm_reset : focus == confirmFocus(confirm_cancel) ? confirm_cancel : -1; }

// Shared character picker, opened from the desktop card or either chat's "…" menu.
struct Picker {
    bool open = false, cancel_focus = false;
    std::vector<companion::Look> choices;
    int focus = 0, hover = no_hit;
};
struct PickerLayout {
    float width, height;
    static constexpr int page_size = 8, cancel = -1, previous = -2, next = -3;
    static constexpr float padding = 28, gap = 14, tile_height = 216;
    int columns(const Picker& picker) const { return std::clamp(static_cast<int>(picker.choices.size()), 1, 4); }
    int first(const Picker& picker) const { return picker.focus / page_size * page_size; }
    Rect panel(const Picker& picker) const {
        const int rows = (std::min(page_size, static_cast<int>(picker.choices.size())) + columns(picker) - 1) / columns(picker);
        const float w = std::min(width - 64, 920.0f), h = 76 + rows * (tile_height + gap) + 80;
        return {(width - w) * 0.5f, (height - h) * 0.5f, (width + w) * 0.5f, (height + h) * 0.5f};
    }
    Rect tile(const Picker& picker, int index) const {
        const auto p = panel(picker);
        const int local = index - first(picker), cols = columns(picker);
        if (local < 0 || local >= page_size) return {};
        const float w = (p.right - p.left - 2 * padding - (cols - 1) * gap) / cols;
        const float x = p.left + padding + (local % cols) * (w + gap), y = p.top + 76 + (local / cols) * (tile_height + gap);
        return {x, y, x + w, y + tile_height};
    }
    Rect button(const Picker& picker, int button) const {
        const auto p = panel(picker);
        const float x = button == cancel ? p.right - padding - 144 : p.left + padding + (button == next ? 64 : 0);
        return {x, p.bottom - 66, x + (button == cancel ? 144 : 52), p.bottom - 22};
    }
    int hit(const Picker& picker, float x, float y) const {
        if (button(picker, cancel).contains(x, y)) return cancel;
        if (first(picker) > 0 && button(picker, previous).contains(x, y)) return previous;
        if (first(picker) + page_size < static_cast<int>(picker.choices.size()) && button(picker, next).contains(x, y)) return next;
        for (int i = first(picker); i < std::min(first(picker) + page_size, static_cast<int>(picker.choices.size())); ++i)
            if (tile(picker, i).contains(x, y)) return i;
        return no_hit;
    }
};

struct Chat {
    std::vector<Message> log;
    size_t reveal = 0;      // Characters of her last message shown so far (typewriter).
    float reveal_clock = 0;
    // A reply of several messages (two short ones, or both companions): the next ones wait here,
    // each after the one before is said and a moment of typing.
    std::vector<Message> queue;
    float queue_clock = 0;
    bool waiting = false;     // A reply is on its way: three dots.
    bool hesitating = false;  // Cerebellar / self-censorship typing hesitation: typing dots briefly pause as if she backspaced.
    float hesitation_clock = 0;
    // The "…" menu: open, its rows (built when it opens) and the focused one.
    bool menu = false;
    std::vector<MenuItem> menu_items;
    int menu_focus = 0;
    // "Zresetuj sympatię" asks first: a dialog over the chat, Anuluj focused.
    bool confirm = false;
    int confirm_focus = confirm_cancel;
    std::wstring input;
    int focus = focus_input, hover = no_hit;
    std::vector<Chip> chips;
    float scroll = 0;       // Pixels scrolled up from the newest message.
    Spring scroll_motion;
    std::wstring status;    // Model and connection, or why she is offline.
    bool offline = false;
    float age = 0;          // Since the screen opened (entrance).
    float pose_age = 0;     // Since her full-figure pose began: the chat opened, or she changed her outfit.
    // The conversation reached its end (companion_chats.hpp): no field; Send becomes "Nowa rozmowa".
    bool ended = false;
    // An older conversation from "Historia czatu": read only, no field, chips or menu.
    bool archive = false;
    // Her photo enlarged over the chat (a click on it, or "…" → "Powiększ zdjęcie"); a click, Back,
    // Enter or A closes it. viewer_motion: its 0–1 appearance.
    bool viewer = false;
    bool feed = false; // Launchfeed replaces the whole chat with the phone.
    float viewer_motion = 0;
    // "…" → "Umysł i pamięć": a 3D neural/memory sphere on the left and her telemetry/profile panel on the right.
    bool mind = false;
    bool mind_from_sidebar = false;
    float mind_motion = 0;
    float mind_yaw = 0.35f, mind_pitch = 0.18f;
    Spring mind_yaw_spring, mind_pitch_spring;
    // The mouse wheel zooms the sphere (1 = fitted … mind_zoom_max); dragging turns it.
    float mind_zoom = 1;
    Spring mind_zoom_spring{1, 0};
    int mind_selected = 0, mind_hover = no_hit;
    MindFilter mind_filter = MindFilter::all;
    bool mind_dragging = false;
    float mind_drag_x = 0, mind_drag_y = 0;
    bool mind_confirm = false;
    int mind_confirm_focus = confirm_cancel;
    // Seconds since the mind screen opened: the sphere builds itself over mind_build_seconds.
    float mind_age = 0;
    // Her hidden thoughts stay masked until the player shows one (Pokaż): those shown in this
    // visit, by when she thought it and a hash of its text; the live one under the sphere too.
    // Masked again whenever the screen opens.
    std::vector<std::pair<uint64_t, size_t>> mind_revealed;
    bool mind_live_revealed = false;
    bool revealing() const { return !log.empty() && !log.back().user && reveal < log.back().text.size(); }
    bool talking() const { return revealing() || !queue.empty(); } // Her reply is not all said yet.
};

// Home: the bubble over the tile. Text is revealed as she speaks it, then fades.
struct Bubble {
    std::wstring text;
    std::vector<companion::Segment> emotions;
    size_t reveal = 0;
    float reveal_clock = 0, shown = 0; // shown: seconds since the whole text appeared.
    bool waiting = false;
    float alpha = 0;
    std::vector<companion::GameMention> mentions;
};

// Her pictures (res/companion/<key>/costume_<NN>[_<name>]/<emotion>[_talk|_blink].png, see
// res/companion/README.md) are embedded as RCDATA (CMakeLists.txt): base frames, then the optional
// open-mouth and closed-eyes frames, per character (her index) and costume slot (0: costume_00).
enum class Frame { base, talk, blink };
inline int spriteId(companion::Look look, Frame frame, companion::Emotion emotion, int costume = 0) {
    return 2000 + ((static_cast<int>(look) * 32 + costume) * 3 + static_cast<int>(frame)) * 32 + static_cast<int>(emotion);
}
// The shared sprite canvas (every picture has the same size): square, so a can in her hand or a waving hand fits.
inline constexpr float sprite_width = 560, sprite_height = 560, sprite_aspect = sprite_width / sprite_height;
// Where her face is on the picture shown for `emotion` (faces.txt, tools/companion_faces.py), by the same fallbacks as
// the picture itself (Renderer::drawCompanionFigure): this costume's feeling, its calm face, then her basic outfit's.
// Canvas pixels; size 0 = unknown.
inline companion::costumes::Face faceOf(companion::Look look, int costume, companion::Emotion emotion) {
    const auto* character = companion::character(look);
    if (!character) return {};
    const auto slot = [&](int wanted) -> const companion::costumes::Costume* {
        for (const auto& c : character->costumes) if (c.slot == wanted) return &c;
        return nullptr;
    };
    for (const int wanted : {costume, 0}) {
        if (const auto* c = slot(wanted)) {
            for (const auto feeling : {emotion, companion::calm})
                if (const auto& face = c->faces[static_cast<size_t>(feeling)]; face.size > 0) return face;
        }
        if (wanted == 0) break;
    }
    return {};
}
// Her round avatar: the face takes this share of the circle's diameter (hair and chin around it).
inline constexpr float face_fill = 0.68f;
// Her full-figure pose in a costume (<costume>/pose.png, optional; any size, cropped to her): it
// fills the chat's left column for a few seconds as the chat opens, then gives way to her
// close-up. Embedded above every sprite ID (CMakeLists.txt).
inline int poseId(companion::Look look, int costume = 0) { return 64000 + static_cast<int>(look) * 32 + costume; }
inline constexpr float pose_in = 0.35f, pose_hold = 3.0f, pose_out = 0.5f; // Seconds since the chat opened.
// The pose's opacity at `age`: in, held, out (gone after pose_hold + pose_out).
inline float poseAlpha(float age) {
    if (age < 0 || age >= pose_hold + pose_out) return 0;
    const float in = std::clamp(age / pose_in, 0.0f, 1.0f), out = std::clamp((pose_hold + pose_out - age) / pose_out, 0.0f, 1.0f);
    const auto ease = [](float t) { return t * t * (3 - 2 * t); };
    return ease(std::min(in, out));
}
// Her close-up comes in as the pose leaves.
inline float afterPose(float age) {
    const float t = std::clamp((age - pose_hold) / pose_out, 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

// The default home's right column: her tile on top, her head rising above it, and Glance's
// shortcut right under it (glance_ui::shortcut at glance_top). PSfive puts the same tile in its
// bottom-right corner (home_widgets.hpp).
// figure_sink: how far her picture's cut bottom edge lies below the tile's (clipped) edge.
inline constexpr float tile_top = 104, tile_bottom = 312, figure_height = 288, figure_sink = 24;
inline constexpr float glance_top = tile_bottom + 18;
inline Rect tile(float width) { return {width - 356, tile_top, width - 56, tile_bottom}; }
// Her picture: centre of its bottom edge and its height. The bottom lies below the tile's edge (it
// is clipped there, so hops and tilts never reveal the cut); her head rises above the tile.
struct Figure { float x, bottom, height; };
inline Figure tileFigure(Rect r) { return {(r.left + r.right) * 0.5f + 4, r.bottom + figure_sink, figure_height}; }
inline Figure tileFigure(float width) { return tileFigure(tile(width)); }
// Her entrance on start (progress 0–1): she jumps up out of the tile's bottom edge, overshoots a
// little (under 4% of her height) and settles. Returns how far below her place she still is, in
// figure heights (negative: above it, mid-jump).
inline float entranceDrop(float progress) {
    const float t = std::clamp(progress, 0.0f, 1.0f) - 1;
    constexpr float overshoot = 1.0f;
    return -((overshoot + 1) * t * t * t + overshoot * t * t);
}
// Hover and clicks: the tile and her head above it.
inline Rect hitArea(Rect r) {
    r.top = tileFigure(r).bottom - figure_height;
    return r;
}
inline Rect hitArea(float width) { return hitArea(tile(width)); }
inline float chipWidth(const std::wstring& label) { return std::clamp(46.0f + 9.4f * static_cast<float>(label.size()), 120.0f, 440.0f); }
struct Layout {
    float width, height;
    Rect back() const { return {56, 38, 110, 92}; }
    Rect more() const { return {right() - 54, 38, right(), 92}; } // "…": the chat's menu.
    Rect feed() const { return {right() - 118, 38, right() - 64, 92}; }
    // The menu under "…", right-aligned, one row per item.
    static constexpr float menu_width = 340, menu_row = 52, menu_padding = 8;
    // "Zmień strój": her costumes as tiles instead — her picture in each one and its name below,
    // under a title; several to a row, Left/Right and Up/Down between them.
    static constexpr float wardrobe_tile = 196, wardrobe_thumb = 168, wardrobe_caption = 58, wardrobe_gap = 12,
                           wardrobe_padding = 14, wardrobe_title = 46;
    static bool wardrobe(const Chat& chat) { return !chat.menu_items.empty() && chat.menu_items.front().kind == MenuItem::Kind::wear; }
    static int wardrobeColumns(const Chat& chat) {
        const int count = static_cast<int>(chat.menu_items.size());
        return std::clamp(count, 1, count <= 8 ? 4 : 6);
    }
    Rect menu(const Chat& chat) const {
        const float top = more().bottom + 10;
        if (wardrobe(chat)) {
            const int columns = wardrobeColumns(chat), rows = (static_cast<int>(chat.menu_items.size()) + columns - 1) / columns;
            const float w = 2 * wardrobe_padding + columns * wardrobe_tile + (columns - 1) * wardrobe_gap;
            const float h = 2 * wardrobe_padding + wardrobe_title + rows * (wardrobe_thumb + wardrobe_caption) + (rows - 1) * wardrobe_gap;
            return {right() - w, top, right(), top + h};
        }
        return {right() - menu_width, top, right(), top + 2 * menu_padding + menu_row * static_cast<float>(chat.menu_items.size())};
    }
    Rect menuRow(int row) const {
        const float top = more().bottom + 10 + menu_padding + menu_row * static_cast<float>(row);
        return {right() - menu_width + menu_padding, top, right() - menu_padding, top + menu_row - 4};
    }
    // One item of the open menu: a row, or a costume's tile.
    Rect menuItem(const Chat& chat, int item) const {
        if (!wardrobe(chat)) return menuRow(item);
        const auto panel = menu(chat);
        const int columns = wardrobeColumns(chat), column = item % columns, line = item / columns;
        const float left = panel.left + wardrobe_padding + column * (wardrobe_tile + wardrobe_gap);
        const float top = panel.top + wardrobe_padding + wardrobe_title + line * (wardrobe_thumb + wardrobe_caption + wardrobe_gap);
        return {left, top, left + wardrobe_tile, top + wardrobe_thumb + wardrobe_caption};
    }
    // Her pose as the chat opens: the left column, top to bottom (companion_ui::poseAlpha).
    Rect poseArea() const { return {16, 16, std::max(left() - 24, 200.0f), height}; }
    // Her photo enlarged: fitted into this area, centred, its caption below (the hints bar beneath).
    Rect viewer() const { return {64, 56, width - 64, height - 150}; }
    // "Umysł i pamięć": the chat's header row (Back, her name, her status), the 3D sphere on the left
    // and one column on the right, its cards the same gap apart: her profile, a strip of four
    // counters and the memory card (the selected memory over the list of all of them).
    static constexpr float mind_top = 164, mind_bottom = 72, mind_gap = 16, mind_padding = 24;
    Rect mindFilter(int index) const {
        const float w = (width - 112 - 7 * 8) / mind_filter_count;
        const float x = 56 + index * (w + 8);
        return {x, 108, x + w, 146};
    }
    static constexpr float mind_profile_height = 188, mind_stats_height = 96;
    // Inside the memory card, from its top: the header row's centre (category, when, Pokaż, Usuń),
    // the memory's first line, the hairline under it, the list's title and its first row.
    static constexpr float mind_header_y = 40, mind_detail_top = 68, mind_divider_y = 152, mind_list_title_y = 172, mind_list_top = 190;
    static constexpr float mind_row_pitch = 38, mind_row_height = 34, mind_delete_width = 96, mind_delete_height = 34, mind_reveal_width = 118;
    // The sphere builds itself as the screen opens (core, ribbons, embers, memories, threads).
    static constexpr float mind_build_seconds = 3.0f;
    static constexpr float mind_zoom_max = 2.0f;
    // The sphere's radius at `zoom` (shared by drawing and the pointer's hit test).
    float mindRadius(float zoom = 1) const {
        const auto s = mindSphere();
        return std::min(s.right - s.left, s.bottom - s.top) * 0.42f * zoom;
    }
    float mindSplit() const { return std::max(480.0f, std::min(width * 0.48f, width - 520.0f)); }
    Rect mindSphere() const { return {48, mind_top, mindSplit() - 12, height - mind_bottom}; }
    Rect mindPanel() const { return {mindSplit() + 12, mind_top, right(), height - mind_bottom}; }
    Rect mindClose() const { return back(); }
    Rect mindProfile() const { const auto p = mindPanel(); return {p.left, p.top, p.right, p.top + mind_profile_height}; }
    Rect mindStats() const {
        const auto above = mindProfile();
        return {above.left, above.bottom + mind_gap, above.right, above.bottom + mind_gap + mind_stats_height};
    }
    Rect mindMemory() const { const auto p = mindPanel(); return {p.left, mindStats().bottom + mind_gap, p.right, p.bottom}; }
    Rect mindDelete() const {
        const auto card = mindMemory();
        const float cy = card.top + mind_header_y;
        return {card.right - mind_padding - mind_delete_width, cy - mind_delete_height * 0.5f, card.right - mind_padding, cy + mind_delete_height * 0.5f};
    }
    // Pokaż/Ukryj of a hidden thought, left of Usuń; its masked text shows it too.
    Rect mindReveal() const {
        const auto del = mindDelete();
        return {del.left - 8 - mind_reveal_width, del.top, del.left - 8, del.bottom};
    }
    Rect mindDetail() const {
        const auto card = mindMemory();
        return {card.left + mind_padding, card.top + mind_detail_top - 6, card.right - mind_padding, card.top + mind_divider_y - 8};
    }
    // What she is thinking right now, in a bar under the sphere (while she answers).
    Rect mindLiveBar() const {
        const auto sphere = mindSphere();
        const float w = std::min(sphere.right - sphere.left - 24, 640.0f), cx = (sphere.left + sphere.right) * 0.5f;
        return {cx - w * 0.5f, sphere.bottom - 30, cx + w * 0.5f, sphere.bottom + 8};
    }
    // As many list rows as the card has room for.
    int mindListRows() const {
        const auto card = mindMemory();
        const float room = card.bottom - 12 - (card.top + mind_list_top) + (mind_row_pitch - mind_row_height);
        return std::max(1, static_cast<int>(room / mind_row_pitch));
    }
    // The list follows the selection and keeps it in the middle where it can.
    int mindListFirst(int selected, int total) const {
        const int rows = mindListRows();
        if (total <= rows) return 0;
        return std::clamp(selected - rows / 2, 0, total - rows);
    }
    Rect mindListRow(int slot) const {
        const auto card = mindMemory();
        const float y = card.top + mind_list_top + static_cast<float>(slot) * mind_row_pitch;
        return {card.left + 12, y, card.right - 12, y + mind_row_height};
    }
    // The reset dialog: centred over the conversation, its two buttons side by side at the bottom.
    static constexpr float confirm_width = 560, confirm_height = 214, confirm_padding = 30, confirm_button = 56;
    Rect confirm() const {
        const float cx = (left() + right()) * 0.5f, cy = height * 0.5f;
        const float w = std::min(confirm_width, right() - left());
        return {cx - w * 0.5f, cy - confirm_height * 0.5f, cx + w * 0.5f, cy + confirm_height * 0.5f};
    }
    static Rect dialogButton(Rect dialog, int button) {
        const float gap = 14, w = (dialog.right - dialog.left - 2 * confirm_padding - gap) * 0.5f;
        const float x = dialog.left + confirm_padding + static_cast<float>(button) * (w + gap);
        return {x, dialog.bottom - confirm_padding - confirm_button, x + w, dialog.bottom - confirm_padding};
    }
    Rect confirmButton(int button) const { return dialogButton(confirm(), button); }
    // "Usunąć to wspomnienie?": the same dialog, centred on the whole mind screen.
    Rect mindConfirm() const {
        const float w = std::min(confirm_width, width - 96);
        return {(width - w) * 0.5f, (height - confirm_height) * 0.5f, (width + w) * 0.5f, (height + confirm_height) * 0.5f};
    }
    Rect mindConfirmButton(int button) const { return dialogButton(mindConfirm(), button); }
    // Her picture on the left, its bottom below the screen's edge.
    Figure figure() const {
        const float h = std::clamp(height * 0.64f, 520.0f, 760.0f);
        return {std::max(24 + h * sprite_aspect * 0.5f, 300.0f), height + h * 0.08f, h}; // The cut edge stays off-screen when she tilts.
    }
    // A guest with her (duo 0 alone … 1 together): both fit where she stood alone. She shrinks a
    // little and keeps her right edge; the guest, smaller still, stands behind her on the left,
    // her face beside the host's hair (her outer hair may leave the screen). Cut edges stay below it.
    struct Pair { Figure host, guest; };
    Pair pair(float duo) const {
        const auto solo = figure();
        const float t = std::clamp(duo, 0.0f, 1.0f);
        const float width_alone = solo.height * sprite_aspect;
        const float host_height = solo.height * (1 - 0.2f * t);
        Pair result;
        result.host = {solo.x + 0.5f * (width_alone - host_height * sprite_aspect), height + host_height * 0.08f, host_height};
        const float guest_height = solo.height * 0.64f;
        const float host_left = solo.x + 0.5f * width_alone - 0.8f * width_alone; // Where her left edge ends up beside a guest.
        result.guest = {host_left - 0.02f * width_alone, height + guest_height * 0.08f, guest_height};
        return result;
    }
    float left() const { return figure().x + figure().height * sprite_aspect * 0.5f + 24; }
    float right() const { return width - 56; }
    Rect input() const { return {left(), height - 152, right() - 132, height - 96}; }
    Rect send() const { return {right() - 120, height - 152, right(), height - 96}; }
    // An ended conversation: "Nowa rozmowa" needs a wider button.
    Rect input(const Chat& chat) const { auto r = input(); if (chat.ended) r.right = right() - 232; return r; }
    Rect send(const Chat& chat) const { auto r = send(); if (chat.ended) r.left = right() - 220; return r; }
    // Chips wrap into rows above the field, the first row on top.
    std::vector<Rect> chips(const Chat& chat) const {
        std::vector<Rect> rects;
        std::vector<int> row_of;
        float x = left();
        int row = 0;
        for (const auto& chip : chat.chips) {
            const float w = std::min(chipWidth(chip.label), right() - left());
            if (x + w > right() && x > left()) { ++row; x = left(); }
            rects.push_back({x, 0, x + w, 44});
            row_of.push_back(row);
            x += w + 10;
        }
        const int rows = rects.empty() ? 0 : row + 1;
        for (size_t i = 0; i < rects.size(); ++i) {
            const float top = input().top - 18 - (rows - row_of[i]) * 54.0f;
            rects[i].top = top; rects[i].bottom = top + 44;
        }
        return rects;
    }
    float logTop() const { return 128; }
    float logBottom(const Chat& chat) const {
        const auto rects = chips(chat);
        return (rects.empty() ? input().top : rects.front().top) - 18;
    }
    int hit(float x, float y, const Chat& chat) const {
        if (chat.confirm) { // Only its buttons while the dialog is open.
            for (int button : {confirm_reset, confirm_cancel}) if (confirmButton(button).contains(x, y)) return confirmFocus(button);
            return no_hit;
        }
        if (chat.menu)
            for (size_t i = 0; i < chat.menu_items.size(); ++i) if (menuItem(chat, static_cast<int>(i)).contains(x, y)) return menuFocus(static_cast<int>(i));
        if (!chat.archive && more().contains(x, y)) return focus_more;
        if (!chat.archive && !chat.menu && feed().contains(x, y)) return focus_feed;
        if (back().contains(x, y)) return focus_back;
        if (chat.archive) return no_hit; // Read only: Back and scrolling.
        if (input(chat).contains(x, y)) return focus_input;
        if (send(chat).contains(x, y)) return focus_send;
        const auto rects = chips(chat);
        for (size_t i = 0; i < rects.size(); ++i) if (rects[i].contains(x, y)) return static_cast<int>(i);
        return no_hit;
    }
    // Rows of chips (for Up/Down between them, the field and Back).
    int chipRow(const Chat& chat, int index) const {
        const auto rects = chips(chat);
        if (index < 0 || index >= static_cast<int>(rects.size())) return -1;
        int row = 0;
        for (int i = 1; i <= index; ++i) if (rects[i].top != rects[i - 1].top) ++row;
        return row;
    }
};
} // namespace companion_ui
