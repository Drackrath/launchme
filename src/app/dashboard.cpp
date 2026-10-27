#include "launcher.hpp"
#include <cmath>

namespace board = friends_layout::dashboard;

board::Layout Launcher::dashboardLayout() const {
    // Indexed like board::Tile, then the plugins' widgets and the FPS reading.
    // Over a game the profile (a whole page) is not offered: its slot stays, empty, so the
    // plugins' and the FPS reading's indices do not move.
    const bool profile = !gameOverlayOpen();
    std::vector<std::string> ids{"steam", "discord", "cpu", "gpu", profile ? "profile" : ""};
    for (const auto& w : plugin_widgets_) ids.push_back(w.plugin + "/" + w.item.id);
    if (settings_.game_fps) ids.push_back("fps");
    const auto& saved = settings_.widget_layout;
    if (!profile || std::any_of(saved.begin(), saved.end(), [](const auto& p) { return p.id == "profile"; })) return {ids, saved};
    // Not placed yet (a new profile, or one from before it existed): the others keep their places
    // and the profile comes after them, as a wide tile.
    auto placed = saved;
    for (const auto& id : ids)
        if (id != "profile" && std::none_of(placed.begin(), placed.end(), [&](const auto& p) { return p.id == id; })) placed.push_back({id, widgets::Size::square});
    placed.push_back({"profile", widgets::Size::wide});
    return {ids, placed};
}

bool Launcher::dashboardEditing() const {
    return dashboard_editing_ && friends_open_ && !friends_list_ && !plugin_drawer_active_;
}

void Launcher::dashboardEnsureVisible() {
    const auto layout = dashboardLayout();
    const auto r = layout.tile(dashboard_tile_);
    const float height = drawerViewport().height;
    if (dashboard_tile_ < 0) dashboard_scroll_ = 0;
    else {
        if (r.top < board::grid_top + dashboard_scroll_) dashboard_scroll_ = r.top - board::grid_top;
        if (r.bottom > height - 20 + dashboard_scroll_) dashboard_scroll_ = r.bottom - height + 20;
    }
    dashboard_scroll_ = std::clamp(dashboard_scroll_, 0.0f, layout.maxScroll(height));
}

