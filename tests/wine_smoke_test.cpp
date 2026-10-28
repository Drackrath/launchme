// Linux only, not part of CTest: starts real Windows programs through the runner, the way the
// launcher does, in a throwaway prefix. Needs Wine, Proton or umu-launcher installed.
//   wine_smoke_test [tool id] [--gui]
// 1. cmd.exe writes a file into the prefix's C: drive (the prefix is created on first use);
// 2. with --gui, notepad.exe runs as the launch job's session and killPrefix() ends it.
#include "../src/library/compat_tools.hpp"
#include "../src/library/launch_options_store.hpp"
#include "../src/platform/game_activity.hpp"
#include "../src/platform/launch.hpp"
#include "../src/platform/user_config.hpp"
#include "../src/platform/wine_runner.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {
bool waitStopped(HANDLE job, std::chrono::seconds limit) {
    const auto until = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < until) {
        if (readLaunchJob(job) == GameActivity::stopped) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    return false;
}
void showLog(const CompatLaunch& launch) {
    for (const auto& file : wine_runner::logFiles(launch)) {
        std::ifstream input(file);
        std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        if (text.size() > 3000) text = "..." + text.substr(text.size() - 3000);
        std::cout << "--- " << file.string() << "\n" << text << "\n";
    }
}
}

// --releases: every source's list from GitHub; --install <source>: download, verify and install
// its newest release into Launchme's data folder (LAUNCHME_CONFIG_DIR moves it).
int releases(const std::string& install) {
    int result = 0;
    for (const auto& source : compat_tools::sources()) {
        if (!install.empty() && install != source.id) continue;
        const auto list = compat_tools::releases(source);
        std::cout << source.id << ": " << list.releases.size() << " releases" << (list.stale ? " (stale)" : "") << (list.available ? "" : " (unavailable)") << "\n";
        for (size_t i = 0; i < list.releases.size() && i < 2; ++i) {
            const auto& release = list.releases[i];
            std::cout << "  " << release.tag << " " << release.published << " " << release.asset << " " << release.size << " bytes"
                      << (release.checksum_asset.empty() ? "" : " + " + release.checksum_asset) << "\n";
        }
        if (install.empty()) continue;
        if (list.releases.empty()) return 1;
        int last_phase = -1;
        uint64_t last_decile = 99;
        const auto begun = std::chrono::steady_clock::now();
        const auto installed = compat_tools::install(list.releases.front(), compat_tools::Target::launchme, {}, [&](const compat_tools::Progress& progress) {
            const auto decile = progress.total ? progress.done * 10 / progress.total : 0;
            if (static_cast<int>(progress.phase) == last_phase && decile == last_decile) return;
            last_phase = static_cast<int>(progress.phase);
            last_decile = decile;
            std::cout << "    " << (progress.phase == compat_tools::Progress::Phase::download ? "download" :
                                    progress.phase == compat_tools::Progress::Phase::verify ? "verify" : "extract")
                      << " " << decile * 10 << "% after "
                      << std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - begun).count() << " s\n" << std::flush;
        });
        std::cout << "  install: error=" << static_cast<int>(installed.error) << " folder=" << installed.folder.string() << "\n";
        if (installed.error != compat_tools::Error::none) result = 1;
    }
    return result;
}

