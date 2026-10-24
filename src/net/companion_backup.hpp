#pragma once
#include "companion_affinity.hpp"
#include "companion_chats.hpp"
#include "companion_cognition.hpp"
#include "companion_mood.hpp"
#include "../platform/user_config.hpp"

namespace companion::backup {
struct Input {
    std::filesystem::path config, executable, destination;
    Look look{};
    AppSettings settings;
    // UI snapshots include unsaved state; a command-line backup reads the persisted versions.
    std::optional<Memory> memory;
    std::optional<mind::State> mind;
    std::optional<cognition::State> cognition;
    std::optional<affinity::State> affinity;
    std::optional<mood::State> mood;
    std::optional<chats::Conversation> live;
    std::string character_text; // Empty: exact manifest embedded in the executable.
};
struct Result {
    bool ok = false;
    std::filesystem::path path;
    std::wstring error;
    size_t files = 0, conversations = 0;
    uint64_t bytes = 0;
};
// Creates a NEW self-contained folder, never overwrites an older backup. Portable JSON rather
// than DPAPI ciphertext; no credentials. The copied executable keeps the exact personality/art.
Result create(const Input& input);
Result verify(const std::filesystem::path& bundle);
// First launch of a copy: verify and materialize its own profile, protecting private JSON with
// the current Windows user's DPAPI. Subsequent launches reuse it; never writes the main profile.
Result openCopy(const std::filesystem::path& bundle);
}