void Launcher::dashboardCancelPress() {
    const bool pointer = dashboard_press_source_ == 3;
    dashboard_press_source_ = 0; dashboard_pressed_tile_ = -1; dashboard_grabbed_ = false;
    if (pointer && GetCapture() == frameWindow()) ReleaseCapture();
    invalidateGameOverlay(); InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::dashboardEdit(bool editing) {
    dashboardCancelPress();
    if (dashboard_editing_ == editing) return;
    dashboard_editing_ = editing;
    dashboard_edit_at_ = GetTickCount64();
    if (editing) {
        if (plugin_command_pending_) { ++plugin_command_generation_; plugin_command_pending_ = false; }
        // Preserve unavailable plugins; their positions/sizes survive disable/re-enable.
        const auto layout = dashboardLayout();
        // Bound stale preferences from repeatedly installing different plugins.
        for (auto i = settings_.widget_layout.begin(); settings_.widget_layout.size() > 64 && i != settings_.widget_layout.end();) {
            if (std::none_of(layout.items.begin(), layout.items.end(), [&](const auto& item) { return item.id == i->id; })) i = settings_.widget_layout.erase(i);
            else ++i;
        }
        for (const auto& item : layout.items)
            if (std::none_of(settings_.widget_layout.begin(), settings_.widget_layout.end(), [&](const auto& p) { return p.id == item.id; }))
                settings_.widget_layout.push_back({item.id, item.size});
    } else if (settings_dirty_) saveSettings();
    drawer_glide_.valid = false;
    invalidateGameOverlay(); InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::dashboardReorder(int target) {
    if (!dashboardEditing() || target < 0 || target == dashboard_tile_) return;
    const auto layout = dashboardLayout();
    const auto find = [&](int source) { return std::find_if(layout.items.begin(), layout.items.end(), [&](const auto& i) { return i.source == source; }); };
    const auto from = find(dashboard_tile_), to = find(target);
    if (from == layout.items.end() || to == layout.items.end()) return;
    auto& saved = settings_.widget_layout;
    const auto a = std::find_if(saved.begin(), saved.end(), [&](const auto& p) { return p.id == from->id; });
    const auto b = std::find_if(saved.begin(), saved.end(), [&](const auto& p) { return p.id == to->id; });
    if (a == saved.end() || b == saved.end()) return;
    // Insert at the target's position, shifting intervening widgets, not their identities.
    if (a < b) std::rotate(a, a + 1, b + 1);
    else std::rotate(b, a, a + 1);
    settings_dirty_ = true; drawer_glide_.valid = false;
    dashboardEnsureVisible();
    invalidateGameOverlay(); InvalidateRect(hwnd_, nullptr, FALSE);
}

bool Launcher::dashboardPress(int source, bool down, uint64_t now) {
    if (!down) {
        if (dashboard_press_source_ != source) return false;
        dashboardPollHold(now);
        const bool resize = !dashboard_grabbed_ && dashboardEditing();
        const int tile = dashboard_pressed_tile_;
        dashboardCancelPress();
        if (resize) dashboardActivate(tile);
        return true;
    }
    if (!dashboardEditing() || dashboard_tile_ < 0) return false;
    if (!dashboard_press_source_) {
        dashboard_press_source_ = source; dashboard_pressed_tile_ = dashboard_tile_;
        dashboard_pressed_at_ = now;
    }
    return true;
}

void Launcher::dashboardPollHold(uint64_t now) {
    if (dashboard_press_source_ && dashboardEditing() && !dashboard_grabbed_ && now - dashboard_pressed_at_ >= 450) {
        dashboard_grabbed_ = true; audio_.navigate();
        invalidateGameOverlay(); InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

// Coordinates are logical pixels in the active window (launcher or game overlay).
void Launcher::dashboardPointerDown(float x, float y) {
    const auto view = drawerViewport();
    const int tile = dashboardLayout().hit(view.width - friends_layout::drawer_width, x, y, dashboard_scroll_, view.height);
    if (tile == board::no_hit) return;
    if (dashboard_press_source_ && dashboard_press_source_ != 3) dashboardCancelPress();
    dashboard_tile_ = tile;
    if (dashboardPress(3, true, GetTickCount64())) {
        dashboard_pointer_release_ = true;
        dashboard_pointer_x_ = x; dashboard_pointer_y_ = y;
        SetCapture(frameWindow());
    } else dashboardActivate(tile);
}

void Launcher::dashboardPointerMove(float x, float y) {
    if (dashboard_press_source_ != 3 || !dashboardEditing()) return;
    if (std::hypot(x - dashboard_pointer_x_, y - dashboard_pointer_y_) > 7) dashboard_grabbed_ = true;
    if (!dashboard_grabbed_) return;
    const auto view = drawerViewport();
    // Mouse movement near either edge scrolls the grid, including off-screen targets.
    const auto layout = dashboardLayout();
    if (y < board::grid_top + 16) dashboard_scroll_ -= 14;
    else if (y > view.height - 36) dashboard_scroll_ += 14;
    dashboard_scroll_ = std::clamp(dashboard_scroll_, 0.0f, layout.maxScroll(view.height));
    const int hit = layout.hit(view.width - friends_layout::drawer_width, x, y, dashboard_scroll_, view.height);
    // Require fresh movement after reflow; a stationary pointer must not shuffle back.
    if (std::hypot(x - dashboard_pointer_x_, y - dashboard_pointer_y_) > 7 && hit >= 0 && hit != dashboard_tile_) {
        dashboardReorder(hit); dashboard_pointer_x_ = x; dashboard_pointer_y_ = y;
    }
    invalidateGameOverlay(); InvalidateRect(hwnd_, nullptr, FALSE);
}
