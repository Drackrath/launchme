# Linux port

Launchme is being ported to run natively on Linux. It is not a Wine build: the same sources
build for both systems, with Windows code kept behind `#ifdef _WIN32` or in per-platform files. The
Windows build is unchanged in behaviour.

## Status

| Part | State |
| --- | --- |
| Build (`cmake/Linux.cmake`) | Configures with CMake + Ninja, Clang or GCC, C++20 without GNU extensions |
| Shared core (`launchme_core`) | All shared sources build on Linux or have a Linux counterpart, except `artwork/platform_logo.cpp` (Direct2D) |
| Platform services (phase 2) | Audio (SDL3), decoding and trailers (FFmpeg), system volume (PulseAudio/PipeWire), controllers (SDL3, hidraw, BlueZ), processes (`/proc`, signals), power (logind), sensors (hwmon), memory, Discord, Steam downloads |
| Stores | Steam (native, Flatpak, Snap), Heroic (Epic and GOG, launched through its legendary/gogdl backends with the game's Wine/Proton settings), Lutris, Prism Launcher/PolyMC/MultiMC, Battle.net in a Lutris prefix |
| Tests | 59 CTest suites build and pass |
| Launcher window | SDL3 library browser with keyboard/gamepad navigation and selected-game details, on `SdlWindow`, its events and `FrameClock` |
| Window and input (phase 3) | One event model for both systems (`platform/window_events.hpp`); the Launcher's key, text, wheel, click and focus routing shared by the Win32 window procedure and the SDL window (`app/window_input.hpp`); frame clock, layered surface and file dialogs (xdg-desktop-portal) on Linux; no in-game Guide on Linux |
| Renderer, web view | Not started (phase 3 item 2, phase 4) |

## Building on Linux

Packages (Arch names): `cmake ninja clang pkgconf icu openssl libsecret curl sdl3 sdl3_ttf ffmpeg libpulse
libpng libjpeg-turbo libwebp expat sqlite systemd-libs glib2`. Optional at run time: `tesseract` (text-aware
cover crops), `steam-devices` (udev rules for the Steam Controller's hidraw access).

```sh
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/linux
./build/linux/launchme
ctest --test-dir build/linux --output-on-failure
```

The SDL3 launcher opens a native window, discovers installed Steam games, and shows the
selected game's basic details. Use the arrow keys or a gamepad D-pad to navigate; **Escape**
or the gamepad Back button closes the window. SDL, SDL_ttf, renderer, and font failures are
reported on stderr.

The data folder is `$XDG_DATA_HOME/launchme` (normally `~/.local/share/launchme`); `LAUNCHME_CONFIG_DIR`
still overrides it.

## Conventions for portable code

- **Wide strings.** `wchar_t` is UTF-16 on Windows and UTF-32 on Linux. Convert only through
  `platform/text_encoding.hpp`; never assume surrogate pairs or a 2-byte `wchar_t`.
- **Paths.** Use `path.wstring()`, not `path.native()` (on Linux `native()` is a narrow `std::string`).
  Pass `std::filesystem::path` to `std::ifstream`/`std::ofstream`, never a `std::wstring`.
- **Text comparison, case and normalization.** `platform/unicode_text.hpp`: NLS on Windows, ICU elsewhere
  (`compareNatural`, `equalIgnoreCase`, `lower`, `decompose`, `isLetterOrDigit`, `isMark`, …). Replaces
  `CompareStringEx`, `CompareStringOrdinal`, `LCMapStringEx`, `NormalizeString`, `FoldStringW`,
  `GetStringTypeW`, `IsCharAlphaNumericW` and `_wcsicmp`.
- **Process and environment.** `platform/system.hpp`: `environment()`, `processId()`, `executablePath()`,
  `randomBytes()`, `localTime()`.
- **Files.** `platform/atomic_file.hpp`: atomic writes, `replace()` (instead of `MoveFileExW`) and
  `ExclusiveLock` (instead of an unshared `CreateFileW`).
- **Encrypted files.** `platform/protected_file.hpp`: DPAPI on Windows. On Linux, AES-256-GCM with a random
  key kept in the desktop keyring (Secret Service, via libsecret), or in a `0600` `protected.key` in the data
  folder when no keyring answers. Files are not portable between systems (the same as DPAPI files between
  accounts).
- **Crash reports.** `platform/diagnostics.hpp`: minidumps on Windows; on Linux a signal handler writes
  `crashes/launchme-<time>.txt` with the signal and a backtrace.
- **MSVC CRT extensions** (`localtime_s`, `swprintf_s`, `_wtoi`, …) and a few Win32 helpers that have an
  exact POSIX equivalent (`SecureZeroMemory`, `GetTickCount64`, `Sleep`) come from
  `platform/crt_compat.hpp`, which the Linux build force-includes. Anything with platform behaviour gets a
  real port, not a shim. Note that in `swprintf` on Linux `%s` takes a narrow string; use `%ls` for
  `wchar_t*`. MSVC accepts `%ls` too.
- **Windows headers** in shared files are wrapped in `#ifdef _WIN32`.
- **Windows and keys.** Window-bound code takes `platform::NativeWindow` (`platform/native_window.hpp`:
  `HWND` on Windows, `SDL_Window*` on Linux). Key handling keeps using virtual-key values; portable
  code names them through `keys::` (`platform/key_codes.hpp`, checked against `VK_*` on Windows), and
  window input is described by `platform/window_events.hpp`.
- **Tests** may use `GetCurrentProcessId`, `GetEnvironmentVariableW` and similar through
  `tests/linux_compat/Windows.h`, a stand-in that is on the include path of test targets only.

## Plan

### Phases 1 and 2: shared core and platform services (done)

Every shared source builds on Linux; Windows code stays behind `#ifdef _WIN32` or in `*_windows.cpp`
files (`http_winhttp`, `image_codec_wic`, `audio_windows`, `system_audio_windows`,
`video_player_windows`, `game_controllers_windows`, `steam_controller_windows`,
`bluetooth_controllers_windows`, `device_service_windows`) with Linux counterparts (`http_curl`,
`image_codec_linux`, `audio_sdl`, `flac_ffmpeg`, `video_player_ffmpeg`, `system_audio_pulse`,
`*_linux.cpp`, `proc_fs`, `process_spawn`, `bluez`, `linux_hid`). New shared helpers: `image_codec`,
`hashing`, `embedded_resources` (companion pictures compiled in on Linux, RCDATA on Windows),
`pe_version` (reads `.exe` version resources without Win32). The Windows side of these moves has not
been compiled yet.

Known gaps on Linux:
- Windows programs (`.exe`) are not run: no Wine integration outside Heroic's per-game settings. GOG
  offline installers report that they are unavailable.
- No FPS reading (PresentMon has no Linux equivalent); no window focus or game-window lookup (Wayland).
- Heroic: Amazon and sideloaded games; Lutris: games without a store; Heroic's AppImage hides its
  backends (games then launch through `heroic://`).
- Images: GIF, TIFF, ICO, RLE BMP and animated WebP are not decoded; `.lnk` shortcuts are not resolved.
- Flatpak Steam: the account switcher and Steam library sync expect the native layout.
- Updates: only as an AppImage (`launchme-<version>-x86_64.AppImage` release asset with a SHA-256).
- Some messages still name Windows (no new translation keys yet).
- No in-game Guide over running games (phase 3, item 3).

### Phase 3: window, input and renderer

This is the largest phase: 48 files in `src/ui` and `src/app` call Direct2D and DirectWrite directly
(`ID2D1RenderTarget`, geometries, effects, `IDWriteTextLayout`), and about 50 use `HWND`.

1. **Window and input** (largely done; branch `linux-port-window`):
   - `platform/window_events.hpp`: one event model for both systems — keys as Windows virtual-key
     values (`platform/key_codes.hpp`, `keys::`) with Shift/Ctrl/Alt, committed text (UTF-8) and IME
     composition, mouse moves/leave, buttons with click counts, the wheel in 1/120 notches, touch,
     gamepad buttons, focus, resize, placement (maximized/restored/minimized/full screen), display
     scale (DPI), display changes, close requests, dropped files, and `HitArea` for desktop mode's own
     top bar.
   - `platform/sdl_window`: translates SDL3 events into it (`translateSdlEvent`, `virtualKey`) and
     wraps the window: `native()`, logical and pixel size, `displayScale()`, text input on/off and the
     IME candidate area, system cursors, hit testing (`SDL_SetWindowHitTest`), maximize/restore/full
     screen. The SDL 2D renderer is optional (`SdlWindowConfig::renderer`).
   - `app/window_input.hpp`: the Launcher's routing of keys, typed text, the wheel (with the
     touchpad accumulation), clicks, mouse moves and focus, moved out of `main.cpp`'s window procedure.
     It is a template over the application: `main.cpp` instantiates it with `Launcher` and calls it
     from `WM_KEYDOWN`, `WM_CHAR`, `WM_KEYUP`, `WM_MOUSEWHEEL`, `WM_[LR]BUTTON*`, `WM_MOUSEMOVE`,
     `WM_MOUSELEAVE`, `WM_CAPTURECHANGED`, `WM_SETFOCUS`/`WM_KILLFOCUS` with the values it used to pass
     (modifiers from `GetKeyState` at the same point), so Windows behaves as before. On Linux,
     `Dispatcher::handle(app, event)` takes every SDL event and also applies what the window procedure
     does before its switch (closing, screensaver, launch animation); the mouse's Back button is Esc,
     as on Windows. `tests/window_input_tests.cpp` drives it with a recording stand-in, from SDL events
     too.
   - `platform/native_window.hpp`: `platform::NativeWindow` is `HWND` on Windows and `SDL_Window*` on
     Linux; window-bound helpers take it.
   - `frame_clock`: `frame_clock_sdl.cpp` takes the refresh rate of the window's display, 5 Hz while
     minimized, hidden or occluded, 30 Hz without focus, and waits with `SDL_WaitEventTimeout` (wakes on
     input) plus `SDL_DelayPrecise`. Work that must wake the loop early posts an SDL event.
   - `file_dialog`: `file_dialog_sdl.cpp` shows every picker through
     `SDL_ShowFileDialogWithProperties` (the xdg-desktop-portal FileChooser; zenity without a portal) and
     waits for the answer while pumping SDL events, so it is modal as on Windows. Game files also offer
     `.sh`, `.desktop` and `.AppImage`, then every file.
   - The interim `src/linux/main.cpp` runs on `SdlWindow`, its events and `FrameClock`.
   - The Windows side of these changes (`main.cpp`, `game_overlay.cpp`, the headers and the renamed
     `file_dialog_windows`, `frame_clock_windows`, `layered_surface_windows`) has not been compiled yet.

   Remaining, once the Launcher itself builds on Linux (it needs the renderer): the SDL main loop
   calling `Dispatcher::handle`, `Launcher::screensaverEvent` (the portable twin of
   `screensaverInput`, which reads `screensaver::Gate` the same way), text input switched on with
   the search fields and the IME area at the caret, the cursor shapes (`desktopCursor`/`hoverButton`
   to `SdlWindow::setCursor`), desktop mode's hit testing (`desktopNonClientHit` returns `HT*` values;
   map to `HitArea`), worker messages posted as SDL user events instead of `PostMessage`
   (`message_dispatch.hpp`), drops (`DropFileEvent` instead of `HDROP`), touch, the single-instance
   handoff and Jump List commands (`WM_COPYDATA`; a socket or D-Bus on Linux), session end and display
   power (logind), and the notification-area icon while a game runs (`tray.cpp`;
   StatusNotifierItem). `desktop_window.cpp`, `pointer.cpp`, `keyboard.cpp` and the rest of
   `app/` still call Win32 (`InvalidateRect`, `SetWindowPos`, `GetKeyState`, ...) and move with the
   Launcher.
   
   **For the renderer:** a window's native surface is `platform::NativeWindow` — `HWND` for Direct2D,
   `SDL_Window*` for a Skia backend (GL/Vulkan surface from the window, or raster into
   `SDL_GetWindowSurface`). Create the `SdlWindow` with `renderer = false` when the Canvas presents by
   itself. Layout units on Linux are logical pixels; `SdlWindow::displayScale()` and `pixelSize()` give
   the backing scale. `LayeredSurface` keeps `dc()` on Windows; on Linux it offers `pixels()`/`stride()`
   (premultiplied BGRA) and `present(NativeWindow)`.
