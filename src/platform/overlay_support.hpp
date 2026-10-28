#pragma once
// Whether the in-game Guide (ui/overlay_window: a topmost, transparent window over a running game)
// can be shown. On Windows it always can. On Linux it is not available yet:
// - Wayland lets no client place a window above another application's, so a global overlay is
//   impossible there (only the compositor could draw one).
// - gamescope (Steam Deck, Steam Big Picture sessions) shows X11 windows that carry its
//   GAMESCOPE_EXTERNAL_OVERLAY property above the game, as MangoHud's mangoapp does; that is the
//   possible route on Linux, not implemented yet.
// - A plain X11 session could stack an override-redirect window over windowed and borderless games,
//   but not over full-screen ones the compositor unredirects; not implemented either.
// Launcher::openGameOverlay asks available() first, so on Linux the Guide button opens the
// launcher's own window instead.
#include <cstdlib>
#include <string_view>

namespace overlay_support {

enum class Session { windows, wayland, x11, gamescope, none };

// From the session's environment (WAYLAND_DISPLAY, DISPLAY, GAMESCOPE_WAYLAND_DISPLAY, XDG_SESSION_TYPE).
inline Session session(std::string_view wayland_display, std::string_view x11_display, std::string_view gamescope_display,
                       std::string_view session_type) {
    if (!gamescope_display.empty()) return Session::gamescope;
    if (!wayland_display.empty() || session_type == "wayland") return Session::wayland;
    if (!x11_display.empty() || session_type == "x11") return Session::x11;
    return Session::none;
}

inline Session currentSession() {
#ifdef _WIN32
    return Session::windows;
#else
    const auto variable = [](const char* name) { const char* value = std::getenv(name); return std::string_view(value ? value : ""); };
    return session(variable("WAYLAND_DISPLAY"), variable("DISPLAY"), variable("GAMESCOPE_WAYLAND_DISPLAY"), variable("XDG_SESSION_TYPE"));
#endif
}

constexpr bool available(Session session) { return session == Session::windows; }
inline bool available() { return available(currentSession()); }

// For the log, in English: why there is no Guide over games.
constexpr std::string_view unavailableReason(Session session) {
    switch (session) {
    case Session::windows: return {};
    case Session::wayland: return "Wayland does not let an application show a window over another application's";
    case Session::gamescope: return "the gamescope overlay (GAMESCOPE_EXTERNAL_OVERLAY) is not implemented yet";
    case Session::x11: return "an X11 overlay window is not implemented yet";
    case Session::none: return "no display server";
    }
    return {};
}

} // namespace overlay_support
