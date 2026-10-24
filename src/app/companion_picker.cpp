// Character selection from the desktop card and the desktop/TV chat menu.
#include "launcher.hpp"

void Launcher::companionPicker(bool open) {
    auto& picker = companion_.picker;
    if (open) {
        if (!settings_.companion) return;
        picker = {};
        picker.choices = companion::availableLooks();
        if (picker.choices.empty()) { navigationBoundary(); return; }
        const auto current = std::find(picker.choices.begin(), picker.choices.end(), companionLook());
        picker.focus = current == picker.choices.end() ? 0 : static_cast<int>(current - picker.choices.begin());
        closeKeyboard(); cancelEnterHold(); cancelTabHold();
        companion_.chat.menu = false;
        companion_.link_pointer.reset();
        picker.open = true;
        audio_.context();
    } else {
        picker = {};
        audio_.navigate();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::companionChoose(companion::Look look) {
    const auto* character = companion::character(look);
    if (!character || character->soon) { navigationBoundary(); return; }
    companionPicker(false);
    if (look == companionLook()) return;
    const bool in_chat = companionOpen();
    if (companion_.live_chat) companionCloseArchive();
    companionFlush();
    companionSaveChat();
    requestCompanionSummary();
    // Saves still belong to the previous character; stale replies and voice are cancelled.
    companionReset(true);
    settings_.companion_look = std::string(companion::lookKey(look));
    settings_dirty_ = true; settings_save_failed_ = false;
    settings_edited_ = std::chrono::steady_clock::now();
    saveSettings();
    companion_.rig = avatar::Rig{17};
    companion_.rig.emote(companion::Emotion::smile);
    if (in_chat) {
        companionLoadChat();
        auto& chat = companion_.chat;
        chat.focus = chat.ended ? companion_ui::focus_send : companion_ui::focus_input;
        if (chat.log.empty() && !chat.waiting) companionBeginAwake(); // Asleep, the chat waits for her morning.
    } else if (!desktop_mode_ && homeWidgets() == home_widgets::Placement::tiles) rebuildCards(false);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::companionPickerKey(WPARAM key, bool repeated) {
    auto& picker = companion_.picker;
    if (!picker.open || picker.choices.empty()) return;
    using L = companion_ui::PickerLayout;
    const auto view = renderer_.viewport();
    const L layout{view.width, view.height};
    if (key == VK_ESCAPE || key == VK_BACK) { companionPicker(false); return; }
    if (key == VK_RETURN || key == VK_SPACE) {
        if (!repeated) {
            if (picker.cancel_focus) companionPicker(false);
            else companionChoose(picker.choices[static_cast<size_t>(picker.focus)]);
        }
        return;
    }
    picker.hover = companion_ui::no_hit;
    if (key == VK_TAB) picker.cancel_focus = !picker.cancel_focus;
    else {
        int step = key == VK_LEFT ? -1 : key == VK_RIGHT ? 1 : key == VK_UP ? -layout.columns(picker) :
                   key == VK_DOWN ? layout.columns(picker) : key == VK_PRIOR ? -L::page_size : key == VK_NEXT ? L::page_size : 0;
        if (!step && key != VK_HOME && key != VK_END) return;
        if (picker.cancel_focus) picker.cancel_focus = false;
        else {
            const int count = static_cast<int>(picker.choices.size());
            const int next = key == VK_HOME ? 0 : key == VK_END ? count - 1 : std::clamp(picker.focus + step, 0, count - 1);
            if (next == picker.focus) { navigationBoundary(); return; }
            picker.focus = next;
        }
    }
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::companionPickerClick(float x, float y) {
    auto& picker = companion_.picker;
    const auto view = renderer_.viewport();
    const companion_ui::PickerLayout layout{view.width, view.height};
    const int hit = layout.hit(picker, x, y);
    if (hit >= 0) companionChoose(picker.choices[static_cast<size_t>(hit)]);
    else if (hit == companion_ui::PickerLayout::cancel || !layout.panel(picker).contains(x, y)) companionPicker(false);
    else if (hit == companion_ui::PickerLayout::previous) companionPickerKey(VK_PRIOR, false);
    else if (hit == companion_ui::PickerLayout::next) companionPickerKey(VK_NEXT, false);
}