2. **Renderer**: introduce a `Canvas` interface that covers the Direct2D subset Launchme actually uses (rects,
   rounded rects, paths, linear and radial gradients, bitmaps with opacity masks, blur and shadow, clip
   layers, text layouts). Port the Windows renderer onto it first, with identical pixels (the UI tests
   compare against reference renders), then add a **Skia** backend (prebuilt Skia, HarfBuzz shaping,
   Fontconfig fonts). `ui/image_decoder.cpp` moves to Skia's codecs.
3. **Layered and overlay windows** (done for now):
   - `layered_surface_sdl.cpp`: the transparent-window backing store is plain premultiplied BGRA
     memory, presented by copying it unblended into the SDL window's surface (a window created with
     `SDL_WINDOW_TRANSPARENT` keeps its alpha). The Windows DIB section is unchanged
     (`layered_surface_windows.cpp`).
   - The in-game Guide over a running game (`ui/overlay_window.cpp`) is **unavailable on Linux**:
     `platform/overlay_support.hpp` says so at run time and `Launcher::openGameOverlay` asks it first
     (always available on Windows), so the Guide button brings up the launcher's own window instead.
     Wayland lets no client put a window above another application's. Possible later routes:
     gamescope (Steam Deck, Big Picture sessions) shows an X11 window that carries
     `GAMESCOPE_EXTERNAL_OVERLAY` above the game, as mangoapp does; on a plain X11 session an
     override-redirect window can cover windowed and borderless games, not unredirected full-screen
     ones. `ui/overlay_window.cpp` itself is not built on Linux (it owns a Direct2D `Renderer`).

### Phase 4: web view

WebView2 (`ui/browser_window.cpp`, store sign-ins and pages) → WebKitGTK 6.0 in its own GTK4 window. GTK's
main loop runs alongside SDL's; cookies and storage go in the data folder.

### Phase 5: packaging

An AppImage first (self-updating through the existing GitHub release check), then a Flatpak. Add a `.desktop`
file, an AppStream metainfo file and icons from `res/`.
