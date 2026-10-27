#pragma once

#include "../model/game.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Minecraft instances of MultiMC and its forks (Prism Launcher, PolyMC): every instance is one
// game (GameSource::multimc). Read from disk only, never executed; the launcher starts an
// instance with "<launcher> --launch <instance folder name>" and, launched that way, keeps
// running until the game (javaw.exe, its child) closes.
// Prism: %APPDATA%\PrismLauncher (portable: UserData\ or portable.txt beside the EXE),
// prismlauncher.cfg; MultiMC: a portable folder, multimc.cfg beside MultiMC.exe.
// instance.cfg: name, iconKey, lastLaunchTime (epoch ms), totalTimePlayed (seconds).
namespace multimc {
enum class Flavor { multimc, prism, polymc };

struct Installation {
    Flavor flavor = Flavor::multimc;
    std::filesystem::path executable; // The launcher's EXE.
    std::filesystem::path data;       // Its data folder: the .cfg, instances\, icons\.
    bool operator==(const Installation&) const = default;
};

struct Instance {
    std::string id;                  // 16 hex digits of the instance folder: Game::source_id.
    std::wstring folder;             // The instance's folder name: its ID for --launch.
    std::wstring name;
    std::wstring icon;               // iconKey ("default", "grass", or a file in icons\).
    std::wstring group;              // instgroups.json; empty when ungrouped.
    std::wstring version;            // Minecraft version (mmc-pack.json, net.minecraft).
    std::filesystem::path directory; // The instance folder.
    uint64_t last_launch = 0;        // Unix seconds (from epoch milliseconds).
    uint64_t played_seconds = 0;     // The launcher's own count (totalTimePlayed).
};

// Key/value pairs of an INI-like .cfg: MultiMC's plain "key=value" lines or Prism's QSettings
// [General] section (quoted values, \n \t \\ \" escapes). Never throws; bad lines are skipped.
std::vector<std::pair<std::wstring, std::wstring>> parseConfig(std::string_view bytes);
std::wstring configValue(const std::vector<std::pair<std::wstring, std::wstring>>& config, std::wstring_view key);

const wchar_t* flavorName(Flavor flavor);
std::string instanceId(const std::filesystem::path& directory);
// The data folder a launcher EXE uses (portable markers first).
std::filesystem::path dataDirectory(const std::filesystem::path& executable, Flavor flavor);
// The flavor named by an EXE's file name (MultiMC.exe, prismlauncher.exe, polymc.exe).
std::optional<Flavor> flavorOf(const std::filesystem::path& executable);
// The instances folder of an installation (InstanceDir in its .cfg, relative to the data folder).
std::filesystem::path instancesDirectory(const Installation& installation);
// One instance folder; empty when it holds no instance.cfg.
std::optional<Instance> readInstance(const std::filesystem::path& directory);
std::vector<Instance> instances(const Installation& installation);

// Installations found on this PC: Prism's registry entries and default folders, PolyMC, scoop
// apps, common MultiMC folders, plus the EXEs the user pointed at (Ustawienia → MultiMC / Prism).
std::vector<Installation> discover(const std::vector<std::filesystem::path>& chosen = {});

GameAction action(const Installation& installation, const Instance& instance);
// GameSource::multimc, key "multimc:<id>".
Game makeGame(const Installation& installation, const Instance& instance);
// Every instance of every installation; one game per instance folder.
std::vector<Game> scan(const std::vector<Installation>& installations);
} // namespace multimc
