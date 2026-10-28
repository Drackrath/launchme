#pragma once
// Linux only: Steam's stand-in for HKCU\Software\Valve\Steam, the text VDF file
// ~/.steam/registry.vdf (Registry/HKCU/Software/Valve/Steam/...). Keys match without case.
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace steam_registry {
// ~/.steam/registry.vdf, or the Flatpak client's; the native path when neither exists.
std::filesystem::path file();
// A value below Software/Valve/Steam: value({"Apps", "570"}, "Running"), value({}, "RunningAppID").
std::optional<std::string> value(const std::filesystem::path& file, const std::vector<std::string>& keys, std::string_view name);
inline std::optional<std::string> value(const std::vector<std::string>& keys, std::string_view name) { return value(file(), keys, name); }
// The same as a number; nullopt when missing or not a number.
std::optional<uint64_t> number(const std::filesystem::path& file, const std::vector<std::string>& keys, std::string_view name);
inline std::optional<uint64_t> number(const std::vector<std::string>& keys, std::string_view name) { return number(file(), keys, name); }
// Sets values directly below Software/Valve/Steam (Steam must not be running: it rewrites the
// file). Other content is kept; false when the file is missing, unreadable or unwritable.
bool set(const std::filesystem::path& file, const std::vector<std::pair<std::string, std::string>>& values);

// The text VDF tree, also for tests.
struct Node {
    std::string key, value;
    bool block = false;
    std::vector<Node> children;
};
std::optional<Node> parse(std::string_view text); // The root holds the top-level entries.
std::string format(const Node& root);
}
