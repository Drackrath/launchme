#pragma once
// The launcher's routing of window input — keys, typed text, the mouse wheel, clicks and focus —
// independent of the window system. On Windows, src/main.cpp's window procedure translates its
// WM_KEYDOWN, WM_CHAR, WM_MOUSEWHEEL, ... into calls of these; on Linux the SDL window's events
// (platform/sdl_window.hpp) go through handle(), which also does what main.cpp's window procedure
// does before its switch (closing, screensaver, launch animation).
//
// A template over the application so that this routing is compiled and tested without the
// Launcher (tests/window_input_tests.cpp drives it with a recording stand-in). App provides the
// Launcher's input methods named below, its Modal enumeration, and for handle() also
// `bool screensaverEvent(const platform::WindowEvent&)` (Launcher::screensaverInput's portable twin).
#include "../platform/key_codes.hpp"
#include "../platform/text_encoding.hpp"
#include "../platform/window_events.hpp"

#include <cstdlib>
#include <string_view>
#include <type_traits>
#include <variant>

namespace window_input {

template <class App>
class Dispatcher {
public:
    // WM_KEYDOWN. The topmost modal layer, then the companion's paste, the desktop window, and
    // the shared shortcuts.
    void keyDown(App& app, const platform::KeyEvent& event) {
        const unsigned key = event.key;
        const bool repeated = event.repeat;
        const bool control = event.modifiers.control, alt = event.modifiers.alt, shift = event.modifiers.shift;
        if (key != keys::f8 && key != keys::f11) { app.noteUserInput(); app.noteDesktopInput(); }
        // The topmost modal layer (exit prompt, power menu, walkthrough, dialogs) takes the key first.
        if (app.modalKey(key, repeated)) return;
        if (app.companionPasteKey(key, repeated)) return;
        // The desktop library: its keys, shortcuts and search field (false: the shared handling below).
        if (app.desktopKey(key, repeated)) return;
        if (key == keys::home && app.homeKeyIsGuide()) app.homeDown(repeated); // Guide: short = menu, hold = power.
        else if (key == keys::letter('G') && !repeated && !control && !alt && app.glanceAvailable()) app.openGlance();
        else if (key == keys::letter('C') && !repeated && !control && !alt && app.companionAvailable()) app.openCompanion();
        else if (key == keys::letter('T') && !repeated && !control && !alt && app.tarotAvailable()) app.openTarot();
        else if ((app.editingSecret() || app.editingArtworkSearch()) && key == keys::letter('V') && control) app.pasteSecret();
        else if ((app.editingSecret() || app.editingArtworkSearch()) && key == keys::letter('A') && control) app.selectSecret();
        else if (app.editingArtworkSearch() && key == keys::del) app.clearArtworkQuery(); // Delete = "Wyczyść".
        else if ((app.editingSecret() || app.editingArtworkSearch()) && (key == keys::back || key == keys::del)) app.eraseSecret();
        else if (key == keys::enter) app.enterDown(repeated);
        else if (key == keys::tab) app.tabDown(repeated);
        // A/D double as Left/Right, except where letters are text (search fields, and the
        // library screen, where typing opens its search).
        else if (key == keys::left || (key == keys::letter('A') && !app.searching() && !app.artworkOpen() && !app.libraryTypeAhead() && !app.editingSecret())) {
            if (app.settingsOpen()) app.settingsAdjust(-1);
            else if (app.quickOpen()) app.quickMove(-1);
            else if (app.categoryMenuOpen()) app.categoryMenuHorizontal(-1, repeated);
            else app.navigate(-1);
        }
        else if (key == keys::right || (key == keys::letter('D') && !app.searching() && !app.artworkOpen() && !app.libraryTypeAhead() && !app.editingSecret())) {
            if (app.settingsOpen()) app.settingsAdjust(1);
            else if (app.quickOpen()) app.quickMove(1);
            else if (app.categoryMenuOpen()) app.categoryMenuHorizontal(1, repeated);
            else app.navigate(1);
        }
        else if (key == keys::down) {
            if (app.settingsOpen() && shift) app.settingsReorder(1);
            else if (app.friendsOpen()) app.friendsMove(1);
            else if (app.categoryMenuOpen()) app.categoryMenuMove(1);
            else if (app.menuOpen()) app.menuMove(1);
            else app.changeCategory(1);
        } else if (key == keys::up) {
            if (app.settingsOpen() && shift) app.settingsReorder(-1);
            else if (app.friendsOpen()) app.friendsMove(-1);
            else if (app.categoryMenuOpen()) app.categoryMenuMove(-1);
            else if (app.menuOpen()) app.menuMove(-1);
            else app.changeCategory(-1);
        } else if (key == keys::space && !app.searching() && !app.artworkOpen() && !app.libraryScreen()) { // Not a pick in the library.
            if (repeated) return;
            if (app.settingsOpen()) app.settingsActivate();
            else if (app.quickOpen()) app.quickAccept();
            else if (app.friendsOpen()) app.friendsAccept();
            else if (app.categoryMenuOpen()) app.categoryMenuAccept();
            else if (app.menuOpen()) app.menuAccept();
            else app.accept();
        }
        else if (key == keys::f11) app.toggleFullscreen();
        else if (key == keys::f6 && !repeated) app.focusLibraryControls();
        else if (key == keys::f8 && !repeated) app.togglePerformance();
        else if (key == keys::f5 && app.settingsOpen()) { app.saveSettings(); app.retryStore(); }
        else if (key == keys::f5) app.retryStore();
        else if ((key == keys::page_up || key == keys::page_down) && app.glanceOpen() && !app.friendsOpen()) app.glanceStepSection(key == keys::page_up ? -1 : 1);
        else if ((key == keys::page_up || key == keys::page_down) && app.companionOpen() && !app.friendsOpen()) app.companionScroll(key == keys::page_up ? 1 : -1);
        else if (key == keys::page_up && app.artworkOpen()) app.artworkPage(-1);
        else if (key == keys::page_down && app.artworkOpen()) app.artworkPage(1);
        else if ((key == keys::page_up || key == keys::page_down) && app.settingsOpen()) app.settingsShoulder(key == keys::page_up ? -1 : 1);
        else if ((key == keys::page_up || key == keys::page_down) && app.libraryTabsShown()) app.switchLibraryTab(key == keys::page_up ? -1 : 1);
        else if (key == keys::page_up || key == keys::page_down) app.homePage(key == keys::page_up ? -1 : 1); // Metro pages; elsewhere nothing.
        else if (key == keys::letter('H') && !app.searching() && control) app.togglePrivateCategory();
        else if (key == keys::del && app.companionOpen() && !app.friendsOpen() && !repeated) app.companionMindDeleteConfirm(true);
        else if (key == keys::letter('F') && app.companionOpen() && !app.searching() && !app.friendsOpen() && !repeated) app.companionMindNextFilter();
        else if (key == keys::back && app.searching()) app.eraseSearchCharacter(repeated);
        // Back takes one step per press: a held Backspace that just emptied a search must not
        // go on to close it, leave the bar and then the library.
        else if ((key == keys::escape || key == keys::back) && repeated) {}
        else if (key == keys::escape || key == keys::back) {
            if (app.quickOpen()) app.closeQuick();
            else if (app.categoryMenuOpen()) app.closeCategoryMenu();
            else if (app.friendsOpen()) app.friendsBack(); // Friends list -> tiles -> closed.
            else if (app.menuOpen()) app.dismissMenu();
            else app.back();
        }
    }

