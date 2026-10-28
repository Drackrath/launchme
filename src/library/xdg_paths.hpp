#pragma once
// Linux only: where launchers keep their files (XDG base directories, Flatpak sandboxes) and
// where their programs are (PATH, Flatpak's exported wrappers). Never runs anything.
#include "../platform/system.hpp"

#include <algorithm>
#include <filesystem>
#include <initializer_list>
#include <string_view>
#include <system_error>

namespace xdg_paths {
inline std::filesystem::path variable(const wchar_t* name) {
    const auto value = system_info::environment(name, 4095);
    return value && std::filesystem::path(*value).is_absolute() ? std::filesystem::path(*value) : std::filesystem::path{};
}
inline std::filesystem::path home() { return variable(L"HOME"); }
// $XDG_DATA_HOME, else ~/.local/share; $XDG_CONFIG_HOME, else ~/.config.
inline std::filesystem::path dataHome() {
    if (auto data = variable(L"XDG_DATA_HOME"); !data.empty()) return data;
    const auto base = home();
    return base.empty() ? base : base / ".local" / "share";
}
inline std::filesystem::path configHome() {
    if (auto config = variable(L"XDG_CONFIG_HOME"); !config.empty()) return config;
    const auto base = home();
    return base.empty() ? base : base / ".config";
}
// ~/.var/app/<app ID>: a Flatpak app's own home (its data/ and config/).
inline std::filesystem::path flatpakHome(std::wstring_view app) {
    const auto base = home();
    return base.empty() ? base : base / ".var" / "app" / app;
}
inline bool executableFile(const std::filesystem::path& path) {
    std::error_code ec;
    return path.is_absolute() && std::filesystem::is_regular_file(path, ec) &&
        (std::filesystem::status(path, ec).permissions() & std::filesystem::perms::others_exec) != std::filesystem::perms::none;
}
// The first of `names` on PATH; empty when none is.
inline std::filesystem::path findProgram(std::initializer_list<std::wstring_view> names) {
    const auto search = system_info::environment(L"PATH");
    if (!search) return {};
    for (const auto name : names) {
        std::wstring_view rest(*search);
        while (!rest.empty()) {
            const auto end = std::min(rest.find(L':'), rest.size());
            if (const std::filesystem::path candidate = std::filesystem::path(rest.substr(0, end)) / name; executableFile(candidate)) return candidate;
            rest.remove_prefix(std::min(end + 1, rest.size()));
        }
    }
    return {};
}
// Flatpak's launch wrapper of an app ("flatpak run <app ID>"), system-wide or per user.
inline std::filesystem::path flatpakWrapper(std::wstring_view app) {
    const auto system = std::filesystem::path(L"/var/lib/flatpak/exports/bin") / app;
    if (executableFile(system)) return system;
    const auto base = home();
    const auto user = base.empty() ? base : base / ".local" / "share" / "flatpak" / "exports" / "bin" / app;
    return executableFile(user) ? user : std::filesystem::path{};
}
}
