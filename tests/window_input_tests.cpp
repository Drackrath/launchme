// app/window_input.hpp, the launcher's routing of keys, text, the wheel and clicks, driven with a
// stand-in that records what the Launcher would be asked to do. Windows' window procedure and the
// SDL window share this routing; on Linux the SDL events come through platform::translateSdlEvent.
#include "app/window_input.hpp"
#include "platform/sdl_window.hpp"

#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {

struct FakeApp {
    enum class Modal { none, exit_prompt, power_menu, tour, whats_new, companion_picker, file_browser, library_dialog,
                       custom_editor, metadata, bulk_covers, identify, gog_install, tarot, profile };
    std::set<std::string> on;               // Queries that answer true.
    std::set<std::string> takes;            // modalKey / companionPasteKey / desktopKey / desktopWheel / screensaverEvent / requestClose.
    Modal modal = Modal::none;
    std::vector<std::string> calls;
    float embed_offset = 0;

    bool is(const char* name) const { return on.contains(name); }
    void log(std::string call) { calls.push_back(std::move(call)); }
    static std::string n(int value) { return std::to_string(value); }
    static std::string f(float value) { return std::to_string(static_cast<int>(value)); }

    // Queries.
    bool artworkOpen() const { return is("artworkOpen"); }
    bool categoryMenuOpen() const { return is("categoryMenuOpen"); }
    bool closing() const { return is("closing"); }
    bool companionAvailable() const { return is("companionAvailable"); }
    bool companionOpen() const { return is("companionOpen"); }
    bool coversMonitor() const { return is("coversMonitor"); }
    bool cropOpen() const { return is("cropOpen"); }
    bool desktopEmbedded() const { return is("desktopEmbedded"); }
    bool devicesOpen() const { return is("devicesOpen"); }
    bool editingArtworkSearch() const { return is("editingArtworkSearch"); }
    bool editingSecret() const { return is("editingSecret"); }
    bool exitPromptOpen() const { return is("exitPromptOpen"); }
    bool friendsOpen() const { return is("friendsOpen"); }
    bool glanceAvailable() const { return is("glanceAvailable"); }
    bool glanceOpen() const { return is("glanceOpen"); }
    bool homeKeyIsGuide() const { return is("homeKeyIsGuide"); }
    bool launchAnimationActive() const { return is("launchAnimationActive"); }
    bool libraryScreen() const { return is("libraryScreen"); }
    bool libraryTabsShown() const { return is("libraryTabsShown"); }
    bool libraryTypeAhead() const { return is("libraryTypeAhead"); }
    bool menuOpen() const { return is("menuOpen"); }
    bool overFriends(float) const { return is("overFriends"); }
    bool powerMenuOpen() const { return is("powerMenuOpen"); }
    bool quickOpen() const { return is("quickOpen"); }
    bool searching() const { return is("searching"); }
    bool settingsOpen() const { return is("settingsOpen"); }
    bool tarotAvailable() const { return is("tarotAvailable"); }
    bool tourOpen() const { return is("tourOpen"); }
    Modal topModal() const { return modal; }

    // Layers that may take an event.
    bool modalKey(unsigned key, bool repeated) { log("modalKey " + n(key) + (repeated ? " r" : "")); return takes.contains("modalKey"); }
    bool companionPasteKey(unsigned, bool) { return takes.contains("companionPasteKey"); }
    bool desktopKey(unsigned key, bool) { if (takes.contains("desktopKey")) { log("desktopKey " + n(key)); return true; } return false; }
    bool desktopWheel(float x, float y, int delta, bool control) {
        if (!takes.contains("desktopWheel")) return false;
        log("desktopWheel " + f(x) + "," + f(y) + " " + n(delta) + (control ? " ctrl" : "")); return true;
    }
    bool screensaverEvent(const platform::WindowEvent&) { return takes.contains("screensaverEvent"); }
    bool requestClose() { log("requestClose"); return takes.contains("requestClose"); }
    void desktopEmbedMap(float& x, float& y) const { x += embed_offset; y += embed_offset; }

