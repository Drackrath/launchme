// Desktop mode, the input side (src/ui/desktop_view.hpp): hit tests on the shared geometry, the
// mouse (hover, buttons acting on release, double-click to play, right-click menus, the wheel,
// dragging games onto collections and collections into order), keys and type-to-search, the menus'
// actions, collections, the Pobierania list under its shortcut and the tooltips.
#include "launcher.hpp"
#include "launcher_internal.hpp"
#include "../library/organization.hpp"
#include "../library/custom_games.hpp"

#include <algorithm>
#include <cmath>

using namespace desktop_ui;
using library_manager_ui::Page;

namespace {
// "1 gra", "3 gry", "12 gier" (English: games).
std::wstring gamesLabel(size_t n) {
    return std::to_wstring(n) + (n == 1 ? tr(L" gra") : n % 10 >= 2 && n % 10 <= 4 && (n % 100 < 12 || n % 100 > 14) ? tr(L" gry") : tr(L" gier"));
}
std::wstring trimmedQuery(const std::wstring& text) {
    const auto first = text.find_first_not_of(L' ');
    return first == std::wstring::npos ? std::wstring{} : text.substr(first, text.find_last_not_of(L' ') - first + 1);
}
bool down(int key) { return (GetKeyState(key) & 0x8000) != 0; }
}

// ---- Hit tests -------------------------------------------------------------------------------

std::pair<bool, bool> Launcher::desktopEmptyButtons() const {
    if (!cards_.empty() || scanning_ || libraryLoading()) return {false, false}; // As drawn: the skeleton has no buttons.
    const auto* entry = desktopEntry();
    if (!trimmedQuery(desktop_query_).empty()) return {true, desktop_section_ != "all"};
    if (settings_.desktop_installed && !desktopSource().empty()) return {true, false};
    if (entry && entry->kind == EntryKind::collection) return {true, false};
    if (entry && entry->kind == EntryKind::favorites) return {false, false};
    if (public_library_.empty()) return {true, true};
    return {false, false};
}

desktop_ui::Hit Launcher::desktopHit(float x, float y) const {
    const auto L = desktopLayout();
    if (desktop_menu_) {
        const auto& menu = *desktop_menu_;
        if (menu.sub_parent >= 0 && !menu.sub.empty()) {
            const auto sub = subRect(menu, L.width, L.height);
            if (sub.contains(x, y)) return {Target::submenu_item, hitItem(sub, menu.sub, x, y)};
        }
        const auto panel = menuRect(menu, L.width, L.height);
        if (panel.contains(x, y)) return {Target::menu_item, hitItem(panel, menu.items, x, y)};
        return {};
    }
    const auto tools = desktopTools();
    if (desktop_widgets_open_) {
        const auto& spots = renderer_.desktopHotspots();
        for (auto it = spots.rbegin(); it != spots.rend(); ++it) if (it->rect.contains(x, y)) {
            switch (it->hit.kind) {
            case SheetHit::Kind::row: return {Target::widget_friend, it->hit.index};
            case SheetHit::Kind::tile: return {Target::widget_item, it->hit.index};
            case SheetHit::Kind::action: return {Target::widget_all, 0};
            default: return {Target::widget_panel, 0};
            }
        }
    }
    if (desktop_downloads_open_) {
        const auto at = std::find(tools.begin(), tools.end(), Tool::downloads);
        const auto anchor = L.tool(static_cast<int>(at - tools.begin()));
        const int rows = static_cast<int>(desktop_download_rows_.size());
        if (L.popover(rows, anchor).contains(x, y)) {
            if (L.popoverAll(rows, anchor).contains(x, y)) return {Target::popover_all, 0};
            if (desktopDownloadsClearable() && L.popoverClear(rows, anchor).contains(x, y)) return {Target::popover_clear, 0};
            for (int i = 0; i < std::min(rows, popover_rows); ++i)
                if (const auto r = L.popoverRow(i, rows, anchor); r.contains(x, y)) return {Layout::popoverAction(r).contains(x, y) ? Target::popover_action : Target::popover_row, i};
            return {Target::popover_row, -1};
        }
    }
    if (y < title_h) {
        for (int i = 0; i < 3; ++i)
            if (L.windowButton(i).contains(x, y)) return {i == 0 ? Target::minimize : i == 1 ? Target::maximize : Target::close, i};
        for (int i = 0; i < static_cast<int>(tools.size()); ++i) if (L.tool(i).contains(x, y)) return {Target::tool, i};
        if (L.sidebarToggle().contains(x, y)) return {Target::sidebar_toggle, 0};
        if (!desktop_query_.empty() && L.searchClear().contains(x, y)) return {Target::search_clear, 0};
        if (L.search().contains(x, y)) return {Target::search, 0};
        return {Target::caption, 0};
    }
    if (settings_.companion && renderer_.companionBubble().contains(x, y)) return {Target::companion_bubble, 0};
    // The pane's left edge: dragged, it resizes the pane (and the grid's columns).
    if (L.paneEdge().contains(x, y) && !desktop_downloads_open_ && !desktop_widgets_open_) return {Target::pane_resize, 0};
    if (x < L.sidebarRight()) {
        if (L.companion_card && L.companionMore().contains(x, y)) return {Target::companion_more, 0};
        if (L.companion_card && L.companionCard().contains(x, y)) return {Target::companion_card, 0};
        const auto rows = desktopRows();
        const int row = L.hitRow(rows, desktop_sidebar_scroll_, x, y);
        if (row >= 0 && rows[static_cast<size_t>(row)].heading && rows[static_cast<size_t>(row)].group == Group::collections &&
            L.headingAdd(rows[static_cast<size_t>(row)], desktop_sidebar_scroll_).contains(x, y)) return {Target::sidebar_add, row};
        // Biblioteka's heading: "+" adds a game from disk, the arrows read the libraries again (F5).
        if (row >= 0 && rows[static_cast<size_t>(row)].heading && rows[static_cast<size_t>(row)].group == Group::library) {
            if (L.headingAdd(rows[static_cast<size_t>(row)], desktop_sidebar_scroll_).contains(x, y)) return {Target::library_add, row};
            if (L.headingRefresh(rows[static_cast<size_t>(row)], desktop_sidebar_scroll_).contains(x, y)) return {Target::library_refresh, row};
        }
        if (row >= 0) return {Target::sidebar_row, row};
        return {};
    }
    if (x >= L.paneLeft()) {
        const size_t marked = desktopTargets().size();
        if (marked > 1) {
            for (int i = 0; i < Layout::bulk_count; ++i) if (L.paneBulk(i).contains(x, y)) return {Target::bulk, i};
            return {};
        }
        if (cards_.empty()) return {};
        const float s = desktop_pane_scroll_;
        if (y < L.paneTop(s) + L.bannerHeight()) return {};
        if (L.panePrimary(s).contains(x, y)) return {Target::pane_primary, 0};
        if (L.paneFavorite(s).contains(x, y)) return {Target::pane_favorite, 0};
        if (L.paneMore(s).contains(x, y)) return {Target::pane_more, 0};
        for (int i = 0; i < Layout::quick_count; ++i) if (L.paneQuick(i, s).contains(x, y)) return {Target::pane_quick, i};
        const auto& chips = renderer_.desktopChips();
        for (int i = 0; i < static_cast<int>(chips.size()); ++i) if (chips[static_cast<size_t>(i)].contains(x, y)) return {Target::pane_chip, i};
        if (renderer_.desktopChipAdd().contains(x, y)) return {Target::pane_chip_add, 0};
        if (renderer_.desktopRating().contains(x, y)) return {Target::pane_rating, 0};
        for (int i = 0; i < 2; ++i) if (renderer_.desktopStudios()[static_cast<size_t>(i)].contains(x, y)) return {Target::pane_studio, i};
        if (renderer_.desktopAchievements().contains(x, y)) return {Target::pane_achievements, 0};
        if (renderer_.desktopCloud().contains(x, y)) return {Target::pane_cloud, 0};
        return {};
    }
    if (L.sortButton().contains(x, y)) return {Target::sort, 0};
    if (L.installedChip().contains(x, y)) return {Target::installed, 0};
    if (L.sizeSlider().contains(x, y)) return {Target::size_slider, 0};
    if (cards_.empty()) {
        const auto [primary, secondary] = desktopEmptyButtons();
        if (primary && L.emptyButton(0, secondary).contains(x, y)) return {Target::empty_primary, 0};
        if (secondary && L.emptyButton(1, true).contains(x, y)) return {Target::empty_secondary, 0};
        return {Target::grid, -1};
    }
    auto track = L.scrollTrack();
    track.left -= 4;
    if (L.maxScroll(cards_.size()) > 0 && track.contains(x, y)) return {Target::scrollbar, 0};
    const int card = L.hitCard(cards_.size(), desktop_scroll_, x, y);
    if (card >= 0) {
        const auto poster = L.poster(static_cast<size_t>(card), desktop_scroll_);
        const float hover = static_cast<size_t>(card) < desktop_card_hover_.size() ? desktop_card_hover_[static_cast<size_t>(card)] : 0.0f;
        const bool shown = hover > 0.3f || desktop_hover_ == Hit{Target::card, card} || desktop_hover_ == Hit{Target::card_play, card} ||
                           desktop_hover_ == Hit{Target::card_check, card};
        if ((shown || !desktop_marked_.empty()) && Layout::checkBox(poster).contains(x, y)) return {Target::card_check, card};
        return {Target::card, card};
    }
    return {Target::grid, -1};
}

// ---- Mouse -----------------------------------------------------------------------------------

