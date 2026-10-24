#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <string_view>
#include <unordered_set>

std::unordered_map<uint32_t, std::wstring> parseSteamAppListNames(
    std::string_view json, const std::unordered_set<uint32_t>& wanted);
std::unordered_map<uint32_t, std::wstring> cachedCollectionGameNames(
    const std::vector<uint32_t>& app_ids, const std::filesystem::path& cache);

// Resolves names only for the AppIDs in the active account's Steam collections.
// Web API owned-game names, then Steam store, then locally cached names.
std::unordered_map<uint32_t, std::wstring> collectionGameNames(
    uint64_t steam_id, const std::vector<uint32_t>& app_ids,
    const std::filesystem::path& cache, const std::atomic_bool* alive = nullptr,
    const std::function<void(const std::unordered_map<uint32_t, std::wstring>&)>& progress = {});