    // WM_KEYUP: the keys with a short press and a hold.
    void keyUp(App& app, unsigned key) {
        if (key == keys::enter) app.enterUp();
        if (key == keys::tab) app.tabUp();
        if (key == keys::home) app.homeUp();
    }

    // WM_CHAR: one UTF-16 code unit on Windows, one code point on Linux.
    void character(App& app, wchar_t character) {
        if (!app.exitPromptOpen() && !app.powerMenuOpen() && !app.tourOpen()) app.onCharacter(character);
    }

    // Committed text (an input method's result, or typed keys): character by character, as
    // Windows delivers WM_CHAR.
    void text(App& app, std::string_view utf8) {
        for (const wchar_t unit : text_encoding::wideLenient(utf8)) character(app, unit);
    }

    // WM_MOUSEWHEEL at a client position. `delta` in 1/120 notches.
    void wheel(App& app, float x, float y, int raw, bool control) {
        if (app.exitPromptOpen() || app.powerMenuOpen() || app.tourOpen()) return;
        app.noteUserInput();
        if (app.desktopEmbedded()) { // Urządzenia, kadrowanie, plugin pages in a desktop sheet: their canvas.
            app.desktopEmbedMap(x, y);
            x = static_cast<float>(static_cast<long>(x)); y = static_cast<float>(static_cast<long>(y)); // Whole pixels, as Windows' POINT.
        }
        using Modal = typename App::Modal;
        const auto modal = app.topModal();
        // Lists move one row per notch: a precision touchpad's many small deltas add up to
        // whole notches instead of each one moving a row (which overshot every list).
        const auto notch = [&] {
            if ((accumulated_ < 0) != (raw < 0)) accumulated_ = 0; // Turned the other way.
            accumulated_ += raw;
            if (std::abs(accumulated_) < platform::wheel_notch) return 0;
            const int step = accumulated_ < 0 ? 1 : -1;
            accumulated_ += step * platform::wheel_notch;
            return step;
        };
        if (app.desktopWheel(x, y, raw, control)) {} // Grid, sidebar, pane, poster size.
        else if (app.companionOpen() && !app.friendsOpen() && modal == Modal::none) app.companionWheel(raw);
        else if (modal == Modal::tarot) {} // The reading has nothing to scroll.
        else if (modal == Modal::profile) app.profileScroll(-static_cast<float>(raw) / platform::wheel_notch * 96.0f); // Smooth, as a page.
        else if (modal == Modal::whats_new) app.whatsNewScroll(-static_cast<float>(raw) / platform::wheel_notch * 60.0f); // Smooth, as text scrolls.
        else if (const int step = notch(); !step) {}
        else if (modal == Modal::file_browser) app.fileBrowserWheel(step);
        else if (modal == Modal::library_dialog) app.libraryDialogKey(step > 0 ? keys::down : keys::up, false);
        else if (modal == Modal::companion_picker) app.companionPickerKey(step > 0 ? keys::page_down : keys::page_up, false);
        else if (app.cropOpen()) app.changeCategory(step);
        else if (app.devicesOpen()) app.deviceWheel(x, step);
        else if (app.artworkOpen()) app.artworkWheel(x, step);
        else if (app.settingsOpen()) app.settingsWheel(x, step);
        else if (app.friendsOpen() && app.overFriends(x)) app.scrollFriends(step);
        else if (app.categoryMenuOpen()) app.scrollCategoryMenu(step);
        else app.wheel(step, x);
    }

