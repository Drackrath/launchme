#pragma once
// A snapshot of Steam's store tag names, so tags work from the first start and offline; the
// game index replaces it with a fresh copy (knowledge/steam-tags.json) when it can.
#include <cstdint>
#include <span>

namespace game_index {
struct BuiltinTag { uint32_t id; const wchar_t* name; };
std::span<const BuiltinTag> builtinSteamTags();
}
