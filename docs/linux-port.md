# Linux port

Launchme is being ported to run natively on Linux. It is not a Wine build: the same sources
build for both systems, with Windows code kept behind `#ifdef _WIN32` or in per-platform files. The
Windows build is unchanged in behaviour.

## Status

| Part | State |
| --- | --- |
| Build (`cmake/Linux.cmake`) | Configures with CMake + Ninja, Clang or GCC, C++20 without GNU extensions |
| Shared core (`launchme_core`) | All shared sources build on Linux or have a Linux counterpart, except `artwork/platform_logo.cpp` (Direct2D) and the window-bound `file_dialog`, `frame_clock` and `layered_surface` (phase 3) |
| Platform services (phase 2) | Audio (SDL3), decoding and trailers (FFmpeg), system volume (PulseAudio/PipeWire), controllers (SDL3, hidraw, BlueZ), processes (`/proc`, signals), power (logind), sensors (hwmon), memory, Discord, Steam downloads |
| Stores | Steam (native, Flatpak, Snap), Heroic (Epic and GOG, launched through its legendary/gogdl backends with the game's Wine/Proton settings), Lutris, Prism Launcher/PolyMC/MultiMC, Battle.net in a Lutris prefix |
| Tests | 58 CTest suites build and pass (`webkitgtk_browser_lifecycle` is skipped without WebKitGTK or a display) |
| Launcher window | SDL3 library browser with keyboard/gamepad navigation and selected-game details; SDL window/input event boundary is available |
| Renderer | Not started (phase 3) |
| Web view (phase 4) | `BrowserWindow` on WebKitGTK 6.0 in the `launchme-web` helper process; not yet opened by the launcher (it needs the phase 3 app loop) |
| Packaging (phase 5) | Install rules, desktop entry, AppStream metainfo, hicolor icons, AppImage script, Flatpak manifest |

## Building on Linux

Packages (Arch names): `cmake ninja clang pkgconf icu openssl libsecret curl sdl3 sdl3_ttf ffmpeg libpulse
libpng libjpeg-turbo libwebp expat sqlite systemd-libs glib2`, and `webkitgtk-6.0` (pulls in `gtk4`) for the
reader window. Without WebKitGTK the build still succeeds: `launchme-web` is not built and pages open in the
default browser (`-DLAUNCHME_WEBKIT=OFF` skips the check). Other distributions: `libwebkitgtk-6.0-dev`
(Debian/Ubuntu), `webkitgtk6.0-devel` (Fedora). Optional at run time: `tesseract` (text-aware cover crops),
`steam-devices` (udev rules for the Steam Controller's hidraw access). Packaging only: `desktop-file-utils`,
`appstream` (validation), `curl` (the AppImage script fetches its pinned tools), `flatpak-builder`.

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

### Phase 3: window, input and renderer

This is the largest phase: 48 files in `src/ui` and `src/app` call Direct2D and DirectWrite directly
(`ID2D1RenderTarget`, geometries, effects, `IDWriteTextLayout`), and about 50 use `HWND`.

1. **Window and input**: SDL3 for the window, DPI, keyboard, mouse, touch and text input (IME). The
   `platform/sdl_window` event boundary now covers window resize/focus, keyboard, text, mouse and
   gamepad events. The `WM_*` handling in `main.cpp` and `app/input.cpp` still needs to move behind
   this interface before the real Launcher can build.
2. **Renderer**: introduce a `Canvas` interface that covers the Direct2D subset Launchme actually uses (rects,
   rounded rects, paths, linear and radial gradients, bitmaps with opacity masks, blur and shadow, clip
   layers, text layouts). Port the Windows renderer onto it first, with identical pixels (the UI tests
   compare against reference renders), then add a **Skia** backend (prebuilt Skia, HarfBuzz shaping,
   Fontconfig fonts). `ui/image_decoder.cpp` moves to Skia's codecs.
3. **Layered and overlay windows** (`layered_surface`, `ui/overlay_window.cpp`): on Wayland an overlay over
   other applications is not possible in general; the in-game guide becomes a gamescope/Steam Deck–style
   feature or is left out.

### Phase 4: web view (done, not yet wired into the app loop)

`BrowserWindow` (`ui/browser_window.hpp`) is the reader window for Glance articles, store news pages and the
TV YouTube player; its only user is `Launcher::openGlanceBrowser` (`app/glance.cpp`), with gamepad commands
from `app/input.cpp`. There are no sign-in flows through it (store accounts use their own token pages), so
callers rely on: open/navigate, back/forward/reload/close, button focus and accept, scrolling, the closed and
external callbacks, low-memory mode and focus checks.

- Windows: WebView2, unchanged, now in `ui/browser_window_windows.cpp`. The header gained `NativeWindow` /
  `ProcessId` (HWND/DWORD there), `focused()` and an empty `pump()`.
- Linux: `ui/browser_window_linux.cpp` starts `launchme-web` (`src/linux/web_helper.cpp`, next to the
  executable): one WebKitGTK 6.0 view in a GTK4 window with the same toolbar, focus model, messages, URL rules
  and served YouTube player (`https://launchme.invalid/youtube/…` through `load_alternate_html`).
- **Why a helper process** rather than a GTK thread or pumping the GLib context from the SDL loop: GTK 4
  aborts in a process that has GTK 3 loaded, and SDL loads GTK 3 itself on common desktops (its tray dlopens
  libgtk-3/appindicator; libdecor's GTK plugin draws SDL's frames on GNOME). The helper also keeps GTK's
  main loop, WebKit's memory and web process crashes out of the launcher, which never links GTK or WebKit.
- Protocol: a socket pair (descriptor 3 in the helper), tab-separated lines (`ui/browser_protocol.hpp`). A
  reader thread queues events and calls `Options::wake`; `pump()` applies them on the UI thread, where
  `Options::closed`/`external` run. End of file on either side closes the window; the helper runs in its own
  session and its group is killed if it does not leave within 3 s.
- Website data: the profile folder the caller passes (`<data folder>/browser`, i.e.
  `$XDG_DATA_HOME/launchme/browser`): `WebKitNetworkSession` `data/` and `cache/`, cookies in
  `data/cookies.sqlite`.
- No WebKitGTK, a helper that cannot start, or no display: `status()` is `missing_runtime` and the page goes to
  `Options::external` (the default browser).
- Low memory: minimized or suspended, the view is unmapped (WebKit throttles hidden pages); started in low
  memory mode, WebKit gets a 512 MB memory-pressure limit. WebKit has no per-page memory target for an
  inactive window.

**Wiring it into the app loop** (with phase 3's Linux main loop and `app/glance.cpp`):
1. Call `browser_->pump()` every loop iteration while `browser_` exists, and set `Options::wake` to push an SDL
   user event (`SDL_PushEvent`) so a waiting loop (`SDL_WaitEvent`) wakes up for it.
2. Pass the SDL window as `owner` (only compared). Use `browser_->focused()` where Windows compares
   `owns(GetForegroundWindow())` (`app/input.cpp`, `app/tray.cpp`, `app/memory.cpp`) to route the gamepad to
   `browser_->command(...)` while the reader window has focus.
3. The launcher window is not disabled while the reader is open (Windows disables its owner): treat
   `browser_->isOpen()` as modal for the launcher's own input.
4. On Wayland, pass an xdg-activation token for the launcher's surface in `Options::activation_token` so the
   compositor gives the new window focus (without one it may open behind or only flash).
5. Set `SDL_SetAppMetadata("Launchme", LAUNCHME_VERSION, "io.github.uxandai.Launchme")` (or
   `SDL_HINT_APP_ID`) so the launcher's window matches the desktop entry, as the helper's already does.

Testing on a machine without the package: the test builds against an unpacked `webkitgtk-6.0` package
(`PKG_CONFIG_PATH` to its `.pc` files) and runs under `bwrap --overlay-src /usr/lib --overlay-src
<package>/usr/lib --ro-overlay /usr/lib` and `xvfb-run`, because WebKit starts its processes from
`/usr/lib/webkitgtk-6.0`.

### Phase 5: packaging (done)

Application id `io.github.uxandai.Launchme` (`packaging/linux/`).

- **Install**: `cmake --install build --prefix /usr` (component `Runtime`): the program and the files it finds
  next to itself in `lib/launchme/` (`launchme`, `launchme-web`, `libonnxruntime.so.1`, `assets/`),
  `bin/launchme` a relative link to it, `share/applications/io.github.uxandai.Launchme.desktop`,
  `share/metainfo/io.github.uxandai.Launchme.metainfo.xml`, `share/icons/hicolor/{16…512,scalable}`,
  `share/licenses/launchme/`. Icons come from `tools/launchme_icon.py --hicolor packaging/linux/icons`.
- **AppImage**: `packaging/linux/build-appimage.sh` → `dist/launchme-<version>-x86_64.AppImage` and `.sha256`,
  and the ``SHA-256: `<hex>` `` line for the release notes. Attach it to the `v<version>` GitHub release under that
  exact name: the updater (`platform/updater.cpp`) only offers releases to a running AppImage (`$APPIMAGE`)
  and checks the asset digest or that line. Build on the oldest distribution you support (CI: an Ubuntu LTS):
  the bundled libraries need that glibc or newer. The reader window uses the system's WebKitGTK 6.0.
- **Flatpak**: `flatpak-builder --user --install-deps-from=flathub --force-clean --install build/flatpak
  packaging/linux/flatpak/io.github.uxandai.Launchme.yml`. Offline build (all downloads are pinned sources),
  GNOME 49 runtime. The data folder is then `~/.var/app/io.github.uxandai.Launchme/data/launchme`.

Not done / known gaps:
- No CI workflow (the repository has none yet); the manifest has not been built with `flatpak-builder` here.
- In the Flatpak, games and store clients must be started on the host with `flatpak-spawn --host` (the
  permission is granted, `platform/process_spawn` does not do it yet), and `/proc` shows only the sandbox, so
  running-game detection does not see host processes.
- The metainfo screenshots are the Windows ones; Flathub wants Linux screenshots once the renderer is ported.
