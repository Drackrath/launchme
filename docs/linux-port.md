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
| Tests | 57 CTest suites build and pass |
| Launcher window | SDL3 library browser with keyboard/gamepad navigation and selected-game details; SDL window/input event boundary is available |
| Renderer, web view | Not started (phases 3–4) |

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

### Phase 4: web view

WebView2 (`ui/browser_window.cpp`, store sign-ins and pages) → WebKitGTK 6.0 in its own GTK4 window. GTK's
main loop runs alongside SDL's; cookies and storage go in the data folder.

### Phase 5: packaging

An AppImage first (self-updating through the existing GitHub release check), then a Flatpak. Add a `.desktop`
file, an AppStream metainfo file and icons from `res/`.
