# Linux port

Launchme is being ported to run natively on Linux. It is not a Wine build: the same sources
build for both systems, with Windows code kept behind `#ifdef _WIN32` or in per-platform files. The
Windows build is unchanged in behaviour.

## Status

| Part | State |
| --- | --- |
| Build (`cmake/Linux.cmake`) | Configures with CMake + Ninja, Clang or GCC, C++20 without GNU extensions |
| Shared core (`launchme_core`) | 71 of 124 sources build on Linux |
| Tests | 14 CTest suites build and pass (search, Steam visibility and shortcuts, play journal, artwork LRU/retry, workers, screensaver, gestures, …) |
| Launcher window | SDL3 library browser with keyboard/gamepad navigation and selected-game details |
| Renderer, web view | Not started (phases 3–4) |

## Building on Linux

Packages (Arch names): `cmake ninja clang icu openssl libsecret pkgconf sdl3`.

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

### Phase 1: shared core (in progress)

The remaining 53 core sources, grouped by what unblocks them:

1. **HTTP**: `net/http.cpp`, `net/http_download.cpp`, `net/glance.cpp` (WinHTTP) → libcurl behind the existing
   `net/http.hpp` interface (`http_curl.cpp`). This unblocks most store, artwork and companion tests.
2. **Image codecs**: WIC in `artwork/text_crop.cpp`, `artwork/steamgriddb.cpp`, `artwork/screenscraper_art.cpp`,
   `net/companion_feed.cpp` and `library/custom_games.cpp` → a small `image_codec` interface (WIC on Windows;
   libpng, libjpeg-turbo and libwebp on Linux). `artwork/platform_logo.cpp` also draws with Direct2D and
   waits for the renderer (phase 3).
3. **Processes**: `platform/launch.cpp`, `store/epic_legendary.cpp`, `library/steam_accounts.cpp`
   (`CreateProcessW`, `ShellExecuteW`) → `posix_spawn`, plus `xdg-open` for URLs and files.
4. **Sockets**: `net/remote_server.cpp`, `platform/steam_library_sync.cpp` (Winsock) → POSIX sockets.
5. **Files and folders**: `platform/file_listing.cpp`, `store/gog_install.cpp`, `net/companion_backup.cpp`,
   `library/drm_free.cpp` (version resources), `library/screenscraper.cpp` and `store/gog_install_native.cpp`
   (BCrypt hashes → OpenSSL).
6. **Store discovery**: `library/pc_platforms.cpp` reads the registry for Epic, GOG, EA and Ubisoft. On Linux
   the sources are different: Steam (native, Flatpak, Snap), Heroic (Epic, GOG, Amazon), Lutris, Bottles and
   Wine prefixes. `library/steam.cpp` needs the Linux Steam roots (`~/.steam/steam`,
   `~/.local/share/Steam`, `~/.var/app/com.valvesoftware.Steam/...`) and the `compatdata` layout.
   `library/battlenet.cpp` and `library/multimc.cpp` (Prism Launcher on Linux) need the same treatment.
7. **Updater**: `platform/updater.cpp` (BCrypt SHA-256, swapping a running `.exe`) → OpenSSL and an
   AppImage-style self-replacement (phase 5).
8. **Companion**: ONNX Runtime's Linux x64 release for `net/companion_embeddings.cpp`; generating
   `companion_manifest.inc` in `cmake/Linux.cmake` as the Windows build does; `net/companion.cpp`.
9. **Plugins**: `plugins/package.cpp` and `plugins/manager.cpp` load PE DLLs → ELF `.so` with `dlopen`, and
   packages declare which systems they support.

### Phase 2: platform services

| Windows | Linux |
| --- | --- |
| XAudio2 (`platform/audio.cpp`) | SDL3 audio |
| Media Foundation FLAC (`platform/flac.cpp`) | dr_flac or libFLAC |
| Media Foundation video (`platform/video_player.cpp`) | FFmpeg (libavformat/libavcodec) |
| WASAPI and endpoint volume (`system_audio`, `volume_monitor`) | PipeWire (or PulseAudio's API) |
| XInput, HID, Bluetooth, Steam Controller (`game_controllers`, `steam_controller`, `bluetooth_controllers`, `device_service`) | SDL3 gamepad (rumble, DualSense/Steam Controller, hotplug); BlueZ over D-Bus for pairing |
| PresentMon/ETW and window enumeration (`game_activity`, `game_overlay_monitor`) | `/proc` for running games; frame times from MangoHud's log or optional |
| Process suspend (`process_freeze`, `process_presence`) | `SIGSTOP`/`SIGCONT`, `/proc` |
| Power (`power.cpp`) | systemd-logind over D-Bus |
| PDH sensors (`sensors.cpp`) | hwmon in `/sys/class/hwmon` |
| Memory and GPU usage (`memory_usage`, `performance`) | `/proc/self/status`, `/proc/meminfo`, DRM fdinfo |
| File dialog (`file_dialog.cpp`) | xdg-desktop-portal FileChooser (via GTK4 `GtkFileDialog`) |
| Discord rich presence (named pipe) | `$XDG_RUNTIME_DIR/discord-ipc-0` socket |
| Tray icon, jump list | StatusNotifierItem over D-Bus; `.desktop` actions |

### Phase 3: window, input and renderer

This is the largest phase: 48 files in `src/ui` and `src/app` call Direct2D and DirectWrite directly
(`ID2D1RenderTarget`, geometries, effects, `IDWriteTextLayout`), and about 50 use `HWND`.

1. **Window and input**: SDL3 for the window, DPI, keyboard, mouse, touch and text input (IME). The
   `WM_*` handling in `main.cpp` and `app/input.cpp` moves behind a small event interface.
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