    // WM_LBUTTONDOWN / WM_LBUTTONDBLCLK.
    void leftDown(App& app, float x, float y, bool double_click) {
        app.noteUserInput(); app.noteDesktopInput();
        app.click(x, y, double_click);
    }
    // WM_LBUTTONUP: desktop buttons act on release over the same target; drags drop here.
    void leftUp(App& app, float x, float y) {
        app.desktopPointerUp(x, y);
        app.pointerUp(false);
    }
    // WM_CAPTURECHANGED: a drag ends without a drop.
    void captureLost(App& app) { app.pointerUp(true); }
    // WM_RBUTTONUP.
    void rightUp(App& app, float x, float y) {
        app.noteUserInput(); app.noteDesktopInput();
        app.contextClick(x, y);
    }
    // WM_MOUSEMOVE over the client area (not the desktop window's maximize button).
    void mouseMove(App& app, float x, float y) {
        app.desktopMaximizeHover(false);
        app.mouseMove(x, y);
    }
    void mouseLeave(App& app) { app.mouseLeave(); }
    // WM_KILLFOCUS / WM_SETFOCUS.
    void focusLost(App& app) { app.cancelEnterHold(); app.cancelTabHold(); app.pointerUp(); }
    void focusGained(App& app) { app.focusRegained(); }