void Launcher::desktopClick(float mouse_x, float mouse_y, bool double_click) {
    if (desktop_meta_.open && desktopActive()) { desktopMetaPointer(mouse_x, mouse_y, true, double_click); return; }
    if (desktop_quick_.open && desktopActive()) { desktopQuickPointer(mouse_x, mouse_y, true, double_click); return; }
    const float scale = desktopScale(), x = mouse_x / scale, y = mouse_y / scale;
    desktop_keyboard_ = false;
    const auto hit = desktopHit(x, y);
    if (desktop_menu_) {
        if ((hit.target == Target::menu_item || hit.target == Target::submenu_item) && hit.index >= 0) { desktop_pressed_ = hit; SetCapture(hwnd_); return; }
        if (hit.target != Target::menu_item && hit.target != Target::submenu_item) desktopCloseMenu(); // A click beside a menu only closes it.
        return;
    }
    const auto tools = desktopTools();
    if (desktop_widgets_open_ && hit.target != Target::widget_friend && hit.target != Target::widget_item && hit.target != Target::widget_all &&
        hit.target != Target::widget_panel) {
        const bool own = hit.target == Target::tool && hit.index >= 0 && hit.index < static_cast<int>(tools.size()) && tools[static_cast<size_t>(hit.index)] == Tool::friends;
        desktop_widgets_open_ = false;
        InvalidateRect(hwnd_, nullptr, FALSE);
        if (own || hit.target != Target::card) return;
    }
    if (hit.target == Target::widget_panel) return;
    if (desktop_downloads_open_ && hit.target != Target::popover_row && hit.target != Target::popover_all && hit.target != Target::popover_clear &&
        hit.target != Target::popover_action) {
        const bool own = hit.target == Target::tool && hit.index >= 0 && hit.index < static_cast<int>(tools.size()) && tools[static_cast<size_t>(hit.index)] == Tool::downloads;
        desktop_downloads_open_ = false;
        InvalidateRect(hwnd_, nullptr, FALSE);
        if (own || hit.target != Target::card) return; // Its own shortcut toggles; a card may still be picked.
    }
    if (hit.target == Target::companion_bubble && !renderer_.companionLinkAt(x, y).empty()) {
        desktop_pressed_ = {};
        if (!double_click) companionLinkClick(x, y);
        return;
    }
    desktop_pressed_ = hit;
    desktop_drag_ = {};
    desktop_drag_.x0 = desktop_drag_.x = x; desktop_drag_.y0 = desktop_drag_.y = y;
    if (hit.target != Target::search && hit.target != Target::search_clear && desktop_search_focus_) desktopSearchFocus(false);
    switch (hit.target) {
    case Target::card: {
        const auto index = static_cast<size_t>(hit.index);
        desktop_zone_ = 1;
        if (double_click && !down(VK_CONTROL) && !down(VK_SHIFT)) {
            desktop_pressed_ = {};
            desktopFocus(index, false);
            desktopPickSelected();
            return;
        }
        if (down(VK_CONTROL)) desktopToggleMark(index);
        else if (down(VK_SHIFT)) desktopMarkRange(desktop_anchor_, index);
        else {
            if (index >= desktop_keys_.size() || !desktop_marked_.contains(desktop_keys_[index])) desktop_marked_.clear();
            desktop_anchor_ = index;
        }
        desktopFocus(index, false);
        desktop_drag_.kind = DesktopDrag::Kind::card;
        desktop_drag_.card = hit.index;
        SetCapture(hwnd_);
        return;
    }
    case Target::sidebar_row: {
        const auto rows = desktopRows();
        if (hit.index < 0 || hit.index >= static_cast<int>(rows.size())) return;
        const auto& row = rows[static_cast<size_t>(hit.index)];
        desktop_zone_ = 0; desktop_sidebar_focus_ = hit.index;
        if (!row.heading) {
            const auto& entry = desktop_entries_[static_cast<size_t>(row.entry)];
            desktopShowSection(entry.id);
            if (entry.reorderable) { desktop_drag_.kind = DesktopDrag::Kind::entry; desktop_drag_.row = hit.index; }
        }
        SetCapture(hwnd_);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    case Target::search:
        desktopSearchFocus(true);
        desktopTextPress(desktopLayout().search().left + 36, x, desktopScale(), double_click, false); // Caret where it falls; drag selects.
        return;
    case Target::scrollbar: {
        const auto L = desktopLayout();
        const auto thumb = L.scrollThumb(cards_.size(), desktop_scroll_);
        if (y >= thumb.top && y < thumb.bottom) desktop_drag_.grab = y - thumb.top;
        else {
            desktop_drag_.grab = (thumb.bottom - thumb.top) * 0.5f;
            desktop_scroll_ = desktop_scroll_target_ = L.scrollFromThumb(cards_.size(), y - desktop_drag_.grab);
            desktopRequestArt();
        }
        desktop_drag_.kind = DesktopDrag::Kind::scrollbar; desktop_drag_.active = true;
        desktop_scrolled_at_ = GetTickCount64();
        SetCapture(hwnd_); InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    case Target::pane_resize:
        if (double_click) { desktop_pressed_ = {}; desktopSetPane(0); settings_dirty_ = true; settings_edited_ = std::chrono::steady_clock::now(); return; } // Back to automatic.
        desktop_drag_.kind = DesktopDrag::Kind::pane; desktop_drag_.active = true;
        desktop_drag_.grab = x - desktopLayout().paneLeft();
        SetCapture(hwnd_); InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    case Target::size_slider: {
        const auto slider = desktopLayout().sizeSlider();
        desktopSetPoster(posterFromLevel((x - slider.left - 7) / std::max(1.0f, slider.right - slider.left - 14)));
        desktop_drag_.kind = DesktopDrag::Kind::slider; desktop_drag_.active = true;
        SetCapture(hwnd_);
        return;
    }
    case Target::grid:
        if (!down(VK_CONTROL) && !desktop_marked_.empty()) { desktop_marked_.clear(); InvalidateRect(hwnd_, nullptr, FALSE); }
        desktop_pressed_ = {};
        return;
    case Target::none: case Target::caption:
        desktop_pressed_ = {};
        return;
    default:
        SetCapture(hwnd_); // Buttons act on release, over the same target.
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
}

void Launcher::desktopPointerUp(float mouse_x, float mouse_y) {
    if (!desktop_mode_) return;
    if (desktopSheet()) {
        desktopSettingsRelease(mouse_x, mouse_y); // Its buttons act on release; drags end in pointerUp().
        if (GetCapture() == hwnd_ && !settings_dragging_ && !color_editor_.dragging) ReleaseCapture();
        return;
    }
    const float scale = desktopScale(), x = mouse_x / scale, y = mouse_y / scale;
    const auto drag = std::exchange(desktop_drag_, {});
    const auto pressed = std::exchange(desktop_pressed_, {});
    if (GetCapture() == hwnd_) ReleaseCapture();
    InvalidateRect(hwnd_, nullptr, FALSE);
    if (!desktopActive()) return;
    if (drag.active) {
        const auto rows = desktopRows();
        if (drag.kind == DesktopDrag::Kind::card && drag.drop_row >= 0 && drag.drop_row < static_cast<int>(rows.size()) && rows[static_cast<size_t>(drag.drop_row)].entry >= 0)
            desktopDrop(desktop_entries_[static_cast<size_t>(rows[static_cast<size_t>(drag.drop_row)].entry)], drag.games);
        else if (drag.kind == DesktopDrag::Kind::entry && drag.row >= 0 && drag.row < static_cast<int>(rows.size()) && rows[static_cast<size_t>(drag.row)].entry >= 0)
            desktopReorder(desktop_entries_[static_cast<size_t>(rows[static_cast<size_t>(drag.row)].entry)].collection, drag.reorder_before);
        else if (drag.kind == DesktopDrag::Kind::slider || drag.kind == DesktopDrag::Kind::pane) { settings_dirty_ = true; settings_edited_ = std::chrono::steady_clock::now(); }
        return;
    }
    const auto hit = desktopHit(x, y);
    if (pressed.target == Target::none || !(hit == pressed)) return;
    desktopActivate(hit);
}

void Launcher::desktopMouseMove(float mouse_x, float mouse_y) {
    if (desktop_meta_.open && desktopActive()) { desktopMetaPointer(mouse_x, mouse_y, false); return; }
    if (desktop_quick_.open && desktopActive()) { desktopQuickPointer(mouse_x, mouse_y, false); return; }
    const float scale = desktopScale(), x = mouse_x / scale, y = mouse_y / scale;
    auto& drag = desktop_drag_;
    if (drag.kind != DesktopDrag::Kind::none) {
        drag.x = x; drag.y = y;
        const auto L = desktopLayout();
        switch (drag.kind) {
        case DesktopDrag::Kind::scrollbar:
            desktop_scroll_ = desktop_scroll_target_ = L.scrollFromThumb(cards_.size(), y - drag.grab);
            desktop_scrolled_at_ = GetTickCount64();
            desktopRequestArt();
            break;
        case DesktopDrag::Kind::slider: {
            const auto slider = L.sizeSlider();
            desktopSetPoster(posterFromLevel((x - slider.left - 7) / std::max(1.0f, slider.right - slider.left - 14)));
            break;
        }
        case DesktopDrag::Kind::pane: desktopSetPane(static_cast<int>(std::round(L.width - (x - drag.grab)))); break;
        case DesktopDrag::Kind::card: {
            if (!drag.active && std::hypot(x - drag.x0, y - drag.y0) > 6) {
                drag.active = true;
                desktop_tooltip_ = 0;
                drag.games.clear();
                for (const auto& game : desktopTargets()) drag.games.push_back(library_view::selectionKey(game));
            }
            if (!drag.active) break;
            drag.drop_row = -1;
            const auto rows = desktopRows();
            const int row = L.hitRow(rows, desktop_sidebar_scroll_, x, y);
            if (row >= 0 && !rows[static_cast<size_t>(row)].heading && desktop_entries_[static_cast<size_t>(rows[static_cast<size_t>(row)].entry)].droppable)
                drag.drop_row = row;
            break;
        }
        case DesktopDrag::Kind::entry: {
            if (!drag.active && std::abs(y - drag.y0) > 6) drag.active = true;
            if (!drag.active) break;
            const auto rows = desktopRows();
            drag.reorder_before = -1; drag.reorder_y = -1;
            int k = 0;
            float last_bottom = -1;
            for (const auto& row : rows) {
                if (row.heading || row.group != Group::collections) continue;
                const auto r = L.sidebarRow(row, desktop_sidebar_scroll_);
                if (y < (r.top + r.bottom) * 0.5f) { drag.reorder_before = k; drag.reorder_y = r.top - 1; break; }
                last_bottom = r.bottom + 1;
                ++k;
            }
            if (drag.reorder_before < 0) drag.reorder_y = last_bottom;
            break;
        }
        default: break;
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const auto hit = desktopHit(x, y);
    if (desktop_menu_) {
        auto& menu = *desktop_menu_;
        if (hit.target == Target::menu_item && hit.index != menu.hover) {
            menu.hover = hit.index; menu.keyboard = false; menu.in_sub = false;
            if (hit.index >= 0 && menu.items[static_cast<size_t>(hit.index)].submenu && menu.items[static_cast<size_t>(hit.index)].enabled) desktopOpenSubmenu(hit.index);
            else if (hit.index >= 0) { menu.sub_parent = -1; menu.sub.clear(); }
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else if (hit.target == Target::submenu_item && hit.index != menu.sub_hover) {
            menu.sub_hover = hit.index; menu.keyboard = false; menu.in_sub = true;
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }
    if (!(hit == desktop_hover_)) {
        desktop_hover_ = hit;
        desktop_hover_at_ = GetTickCount64();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void Launcher::desktopContextClick(float mouse_x, float mouse_y) {
    if (desktop_meta_.open) return; // Its edits stay until Zapisz or Anuluj.
    if (desktop_quick_.open) { desktopQuickClose(); return; }
    const float scale = desktopScale(), x = mouse_x / scale, y = mouse_y / scale;
    desktop_keyboard_ = false;
    if (desktop_menu_) desktopCloseMenu();
    desktop_downloads_open_ = false;
    const auto hit = desktopHit(x, y);
    switch (hit.target) {
    case Target::card: case Target::card_play: case Target::card_check: {
        const auto index = static_cast<size_t>(hit.index);
        if (index >= desktop_keys_.size() || !desktop_marked_.contains(desktop_keys_[index])) desktop_marked_.clear();
        desktop_zone_ = 1;
        desktopFocus(index, false);
        desktopOpenGameMenu(x, y, false);
        return;
    }
    case Target::sidebar_row: case Target::sidebar_add: {
        const auto rows = desktopRows();
        if (hit.index < 0 || hit.index >= static_cast<int>(rows.size())) return;
        const auto& row = rows[static_cast<size_t>(hit.index)];
        if (!row.heading) desktopOpenSectionMenu(row.entry, x, y);
        else if (row.group == Group::collections) {
            Menu menu; menu.kind = Menu::Kind::section; menu.x = x; menu.y = y;
            menu.items.push_back({Action::collection_new, tr(L"Nowa kolekcja…"), {}, glyph::add});
            menu.items.push_back({Action::collection_manage, tr(L"Zarządzaj kolekcjami…"), {}, glyph::edit});
            desktop_menu_ = std::move(menu);
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
    }
    case Target::pane_primary: case Target::pane_favorite: case Target::pane_more: case Target::pane_quick: case Target::pane_chip:
    case Target::pane_chip_add: case Target::pane_rating: case Target::pane_studio: case Target::pane_achievements: case Target::pane_cloud:
        desktopOpenGameMenu(x, y, false);
        return;
    default: return;
    }
}

bool Launcher::desktopWheel(float mouse_x, float mouse_y, int delta, bool control) {
    if ((desktop_quick_.open || desktop_meta_.open) && desktopActive()) return true; // The list under them stays put.
    if (desktopSheet()) return desktopSheetWheel(mouse_x, mouse_y, delta);
    if (!desktopActive() || topModal() != Modal::none || friends_open_ || keyboard_.open) return false;
    const float scale = desktopScale(), x = mouse_x / scale, y = mouse_y / scale;
    if (desktop_menu_) { desktopCloseMenu(); return true; }
    if (desktop_downloads_open_) { desktop_downloads_open_ = false; InvalidateRect(hwnd_, nullptr, FALSE); }
    const float notches = static_cast<float>(delta) / WHEEL_DELTA;
    const auto L = desktopLayout();
    if (control) { desktopSetPoster(static_cast<float>(settings_.desktop_poster) + notches * 12); return true; }
    if (y < title_h) return true;
    if (x < L.sidebarRight()) {
        desktop_sidebar_scroll_ = std::clamp(desktop_sidebar_scroll_ - notches * 64, 0.0f, L.sidebarMaxScroll(desktopRows()));
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (x >= L.paneLeft()) {
        const float limit = std::max(0.0f, renderer_.desktopPaneHeight() - (L.height - title_h));
        desktop_pane_scroll_ = std::clamp(desktop_pane_scroll_ - notches * 72, 0.0f, limit);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    desktopScrollBy(-notches * 120);
    return true;
}

void Launcher::desktopScrollBy(float amount) {
    const auto L = desktopLayout();
    desktop_scroll_target_ = std::clamp(desktop_scroll_target_ + amount, 0.0f, L.maxScroll(cards_.size()));
    desktop_scrolled_at_ = GetTickCount64();
    desktopRequestArt();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

LPCWSTR Launcher::desktopCursor(float mouse_x, float mouse_y) const {
    if (const auto shape = desktopModalCursor(mouse_x, mouse_y)) return shape;
    if (desktop_meta_.open && desktopActive()) { // Edytuj metadane: its fields take text, its buttons a click.
        const auto view = renderer_.desktopViewport();
        const int hit = desktop_ui::MetaLayout{view.width, view.height}.hit(mouse_x / desktopScale(), mouse_y / desktopScale(), desktop_meta_.identify);
        return hit < 0 ? IDC_ARROW : hit < desktop_ui::meta_fields ? IDC_IBEAM : IDC_HAND;
    }
    if (desktop_quick_.open && desktopActive()) {
        const auto view = renderer_.desktopViewport();
        const desktop_ui::QuickLayout layout{view.width, view.height, static_cast<int>(desktop_quick_.items.size())};
        return layout.hit(mouse_x / desktopScale(), mouse_y / desktopScale()) >= 0 ? IDC_HAND : IDC_ARROW;
    }
    if (desktopEmbedded()) { // The sheet's ✕; the screen beneath answers through hoverButton().
        const float scale = desktopScale();
        const auto view = renderer_.desktopViewport();
        return embedSheet(view.width, view.height).close().contains(mouse_x / scale, mouse_y / scale) ? IDC_HAND : nullptr;
    }
    if (desktopSheet()) return windowStripHit(mouse_x, mouse_y) >= 0 ? nullptr : desktopSheetCursor(mouse_x, mouse_y);
    if (!desktopActive() || topModal() != Modal::none || friends_open_ || pending_launch_) return nullptr;
    if (desktop_drag_.active && desktop_drag_.kind == DesktopDrag::Kind::card) return desktop_drag_.drop_row >= 0 ? IDC_HAND : IDC_ARROW;
    if (desktop_drag_.active && desktop_drag_.kind == DesktopDrag::Kind::entry) return IDC_SIZENS;
    if (desktop_drag_.active && desktop_drag_.kind == DesktopDrag::Kind::pane) return IDC_SIZEWE;
    const float scale = desktopScale();
    const auto hit = desktopHit(mouse_x / scale, mouse_y / scale);
    switch (hit.target) {
    case Target::search: return IDC_IBEAM;
    case Target::pane_resize: return IDC_SIZEWE;
    case Target::none: case Target::caption: case Target::grid: case Target::widget_panel: case Target::widget_item: return IDC_ARROW;
    case Target::menu_item: case Target::submenu_item: return IDC_ARROW;
    case Target::pane_quick:
        if (hit.index == 2 && !cards_.empty() && gameFolder(cards_[selected_].game).empty()) return IDC_ARROW;
        return IDC_HAND;
    default: return IDC_HAND;
    }
}

// ---- Actions ---------------------------------------------------------------------------------

void Launcher::desktopActivate(const desktop_ui::Hit& hit) {
    const auto tools = desktopTools();
    switch (hit.target) {
    case Target::minimize: ShowWindow(hwnd_, SW_MINIMIZE); return;
    case Target::maximize: toggleMaximized(); return;
    case Target::close: PostMessageW(hwnd_, WM_CLOSE, 0, 0); return;
    case Target::sidebar_toggle: desktopToggleSidebar(); return;
    case Target::tool:
        if (hit.index >= 0 && hit.index < static_cast<int>(tools.size())) desktopToolPick(tools[static_cast<size_t>(hit.index)]);
        return;
    case Target::search_clear:
        desktop_query_.clear(); desktop_caret_ = 0; desktopApplyQuery(); desktopSearchFocus(true);
        return;
    case Target::sidebar_add: desktopNewCollection({}); return;
    case Target::library_add: openCustomEditor({}); return;
    case Target::library_refresh: desktopRefreshLibraries(); return;
    case Target::sidebar_row: {
        const auto rows = desktopRows();
        if (hit.index < 0 || hit.index >= static_cast<int>(rows.size()) || !rows[static_cast<size_t>(hit.index)].heading) return;
        auto& collapsed = desktop_collapsed_[static_cast<size_t>(rows[static_cast<size_t>(hit.index)].group)];
        collapsed = !collapsed;
        desktop_sidebar_scroll_ = std::clamp(desktop_sidebar_scroll_, 0.0f, desktopLayout().sidebarMaxScroll(desktopRows()));
        desktopSaveState();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    case Target::companion_card: case Target::companion_bubble: openCompanion(); return;
    case Target::companion_more: {
        const auto anchor = desktopLayout().companionMore();
        Menu menu; menu.kind = Menu::Kind::companion;
        menu.x = anchor.right; menu.y = anchor.top; menu.align_right = true;
        menu.items.push_back({Action::companion_change, tr(L"Zmień"), {}, glyph::people, companion::availableLooks().size() > 1});
        menu.items.push_back({Action::companion_mind, tr(L"Umysł i pamięć"), {}, glyph::info, true});
        menu.items.push_back({Action::companion_settings, tr(L"Ustawienia"), {}, glyph::settings, true});
        desktop_menu_ = std::move(menu);
        companion_.link_pointer.reset();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    case Target::card_play:
        desktopFocus(static_cast<size_t>(hit.index), false);
        desktopLaunchSelected();
        return;
    case Target::card_check: desktopToggleMark(static_cast<size_t>(hit.index)); return;
    case Target::sort: desktopOpenSortMenu(); return;
    case Target::installed:
        settings_.desktop_installed = !settings_.desktop_installed;
        settings_dirty_ = true; settings_edited_ = std::chrono::steady_clock::now();
        desktop_scroll_ = desktop_scroll_target_ = 0;
        rebuildCards(true);
        return;
    case Target::pane_primary: desktopLaunchSelected(); return;
    case Target::pane_favorite: if (!cards_.empty()) toggleFavorite(cards_[selected_].game); return;
    case Target::pane_more: {
        const auto r = desktopLayout().paneMore(desktop_pane_scroll_);
        desktopOpenGameMenu(r.right, r.bottom + 6, false);
        if (desktop_menu_) desktop_menu_->align_right = true;
        return;
    }
    case Target::pane_quick: {
        if (cards_.empty()) return;
        const auto game = cards_[selected_].game;
        if (hit.index == 0) openDetails(game);
        else if (hit.index == 1) openArtworkBrowser(game);
        else if (hit.index == 2) {
            if (const auto folder = gameFolder(game); !folder.empty())
                if (execute_(hwnd_, GameAction{folder.wstring(), {}, {}, ActionKind::open_client}) <= 32)
                    showNotification(NotificationKind::error, tr(L"Nie można otworzyć folderu"), folder.wstring());
        } else if (hit.index == 3 && !hideableCopies(game).empty()) toggleHidden(game);
        return;
    }
    case Target::pane_chip: {
        if (cards_.empty()) return;
        std::vector<std::string> ids;
        for (const auto& c : settings_collections_) {
            if (favoriteCollection(c) || (settings_.show_emulation && srmCollection(c))) continue;
            if (desktopMember(cards_[selected_].game, c)) ids.push_back("collection:" + c.id);
        }
        if (hit.index >= 0 && hit.index < static_cast<int>(ids.size())) desktopShowSection(ids[static_cast<size_t>(hit.index)]);
        return;
    }
    case Target::pane_chip_add: {
        const auto r = renderer_.desktopChipAdd();
        desktopOpenCollectionsMenu(r.left, r.bottom + 6);
        return;
    }
    case Target::pane_rating: openRatingSource(); return;
    case Target::pane_studio: { // Developer or Wydawca: that studio's games.
        if (cards_.empty()) return;
        bool loading = false;
        const auto& details = desktopDetails(cards_[selected_].game, loading);
        const auto& value = hit.index == 0 ? details.developer : details.publisher;
        desktopFilterStudio(value.substr(0, value.find(L", ")), hit.index == 0 ? studios::Role::developer : studios::Role::publisher);
        return;
    }
    case Target::pane_achievements: // Szczegóły with its achievements list open (Wstecz: the details).
        if (cards_.empty()) return;
        openDetails(cards_[selected_].game);
        openAchievements();
        return;
    case Target::pane_cloud: // Zarządzaj · Aurelia on its Zapisy w chmurze page.
        if (cards_.empty() || !aureliaCloudFor(cards_[selected_].game)) return;
        { const auto game = cards_[selected_].game; openAurelia(&game); aureliaPage(aurelia_ui::Page::cloud); }
        return;
    case Target::bulk: {
        const auto games = desktopTargets();
        const auto r = desktopLayout().paneBulk(hit.index);
        if (hit.index == 0) desktopOpenCollectionsMenu(r.left, r.bottom + 6);
        else if (hit.index == 1) {
            desktopSetMember(games, desktopFavorites(), true);
            showNotification(NotificationKind::launch, tr(L"Dodano do ulubionych"), gamesLabel(games.size()));
        } else if (hit.index == 2) {
            for (const auto& game : games) retryGameArtwork(game, &game == &games.front());
        } else if (hit.index == 3) {
            for (const auto& game : games) if (!hideableCopies(game).empty() && !hiddenInLauncher(game)) toggleHidden(game);
            desktop_marked_.clear();
        } else { desktop_marked_.clear(); InvalidateRect(hwnd_, nullptr, FALSE); }
        return;
    }
    case Target::menu_item: case Target::submenu_item: {
        if (!desktop_menu_ || hit.index < 0) return;
        const auto& list = hit.target == Target::submenu_item ? desktop_menu_->sub : desktop_menu_->items;
        if (hit.index >= static_cast<int>(list.size())) return;
        const auto item = list[static_cast<size_t>(hit.index)];
        if (!item.enabled || item.separator) return;
        if (item.submenu) { desktopOpenSubmenu(hit.index); return; }
        desktopMenuPick(item, hit.target == Target::submenu_item);
        return;
    }
    case Target::popover_row: {
        if (hit.index < 0 || hit.index >= static_cast<int>(desktop_download_games_.size())) return;
        const auto key = gameKey(desktop_download_games_[static_cast<size_t>(hit.index)]);
        desktop_downloads_open_ = false;
        for (size_t i = 0; i < cards_.size(); ++i)
            if (gameKey(cards_[i].game) == key) { desktopFocus(i); return; }
        openDownloads();
        return;
    }
    case Target::popover_all: desktop_downloads_open_ = false; openDownloads(); return;
    case Target::popover_clear: // What is not moving goes until its state changes (as on the Pobierania page).
        for (size_t i = 0; i < desktop_download_rows_.size() && i < desktop_download_games_.size(); ++i)
            if (!desktop_download_rows_[i].moving) desktop_cleared_downloads_[gameKey(desktop_download_games_[i])] = desktop_download_rows_[i].status;
        desktopRefreshDownloads();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    case Target::popover_action:
        if (hit.index < 0 || hit.index >= static_cast<int>(desktop_download_games_.size()) || hit.index >= static_cast<int>(desktop_download_rows_.size())) return;
        desktop_downloads_open_ = false;
        openDownloadSource(desktop_download_games_[static_cast<size_t>(hit.index)], desktop_download_rows_[static_cast<size_t>(hit.index)]);
        return;
    case Target::widget_friend: {
        if (hit.index < 0 || static_cast<size_t>(hit.index) >= desktop_widget_friend_ids_.size()) return;
        const auto id = desktop_widget_friend_ids_[static_cast<size_t>(hit.index)];
        // With Aurelia the conversation opens in the drawer, as from its friends list; the Steam client stays closed.
        if (aurelia_active_) {
            const auto found = std::find_if(friends_.friends.begin(), friends_.friends.end(), [&](const auto& f) { return f.id == id; });
            if (found == friends_.friends.end()) return;
            const auto cursor = static_cast<size_t>(found - friends_.friends.begin());
            desktop_widgets_open_ = false;
            if (!friends_open_) toggleFriendsDrawer();
            if (!friends_open_) return;
            friends_list_ = true; friends_back_ = false; friends_cursor_ = cursor;
            friendsChat();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        if (execute_(hwnd_, GameAction{L"steam://friends/message/" + std::to_wstring(id)}) <= 32)
            showNotification(NotificationKind::error, tr(L"Nie można otworzyć czatu"), tr(L"Uruchom klienta Steam"));
        return;
    }
    case Target::widget_item: return;
    case Target::widget_all: desktop_widgets_open_ = false; toggleFriendsDrawer(); return; // The full drawer.
    case Target::empty_primary: case Target::empty_secondary: {
        const bool primary = hit.target == Target::empty_primary;
        const auto* entry = desktopEntry();
        if (!trimmedQuery(desktop_query_).empty()) {
            if (primary) { desktop_query_.clear(); desktop_caret_ = 0; desktopApplyQuery(); }
            else { const auto query = desktop_query_; desktopShowSection("all"); desktop_query_ = query; desktopApplyQuery(); }
        } else if (settings_.desktop_installed && !desktopSource().empty()) {
            settings_.desktop_installed = false; settings_dirty_ = true; settings_edited_ = std::chrono::steady_clock::now();
            rebuildCards(true);
        } else if (entry && entry->kind == EntryKind::collection) desktopCollectionDialog(entry->collection, Page::games);
        else if (public_library_.empty()) {
            if (primary) openCustomEditor({});
            else { settings_state_.category = static_cast<int>(settings_ui::Category::library); openSettings(); }
        }
        return;
    }
    default: return;
    }
}

void Launcher::desktopToolPick(desktop_ui::Tool tool) {
    switch (tool) {
    case Tool::glance: openGlance(); return;
    case Tool::tarot: openTarot(); return;
    case Tool::companion: openCompanion(); return;
    case Tool::store:
        if (plugin_menu_.size() == 1) pluginAction(plugin_menu_.front());
        else desktopOpenStoreMenu();
        return;
    case Tool::friends: toggleFriendsDrawer(); return; // The widgets themselves, as Tryb TV's drawer.
    case Tool::downloads: desktopToggleDownloads(); return;
    case Tool::tv: setDesktopMode(false); return;
    case Tool::settings: openSettings(); return;
    case Tool::update: updateAction(); return; // Downloads, installs and restarts (or says why not now).
    }
}

// Widżety under their shortcut: the friends list is fetched again on each opening.
void Launcher::desktopToggleWidgets() {
    desktop_widgets_open_ = !desktop_widgets_open_;
    if (desktop_widgets_open_) { desktop_downloads_open_ = false; loadFriends(); }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::desktopToggleDownloads() {
    desktop_widgets_open_ = false;
    desktop_downloads_open_ = !desktop_downloads_open_;
    if (desktop_downloads_open_) {
        plugin_download_poll_at_ = 0; pollPluginDownloads();
        desktopRefreshDownloads();
        desktop_downloads_at_ = GetTickCount64();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::desktopLaunchSelected() {
    if (cards_.empty() || pending_launch_) return;
    desktop_marked_.clear();
    launch();
}

// A pick of the focused card (Enter, A, double click, the search's one result): the default game
// action, its details or launch/install. The pane's Play button and the menu's Graj launch.
void Launcher::desktopPickSelected() {
    if (cards_.empty() || pending_launch_ || selected_ >= cards_.size()) return;
    const auto game = cards_[selected_].game;
    if (settings_.accept_action != AppSettings::AcceptAction::details || !isGame(game) || game.kind == GameKind::catalog) {
        desktopLaunchSelected();
        return;
    }
    desktop_marked_.clear();
    openDetails(game);
}

void Launcher::desktopSetPoster(float width) {
    const int next = std::clamp(static_cast<int>(std::lround(width)), static_cast<int>(poster_min), static_cast<int>(poster_max));
    if (next == settings_.desktop_poster) return;
    settings_.desktop_poster = next;
    settings_dirty_ = true; settings_edited_ = std::chrono::steady_clock::now();
    const auto L = desktopLayout();
    // The selected game stays in view while the grid reflows.
    desktop_scroll_target_ = desktop_scroll_ = std::clamp(desktop_scroll_target_, 0.0f, L.maxScroll(cards_.size()));
    if (!cards_.empty()) desktop_scroll_target_ = desktop_scroll_ = L.reveal(selected_, cards_.size(), desktop_scroll_target_);
    desktopRequestArt();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// The pane's width (dragged by its edge; 0 automatic): the grid reflows around the selected game.
void Launcher::desktopSetPane(int width) {
    auto L = desktopLayout();
    L.pane_width = static_cast<float>(width);
    const int next = width <= 0 ? 0 : static_cast<int>(L.paneWidth()); // As far as the window allows.
    if (next == settings_.desktop_pane) return;
    settings_.desktop_pane = next;
    desktopReflow();
}

void Launcher::desktopToggleSidebar() {
    settings_.desktop_narrow = !settings_.desktop_narrow;
    settings_dirty_ = true; settings_edited_ = std::chrono::steady_clock::now();
    desktop_active_y_valid_ = false; // Its rows change: the active bar snaps to its new place.
    desktop_sidebar_scroll_ = std::clamp(desktop_sidebar_scroll_, 0.0f, desktopLayout().sidebarMaxScroll(desktopRows()));
    desktop_sidebar_focus_ = -1;
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::desktopReflow() {
    const auto L = desktopLayout();
    desktop_scroll_target_ = desktop_scroll_ = std::clamp(desktop_scroll_target_, 0.0f, L.maxScroll(cards_.size()));
    if (!cards_.empty()) desktop_scroll_target_ = desktop_scroll_ = L.reveal(selected_, cards_.size(), desktop_scroll_target_);
    desktop_columns_ = L.columns();
    desktopRequestArt();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// ---- Selection -------------------------------------------------------------------------------

void Launcher::desktopToggleMark(size_t index) {
    if (index >= desktop_keys_.size()) return;
    // The first Ctrl+click keeps the game already selected, as in Explorer.
    if (desktop_marked_.empty() && selected_ < desktop_keys_.size() && selected_ != index) {
        desktop_marked_.insert(desktop_keys_[selected_]);
        desktop_mark_pulse_[desktop_keys_[selected_]] = 1.0f;
    }
    const auto& key = desktop_keys_[index];
    if (!desktop_marked_.erase(key)) desktop_marked_.insert(key);
    desktop_mark_pulse_[key] = 1.0f;
    desktop_anchor_ = index;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::desktopMarkRange(size_t from, size_t to) {
    if (desktop_keys_.empty()) return;
    from = std::min(from, desktop_keys_.size() - 1); to = std::min(to, desktop_keys_.size() - 1);
    desktop_marked_.clear();
    for (size_t i = std::min(from, to); i <= std::max(from, to); ++i) {
        desktop_marked_.insert(desktop_keys_[i]);
        desktop_mark_pulse_[desktop_keys_[i]] = 1.0f;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

std::vector<Game> Launcher::desktopGamesOf(const std::vector<std::string>& keys) const {
    std::vector<Game> games;
    const std::unordered_set<std::string> wanted(keys.begin(), keys.end());
    for (size_t i = 0; i < cards_.size() && i < desktop_keys_.size(); ++i) if (wanted.contains(desktop_keys_[i])) games.push_back(cards_[i].game);
    return games;
}

// The marked games (in the grid's order) when the selected one is among them, else the selected one.
std::vector<Game> Launcher::desktopTargets() const {
    if (cards_.empty()) return {};
    const bool selected_marked = selected_ < desktop_keys_.size() && desktop_marked_.contains(desktop_keys_[selected_]);
    if (desktop_marked_.size() > 1 || (selected_marked && !desktop_marked_.empty())) {
        auto games = desktopGamesOf(std::vector<std::string>(desktop_marked_.begin(), desktop_marked_.end()));
        if (!games.empty()) return games;
    }
    return {cards_[selected_].game};
}

// ---- Keys ------------------------------------------------------------------------------------

bool Launcher::desktopKey(WPARAM key, bool repeated) {
    if (desktopTextKey(key)) return true; // Selection, words and the clipboard in the focused field.
    if (desktop_mode_ && screen_ == ScreenMode::store && topModal() == Modal::none && !friends_open_ && !keyboard_.open && !launchAnimationActive() && !down(VK_MENU))
        return desktopStoreKey(key);
    if (desktop_meta_.open) {
        if (desktopActive() && topModal() == Modal::none && !friends_open_) return desktopMetaKey(key);
        desktopMetaClose();
    }
    if (desktop_quick_.open) {
        if (desktopActive() && topModal() == Modal::none && !friends_open_) return desktopQuickKey(key);
        desktopQuickClose();
    }
    if (!desktopActive() || topModal() != Modal::none || friends_open_ || keyboard_.open || launchAnimationActive()) return false;
    const bool control = down(VK_CONTROL), shift = down(VK_SHIFT);
    if (down(VK_MENU)) return false;
    if (control && key == 'K') { if (!repeated) desktopQuickOpen(); return true; } // Quick search over the library.
    if (pending_launch_) return key != VK_F8;
    if (desktop_menu_) { desktopMenuKey(key); return true; }
    if (desktop_downloads_open_ && (key == VK_ESCAPE || key == VK_BACK)) { desktop_downloads_open_ = false; InvalidateRect(hwnd_, nullptr, FALSE); return true; }
    if (desktop_widgets_open_ && (key == VK_ESCAPE || key == VK_BACK)) { desktop_widgets_open_ = false; InvalidateRect(hwnd_, nullptr, FALSE); return true; }
    if (key == VK_F11) { if (!repeated) toggleMaximized(); return true; }
    if (key == VK_F5) {
        if (!repeated) {
            desktopRefreshLibraries();
        }
        return true;
    }
    if (control && key == 'F') { desktopSearchFocus(true); return true; }
    if (control && key == 'H') { if (!repeated) togglePrivateCategory(); return true; }
    if (control && (key == VK_OEM_PLUS || key == VK_ADD)) { desktopSetPoster(static_cast<float>(settings_.desktop_poster) + 16); return true; }
    if (control && (key == VK_OEM_MINUS || key == VK_SUBTRACT)) { desktopSetPoster(static_cast<float>(settings_.desktop_poster) - 16); return true; }
    if (control && (key == '0' || key == VK_NUMPAD0)) { desktopSetPoster(poster_default); return true; }
    if (desktop_search_focus_) {
        auto& query = desktop_query_;
        auto& caret = desktop_caret_;
        caret = std::min(caret, query.size());
        const auto edited = [&] { desktop_query_dirty_ = true; desktop_query_edited_ = std::chrono::steady_clock::now(); InvalidateRect(hwnd_, nullptr, FALSE); };
        switch (key) {
        case VK_ESCAPE:
            if (!query.empty()) { query.clear(); caret = 0; desktopApplyQuery(); }
            else desktopSearchFocus(false);
            return true;
        case VK_RETURN: case VK_DOWN: case VK_TAB:
            desktopSearchFocus(false);
            desktop_zone_ = 1; desktop_keyboard_ = true;
            if (desktop_query_dirty_) desktopApplyQuery();
            if (!cards_.empty()) desktopFocus(0);
            if (key == VK_RETURN && cards_.size() == 1) desktopPickSelected(); // One result: Enter picks it.
            return true;
        case VK_LEFT: if (caret) --caret; InvalidateRect(hwnd_, nullptr, FALSE); return true;
        case VK_RIGHT: if (caret < query.size()) ++caret; InvalidateRect(hwnd_, nullptr, FALSE); return true;
        case VK_HOME: caret = 0; InvalidateRect(hwnd_, nullptr, FALSE); return true;
        case VK_END: caret = query.size(); InvalidateRect(hwnd_, nullptr, FALSE); return true;
        case VK_BACK:
            if (!caret) return true;
            if (control) { // The word before the caret.
                size_t start = caret;
                while (start && query[start - 1] == L' ') --start;
                while (start && query[start - 1] != L' ') --start;
                query.erase(start, caret - start); caret = start;
            } else { query.erase(caret - 1, 1); --caret; }
            edited();
            return true;
        case VK_DELETE: if (caret < query.size()) { query.erase(caret, 1); edited(); } return true;
        case 'V':
            if (control && OpenClipboard(hwnd_)) {
                if (const auto handle = GetClipboardData(CF_UNICODETEXT)) {
                    if (const auto* text = static_cast<const wchar_t*>(GlobalLock(handle))) {
                        const size_t limit = std::min<size_t>(GlobalSize(handle) / sizeof(wchar_t), 80);
                        for (size_t i = 0; i < limit && text[i] && query.size() < 80; ++i)
                            if (text[i] >= L' ') query.insert(caret++, 1, text[i]);
                        GlobalUnlock(handle);
                    }
                }
                CloseClipboard();
                edited();
            }
            return true;
        default: return !control; // WM_CHAR types the rest.
        }
    }
    desktop_keyboard_ = true;
    switch (key) {
    case VK_LEFT: desktopMove(-1, 0, shift); return true;
    case VK_RIGHT: desktopMove(1, 0, shift); return true;
    case VK_UP: desktopMove(0, -1, shift); return true;
    case VK_DOWN: desktopMove(0, 1, shift); return true;
    case VK_HOME: case VK_END:
        if (desktop_zone_ == 1 && !cards_.empty()) {
            const size_t target = key == VK_HOME ? 0 : cards_.size() - 1;
            if (shift) desktopMarkRange(desktop_anchor_, target); else { desktop_marked_.clear(); desktop_anchor_ = target; }
            desktopFocus(target);
        }
        return true;
    case VK_PRIOR: case VK_NEXT:
        if (desktop_zone_ == 1 && !cards_.empty()) {
            const auto L = desktopLayout();
            const int rows = std::max(1, static_cast<int>(L.viewHeight() / L.rowStep()));
            desktopMove(0, key == VK_PRIOR ? -rows : rows, shift);
        }
        return true;
    case VK_RETURN: if (!repeated) desktopAccept(); return true;
    case VK_SPACE:
        if (!repeated) {
            if (control && desktop_zone_ == 1 && !cards_.empty()) desktopToggleMark(selected_);
            else desktopAccept();
        }
        return true;
    case VK_APPS: case VK_F10:
        if (key == VK_F10 && !shift) return false;
        if (!cards_.empty()) {
            const auto r = desktopLayout().poster(selected_, desktop_scroll_);
            desktopOpenGameMenu(r.left + 16, r.top + 16, true);
        }
        return true;
    case VK_ESCAPE: case VK_BACK: desktopBack(); return true;
    case VK_TAB: {
        // Sidebar → grid → pane → search, Shift backwards.
        constexpr int order[] = {0, 1, 2, 3};
        int at = desktop_zone_;
        at = (at + (shift ? 3 : 1)) % 4;
        if (order[at] == 3) desktopSearchFocus(true);
        else {
            desktop_zone_ = order[at];
            if (desktop_zone_ == 0) {
                const auto rows = desktopRows();
                for (int i = 0; i < static_cast<int>(rows.size()); ++i)
                    if (!rows[static_cast<size_t>(i)].heading && rows[static_cast<size_t>(i)].entry == desktopEntryIndex(desktop_section_)) desktop_sidebar_focus_ = i;
            }
            if (desktop_zone_ == 2) desktop_pane_focus_ = 0;
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    case VK_F2: {
        const auto* entry = desktopEntry();
        if (entry && entry->kind == EntryKind::collection) desktopCollectionDialog(entry->collection, Page::name);
        return true;
    }
    case 'A':
        if (control) { desktopMarkRange(0, cards_.empty() ? 0 : cards_.size() - 1); return true; }
        break;
    default: break;
    }
    if (control) return false;
    // Letters and digits: WM_CHAR starts the search with them.
    return (key >= '0' && key <= '9') || (key >= 'A' && key <= 'Z') || (key >= VK_OEM_1 && key <= VK_OEM_8) || key == VK_OEM_102 ||
           (key >= VK_NUMPAD0 && key <= VK_DIVIDE);
}

void Launcher::desktopMove(int dx, int dy, bool extend) {
    if (!desktopActive()) return;
    desktop_keyboard_ = true;
    if (desktop_menu_) { desktopMenuKey(dy < 0 ? VK_UP : dy > 0 ? VK_DOWN : dx < 0 ? VK_LEFT : VK_RIGHT); return; }
    const auto L = desktopLayout();
    if (desktop_zone_ == 3) { if (dy > 0) { desktopSearchFocus(false); desktop_zone_ = 1; if (!cards_.empty()) desktopFocus(selected_); } return; }
    if (desktop_zone_ == 0) {
        const auto rows = desktopRows();
        if (rows.empty()) return;
        if (dx > 0) { desktop_zone_ = 1; if (!cards_.empty()) desktopFocus(selected_); InvalidateRect(hwnd_, nullptr, FALSE); return; }
        if (dx < 0) {
            // Left on a heading folds its group, as in a tree.
            if (desktop_sidebar_focus_ >= 0 && desktop_sidebar_focus_ < static_cast<int>(rows.size()) && rows[static_cast<size_t>(desktop_sidebar_focus_)].heading) {
                desktop_collapsed_[static_cast<size_t>(rows[static_cast<size_t>(desktop_sidebar_focus_)].group)] = true;
                desktopSaveState();
            } else navigationBoundary();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        const int current = std::clamp(desktop_sidebar_focus_, 0, static_cast<int>(rows.size()) - 1);
        const int next = std::clamp(current + dy, 0, static_cast<int>(rows.size()) - 1);
        if (next == current && desktop_sidebar_focus_ >= 0) { navigationBoundary(); return; }
        desktop_sidebar_focus_ = next;
        const auto& row = rows[static_cast<size_t>(next)];
        if (!row.heading) desktopShowSection(desktop_entries_[static_cast<size_t>(row.entry)].id);
        // Keep the row in view.
        const auto r = L.sidebarRow(row, desktop_sidebar_scroll_);
        if (r.top < L.sidebarTop()) desktop_sidebar_scroll_ -= L.sidebarTop() - r.top;
        else if (r.bottom > L.sidebarBottom()) desktop_sidebar_scroll_ += r.bottom - L.sidebarBottom();
        desktop_sidebar_scroll_ = std::clamp(desktop_sidebar_scroll_, 0.0f, L.sidebarMaxScroll(rows));
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (desktop_zone_ == 2) {
        const bool bulk = desktopTargets().size() > 1;
        int focus = desktop_pane_focus_;
        if (bulk) focus = std::clamp(focus + dy + dx, 0, Layout::bulk_count - 1);
        else if (dy) focus = dy > 0 ? (focus < 3 ? 3 + std::min(focus, 2) : focus) : (focus >= 3 ? std::min(focus - 3, 2) : focus);
        else if (dx < 0 && (focus == 0 || focus == 3)) { desktop_zone_ = 1; InvalidateRect(hwnd_, nullptr, FALSE); return; }
        else focus = std::clamp(focus + dx, focus >= 3 ? 3 : 0, focus >= 3 ? 3 + Layout::quick_count - 1 : 2);
        if (focus == desktop_pane_focus_) navigationBoundary();
        desktop_pane_focus_ = focus;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    // The grid.
    if (cards_.empty()) {
        if (dx < 0) desktop_zone_ = 0;
        else if (dy < 0) desktopSearchFocus(true);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const int columns = L.columns();
    const int count = static_cast<int>(cards_.size()), current = static_cast<int>(selected_);
    const int column = current % columns;
    if (dx < 0 && column == 0 && !extend) {
        desktop_zone_ = 0;
        const auto rows = desktopRows();
        for (int i = 0; i < static_cast<int>(rows.size()); ++i)
            if (!rows[static_cast<size_t>(i)].heading && rows[static_cast<size_t>(i)].entry == desktopEntryIndex(desktop_section_)) desktop_sidebar_focus_ = i;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (dx > 0 && (column == columns - 1 || current + 1 >= count) && !extend) { desktop_zone_ = 2; desktop_pane_focus_ = 0; InvalidateRect(hwnd_, nullptr, FALSE); return; }
    if (dy < 0 && current < columns && !extend) { desktopSearchFocus(true); return; }
    const int next = std::clamp(current + dx + dy * columns, 0, count - 1);
    if (next == current) { navigationBoundary(); return; }
    if (extend) desktopMarkRange(desktop_anchor_, static_cast<size_t>(next));
    else { desktop_marked_.clear(); desktop_anchor_ = static_cast<size_t>(next); }
    desktopFocus(static_cast<size_t>(next));
}

void Launcher::desktopAccept() {
    if (desktop_menu_) { desktopMenuKey(VK_RETURN); return; }
    switch (desktop_zone_) {
    case 0: {
        const auto rows = desktopRows();
        if (desktop_sidebar_focus_ < 0 || desktop_sidebar_focus_ >= static_cast<int>(rows.size())) { desktop_zone_ = 1; break; }
        const auto& row = rows[static_cast<size_t>(desktop_sidebar_focus_)];
        if (row.heading) desktopActivate({Target::sidebar_row, desktop_sidebar_focus_});
        else desktop_zone_ = 1;
        break;
    }
    case 2: {
        if (desktopTargets().size() > 1) { desktopActivate({Target::bulk, desktop_pane_focus_}); break; }
        const int focus = desktop_pane_focus_;
        if (focus == 0) desktopActivate({Target::pane_primary, 0});
        else if (focus == 1) desktopActivate({Target::pane_favorite, 0});
        else if (focus == 2) desktopActivate({Target::pane_more, 0});
        else desktopActivate({Target::pane_quick, focus - 3});
        break;
    }
    case 3: desktopKey(VK_RETURN, false); break;
    default: desktopPickSelected(); break;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::desktopBack() {
    if (desktop_menu_) { desktopCloseMenu(); return; }
    if (desktop_downloads_open_) desktop_downloads_open_ = false;
    else if (desktop_drag_.kind != DesktopDrag::Kind::none) { desktop_drag_ = {}; if (GetCapture() == hwnd_) ReleaseCapture(); }
    else if (desktop_search_focus_ || !desktop_query_.empty()) {
        if (!desktop_query_.empty()) { desktop_query_.clear(); desktop_caret_ = 0; desktopApplyQuery(); }
        desktopSearchFocus(false);
    } else if (!desktop_marked_.empty()) desktop_marked_.clear();
    else if (desktop_zone_ != 1) desktop_zone_ = 1;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool Launcher::desktopCharacter(wchar_t character) {
    if (!desktopActive() || friends_open_ || desktop_menu_ || topDialog() != Modal::none) return false;
    if (character < L' ' || character == 127) return desktop_search_focus_;
    if (!desktop_search_focus_) {
        if (character == L' ') return true; // Space plays the game (WM_KEYDOWN); it starts no search.
        desktopSearchFocus(true);
        desktop_query_.clear(); desktop_caret_ = 0; // Typing over the grid starts a new search.
    }
    if (desktop_query_.size() >= 80) return true;
    desktop_caret_ = std::min(desktop_caret_, desktop_query_.size());
    desktop_query_.insert(desktop_caret_++, 1, character);
    desktop_query_dirty_ = true;
    desktop_query_edited_ = std::chrono::steady_clock::now();
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

void Launcher::desktopSearchFocus(bool focus) {
    desktop_search_focus_ = focus;
    if (focus) { desktop_zone_ = 3; desktop_caret_ = desktop_query_.size(); desktop_downloads_open_ = false; }
    else if (desktop_zone_ == 3) desktop_zone_ = 1;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::desktopApplyQuery() {
    desktop_query_dirty_ = false;
    desktop_marked_.clear();
    desktop_scroll_ = desktop_scroll_target_ = 0;
    desktop_focus_key_.clear();
    selected_ = 0;
    if (desktopActive()) rebuildCards(true);
}

// ---- Menus -----------------------------------------------------------------------------------

std::vector<desktop_ui::MenuItem> Launcher::desktopCollectionItems(const std::vector<Game>& games) const {
    std::vector<MenuItem> items;
    for (const auto& c : settings_collections_) {
        if (favoriteCollection(c) || (settings_.show_emulation && srmCollection(c))) continue;
        MenuItem item{Action::collection_toggle, c.name, {}, glyph::folder};
        item.key = c.id;
        item.checked = !games.empty() && std::all_of(games.begin(), games.end(), [&](const Game& game) { return desktopMember(game, c); });
        items.push_back(std::move(item));
    }
    if (!items.empty()) { MenuItem separator; separator.separator = true; items.push_back(separator); }
    items.push_back({Action::collection_new, tr(L"Nowa kolekcja…"), {}, glyph::add});
    items.push_back({Action::collection_manage, tr(L"Zarządzaj kolekcjami…"), {}, glyph::edit});
    return items;
}

void Launcher::desktopOpenGameMenu(float x, float y, bool keyboard) {
    const auto games = desktopTargets();
    if (games.empty()) return;
    desktop_downloads_open_ = false;
    Menu menu;
    menu.kind = Menu::Kind::game;
    menu.x = x; menu.y = y; menu.keyboard = keyboard;
    for (const auto& game : games) menu.games.push_back(library_view::selectionKey(game));
    auto& items = menu.items;
    const auto separator = [&] { if (!items.empty() && !items.back().separator) { MenuItem line; line.separator = true; items.push_back(line); } };
    if (games.size() == 1) {
        const auto& game = games.front();
        // "Enter" beside what Enter does on the card: launch, or its details (the default game action).
        const bool enter_details = settings_.accept_action == AppSettings::AcceptAction::details;
        MenuItem play{Action::play, actionLabel(game), enter_details ? std::wstring{} : std::wstring(tr(L"Enter")), glyph::play};
        play.enabled = actionEnabled(game) && !pending_launch_;
        items.push_back(std::move(play));
        const auto session = sessionState(game);
        if (settings_.quick_resume && session == SessionState::running) items.push_back({Action::suspend, tr(L"Wstrzymaj"), {}, glyph::pause});
        if (session != SessionState::none) items.push_back({Action::close_game, tr(L"Zamknij grę"), {}, glyph::stop});
        separator();
        const bool favorite = isFavorite(game);
        items.push_back({Action::favorite, favorite ? tr(L"Usuń z ulubionych") : tr(L"Dodaj do ulubionych"), {}, favorite ? glyph::star_fill : glyph::star});
        MenuItem collections{Action::collections, tr(L"Kolekcje"), {}, glyph::folder};
        collections.submenu = true;
        items.push_back(std::move(collections));
        if (const auto* group = versions_.find(game); group && group->size() > 1) items.push_back({Action::versions, tr(L"Wybierz wersję…"), {}, glyph::sync});
        separator();
        items.push_back({Action::details, tr(L"Szczegóły"), enter_details ? std::wstring(tr(L"Enter")) : std::wstring{}, glyph::info});
        items.push_back({Action::artwork, tr(L"Zmień grafikę…"), {}, glyph::picture});
        items.push_back({Action::retry_art, tr(L"Pobierz grafiki ponownie"), {}, glyph::refresh});
        if (!steamArtwork(game) && !gameKey(game).empty()) items.push_back({Action::identify, tr(L"Zidentyfikuj grę…"), {}, glyph::search});
        if (!gameKey(game).empty()) items.push_back({Action::metadata, tr(L"Edytuj metadane…"), {}, glyph::edit}); // Own title and details.
        if (aurelia_active_ && aurelia::validGame(game)) items.push_back({Action::aurelia, tr(L"Zarządzaj · Aurelia"), {}, glyph::edit});
        // The file, options and folder it starts with (Steam: its launch options).
        if (steamAccountChoices(game).size() > 1) items.push_back({Action::steam_account, tr(L"Konto Steam: ") + steamAccountLabel(game), {}, glyph::people});
        if (launchEditable(game)) // A game added by hand: the file, name and options are its own editor.
            items.push_back({Action::launch_options, game.source == GameSource::custom ? tr(L"Edytuj grę…") : tr(L"Opcje uruchamiania…"), {}, glyph::settings});
        if (!gameFolder(game).empty()) items.push_back({Action::folder, tr(L"Otwórz folder gry"), {}, glyph::folder_open});
        const auto plugins = pluginContext(game);
        if (!plugins.empty()) separator();
        for (size_t i = 0; i < plugins.size(); ++i) {
            MenuItem item{Action::plugin, plugins[i].item.title.localized(), {}, glyph::open};
            item.value = static_cast<int>(i);
            items.push_back(std::move(item));
        }
        if (!hideableCopies(game).empty() || canExclude(game)) separator();
        if (!hideableCopies(game).empty()) {
            const bool hidden = hiddenInLauncher(game);
            items.push_back({Action::hide, hidden ? tr(L"Pokaż w bibliotece") : tr(L"Ukryj"), {}, hidden ? glyph::view : glyph::hide});
        }
        if (canExclude(game)) { MenuItem exclude{Action::exclude, tr(L"Wyklucz z biblioteki"), {}, glyph::remove}; exclude.danger = true; items.push_back(std::move(exclude)); }
        if (canUninstall(game) && session == SessionState::none) { // Never while it runs or is suspended.
            if (hideableCopies(game).empty() && !canExclude(game)) separator();
            MenuItem uninstall{Action::uninstall, tr(L"Odinstaluj…"), {}, glyph::remove};
            uninstall.danger = true;
            items.push_back(std::move(uninstall));
        }
        if (canRemoveFromLibrary(game) && session == SessionState::none) { // A game added by hand: its entry, after a question.
            if (hideableCopies(game).empty() && !canExclude(game)) separator();
            MenuItem remove{Action::remove_game, tr(L"Usuń z biblioteki…"), {}, glyph::remove};
            remove.danger = true;
            items.push_back(std::move(remove));
        }
    } else {
        MenuItem title{Action::none, std::wstring(tr(L"Zaznaczone: ")) + gamesLabel(games.size()), {}, glyph::check};
        title.enabled = false;
        items.push_back(std::move(title));
        separator();
        items.push_back({Action::favorite, tr(L"Dodaj do ulubionych"), {}, glyph::star});
        MenuItem collections{Action::collections, tr(L"Kolekcje"), {}, glyph::folder};
        collections.submenu = true;
        items.push_back(std::move(collections));
        // Every selected game's missing or failed artwork asked for again (SteamGridDB, Epic's own art).
        items.push_back({Action::retry_art, tr(L"Pobierz grafiki ponownie"), {}, glyph::refresh});
        items.push_back({Action::missing_art, tr(L"Pobierz brakujące okładki…"), {}, glyph::picture});
        if (std::all_of(games.begin(), games.end(), [&](const Game& game) { return !hideableCopies(game).empty(); }))
            items.push_back({Action::hide, tr(L"Ukryj zaznaczone"), {}, glyph::hide});
        separator();
        items.push_back({Action::clear_marks, tr(L"Wyczyść zaznaczenie"), tr(L"Esc"), glyph::close});
    }
    if (keyboard) menu.hover = stepItem(items, -1, 1);
    desktop_menu_ = std::move(menu);
    desktop_tooltip_ = 0;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::desktopOpenSubmenu(int index) {
    if (!desktop_menu_ || index < 0 || index >= static_cast<int>(desktop_menu_->items.size())) return;
    auto& menu = *desktop_menu_;
    menu.sub_parent = index;
    menu.sub = desktopCollectionItems(desktopGamesOf(menu.games));
    menu.sub_hover = menu.keyboard ? stepItem(menu.sub, -1, 1) : -1;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::desktopOpenSectionMenu(int entry_index, float x, float y) {
    if (entry_index < 0 || entry_index >= static_cast<int>(desktop_entries_.size())) return;
    const auto& entry = desktop_entries_[static_cast<size_t>(entry_index)];
    Menu menu;
    menu.kind = Menu::Kind::section;
    menu.x = x; menu.y = y; menu.entry = entry.id;
    if (entry.kind == EntryKind::favorites) {
        MenuItem games{Action::section_games, tr(L"Wybierz gry…"), {}, glyph::check};
        games.key = desktopFavorites().id;
        menu.items.push_back(std::move(games));
    } else if (entry.kind == EntryKind::collection) {
        const auto* collection = desktopCollection(entry.collection);
        if (!collection) return;
        const auto scope = collectionKey(account_, *collection);
        const auto add = [&](Action action, std::wstring label, wchar_t icon, std::wstring shortcut = {}) -> MenuItem& {
            MenuItem item{action, std::move(label), std::move(shortcut), icon};
            item.key = collection->id;
            menu.items.push_back(std::move(item));
            return menu.items.back();
        };
        add(Action::section_rename, tr(L"Zmień nazwę…"), glyph::edit, L"F2");
        add(Action::section_games, tr(L"Wybierz gry…"), glyph::check);
        add(Action::section_tv, tr(L"Pokaż w trybie TV"), glyph::tv).checked = !settings_.hidden_collections.contains(scope);
        MenuItem line; line.separator = true; menu.items.push_back(line);
        std::vector<std::string> shown;
        for (const auto& item : desktop_entries_) if (item.kind == EntryKind::collection) shown.push_back(item.collection);
        const auto at = std::find(shown.begin(), shown.end(), collection->id);
        add(Action::section_up, tr(L"Przenieś wyżej"), glyph::chevron_down).enabled = at != shown.begin();
        add(Action::section_down, tr(L"Przenieś niżej"), glyph::chevron_down).enabled = at != shown.end() && at + 1 != shown.end();
        menu.items.push_back(line);
        add(Action::section_delete, localCollection(*collection) ? tr(L"Usuń kolekcję…") : tr(L"Przywróć przypisania Steam…"), glyph::remove).danger = true;
    } else if (entry.kind == EntryKind::studio) {
        // A publisher or developer: its place among the others, or hidden (Ustawienia brings it back).
        std::vector<std::string> shown;
        for (const auto& item : desktop_entries_) if (item.kind == EntryKind::studio && item.group == entry.group) shown.push_back(item.id);
        const auto at = std::find(shown.begin(), shown.end(), entry.id);
        const auto add = [&](Action action, std::wstring label, wchar_t icon) -> MenuItem& {
            MenuItem item{action, std::move(label), {}, icon};
            item.key = entry.id;
            menu.items.push_back(std::move(item));
            return menu.items.back();
        };
        add(Action::section_up, tr(L"Przenieś wyżej"), glyph::chevron_down).enabled = at != shown.begin();
        add(Action::section_down, tr(L"Przenieś niżej"), glyph::chevron_down).enabled = at != shown.end() && at + 1 != shown.end();
        MenuItem line; line.separator = true; menu.items.push_back(line);
        add(Action::section_hide, tr(L"Ukryj"), glyph::hide);
    } else return;
    desktop_menu_ = std::move(menu);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::desktopOpenSortMenu() {
    const auto r = desktopLayout().sortButton();
    Menu menu;
    menu.kind = Menu::Kind::sort;
    menu.x = r.left; menu.y = r.bottom + 6;
    using library_view::Order;
    for (const auto order : {Order::alphabetical, Order::last_played, Order::added, Order::release}) {
        MenuItem item{Action::sort, library_view::label(order), {}, 0};
        item.value = library_view::persistedOrder(order);
        item.checked = settings_.desktop_sort == item.value;
        menu.items.push_back(std::move(item));
    }
    desktop_menu_ = std::move(menu);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::desktopOpenCollectionsMenu(float x, float y) {
    const auto games = desktopTargets();
    if (games.empty()) return;
    Menu menu;
    menu.kind = Menu::Kind::collections;
    menu.x = x; menu.y = y;
    for (const auto& game : games) menu.games.push_back(library_view::selectionKey(game));
    menu.items = desktopCollectionItems(games);
    desktop_menu_ = std::move(menu);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::desktopOpenStoreMenu() {
    const auto tools = desktopTools();
    const auto at = std::find(tools.begin(), tools.end(), Tool::store);
    const auto r = desktopLayout().tool(static_cast<int>(at - tools.begin()));
    Menu menu;
    menu.kind = Menu::Kind::store;
    menu.x = r.right; menu.y = r.bottom + 6; menu.align_right = true;
    for (size_t i = 0; i < plugin_menu_.size(); ++i) {
        MenuItem item{Action::store_page, plugin_menu_[i].item.title.localized(), {}, glyph::shop};
        item.value = static_cast<int>(i);
        menu.items.push_back(std::move(item));
    }
    desktop_menu_ = std::move(menu);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::desktopMenuKey(WPARAM key) {
    if (!desktop_menu_) return;
    auto& menu = *desktop_menu_;
    menu.keyboard = true;
    const bool sub = menu.in_sub && menu.sub_parent >= 0;
    auto& items = sub ? menu.sub : menu.items;
    int& hover = sub ? menu.sub_hover : menu.hover;
    switch (key) {
    case VK_UP: hover = stepItem(items, hover, -1); break;
    case VK_DOWN: hover = stepItem(items, hover, 1); break;
    case VK_RIGHT:
        if (!sub && hover >= 0 && menu.items[static_cast<size_t>(hover)].submenu) { desktopOpenSubmenu(hover); menu.in_sub = true; menu.sub_hover = stepItem(menu.sub, -1, 1); }
        break;
    case VK_LEFT: case VK_BACK:
        if (sub) { menu.in_sub = false; menu.sub_parent = -1; menu.sub.clear(); }
        else if (key == VK_BACK) { desktopCloseMenu(); return; }
        break;
    case VK_ESCAPE:
        if (sub) { menu.in_sub = false; menu.sub_parent = -1; menu.sub.clear(); }
        else { desktopCloseMenu(); return; }
        break;
    case VK_RETURN: case VK_SPACE:
        if (hover >= 0 && hover < static_cast<int>(items.size())) {
            const auto item = items[static_cast<size_t>(hover)];
            if (!item.enabled || item.separator) break;
            if (item.submenu && !sub) { desktopOpenSubmenu(hover); menu.in_sub = true; menu.sub_hover = stepItem(menu.sub, -1, 1); break; }
            desktopMenuPick(item, sub);
            return;
        }
        break;
    default: break;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::desktopCloseMenu() {
    desktop_menu_.reset();
    desktop_pressed_ = {};
    mouse_transition_until_ = GetTickCount64() + GetDoubleClickTime();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::desktopMenuPick(const desktop_ui::MenuItem& item, bool /*from_sub*/) {
    if (!desktop_menu_) return;
    const auto games = desktopGamesOf(desktop_menu_->games);
    const auto entry = desktop_menu_->entry;
    desktopCloseMenu();
    const Game* game = games.empty() ? nullptr : &games.front();
    switch (item.action) {
    case Action::companion_change: companionPicker(true); return;
    case Action::companion_mind:
        openCompanion();
        if (companionOpen()) {
            companionMind(true);
            companion_.chat.mind_from_sidebar = true;
        }
        return;
    case Action::companion_settings: openCompanionSettings(); return;
    case Action::play: desktopLaunchSelected(); return;
    case Action::suspend: if (game) freezeGame(gameKey(*game), true); return;
    case Action::close_game: if (game) closeGame(*game); return;
    case Action::favorite:
        if (games.size() == 1) toggleFavorite(*game);
        else if (!games.empty()) {
            desktopSetMember(games, desktopFavorites(), true);
            showNotification(NotificationKind::launch, tr(L"Dodano do ulubionych"), gamesLabel(games.size()));
        }
        return;
    case Action::collection_toggle: {
        const auto* collection = desktopCollection(item.key);
        if (!collection || games.empty()) return;
        const auto c = *collection;
        desktopSetMember(games, c, !item.checked);
        showNotification(NotificationKind::launch, item.checked ? tr(L"Usunięto z kolekcji") : tr(L"Dodano do kolekcji"),
                         c.name + L" · " + (games.size() == 1 ? games.front().name : gamesLabel(games.size())));
        return;
    }
    case Action::collection_new: desktopNewCollection(games); return;
    case Action::collection_manage:
        if (games.size() == 1) openCollectionManager(games.front()); else openCollectionManager();
        return;
    case Action::versions: if (game) openVersions(*game, false); return;
    case Action::details: if (game) openDetails(*game); return;
    case Action::artwork: if (game) openArtworkBrowser(*game); return;
    case Action::retry_art: for (const auto& each : games) retryGameArtwork(each, &each == &games.front()); return;
    case Action::missing_art: openBulkCovers(games); return; // Pobierz brakujące okładki, for these games only.
    case Action::identify: if (game) openIdentifyFor(*game); return;
    case Action::metadata: if (game) desktopMetaOpen(*game); return;
    case Action::aurelia: if (game) openAurelia(game); return;
    case Action::steam_account: if (game) cycleSteamAccount(*game); return;
    case Action::launch_options: if (game) openLaunchEditor(*game); return;
    case Action::folder:
        if (game) if (const auto folder = gameFolder(*game); !folder.empty())
            if (execute_(hwnd_, GameAction{folder.wstring(), {}, {}, ActionKind::open_client}) <= 32)
                showNotification(NotificationKind::error, tr(L"Nie można otworzyć folderu"), folder.wstring());
        return;
    case Action::plugin:
        if (game) {
            const auto actions = pluginContext(*game);
            if (item.value >= 0 && item.value < static_cast<int>(actions.size())) pluginAction(actions[static_cast<size_t>(item.value)], *game);
        }
        return;
    case Action::hide:
        if (games.size() == 1) toggleHidden(*game);
        else for (const auto& each : games) if (!hiddenInLauncher(each)) toggleHidden(each);
        desktop_marked_.clear();
        return;
    case Action::exclude: if (game) excludeGame(*game); return;
    case Action::uninstall: if (game) uninstallGame(*game); return;
    case Action::remove_game: if (game) askRemoveFromLibrary(*game); return;
    case Action::sort:
        settings_.desktop_sort = std::clamp(item.value, 1, 4);
        settings_dirty_ = true; settings_edited_ = std::chrono::steady_clock::now();
        desktop_scroll_ = desktop_scroll_target_ = 0;
        rebuildCards(true);
        if (!cards_.empty()) desktopFocus(selected_);
        return;
    case Action::clear_marks: desktop_marked_.clear(); InvalidateRect(hwnd_, nullptr, FALSE); return;
    case Action::store_page:
        if (item.value >= 0 && item.value < static_cast<int>(plugin_menu_.size())) pluginAction(plugin_menu_[static_cast<size_t>(item.value)]);
        return;
    case Action::downloads: openDownloads(); return;
    case Action::section_rename: desktopCollectionDialog(item.key, Page::name); return;
    case Action::section_games: desktopCollectionDialog(item.key, Page::games); return;
    case Action::section_options: desktopCollectionDialog(item.key, Page::collection); return;
    case Action::section_delete: desktopCollectionDialog(item.key, Page::collection, true); return;
    case Action::section_up: if (studios::parse(item.key)) moveStudio(item.key, -1); else desktopMoveCollection(item.key, -1); return;
    case Action::section_down: if (studios::parse(item.key)) moveStudio(item.key, 1); else desktopMoveCollection(item.key, 1); return;
    case Action::section_hide: hideStudio(item.key); return;
    case Action::section_tv: {
        const auto* collection = desktopCollection(item.key);
        if (!collection) return;
        const auto scope = collectionKey(account_, *collection);
        if (!settings_.hidden_collections.erase(scope)) settings_.hidden_collections.insert(scope);
        settingsChanged(true);
        return;
    }
    default: (void)entry; return;
    }
}

// ---- Collections -----------------------------------------------------------------------------

void Launcher::desktopSetMember(const std::vector<Game>& games, const SteamCollection& collection, bool member) {
    if (!library_state_readable_) { showNotification(NotificationKind::error, tr(L"Nie można odczytać historii biblioteki"), L""); return; }
    for (const auto& game : games) {
        if (!game.version_group.empty()) if (const auto* copies = versions_.find(game)) {
            for (const auto& copy : *copies) setCollectionMember(library_state_.collections, account_, collection, copy, member);
            continue;
        }
        setCollectionMember(library_state_.collections, account_, collection, game, member);
    }
    collectionChanged();
}

void Launcher::desktopDrop(const desktop_ui::Entry& entry, const std::vector<std::string>& keys) {
    const auto games = desktopGamesOf(keys);
    if (games.empty() || !entry.droppable) return;
    const std::string target_id = entry.id;
    const auto subtitle = games.size() == 1 ? games.front().name : gamesLabel(games.size());
    if (entry.kind == EntryKind::favorites) {
        desktopSetMember(games, desktopFavorites(), true);
        desktop_drop_flash_id_ = target_id;
        desktop_drop_flash_ = 1.0f;
        showNotification(NotificationKind::launch, tr(L"Dodano do ulubionych"), subtitle);
    } else if (entry.kind == EntryKind::collection) {
        const auto* collection = desktopCollection(entry.collection);
        if (!collection) return;
        const auto c = *collection;
        desktopSetMember(games, c, true);
        desktop_drop_flash_id_ = target_id;
        desktop_drop_flash_ = 1.0f;
        showNotification(NotificationKind::launch, std::wstring(tr(L"Dodano do kolekcji „")) + c.name + L"”", subtitle);
    }
}

// The collection order of the sidebar (and the TV home): `id` moves before the `before`-th shown
// collection (-1: after the last).
void Launcher::desktopReorder(const std::string& id, int before) {
    std::vector<std::string> shown;
    for (const auto& entry : desktop_entries_) if (entry.kind == EntryKind::collection) shown.push_back(entry.collection);
    const auto from = std::find(shown.begin(), shown.end(), id);
    if (from == shown.end()) return;
    const std::string before_id = before >= 0 && before < static_cast<int>(shown.size()) ? shown[static_cast<size_t>(before)] : std::string{};
    if (before_id == id) return;
    if (before_id.empty() && shown.back() == id) return;
    if (!before_id.empty() && from + 1 != shown.end() && *(from + 1) == before_id) return; // Already there.
    auto order = settings_collections_;
    const auto moved = std::find_if(order.begin(), order.end(), [&](const SteamCollection& c) { return c.id == id; });
    if (moved == order.end()) return;
    const auto item = *moved;
    order.erase(moved);
    auto at = order.end();
    if (!before_id.empty()) at = std::find_if(order.begin(), order.end(), [&](const SteamCollection& c) { return c.id == before_id; });
    else if (const auto last = std::find_if(order.begin(), order.end(), [&](const SteamCollection& c) { return c.id == shown.back(); }); last != order.end()) at = last + 1;
    order.insert(at, item);
    for (const auto& c : order) std::erase(settings_.collection_order, collectionKey(account_, c));
    for (const auto& c : order) settings_.collection_order.push_back(collectionKey(account_, c));
    settingsChanged(true);
}

void Launcher::desktopMoveCollection(const std::string& id, int amount) {
    std::vector<std::string> shown;
    for (const auto& entry : desktop_entries_) if (entry.kind == EntryKind::collection) shown.push_back(entry.collection);
    const auto at = std::find(shown.begin(), shown.end(), id);
    if (at == shown.end()) return;
    const int index = static_cast<int>(at - shown.begin());
    const int before = amount < 0 ? index - 1 : index + 2;
    if (before < 0) return;
    desktopReorder(id, before >= static_cast<int>(shown.size()) ? -1 : before);
}

void Launcher::desktopNewCollection(std::vector<Game> games) {
    if (!library_state_readable_) { showNotification(NotificationKind::error, tr(L"Nie można odczytać historii biblioteki"), L""); return; }
    auto& ui = library_dialog_;
    ui = {};
    ui.open = true; ui.account = account_;
    ui.page = Page::name;
    ui.name_return = games.empty() ? Page::collections : Page::membership;
    if (!games.empty()) {
        ui.game = games.front();
        ui.more_games.assign(games.begin() + 1, games.end());
    }
    ui.steam_collection = account_ != 0 && (games.empty() || std::all_of(games.begin(), games.end(), [](const Game& g) { return g.source == GameSource::steam; }));
    ui.focus = -2;
    ui.close_after_save = true;
    closeFriends();
    rebuildCollectionDialog();
    audio_.drawer();
}

void Launcher::desktopCollectionDialog(const std::string& id, library_manager_ui::Page page, bool confirm) {
    if (!library_state_readable_) { showNotification(NotificationKind::error, tr(L"Nie można odczytać historii biblioteki"), L""); return; }
    const auto* collection = desktopCollection(id);
    if (!collection) return;
    auto& ui = library_dialog_;
    ui = {};
    ui.open = true; ui.account = account_;
    ui.collection = id;
    ui.page = page;
    if (page == Page::name) {
        if (favoriteCollection(*collection)) return; // Ulubione keeps its name.
        ui.input = collection->name; ui.selected = true; ui.focus = -2;
        ui.name_return = Page::collection; ui.close_after_save = true;
    } else if (page == Page::games) { ui.focus = -2; }
    closeFriends();
    rebuildCollectionDialog();
    if (confirm) {
        for (size_t i = 0; i < ui.rows.size(); ++i)
            if (ui.rows[i].key == "@delete" || ui.rows[i].key == "@reset") { ui.focus = static_cast<int>(i); ui.confirm = true; }
        rebuildCollectionDialog();
    }
    audio_.drawer();
}

// ---- Tooltips --------------------------------------------------------------------------------

std::wstring Launcher::desktopTooltip(const desktop_ui::Hit& hit) const {
    const auto tools = desktopTools();
    switch (hit.target) {
    case Target::minimize: return tr(L"Minimalizuj");
    case Target::maximize: return IsZoomed(hwnd_) ? tr(L"Przywróć") : tr(L"Maksymalizuj");
    case Target::close: return tr(L"Zamknij");
    case Target::sidebar_toggle: return settings_.desktop_narrow ? tr(L"Rozwiń panel boczny") : tr(L"Zwiń panel boczny");
    case Target::pane_resize: return tr(L"Przeciągnij, aby zmienić szerokość; dwuklik: domyślna");
    case Target::sidebar_row: { // Narrow, the icons alone: their names on hover.
        if (!settings_.desktop_narrow) return {};
        const auto rows = desktopRows();
        if (hit.index < 0 || hit.index >= static_cast<int>(rows.size()) || rows[static_cast<size_t>(hit.index)].entry < 0) return {};
        return desktop_entries_[static_cast<size_t>(rows[static_cast<size_t>(hit.index)].entry)].title;
    }
    case Target::tool:
        if (hit.index < 0 || hit.index >= static_cast<int>(tools.size())) return {};
        switch (tools[static_cast<size_t>(hit.index)]) {
        case Tool::glance: return tr(L"Glance — nowości i premiery");
        case Tool::tarot: return tr(L"Tarot — wróżba z kart");
        case Tool::companion: return companion::name(companionLook());
        case Tool::store: return tr(L"Sklep");
        case Tool::friends: return tr(L"Widżety · profil i znajomi");
        case Tool::downloads: {
            const auto moving = std::count_if(desktop_download_rows_.begin(), desktop_download_rows_.end(), [](const auto& row) { return row.moving; });
            return moving ? std::wstring(tr(L"Pobierania")) + L" · " + std::to_wstring(moving) : std::wstring(tr(L"Pobierania"));
        }
        case Tool::tv: return tr(L"Tryb TV");
        case Tool::settings: return tr(L"Ustawienia");
        case Tool::update: return std::get<0>(updateRow()) + L" · " + std::get<2>(updateRow());
        }
        return {};
    case Target::search_clear: return tr(L"Wyczyść");
    case Target::sidebar_add: return tr(L"Nowa kolekcja");
    case Target::library_add: return tr(L"Dodaj grę z dysku");
    case Target::library_refresh: return tr(L"Odśwież biblioteki (F5)");
    case Target::companion_card: return std::wstring(tr(L"Porozmawiaj: ")) + companion::name(companionLook());
    case Target::companion_more: return tr(L"Więcej opcji");
    case Target::card:
        if (hit.index >= 0 && hit.index < static_cast<int>(cards_.size())) return cards_[static_cast<size_t>(hit.index)].game.name;
        return {};
    case Target::card_play:
        if (hit.index >= 0 && hit.index < static_cast<int>(cards_.size())) return actionLabel(cards_[static_cast<size_t>(hit.index)].game);
        return {};
    case Target::card_check: return tr(L"Zaznacz (Ctrl+klik)");
    case Target::sort: return tr(L"Sortowanie");
    case Target::installed: return tr(L"Tylko zainstalowane gry");
    case Target::size_slider: return tr(L"Rozmiar plakatów (Ctrl+kółko)");
    case Target::pane_favorite:
        return !cards_.empty() && isFavorite(cards_[selected_].game) ? tr(L"Usuń z ulubionych") : tr(L"Dodaj do ulubionych");
    case Target::pane_more: return tr(L"Więcej opcji");
    case Target::pane_quick:
        if (hit.index == 2 && !cards_.empty() && gameFolder(cards_[selected_].game).empty()) return tr(L"Gra nie jest zainstalowana");
        return {};
    case Target::pane_chip_add: return tr(L"Dodaj do kolekcji");
    case Target::pane_achievements: return tr(L"Pokaż osiągnięcia");
    case Target::pane_cloud: return tr(L"Zapisy w chmurze");
    default: return {};
    }
}

desktop_ui::Rect Launcher::desktopTooltipAnchor(const desktop_ui::Hit& hit) const {
    const auto L = desktopLayout();
    switch (hit.target) {
    case Target::minimize: return L.windowButton(0);
    case Target::maximize: return L.windowButton(1);
    case Target::close: return L.windowButton(2);
    case Target::sidebar_toggle: return L.sidebarToggle();
    case Target::pane_resize: { const auto e = L.paneEdge(); const float y = desktop_drag_.y > 0 ? desktop_drag_.y : (e.top + e.bottom) * 0.5f; return {e.left, y - 12, e.right, y + 12}; }
    case Target::sidebar_row: {
        const auto rows = desktopRows();
        return hit.index >= 0 && hit.index < static_cast<int>(rows.size()) ? L.sidebarRow(rows[static_cast<size_t>(hit.index)], desktop_sidebar_scroll_) : Rect{};
    }
    case Target::tool: return L.tool(hit.index);
    case Target::search_clear: return L.searchClear();
    case Target::sidebar_add: {
        const auto rows = desktopRows();
        return hit.index >= 0 && hit.index < static_cast<int>(rows.size()) ? L.headingAdd(rows[static_cast<size_t>(hit.index)], desktop_sidebar_scroll_) : Rect{};
    }
    case Target::library_add: case Target::library_refresh: {
        const auto rows = desktopRows();
        if (hit.index < 0 || hit.index >= static_cast<int>(rows.size())) return {};
        const auto& row = rows[static_cast<size_t>(hit.index)];
        return hit.target == Target::library_add ? L.headingAdd(row, desktop_sidebar_scroll_) : L.headingRefresh(row, desktop_sidebar_scroll_);
    }
    case Target::companion_card: return L.companionCard();
    case Target::companion_more: return L.companionMore();
    case Target::card: case Target::card_play: case Target::card_check: {
        const auto poster = L.poster(static_cast<size_t>(std::max(hit.index, 0)), desktop_scroll_);
        if (hit.target == Target::card_play) return Layout::playButton(poster);
        if (hit.target == Target::card_check) return Layout::checkBox(poster);
        return {poster.left, poster.top, poster.right, poster.bottom + caption_h};
    }
    case Target::sort: return L.sortButton();
    case Target::installed: return L.installedChip();
    case Target::size_slider: return L.sizeSlider();
    case Target::pane_favorite: return L.paneFavorite(desktop_pane_scroll_);
    case Target::pane_more: return L.paneMore(desktop_pane_scroll_);
    case Target::pane_quick: return L.paneQuick(hit.index, desktop_pane_scroll_);
    case Target::pane_chip_add: return renderer_.desktopChipAdd();
    case Target::pane_achievements: return renderer_.desktopAchievements();
    case Target::pane_cloud: return renderer_.desktopCloud();
    default: return {};
    }
}

// Steam's and the PC stores' libraries and the user's games read again (F5, Biblioteka's arrows):
// a game bought or installed meanwhile joins the list when its store reports it.
void Launcher::desktopRefreshLibraries() {
    desktop_refresh_spin_ = 0.75f;
    steam_rescan_->request(); refreshPcLibraries(); refreshCustomGames();
    // DRM-free: a full scan of its folders (a deleted game's folder takes it off the list); the signed-in
    // GOG and Epic accounts sync their libraries.
    startDrmScan(true);
    if (!gog_state_.account.empty()) gogCommand(gog::Command::sync);
    if (!epic_state_.account.empty()) epicCommand(epic::Command::sync);
    showNotification(NotificationKind::launch, tr(L"Odświeżanie biblioteki"), L"");
}
