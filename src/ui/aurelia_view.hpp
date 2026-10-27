#pragma once
#include "settings_view.hpp"
#include "../store/aurelia.hpp"

namespace aurelia_ui {
inline const wchar_t* notice() {
    return tr(L"Głównie do gier single player / offline. Niektóre gry, zwłaszcza multiplayer i korzystające z DRM lub Steamworks, mogą wymagać uruchomionego klienta Steam.");
}
enum class Page { setup, game, cloud, dlc, versions, branches, workshop, friends, chat, libraries };
enum class Action {
    none, toggle, helper, repository, login, logout, reconnect, refresh, close, game, cloud, dlc, versions, branches, workshop, friends,
    sync, resolve_cloud, resolve_local, install, update, verify, cancel, uninstall, steam, play, auto_cloud,
    enable_dlc, disable_dlc, pin, unpin, downgrade, set_branch, move, choose_library,
    workshop_install, workshop_subscribe, workshop_unsubscribe, chat, send, presence, confirm, dismiss
};
struct Row {
    std::wstring title, description, value;
    Action action = Action::none;
    uint64_t id = 0;
    std::wstring payload;
    bool checked = false, toggle = false, danger = false, locked = false;
};
struct State {
    bool open = false, intro = false, busy = false, input_focus = false, sending = false, live = false;
    bool enabled = false, active = false, installed = false, authenticated = false;
    Page page = Page::setup;
    Game game;
    uint64_t friend_id = 0;
    std::wstring friend_name, title, subtitle, status, input, input_hint;
    std::vector<Row> rows;
    std::vector<aurelia::ChatMessage> messages;
    int focus = 0, first = 0, hover = -9;
    std::optional<Row> confirmation;
    float progress = -1;
    float chat_scroll = 0, chat_content = 0;
    bool chat_follow = true;
};
struct Layout {
    float width = 1600, height = 900;
    settings_ui::Rect panel() const {
        const float w = std::min(1080.0f, width - 64), h = std::min(770.0f, height - 48);
        return {(width - w) / 2, (height - h) / 2, (width + w) / 2, (height + h) / 2};
    }
    settings_ui::Rect close() const { const auto p = panel(); return {p.right - 72, p.top + 22, p.right - 24, p.top + 66}; }
    float rowsTop(const State& s) const { return panel().top + (s.page == Page::setup ? 246 : 138); }
    int visible(const State& s) const { return std::max(1, static_cast<int>((panel().bottom - 102 - rowsTop(s)) / 68)); }
    settings_ui::Rect row(const State& s, int index) const {
        const auto p = panel(); const float y = rowsTop(s) + (index - s.first) * 68;
        return {p.left + 28, y, p.right - 28, y + 60};
    }
    settings_ui::Rect input() const { const auto p = panel(); return {p.left + 28, p.bottom - 80, p.right - 190, p.bottom - 28}; }
    settings_ui::Rect send() const { const auto p = panel(); return {p.right - 174, p.bottom - 80, p.right - 28, p.bottom - 28}; }
    settings_ui::Rect log() const { const auto p = panel(); return {p.left + 28, p.top + 138, p.right - 28, p.bottom - 102}; }
    int hit(const State& s, float x, float y) const {
        if (close().contains(x, y)) return -1;
        if (s.confirmation) {
            if (input().contains(x, y)) return -4;
            if (send().contains(x, y)) return -5;
            return -9;
        }
        if (!s.input_hint.empty()) {
            if (input().contains(x, y)) return -2;
            if (send().contains(x, y)) return -3;
        }
        if (s.page != Page::chat) for (int i = s.first; i < std::min(static_cast<int>(s.rows.size()), s.first + visible(s)); ++i)
            if (row(s, i).contains(x, y)) return i;
        return -9;
    }
};
}
