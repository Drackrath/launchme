// SDL3 events as the window messages the launcher handles: keys (virtual-key codes, repeat bit),
// text input (WM_CHAR), the mouse (client pixels, double clicks, wheel notches, leave, the custom
// caption's maximize button), window state, focus, dropped files and the session's end.
#include "window_internal.hpp"
#include "../../platform/text_encoding.hpp"

#include <algorithm>
#include <cmath>

using namespace lm_window::detail;

namespace {
Window* windowById(SDL_WindowID id) {
    auto& s = state();
    std::lock_guard lock(s.windows_mutex);
    const auto found = s.by_id.find(id);
    return found == s.by_id.end() || found->second->destroyed ? nullptr : found->second;
}

UINT virtualKey(const SDL_KeyboardEvent& key) {
    const SDL_Keycode code = key.key;
    if (code >= 'a' && code <= 'z') return static_cast<UINT>(code - 'a' + 'A');
    if (code >= '0' && code <= '9') return static_cast<UINT>(code);
    // Letters of other layouts (ą, ł, ö...) answer by their physical key, as Windows' VK does.
    if (key.scancode >= SDL_SCANCODE_A && key.scancode <= SDL_SCANCODE_Z && code >= 0x80 && !(code & SDLK_SCANCODE_MASK))
        return static_cast<UINT>('A' + (key.scancode - SDL_SCANCODE_A));
    switch (code) {
    case SDLK_RETURN: case SDLK_KP_ENTER: return VK_RETURN;
    case SDLK_ESCAPE: return VK_ESCAPE;
    case SDLK_BACKSPACE: return VK_BACK;
    case SDLK_TAB: return VK_TAB;
    case SDLK_SPACE: return VK_SPACE;
    case SDLK_LEFT: return VK_LEFT;
    case SDLK_RIGHT: return VK_RIGHT;
    case SDLK_UP: return VK_UP;
    case SDLK_DOWN: return VK_DOWN;
    case SDLK_HOME: return VK_HOME;
    case SDLK_END: return VK_END;
    case SDLK_PAGEUP: return VK_PRIOR;
    case SDLK_PAGEDOWN: return VK_NEXT;
    case SDLK_INSERT: return VK_INSERT;
    case SDLK_DELETE: return VK_DELETE;
    case SDLK_LSHIFT: case SDLK_RSHIFT: return VK_SHIFT;
    case SDLK_LCTRL: case SDLK_RCTRL: return VK_CONTROL;
    case SDLK_LALT: case SDLK_RALT: return VK_MENU;
    case SDLK_LGUI: return VK_LWIN;
    case SDLK_RGUI: return VK_RWIN;
    case SDLK_APPLICATION: case SDLK_MENU: return VK_APPS;
    case SDLK_CAPSLOCK: return VK_CAPITAL;
    case SDLK_NUMLOCKCLEAR: return VK_NUMLOCK;
    case SDLK_SCROLLLOCK: return VK_SCROLL;
    case SDLK_PAUSE: return VK_PAUSE;
    case SDLK_PRINTSCREEN: return VK_SNAPSHOT;
    case SDLK_F1: return VK_F1; case SDLK_F2: return VK_F2; case SDLK_F3: return VK_F3; case SDLK_F4: return VK_F4;
    case SDLK_F5: return VK_F5; case SDLK_F6: return VK_F6; case SDLK_F7: return VK_F7; case SDLK_F8: return VK_F8;
    case SDLK_F9: return VK_F9; case SDLK_F10: return VK_F10; case SDLK_F11: return VK_F11; case SDLK_F12: return VK_F12;
    case SDLK_KP_0: return VK_NUMPAD0; case SDLK_KP_1: return VK_NUMPAD1; case SDLK_KP_2: return VK_NUMPAD2;
    case SDLK_KP_3: return VK_NUMPAD3; case SDLK_KP_4: return VK_NUMPAD4; case SDLK_KP_5: return VK_NUMPAD5;
    case SDLK_KP_6: return VK_NUMPAD6; case SDLK_KP_7: return VK_NUMPAD7; case SDLK_KP_8: return VK_NUMPAD8;
    case SDLK_KP_9: return VK_NUMPAD9;
    case SDLK_KP_PLUS: return VK_ADD;
    case SDLK_KP_MINUS: return VK_SUBTRACT;
    case SDLK_KP_MULTIPLY: return VK_MULTIPLY;
    case SDLK_KP_DIVIDE: return VK_DIVIDE;
    case SDLK_KP_PERIOD: return VK_DECIMAL;
    case SDLK_PLUS: case SDLK_EQUALS: return VK_OEM_PLUS;
    case SDLK_MINUS: return VK_OEM_MINUS;
    case SDLK_COMMA: return VK_OEM_COMMA;
    case SDLK_PERIOD: return VK_OEM_PERIOD;
    case SDLK_SEMICOLON: return VK_OEM_1;
    case SDLK_SLASH: return VK_OEM_2;
    case SDLK_GRAVE: return VK_OEM_3;
    case SDLK_LEFTBRACKET: return VK_OEM_4;
    case SDLK_BACKSLASH: return VK_OEM_5;
    case SDLK_RIGHTBRACKET: return VK_OEM_6;
    case SDLK_APOSTROPHE: return VK_OEM_7;
    case SDLK_AC_BACK: return VK_BROWSER_BACK;
    case SDLK_AC_FORWARD: return VK_BROWSER_FORWARD;
    case SDLK_MUTE: return VK_VOLUME_MUTE;
    case SDLK_VOLUMEDOWN: return VK_VOLUME_DOWN;
    case SDLK_VOLUMEUP: return VK_VOLUME_UP;
    case SDLK_MEDIA_NEXT_TRACK: return VK_MEDIA_NEXT_TRACK;
    case SDLK_MEDIA_PREVIOUS_TRACK: return VK_MEDIA_PREV_TRACK;
    case SDLK_MEDIA_PLAY_PAUSE: case SDLK_MEDIA_PLAY: return VK_MEDIA_PLAY_PAUSE;
    case SDLK_MEDIA_STOP: return VK_MEDIA_STOP;
    default: return 0;
    }
}

void setKey(UINT key, bool down) {
    auto& s = state();
    if (key >= s.keys.size()) return;
    if (down && !s.keys[key]) s.toggled[key] = !s.toggled[key];
    s.keys[key] = down;
}

WPARAM mouseKeys() {
    const SDL_MouseButtonFlags buttons = SDL_GetMouseState(nullptr, nullptr);
    WPARAM flags = 0;
    if (buttons & SDL_BUTTON_LMASK) flags |= MK_LBUTTON;
    if (buttons & SDL_BUTTON_RMASK) flags |= MK_RBUTTON;
    if (buttons & SDL_BUTTON_MMASK) flags |= MK_MBUTTON;
    if (buttons & SDL_BUTTON_X1MASK) flags |= MK_XBUTTON1;
    if (buttons & SDL_BUTTON_X2MASK) flags |= MK_XBUTTON2;
    const SDL_Keymod mods = SDL_GetModState();
    if (mods & SDL_KMOD_SHIFT) flags |= MK_SHIFT;
    if (mods & SDL_KMOD_CTRL) flags |= MK_CONTROL;
    return flags;
}

POINT clientPoint(Window* target, float x, float y) {
    const float d = pixelDensity(target);
    return {static_cast<LONG>(std::lround(x * d)), static_cast<LONG>(std::lround(y * d))};
}

// The launcher's caption (desktop mode): which part a point is.
LRESULT nonClientHit(Window* target, POINT client) {
    if (!target->custom_frame) return HTCLIENT;
    POINT screen = client;
    ClientToScreen(static_cast<HWND>(target), &screen);
    return send(target, WM_NCHITTEST, 0, MAKELPARAM(screen.x, screen.y));
}

void mouseButton(const SDL_MouseButtonEvent& button) {
    Window* target = windowById(button.windowID);
    if (!target) return;
    const HWND handle = static_cast<HWND>(target);
    const POINT point = clientPoint(target, button.x, button.y);
    const LPARAM where = MAKELPARAM(point.x, point.y);
    const bool down = button.down;
    if (button.button == SDL_BUTTON_LEFT && nonClientHit(target, point) == HTMAXBUTTON) {
        POINT screen = point;
        ClientToScreen(handle, &screen);
        pushInput(handle, down ? (button.clicks >= 2 ? WM_NCLBUTTONDBLCLK : WM_NCLBUTTONDOWN) : WM_NCLBUTTONUP, HTMAXBUTTON, MAKELPARAM(screen.x, screen.y));
        return;
    }
    const bool doubled = down && button.clicks >= 2 && (button.clicks % 2) == 0 && (target->type->style & CS_DBLCLKS);
    UINT message = 0;
    WPARAM wparam = mouseKeys();
    switch (button.button) {
    case SDL_BUTTON_LEFT: message = down ? (doubled ? WM_LBUTTONDBLCLK : WM_LBUTTONDOWN) : WM_LBUTTONUP; break;
    case SDL_BUTTON_RIGHT: message = down ? (doubled ? WM_RBUTTONDBLCLK : WM_RBUTTONDOWN) : WM_RBUTTONUP; break;
    case SDL_BUTTON_MIDDLE: message = down ? (doubled ? WM_MBUTTONDBLCLK : WM_MBUTTONDOWN) : WM_MBUTTONUP; break;
    case SDL_BUTTON_X1: case SDL_BUTTON_X2:
        message = down ? WM_XBUTTONDOWN : WM_XBUTTONUP;
        wparam = MAKEWPARAM(static_cast<WORD>(wparam), button.button == SDL_BUTTON_X1 ? XBUTTON1 : XBUTTON2);
        break;
    default: return;
    }
    if (button.button == SDL_BUTTON_LEFT) setKey(VK_LBUTTON, down);
    if (button.button == SDL_BUTTON_RIGHT) setKey(VK_RBUTTON, down);
    pushInput(handle, message, wparam, where);
    // A right click releases into the context menu, as Windows follows WM_RBUTTONUP with it.
    if (button.button == SDL_BUTTON_RIGHT && !down) {
        POINT screen = point;
        ClientToScreen(handle, &screen);
        pushInput(handle, WM_CONTEXTMENU, reinterpret_cast<WPARAM>(handle), MAKELPARAM(screen.x, screen.y));
    }
}

void mouseMotion(const SDL_MouseMotionEvent& motion) {
    Window* target = windowById(motion.windowID);
    if (!target) return;
    const HWND handle = static_cast<HWND>(target);
    const POINT point = clientPoint(target, motion.x, motion.y);
    if (point.x == target->last_mouse.x && point.y == target->last_mouse.y) return;
    target->last_mouse = point;
    // The cursor shape the window asks for, then the move (WM_SETCURSOR precedes WM_MOUSEMOVE).
    const LRESULT hit = nonClientHit(target, point);
    if (hit == HTMAXBUTTON || (target->custom_frame && hit != HTCLIENT)) {
        POINT screen = point;
        ClientToScreen(handle, &screen);
        target->nonclient_hover = true;
        pushInput(handle, WM_NCMOUSEMOVE, static_cast<WPARAM>(hit), MAKELPARAM(screen.x, screen.y));
        return;
    }
    if (target->nonclient_hover) { target->nonclient_hover = false; pushInput(handle, WM_NCMOUSELEAVE, 0, 0); }
    pushInput(handle, WM_SETCURSOR, reinterpret_cast<WPARAM>(handle), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
    pushInput(handle, WM_MOUSEMOVE, mouseKeys(), MAKELPARAM(point.x, point.y));
}

void mouseWheel(const SDL_MouseWheelEvent& wheel) {
    Window* target = windowById(wheel.windowID);
    if (!target) return;
    const HWND handle = static_cast<HWND>(target);
    const float flip = wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
    // Whole and fractional notches as WHEEL_DELTA units (precision touchpads send fractions).
    target->wheel_y += wheel.y * flip * WHEEL_DELTA;
    target->wheel_x += wheel.x * flip * WHEEL_DELTA;
    POINT screen = clientPoint(target, wheel.mouse_x, wheel.mouse_y);
    ClientToScreen(handle, &screen);
    const auto emit = [&](float& amount, UINT message) {
        const auto delta = static_cast<int>(amount);
        if (!delta) return;
        amount -= static_cast<float>(delta);
        pushInput(handle, message, MAKEWPARAM(static_cast<WORD>(mouseKeys()), static_cast<WORD>(static_cast<short>(delta))), MAKELPARAM(screen.x, screen.y));
    };
    emit(target->wheel_y, WM_MOUSEWHEEL);
    emit(target->wheel_x, WM_MOUSEHWHEEL);
}

void keyboard(const SDL_KeyboardEvent& key) {
    Window* target = windowById(key.windowID);
    if (!target) target = static_cast<Window*>(state().focus);
    if (!target) return;
    const UINT code = virtualKey(key);
    if (!code) return;
    setKey(code, key.down);
    if (code == VK_SHIFT) setKey(key.key == SDLK_LSHIFT ? VK_LSHIFT : VK_RSHIFT, key.down);
    if (code == VK_CONTROL) setKey(key.key == SDLK_LCTRL ? VK_LCONTROL : VK_RCONTROL, key.down);
    if (code == VK_MENU) setKey(key.key == SDLK_LALT ? VK_LMENU : VK_RMENU, key.down);
    // lparam: repeat count 1, scan code, bit 30 = was down (a held key), bit 31 = released.
    LPARAM lparam = 1 | (static_cast<LPARAM>(key.scancode & 0xFF) << 16);
    if (key.repeat) lparam |= 1LL << 30;
    if (!key.down) lparam |= (1LL << 30) | (1LL << 31);
    const bool alt = (SDL_GetModState() & SDL_KMOD_ALT) && code != VK_MENU && !(SDL_GetModState() & SDL_KMOD_CTRL);
    // Alt+F4 closes the window, as the system menu does on Windows.
    if (key.down && code == VK_F4 && alt) { pushInput(static_cast<HWND>(target), WM_CLOSE, 0, 0); return; }
    pushInput(static_cast<HWND>(target), key.down ? (alt ? WM_SYSKEYDOWN : WM_KEYDOWN) : (alt ? WM_SYSKEYUP : WM_KEYUP), code, lparam);
}

void text(const SDL_TextInputEvent& input) {
    Window* target = windowById(input.windowID);
    if (!target || !input.text) return;
    for (const wchar_t character : text_encoding::wideLenient(input.text)) {
        if (character < 0x20 || character == 0x7F) continue; // Control characters come from their keys.
        pushInput(static_cast<HWND>(target), WM_CHAR, static_cast<WPARAM>(character), 1);
    }
}

void windowEvent(const SDL_WindowEvent& event) {
    Window* target = windowById(event.windowID);
    if (!target) return;
    auto& s = state();
    const HWND handle = static_cast<HWND>(target);
    const auto size = [&](WPARAM kind) {
        int width = 0, height = 0;
        SDL_GetWindowSizeInPixels(target->sdl, &width, &height);
        target->invalid = true;
        pushInput(handle, WM_SIZE, kind, MAKELPARAM(width, height));
    };
    switch (event.type) {
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: size(IsZoomed(handle) ? SIZE_MAXIMIZED : SIZE_RESTORED); break;
    case SDL_EVENT_WINDOW_MAXIMIZED: size(SIZE_MAXIMIZED); break;
    case SDL_EVENT_WINDOW_RESTORED: size(SIZE_RESTORED); break;
    case SDL_EVENT_WINDOW_MINIMIZED: pushInput(handle, WM_SIZE, SIZE_MINIMIZED, 0); break;
    case SDL_EVENT_WINDOW_MOVED: pushInput(handle, WM_MOVE, 0, MAKELPARAM(event.data1, event.data2)); break;
    case SDL_EVENT_WINDOW_EXPOSED: case SDL_EVENT_WINDOW_SHOWN: target->invalid = true; break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        s.focus = handle;
        SDL_StartTextInput(target->sdl);
        pushInput(handle, WM_ACTIVATE, WA_ACTIVE, 0);
        pushInput(handle, WM_SETFOCUS, 0, 0);
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        if (s.focus == handle) s.focus = nullptr;
        s.keys.fill(false); // Keys released elsewhere never reach this window.
        pushInput(handle, WM_ACTIVATE, WA_INACTIVE, 0);
        pushInput(handle, WM_KILLFOCUS, 0, 0);
        break;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        target->last_mouse = POINT{-1, -1};
        if (target->nonclient_hover) { target->nonclient_hover = false; pushInput(handle, WM_NCMOUSELEAVE, 0, 0); }
        if (target->track_leave) { target->track_leave = false; pushInput(handle, WM_MOUSELEAVE, 0, 0); }
        break;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED: pushInput(handle, WM_CLOSE, 0, 0); break;
    case SDL_EVENT_WINDOW_DISPLAY_CHANGED: pushInput(handle, WM_DISPLAYCHANGE, 32, 0); break;
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED: {
        RECT bounds{};
        GetWindowRect(handle, &bounds);
        static thread_local RECT suggested;
        suggested = bounds;
        const UINT dpi = GetDpiForWindow(handle);
        pushInput(handle, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&suggested));
        break;
    }
    default: break;
    }
}

void drop(const SDL_DropEvent& event) {
    auto& s = state();
    Window* target = windowById(event.windowID);
    switch (event.type) {
    case SDL_EVENT_DROP_BEGIN: s.dropping.clear(); s.drop_target = target ? static_cast<HWND>(target) : nullptr; break;
    case SDL_EVENT_DROP_FILE: if (event.data) s.dropping.push_back(text_encoding::wideLenient(event.data)); break;
    case SDL_EVENT_DROP_COMPLETE:
        if (s.drop_target && !s.dropping.empty())
            if (Window* receiver = window(s.drop_target); receiver && receiver->accept_files) {
                auto* files = new std::vector<std::wstring>(std::move(s.dropping));
                pushInput(s.drop_target, WM_DROPFILES, reinterpret_cast<WPARAM>(files), 0);
            }
        s.dropping.clear();
        s.drop_target = nullptr;
        break;
    default: break;
    }
}
}

