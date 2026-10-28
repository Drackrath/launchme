#pragma once
// The program's own embedded files by number: her pictures (companion_ui::spriteId, poseId) and the
// phone page (65000). Windows: RCDATA resources (companion.rc, CMakeLists.txt). Linux: the same files
// assembled into the binary with .incbin (generated/companion_resources.cpp, cmake/Linux.cmake), linked
// only into targets that link launchme_companion_resources; elsewhere every lookup is nullopt.
#include <cstdint>
#include <optional>
#include <span>

namespace embedded_resources {
// The bytes stay valid for the program's lifetime (mapped with the image, never copied).
std::optional<std::span<const uint8_t>> find(int id);

// Linux: one embedded file (generated table).
struct Entry {
    int id;
    const uint8_t* begin;
    const uint8_t* end;
};
struct Table {
    const Entry* entries; // Sorted by id.
    size_t count;
};
}
