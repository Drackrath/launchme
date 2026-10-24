#pragma once

#include <cstdint>
#include <filesystem>
#include <unordered_set>

// Read visibility settings for the account Steam will auto-login to (or the
// last account used). Never mix hidden/private lists across different users.
std::unordered_set<uint32_t> excludedSteamApps(const std::filesystem::path& steam_root);
uint64_t activeSteamId(const std::filesystem::path& steam_root);