    // Actions.
    void accept() { log("accept"); }
    void artworkPage(int a) { log("artworkPage " + n(a)); }
    void artworkWheel(float, int a) { log("artworkWheel " + n(a)); }
    void back() { log("back"); }
    void cancelEnterHold() { log("cancelEnterHold"); }
    void cancelTabHold() { log("cancelTabHold"); }
    void categoryMenuAccept() { log("categoryMenuAccept"); }
    void categoryMenuHorizontal(int a, bool r) { log("categoryMenuHorizontal " + n(a) + (r ? " r" : "")); }
    void categoryMenuMove(int a) { log("categoryMenuMove " + n(a)); }
    void changeCategory(int a) { log("changeCategory " + n(a)); }
    void clearArtworkQuery() { log("clearArtworkQuery"); }
    void click(float x, float y, bool d) { log("click " + f(x) + "," + f(y) + (d ? " double" : "")); }
    void closeCategoryMenu() { log("closeCategoryMenu"); }
    void closeQuick() { log("closeQuick"); }
    void companionMindDeleteConfirm(bool) { log("companionMindDeleteConfirm"); }
    void companionMindNextFilter() { log("companionMindNextFilter"); }
    void companionPickerKey(unsigned key, bool) { log("companionPickerKey " + n(key)); }
    void companionScroll(int a) { log("companionScroll " + n(a)); }
    void companionWheel(int d) { log("companionWheel " + n(d)); }
    void contextClick(float x, float y) { log("contextClick " + f(x) + "," + f(y)); }
    void desktopMaximizeHover(bool h) { log(std::string("desktopMaximizeHover ") + (h ? "1" : "0")); }
    void desktopPointerUp(float x, float y) { log("desktopPointerUp " + f(x) + "," + f(y)); }
    void deviceWheel(float, int a) { log("deviceWheel " + n(a)); }
    void dismissLaunchAnimation() { log("dismissLaunchAnimation"); }
    void dismissMenu() { log("dismissMenu"); }
    void displayChanged() { log("displayChanged"); }
    void enterDown(bool r) { log(std::string("enterDown") + (r ? " r" : "")); }
    void enterUp() { log("enterUp"); }
    void eraseSearchCharacter(bool r) { log(std::string("eraseSearchCharacter") + (r ? " r" : "")); }
    void eraseSecret() { log("eraseSecret"); }
    void fileBrowserWheel(int a) { log("fileBrowserWheel " + n(a)); }
    void focusLibraryControls() { log("focusLibraryControls"); }
    void focusRegained() { log("focusRegained"); }
    void friendsAccept() { log("friendsAccept"); }
    void friendsBack() { log("friendsBack"); }
    void friendsMove(int a) { log("friendsMove " + n(a)); }
    void glanceStepSection(int a) { log("glanceStepSection " + n(a)); }
    void homeDown(bool r) { log(std::string("homeDown") + (r ? " r" : "")); }
    void homePage(int a) { log("homePage " + n(a)); }
    void homeUp() { log("homeUp"); }
    void libraryDialogKey(unsigned key, bool) { log("libraryDialogKey " + n(key)); }
    void menuAccept() { log("menuAccept"); }
    void menuMove(int a) { log("menuMove " + n(a)); }
    void mouseLeave() { log("mouseLeave"); }
    void mouseMove(float x, float y) { log("mouseMove " + f(x) + "," + f(y)); }
    void navigate(int a) { log("navigate " + n(a)); }
    void noteDesktopInput() {}
    void noteUserInput() {}
    void onCharacter(wchar_t c) { log("char " + n(static_cast<int>(c))); }
    void openCompanion() { log("openCompanion"); }
    void openGlance() { log("openGlance"); }
    void openTarot() { log("openTarot"); }
    void pasteSecret() { log("pasteSecret"); }
    void pointerUp(bool cancelled = false) { log(std::string("pointerUp") + (cancelled ? " cancelled" : "")); }
    void profileScroll(float a) { log("profileScroll " + f(a)); }
    void quickAccept() { log("quickAccept"); }
    void quickMove(int a) { log("quickMove " + n(a)); }
    void resize() { log("resize"); }
    void retryStore() { log("retryStore"); }
    void saveSettings() { log("saveSettings"); }
    void scrollCategoryMenu(int a) { log("scrollCategoryMenu " + n(a)); }
    void scrollFriends(int a) { log("scrollFriends " + n(a)); }
    void selectSecret() { log("selectSecret"); }
    void settingsActivate() { log("settingsActivate"); }
    void settingsAdjust(int a) { log("settingsAdjust " + n(a)); }
    void settingsReorder(int a) { log("settingsReorder " + n(a)); }
    void settingsShoulder(int a) { log("settingsShoulder " + n(a)); }
    void settingsWheel(float, int a) { log("settingsWheel " + n(a)); }
    void switchLibraryTab(int a) { log("switchLibraryTab " + n(a)); }
    void tabDown(bool r) { log(std::string("tabDown") + (r ? " r" : "")); }
    void tabUp() { log("tabUp"); }
    void toggleFullscreen() { log("toggleFullscreen"); }
    void togglePerformance() { log("togglePerformance"); }
    void togglePrivateCategory() { log("togglePrivateCategory"); }
    void wheel(int a, float x) { log("wheel " + n(a) + " " + f(x)); }
    void whatsNewScroll(float a) { log("whatsNewScroll " + f(a)); }
    void windowActivated(bool a) { log(std::string("windowActivated ") + (a ? "1" : "0")); }
    void windowPlacementChanged() { log("windowPlacementChanged"); }
};