    // Every event of a window without a window procedure of its own (the SDL window). True when
    // the launcher took it; a CloseRequestEvent it does not take closes the window.
    bool handle(App& app, const platform::WindowEvent& event) {
        const bool input = isInput(event);
        if (app.closing()) return input; // The departing scene takes no more input.
        // Screensaver: while it runs, and for the rest of the input that ends it, keyboard
        // and mouse events never reach the screen beneath.
        if (input && app.screensaverEvent(event)) return true;
        if (app.launchAnimationActive() && input) {
            // Back hides the starting game's picture; every other key and click waits for its end.
            const auto* key = std::get_if<platform::KeyEvent>(&event);
            const auto* button = std::get_if<platform::MouseButtonEvent>(&event);
            if ((key && key->pressed && !key->repeat && (key->key == keys::escape || key->key == keys::back)) ||
                (button && !button->pressed && button->button == platform::MouseButton::back)) app.dismissLaunchAnimation();
            return true;
        }
        return std::visit([&](const auto& value) { return route(app, value); }, event);
    }

private:
    static bool isInput(const platform::WindowEvent& event) {
        return std::holds_alternative<platform::KeyEvent>(event) || std::holds_alternative<platform::TextEvent>(event) ||
               std::holds_alternative<platform::TextEditingEvent>(event) || std::holds_alternative<platform::MouseMoveEvent>(event) ||
               std::holds_alternative<platform::MouseButtonEvent>(event) || std::holds_alternative<platform::MouseWheelEvent>(event) ||
               std::holds_alternative<platform::TouchEvent>(event);
    }

    bool route(App& app, const platform::KeyEvent& event) {
        if (event.pressed) keyDown(app, event);
        else keyUp(app, event.key);
        return true;
    }
    bool route(App& app, const platform::TextEvent& event) { text(app, event.text); return true; }
    bool route(App& app, const platform::MouseMoveEvent& event) { mouseMove(app, event.x, event.y); return true; }
    bool route(App& app, const platform::MouseLeaveEvent&) { mouseLeave(app); return true; }
    bool route(App& app, const platform::MouseWheelEvent& event) { wheel(app, event.x, event.y, event.delta, event.control); return true; }
    bool route(App& app, const platform::MouseButtonEvent& event) {
        using platform::MouseButton;
        if (event.button == MouseButton::left && event.pressed) {
            // Windows' CS_DBLCLKS: the second click of a pair is the double click, a third starts over.
            leftDown(app, event.x, event.y, event.clicks >= 2 && event.clicks % 2 == 0);
            return true;
        }
        if (event.button == MouseButton::left) { leftUp(app, event.x, event.y); return true; }
        if (event.button == MouseButton::right && !event.pressed) { rightUp(app, event.x, event.y); return true; }
        if (event.button == MouseButton::back && !event.pressed) {
            // The mouse's Back button: what Esc does on every screen, through the same filters as a key.
            handle(app, platform::KeyEvent{keys::escape, 0, true, false, {}});
            handle(app, platform::KeyEvent{keys::escape, 0, false, false, {}});
            return true;
        }
        return false;
    }
    bool route(App& app, const platform::WindowFocusEvent& event) {
        // Windows sends WM_ACTIVATE before WM_SETFOCUS and WM_KILLFOCUS; SDL has one focus event.
        app.windowActivated(event.focused);
        if (event.focused) focusGained(app);
        else focusLost(app);
        return true;
    }
    bool route(App& app, const platform::ResizeEvent&) { app.resize(); return true; }
    bool route(App& app, const platform::PlacementEvent& event) {
        if (event.placement == platform::WindowPlacement::maximized || event.placement == platform::WindowPlacement::restored)
            app.windowPlacementChanged();
        return true;
    }
    bool route(App& app, const platform::CloseRequestEvent&) { return app.requestClose(); } // Confirmation, then the one-second exit.
    bool route(App& app, const platform::DisplayChangedEvent&) { app.displayChanged(); return true; }
    bool route(App& app, const platform::DisplayScaleEvent&) {
        // Full-screen Tryb TV keeps covering its monitor; a window keeps its logical size (SDL resizes it).
        if (app.coversMonitor()) app.displayChanged();
        else app.resize();
        return true;
    }
    // Quit ends the loop (its owner's decision); gamepads are read by platform/game_controllers;
    // composition, touch and drops have no launcher handling yet.
    template <class Other>
    bool route(App&, const Other&) { return false; }

    int accumulated_ = 0;
};

} // namespace window_input
