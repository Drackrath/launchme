#pragma once
// Launchme's own update in Launcher (src/app/updates.cpp, src/platform/updater.hpp).
#include "../platform/updater.hpp"

#include <Windows.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace launcher_state {
struct UpdateState {
    enum class Phase { idle, checking, downloading, ready, installing, failed } phase = Phase::idle;
    std::optional<updater::Release> available; // Newer than this version.
    std::vector<std::filesystem::path> files;   // Phase::ready: the unpacked package.
    uint64_t done = 0;                          // Downloaded bytes.
    std::wstring error;
    std::wstring announced;                     // The version already notified in this run.
    ULONGLONG next_check = 0;                   // GetTickCount64 of the next look; 0: not scheduled yet.
    uint32_t generation = 0;
    bool manual = false;                        // Sprawdź teraz: also say when there is none.
};
} // namespace launcher_state