using Calls = std::vector<std::string>;
platform::KeyEvent key(unsigned code, bool repeat = false, platform::Modifiers modifiers = {}) {
    return platform::KeyEvent{code, code, true, repeat, modifiers};
}
// What the launcher did after the modal layer was asked (its "modalKey" line removed).
Calls after(const FakeApp& app) {
    Calls calls;
    for (const auto& call : app.calls) if (!call.starts_with("modalKey")) calls.push_back(call);
    return calls;
}

int keyRouting() {
    window_input::Dispatcher<FakeApp> input;
    {
        FakeApp app; app.takes.insert("modalKey");
        input.keyDown(app, key(keys::escape));
        if (app.calls != Calls{"modalKey 27"}) return 1; // The modal layer took it; nothing else runs.
    }
    {
        FakeApp app; app.takes.insert("desktopKey");
        input.keyDown(app, key(keys::down));
        if (after(app) != Calls{"desktopKey 40"}) return 2;
    }
    {
        FakeApp app;
        input.keyDown(app, key(keys::left));
        input.keyDown(app, key(keys::letter('D')));
        app.on.insert("searching");
        input.keyDown(app, key(keys::letter('A'))); // Text while searching, not a move.
        if (after(app) != Calls{"navigate -1", "navigate 1"}) return 3;
    }
    {
        FakeApp app; app.on = {"settingsOpen"};
        input.keyDown(app, key(keys::down, false, {.shift = true}));
        input.keyDown(app, key(keys::down));
        input.keyDown(app, key(keys::right));
        input.keyDown(app, key(keys::page_up));
        input.keyDown(app, key(keys::f5));
        if (after(app) != Calls{"settingsReorder 1", "changeCategory 1", "settingsAdjust 1", "settingsShoulder -1", "saveSettings", "retryStore"}) return 4;
    }
    {
        FakeApp app; app.on = {"glanceAvailable", "companionAvailable", "tarotAvailable"};
        input.keyDown(app, key(keys::letter('G')));
        input.keyDown(app, key(keys::letter('G'), false, {.control = true})); // Ctrl+G is not the shortcut.
        input.keyDown(app, key(keys::letter('C'), true));                      // Nor a held key.
        input.keyDown(app, key(keys::letter('T'), false, {.alt = true}));
        input.keyDown(app, key(keys::letter('C')));
        if (after(app) != Calls{"openGlance", "openCompanion"}) return 5;
    }
    {
        FakeApp app; app.on = {"quickOpen"};
        input.keyDown(app, key(keys::space));
        input.keyDown(app, key(keys::space, true)); // A held Space accepts once.
        input.keyDown(app, key(keys::escape));
        input.keyDown(app, key(keys::escape, true)); // Back takes one step per press.
        if (after(app) != Calls{"quickAccept", "closeQuick"}) return 6;
    }
    {
        FakeApp app; app.on = {"searching"};
        input.keyDown(app, key(keys::back, true));
        input.keyDown(app, key(keys::space)); // Typed into the search, not an accept.
        if (after(app) != Calls{"eraseSearchCharacter r"}) return 7;
    }
    {
        FakeApp app; app.on = {"homeKeyIsGuide"};
        input.keyDown(app, key(keys::home));
        input.keyUp(app, keys::home);
        input.keyDown(app, key(keys::enter));
        input.keyUp(app, keys::enter);
        input.keyDown(app, key(keys::tab, true));
        input.keyUp(app, keys::tab);
        if (after(app) != Calls{"homeDown", "homeUp", "enterDown", "enterUp", "tabDown r", "tabUp"}) return 8;
    }
    {
        FakeApp app; app.on = {"editingSecret"};
        input.keyDown(app, key(keys::letter('V'), false, {.control = true}));
        input.keyDown(app, key(keys::del));
        if (after(app) != Calls{"pasteSecret", "eraseSecret"}) return 9;
    }
    return 0;
}