namespace lm_window::detail {
void translate(const SDL_Event& event) {
    auto& s = state();
    if (event.type == s.wake_event) { s.wake_pending = false; return; }
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP: keyboard(event.key); break;
    case SDL_EVENT_TEXT_INPUT: text(event.text); break;
    case SDL_EVENT_MOUSE_MOTION: mouseMotion(event.motion); break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP: mouseButton(event.button); break;
    case SDL_EVENT_MOUSE_WHEEL: mouseWheel(event.wheel); break;
    case SDL_EVENT_DROP_BEGIN: case SDL_EVENT_DROP_FILE: case SDL_EVENT_DROP_COMPLETE: drop(event.drop); break;
    case SDL_EVENT_QUIT: case SDL_EVENT_TERMINATING: {
        // The session ends (logout, SIGTERM): every window saves, then the loop ends.
        std::vector<HWND> windows;
        {
            std::lock_guard lock(s.windows_mutex);
            for (Window* target : s.live) windows.push_back(static_cast<HWND>(target));
        }
        for (const HWND handle : windows) {
            if (!window(handle)) continue;
            SendMessageW(handle, WM_QUERYENDSESSION, 0, 0);
            SendMessageW(handle, WM_ENDSESSION, TRUE, 0);
        }
        for (const HWND handle : windows) if (window(handle)) DestroyWindow(handle);
        PostQuitMessage(0);
        break;
    }
    case SDL_EVENT_DISPLAY_ADDED: case SDL_EVENT_DISPLAY_REMOVED: case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED: {
        std::lock_guard lock(s.windows_mutex);
        for (Window* target : s.live) pushInput(static_cast<HWND>(target), WM_DISPLAYCHANGE, 32, 0);
        break;
    }
    // A pad or an audio device came or went: WM_DEVICECHANGE's DBT_DEVNODES_CHANGED, which rescans
    // the controllers and the headset (Launcher::devicesChanged).
    case SDL_EVENT_GAMEPAD_ADDED: case SDL_EVENT_GAMEPAD_REMOVED: case SDL_EVENT_JOYSTICK_ADDED: case SDL_EVENT_JOYSTICK_REMOVED:
    case SDL_EVENT_AUDIO_DEVICE_ADDED: case SDL_EVENT_AUDIO_DEVICE_REMOVED: {
        std::lock_guard lock(s.windows_mutex);
        for (Window* target : s.live)
            if (!target->parent && !std::any_of(s.input.begin(), s.input.end(), [&](const MSG& queued) { return queued.hwnd == target && queued.message == WM_DEVICECHANGE; }))
                pushInput(static_cast<HWND>(target), WM_DEVICECHANGE, 0x0007 /* DBT_DEVNODES_CHANGED */, 0);
        break;
    }
    case SDL_EVENT_DID_ENTER_FOREGROUND: break;
    default:
        if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST) windowEvent(event.window);
        break;
    }
}
}
