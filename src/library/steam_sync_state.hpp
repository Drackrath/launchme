#pragma once
#include "../platform/library_state.hpp"
#include "../platform/steam_library_sync.hpp"
#include "../platform/user_config.hpp"

namespace steam_sync {
// Existing Steam-only overlays are the durable outbox. Local/cross-store collections never enter it.
std::vector<Change> pending(const LibraryState& state, uint64_t account);
bool acknowledge(LibraryState& state, uint64_t account, const Change& change);
// Explicit conversion, retaining non-Steam members locally in the Steam collection overlay.
// Refuses to lose members belonging to another Steam account.
bool moveCollection(LibraryState& state, AppSettings& settings, uint64_t account, const std::string& local_id, const std::string& steam_id);
}