int textAndWheel() {
    window_input::Dispatcher<FakeApp> input;
    {
        FakeApp app;
        input.text(app, "a\xC3\xA9"); // "aé": one call per character.
        app.on.insert("powerMenuOpen");
        input.text(app, "b");
        if (app.calls != Calls{"char 97", "char 233"}) return 20;
    }
    {
        // A precision touchpad: 40 + 40 is not a notch yet; the third 40 is one step down.
        FakeApp app;
        input.wheel(app, 10, 20, -40, false);
        input.wheel(app, 10, 20, -40, false);
        if (!app.calls.empty()) return 21;
        input.wheel(app, 10, 20, -40, false);
        if (app.calls != Calls{"wheel 1 10"}) return 22;
        input.wheel(app, 10, 20, 120, false); // Turned the other way: starts over, one step up.
        if (app.calls.back() != "wheel -1 10") return 23;
    }
    {
        FakeApp app; app.modal = FakeApp::Modal::profile;
        input.wheel(app, 0, 0, -120, false);
        app.modal = FakeApp::Modal::library_dialog;
        input.wheel(app, 0, 0, -120, false);
        if (app.calls != Calls{"profileScroll 96", "libraryDialogKey 40"}) return 24;
    }
    {
        FakeApp app; app.takes.insert("desktopWheel"); app.on.insert("desktopEmbedded"); app.embed_offset = 0.75f;
        input.wheel(app, 10, 20, 120, true);
        if (app.calls != Calls{"desktopWheel 10,20 120 ctrl"}) return 25; // Embedded: mapped, whole pixels.
    }
    {
        FakeApp app; app.on.insert("tourOpen");
        input.wheel(app, 0, 0, 120, false);
        if (!app.calls.empty()) return 26;
    }
    return 0;
}

