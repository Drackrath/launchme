#pragma once
// How a Windows program runs on Linux (umu-launcher, Proton or Wine): one set of options per game
// plus the global default, kept by src/library/launch_options_store.hpp and turned into a command
// and its environment by src/platform/wine_runner.hpp. The comment of each field names exactly
// what it sets ("Proton" means umu-run and Proton run directly; "Wine" a plain Wine build). The
// settings UI iterates src/model/launch_options_catalog.hpp instead of these fields.
// Portable model (compiles everywhere); only the Linux build uses it. docs/LINUX_WINE.md.
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

struct LaunchOptions {
    enum class Sync : uint8_t { automatic, ntsync, fsync, esync, wineserver };
    enum class Hdr : uint8_t { automatic, on, force, off };
    enum class Switch : uint8_t { automatic, on, off };
    enum class PresentMode : uint8_t { automatic, fifo, mailbox, immediate, relaxed };
    enum class GamescopeWindow : uint8_t { fullscreen, borderless, windowed };
    enum class GamescopeFilter : uint8_t { automatic, linear, nearest, fsr, nis, pixel };
    enum class GamescopeScaler : uint8_t { automatic, integer, fit, fill, stretch };
    struct Variable {
        std::string name, value;
        bool operator==(const Variable&) const = default;
    };

    // --- Runner ---
    // compat_tools::Tool::id ("launchme:GE-Proton11-11", "steam-common:Proton 10.0", "system:wine",
    // "umu:GE-Proton"...), or "default": the global default's tool, whose own "default" is the
    // automatic choice (the newest GE-Proton, else UMU-Proton through umu, else Steam's Proton, else Wine).
    std::string compat_tool = "default";
    // WINEPREFIX (and STEAM_COMPAT_DATA_PATH for Proton). Empty: the game's own prefix under the
    // prefix root (~/Games/launchme/prefixes/<slug>), assigned on first use.
    std::filesystem::path prefix;
    bool prefer_umu = true;  // A Proton tool runs through umu-run (Steam Linux Runtime) when it is installed.
    std::string umu_id;      // GAMEID override; empty: umu-database's ID for the store's game, else umu-default.

    // --- Proton / Wine ---
    // automatic: the tool decides. Proton: ntsync PROTON_USE_NTSYNC=1; fsync PROTON_NO_NTSYNC=1;
    // esync PROTON_NO_NTSYNC=1 PROTON_NO_FSYNC=1; wineserver also PROTON_NO_ESYNC=1.
    // Wine: fsync WINEFSYNC=1 WINEESYNC=1; esync WINEESYNC=1 (ntsync: Wine 10.16+ uses /dev/ntsync itself).
    Sync sync = Sync::automatic;
    bool wined3d = false;             // Proton: PROTON_USE_WINED3D=1. Wine: DXVK not used (d3d*/dxgi=b).
    bool wayland = false;             // Proton: PROTON_ENABLE_WAYLAND=1. Wine: DISPLAY unset (winewayland.drv).
    // on: PROTON_ENABLE_HDR=1 DXVK_HDR=1; force: also PROTON_FORCE_HDR=1 (GE, Wayland); off: DXVK_NO_HDR=1.
    Hdr hdr = Hdr::automatic;
    bool fsr = false;                 // WINE_FULLSCREEN_FSR=1 (GE-Proton, Wine-GE/TkG full-screen FSR 1).
    uint32_t fsr_strength = 2;        // WINE_FULLSCREEN_FSR_STRENGTH, 0 (sharpest) … 5.
    std::string fsr_resolution;       // WINE_FULLSCREEN_FSR_CUSTOM_MODE ("1280x720"): the fake resolution.
    bool fsr4_upgrade = false;        // PROTON_FSR4_UPGRADE=1 (GE): FSR 3.1 games get FSR 4.
    bool xess_upgrade = false;        // PROTON_XESS_UPGRADE=1 (GE): newer XeSS DLLs.
    bool large_address_aware = false; // Proton: PROTON_FORCE_LARGE_ADDRESS_AWARE=1. Wine: WINE_LARGE_ADDRESS_AWARE=1.
    bool wow64 = false;               // Proton: PROTON_USE_WOW64=1. Wine: WINEARCH=wow64 (new prefixes).
    std::string dxvk_hud;             // DXVK_HUD ("fps", "fps,frametimes,gpuload", "full"); empty: off.
    uint32_t dxvk_frame_rate = 0;     // DXVK_FRAME_RATE (also VKD3D-Proton); 0: no limit.
    bool install_dxvk = false;        // Wine: DXVK's DLLs in the prefix, WINEDLLOVERRIDES d3d8,d3d9,d3d10core,d3d11,dxgi=n,b.
    bool install_vkd3d = false;       // Wine: VKD3D-Proton's DLLs in the prefix, WINEDLLOVERRIDES d3d12,d3d12core=n,b.
    bool eac_runtime = false;         // Proton: PROTON_EAC_RUNTIME=<Steam's "Proton EasyAntiCheat Runtime">.
    bool battleye_runtime = false;    // Proton: PROTON_BATTLEYE_RUNTIME=<Steam's "Proton BattlEye Runtime">.
    std::string dll_overrides;        // WINEDLLOVERRIDES ("dinput8=n,b;xinput1_3=n"), after Launchme's own.
    std::string locale;               // Proton: HOST_LC_ALL. Wine: LC_ALL ("ja_JP.UTF-8").
    // Proton: PROTON_LOG=1 PROTON_LOG_DIR=<config>/logs/wine/<slug> (+ UMU_LOG=1). Wine: WINEDEBUG left
    // at Wine's default and DXVK's logs beside it; off: WINEDEBUG=-all DXVK_LOG_LEVEL=none.
    bool log = false;