int main(int argc, char** argv) {
    std::string tool = "default";
    bool gui = false, dxvk = false, no_umu = false, log = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--gui") gui = true;
        else if (argument == "--dxvk") dxvk = true;  // DXVK, VKD3D-Proton and DXVK-NVAPI copied into a plain Wine prefix.
        else if (argument == "--no-umu") no_umu = true; // Proton directly.
        else if (argument == "--log") log = true;
        else if (argument == "--releases") return releases({});
        else if (argument == "--install" && i + 1 < argc) return releases(argv[++i]);
        else if (argument == "--umu-database") return compat_tools::refreshUmuDatabase(userConfigDirectory(), true) ? 0 : 1;
        else tool = argument;
    }
    const auto root = fs::temp_directory_path() / ("launchme-wine-smoke-" + std::to_string(getpid()));
    fs::create_directories(root);
    std::cout << "Tools found:\n";
    for (const auto& found : compat_tools::runners(compat_tools::discover()))
        std::cout << "  " << found.id << " (" << found.family << ", " << found.version << ")\n";

    auto launch = std::make_shared<CompatLaunch>();
    launch->game_key = "smoke:test";
    launch->title = L"Smoke test";
    launch->prefix = root / "prefix";
    launch->options.compat_tool = tool;
    launch->options.install_dxvk = launch->options.install_vkd3d = launch->options.install_dxvk_nvapi = dxvk;
    launch->options.prefer_umu = !no_umu;
    launch->options.log = log;
    const auto context = wine_runner::context(*launch);
    if (!context.tool) { std::cout << "No Wine, Proton or umu-launcher installed\n"; return 2; }
    std::cout << "Using " << context.tool->id << (context.umu_run.empty() ? "" : " (umu-run: " + context.umu_run.string() + ")") << "\n";

    int result = 0;
    // 1. A console program: the prefix is made and cmd.exe writes C:\launchme-ok.txt.
    GameAction action{L"cmd.exe", L"/c echo launchme-ok>C:\\launchme-ok.txt", {}, ActionKind::play};
    action.compat = launch;
    const auto started = std::chrono::steady_clock::now();
    if (executeGameAction(nullptr, action) <= 32) { std::cout << "cmd.exe did not start\n"; showLog(*launch); return 1; }
    const auto job = launchJob(takeLaunchedProcess());
    const bool ended = waitStopped(job, std::chrono::seconds(600));
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started).count();
    std::ifstream written(launch->prefix / "drive_c" / "launchme-ok.txt");
    std::string text;
    std::getline(written, text);
    const bool ok = ended && text.starts_with("launchme-ok");
    std::cout << (ok ? "OK" : "FAILED") << ": cmd.exe in a new prefix (" << seconds << " s, ended=" << ended << ", file='" << text << "')\n";
    if (!ok) { result = 1; showLog(*launch); }
    std::cout << "Prefix exists: " << wine_runner::prefixExists(launch->prefix) << "\n";
    if (dxvk && context.dxvk) {
        std::error_code ec;
        const auto system32 = launch->prefix / "drive_c" / "windows" / "system32";
        const bool copied = fs::file_size(system32 / "d3d11.dll", ec) == fs::file_size(context.dxvk->path / "x64" / "d3d11.dll", ec) && !ec &&
                            (!context.vkd3d || fs::exists(system32 / "d3d12core.dll")) && (!context.dxvk_nvapi || fs::exists(system32 / "nvapi64.dll"));
        std::cout << (copied ? "OK" : "FAILED") << ": DXVK" << (context.vkd3d ? ", VKD3D-Proton" : "") << (context.dxvk_nvapi ? ", DXVK-NVAPI" : "")
                  << " in the prefix's system32\n";
        if (!copied) { result = 1; showLog(*launch); }
    }
    if (log) showLog(*launch);

    // 2. A window: notepad.exe stays in the launch job's session until the prefix is killed.
    if (gui) {
        GameAction notepad{L"notepad.exe", {}, {}, ActionKind::play};
        notepad.compat = launch;
        if (executeGameAction(nullptr, notepad) <= 32) { std::cout << "notepad.exe did not start\n"; return 1; }
        const auto window = launchJob(takeLaunchedProcess());
        std::this_thread::sleep_for(std::chrono::seconds(8));
        const bool running = readLaunchJob(window) == GameActivity::running;
        const bool killed = wine_runner::killPrefix(*launch);
        const bool stopped = waitStopped(window, std::chrono::seconds(30));
        std::cout << (running && stopped ? "OK" : "FAILED") << ": notepad.exe running=" << running << ", killPrefix=" << killed << ", stopped=" << stopped << "\n";
        if (!(running && stopped)) { result = 1; showLog(*launch); }
    }
    wine_runner::killPrefix(*launch);
    std::cout << "Prefix deleted: " << wine_runner::deletePrefix(launch->prefix) << "\n";
    std::error_code ignored;
    fs::remove_all(root, ignored);
    return result;
}