int events() {
    window_input::Dispatcher<FakeApp> input;
    using namespace platform;
    {
        FakeApp app;
        input.handle(app, MouseButtonEvent{MouseButton::left, 5, 6, true, 1});
        input.handle(app, MouseButtonEvent{MouseButton::left, 5, 6, true, 2});
        input.handle(app, MouseButtonEvent{MouseButton::left, 5, 6, true, 3});
        input.handle(app, MouseButtonEvent{MouseButton::left, 7, 8, false, 1});
        input.handle(app, MouseButtonEvent{MouseButton::right, 9, 9, false, 1});
        input.handle(app, MouseMoveEvent{3, 4, 1, 1});
        input.handle(app, MouseLeaveEvent{});
        if (app.calls != Calls{"click 5,6", "click 5,6 double", "click 5,6", "desktopPointerUp 7,8", "pointerUp",
                               "contextClick 9,9", "desktopMaximizeHover 0", "mouseMove 3,4", "mouseLeave"}) return 30;
    }
    {
        FakeApp app; app.on.insert("menuOpen");
        input.handle(app, MouseButtonEvent{MouseButton::back, 0, 0, false, 1}); // The mouse's Back is Esc.
        if (after(app) != Calls{"dismissMenu"}) return 31;
    }
    {
        FakeApp app;
        input.handle(app, WindowFocusEvent{false});
        input.handle(app, WindowFocusEvent{true});
        if (app.calls != Calls{"windowActivated 0", "cancelEnterHold", "cancelTabHold", "pointerUp", "windowActivated 1", "focusRegained"}) return 32;
    }
    {
        FakeApp app; app.on.insert("launchAnimationActive");
        if (!input.handle(app, key(keys::letter('X')))) return 33; // Swallowed while the game starts.
        input.handle(app, key(keys::escape));
        input.handle(app, MouseButtonEvent{MouseButton::left, 1, 1, true, 1});
        input.handle(app, ResizeEvent{100, 100}); // Not input: still handled.
        if (app.calls != Calls{"dismissLaunchAnimation", "resize"}) return 34;
    }
    {
        FakeApp app; app.on.insert("closing");
        if (!input.handle(app, key(keys::escape)) || input.handle(app, ResizeEvent{})) return 35;
        if (!app.calls.empty()) return 36;
    }
    {
        FakeApp app; app.takes.insert("screensaverEvent");
        input.handle(app, key(keys::escape));
        input.handle(app, PlacementEvent{WindowPlacement::maximized});
        input.handle(app, PlacementEvent{WindowPlacement::minimized});
        if (app.calls != Calls{"windowPlacementChanged"}) return 37;
    }
    {
        FakeApp app;
        if (input.handle(app, CloseRequestEvent{})) return 38; // Not refused: the window closes.
        app.takes.insert("requestClose");
        if (!input.handle(app, CloseRequestEvent{})) return 39; // Asked first.
        if (input.handle(app, QuitEvent{}) || input.handle(app, TouchEvent{})) return 40;
        input.handle(app, DisplayScaleEvent{2});
        app.on.insert("coversMonitor");
        input.handle(app, DisplayScaleEvent{1});
        if (app.calls != Calls{"requestClose", "requestClose", "resize", "displayChanged"}) return 41;
    }
    return 0;
}

// SDL events all the way to the launcher's calls.
int fromSdl() {
    window_input::Dispatcher<FakeApp> input;
    FakeApp app; app.on.insert("settingsOpen");
    const auto send = [&](const SDL_Event& event) {
        if (auto translated = platform::translateSdlEvent(event)) input.handle(app, *translated);
    };
    SDL_Event down{}; down.type = SDL_EVENT_KEY_DOWN; down.key.key = SDLK_DOWN; down.key.down = true; down.key.mod = SDL_KMOD_LSHIFT;
    send(down);
    SDL_Event letter{}; letter.type = SDL_EVENT_KEY_DOWN; letter.key.key = SDLK_D; letter.key.down = true;
    send(letter);
    SDL_Event wheel{}; wheel.type = SDL_EVENT_MOUSE_WHEEL; wheel.wheel.y = -1; wheel.wheel.mouse_x = 50; wheel.wheel.mouse_y = 60;
    send(wheel);
    SDL_Event text{}; text.type = SDL_EVENT_TEXT_INPUT; text.text.text = "\xE2\x82\xAC"; // €
    send(text);
    SDL_Event x1{}; x1.type = SDL_EVENT_MOUSE_BUTTON_UP; x1.button.button = SDL_BUTTON_X1;
    app.on = {"categoryMenuOpen"};
    send(x1);
    if (after(app) != Calls{"settingsReorder 1", "settingsAdjust 1", "settingsWheel 1", "char 8364", "closeCategoryMenu"}) return 50;
    return 0;
}

} // namespace

int main() {
    for (const auto test : {keyRouting, textAndWheel, events, fromSdl}) {
        if (const int failure = test()) {
            std::cerr << "window_input failure " << failure << "\n";
            return failure;
        }
    }
    std::cout << "window input routing passed\n";
}