    // --- NVIDIA ---
    // on: PROTON_ENABLE_NVAPI=1 DXVK_ENABLE_NVAPI=1 (DLSS, Reflex); off: PROTON_DISABLE_NVAPI=1.
    Switch nvapi = Switch::automatic;
    bool install_dxvk_nvapi = false;  // Wine: DXVK-NVAPI's DLLs in the prefix, WINEDLLOVERRIDES nvapi,nvapi64,nvofapi64=n.
    bool dlss_upgrade = false;        // PROTON_DLSS_UPGRADE=1 (GE): the newest DLSS DLLs and preset.
    // PROTON_DLSS_INDICATOR=1 (GE); DXVK_NVAPI_SET_NGX_DEBUG_OPTIONS=DLSSIndicator=1024,DLSSGIndicator=2.
    bool dlss_indicator = false;
    bool hide_nvidia_gpu = false;     // PROTON_HIDE_NVIDIA_GPU=1: the GPU is reported as AMD.
    // Hybrid laptops (PRIME render offload): __NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia
    // __VK_LAYER_NV_optimus=NVIDIA_only.
    bool prime_offload = false;
    uint32_t gl_shader_cache_gb = 0;  // __GL_SHADER_DISK_CACHE_SIZE=<GiB in bytes>; 0: the driver's.
    bool gl_shader_cache_keep = false;// __GL_SHADER_DISK_CACHE_SKIP_CLEANUP=1.
    bool gl_threaded = false;         // __GL_THREADED_OPTIMIZATIONS=1 (OpenGL games).

    // --- AMD / Mesa ---
    bool dri_prime = false;           // DRI_PRIME=1: the discrete GPU of a Mesa hybrid laptop.
    bool radv_sam = false;            // RADV_PERFTEST=sam (Resizable BAR: driver objects in VRAM).
    bool radv_nggc = false;           // RADV_PERFTEST=nggc (NGG culling, RDNA3 and newer).
    bool radv_emulate_rt = false;     // RADV_EXPERIMENTAL=emulate_rt (ray tracing in software, older GPUs).
    bool mesa_glthread = false;       // mesa_glthread=true (OpenGL on a separate thread).
    PresentMode present_mode = PresentMode::automatic; // MESA_VK_WSI_PRESENT_MODE=fifo|mailbox|immediate|relaxed.
    uint32_t anisotropy = 0;          // RADV_TEX_ANISO (2, 4, 8, 16); 0: the game's.
    std::string mesa_shader_cache_size; // MESA_SHADER_CACHE_MAX_SIZE ("10G").

    // --- Wrappers: <wrapper> gamemoderun mangohud gamescope … -- <runner> ---
    bool gamemode = false;            // gamemoderun (Feral GameMode).
    bool mangohud = false;            // mangohud; inside gamescope its --mangoapp instead.
    std::string wrapper;              // A command before everything else ("obs-gamecapture"), shell words.
    bool gamescope = false;           // gamescope [options] -- (not nested inside Steam's Game Mode).

    // --- Gamescope ---
    uint32_t gamescope_game_width = 0, gamescope_game_height = 0;     // -w -h: the game's resolution.
    uint32_t gamescope_output_width = 0, gamescope_output_height = 0; // -W -H: gamescope's window.
    uint32_t gamescope_fps_limit = 0;           // -r
    uint32_t gamescope_fps_limit_unfocused = 0; // -o
    GamescopeWindow gamescope_window = GamescopeWindow::fullscreen; // -f, -b, or a window.
    GamescopeFilter gamescope_filter = GamescopeFilter::automatic;  // -F linear|nearest|fsr|nis|pixel
    GamescopeScaler gamescope_scaler = GamescopeScaler::automatic;  // -S integer|fit|fill|stretch
    uint32_t gamescope_sharpness = 2;           // --sharpness 0 (sharpest) … 20, with the FSR or NIS filter only.
    bool gamescope_hdr = false;                 // --hdr-enabled
    bool gamescope_grab_cursor = false;         // --force-grab-cursor
    std::string gamescope_options;              // More options, shell words.

    // --- The game ---
    std::vector<Variable> environment;          // KEY=value, set last (they win over the toggles above).
    std::string arguments;                      // After the action's own arguments (Windows command-line rules).
    std::filesystem::path working_directory;    // Empty: the action's, else the program's folder.
    std::filesystem::path before_script;        // Runs (and is waited for) before the game.
    std::filesystem::path after_script;         // Runs after the game has exited.

    bool operator==(const LaunchOptions&) const = default;
};

// A Windows program's launch context (GameAction::compat): which game it belongs to and its options,
// already resolved by launch_options_store::compatLaunch(game). Without it a Windows program runs
// with the global default options in the shared "default" prefix.
struct CompatLaunch {
    std::string game_key;                 // gameKey(): log names, LAUNCHME_GAME_KEY.
    std::wstring title;                   // The game's name (LAUNCHME_GAME_NAME).
    std::string store;                    // umu's STORE: egs, gog, amazon, ubisoft, ea, battlenet, steam, or empty.
    std::string store_id;                 // The store's own ID (umu-database CODENAME).
    uint32_t steam_app_id = 0;            // Steam games: protontricks, umu-<app ID>.
    std::filesystem::path install_directory; // STEAM_COMPAT_INSTALL_PATH (pressure-vessel mounts it).
    std::filesystem::path prefix;         // Absolute: options.prefix, else the game's assigned prefix.
    LaunchOptions options;                // Effective: the game's own options, else the global default;
                                          // compat_tool "default" already replaced by the global choice.
};
