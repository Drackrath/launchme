#include "embedded_resources.hpp"

#ifdef _WIN32
#include <Windows.h>
#else
#include <algorithm>
// Defined by generated/companion_resources.cpp; a weak reference stays null when it is not linked in.
extern "C" __attribute__((weak)) const embedded_resources::Table launchme_embedded_resources;
#endif

namespace embedded_resources {
std::optional<std::span<const uint8_t>> find(int id) {
#ifdef _WIN32
    const HMODULE module = GetModuleHandleW(nullptr);
    const HRSRC found = FindResourceW(module, MAKEINTRESOURCEW(id), RT_RCDATA);
    const HGLOBAL handle = found ? LoadResource(module, found) : nullptr;
    const auto* bytes = handle ? static_cast<const uint8_t*>(LockResource(handle)) : nullptr;
    const DWORD size = found ? SizeofResource(module, found) : 0;
    if (!bytes || !size) return std::nullopt;
    return std::span<const uint8_t>(bytes, size);
#else
    const Table* table = &launchme_embedded_resources;
    if (!table || !table->entries) return std::nullopt;
    const auto* end = table->entries + table->count;
    const auto* at = std::lower_bound(table->entries, end, id, [](const Entry& entry, int wanted) { return entry.id < wanted; });
    if (at == end || at->id != id || at->end <= at->begin) return std::nullopt;
    return std::span<const uint8_t>(at->begin, static_cast<size_t>(at->end - at->begin));
#endif
}
}
