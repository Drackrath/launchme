#pragma once
// Every LaunchOptions field as data, so the settings UI ("Wine / Proton" accordion) and the JSON
// store iterate one table instead of knowing the fields: id (the JSON key), section, type, Polish
// and English text, the exact variables or flags it sets, choices and limits, and a typed get/set
// through canonical text. Texts exist in Polish and English only (as Tarot's): text() picks by
// i18n::english(); they are not tr() keys. Portable.
#include "launch_options.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace launch_options_catalog {
struct Text {
    const wchar_t* polish;
    const wchar_t* english;
};
// The active language's text (English for every language but Polish).
const wchar_t* text(const Text& value);

enum class Section { runner, proton, extras, nvidia, mesa, wrappers, gamescope, game };
struct SectionInfo {
    Section section;
    const char* id; // "runner", "proton", "extras", "nvidia", "mesa", "wrappers", "gamescope", "game"
    Text title;
};
std::span<const SectionInfo> sections(); // In display order.

enum class Type {
    toggle,      // "1" / "0"
    choice,      // One of `choices` (value strings)
    number,      // Decimal in [minimum, maximum]
    text,        // One line of UTF-8 text
    path,        // An absolute path (UTF-8), or empty
    tool,        // A compat_tools::Tool::id, or "default"
    environment, // KEY=value lines (one per line); see value()/setValue()
};
// Which runners an option affects (several may be set).
inline constexpr uint8_t proton = 1; // umu-run and Proton run directly
inline constexpr uint8_t wine = 2;   // a plain Wine build
inline constexpr uint8_t any = proton | wine;

struct Choice {
    const char* value;
    Text label;
};
struct Option {
    const char* id;
    Section section;
    Type type;
    Text label;
    Text description;
    const char* sets;          // What it sets, exactly: "PROTON_USE_WINED3D=1", "gamescope -w <n>", ...
    uint8_t runners = any;
    std::span<const Choice> choices{};
    uint32_t minimum = 0, maximum = 0; // Type::number
    const char* depends_on = nullptr;  // Shown/enabled only while this toggle is on ("gamescope", "fsr").
    // Canonical text of the field in `options`; setValue() parses the same text.
    std::string (*get)(const Option&, const LaunchOptions&) = nullptr;
    bool (*set)(const Option&, LaunchOptions&, std::string_view) = nullptr;
    // A switch or choice over one environment variable (LaunchOptions::environment) instead of a field:
    // on sets `variable` to `variable_on`; a choice sets it to the chosen value ("" removes it). Kept in
    // the environment list only: the JSON store skips these options.
    const char* variable = nullptr;
    const char* variable_on = "1";
};
std::span<const Option> options(); // In display order, grouped by section.
const Option* find(std::string_view id);
// The variable switch or choice that stands for an environment variable, or null.
const Option* forVariable(std::string_view name);

// The canonical text of an option (empty for an unknown id). Toggles "1"/"0", choices their value,
// numbers decimal, environment "KEY=value" lines joined by '\n'.
std::string value(const LaunchOptions& options, std::string_view id);
// False (and `options` unchanged) for an unknown id or a value the option does not accept.
bool setValue(LaunchOptions& options, std::string_view id, std::string_view value);
// A variable name the environment list accepts: [A-Za-z_][A-Za-z0-9_]*, at most 128 characters.
bool variableName(std::string_view name);

// Popular winetricks verbs for the prefix tools ("Winetricks" list).
struct Verb {
    const char* verb;
    Text label;
};
std::span<const Verb> winetricksVerbs();

// WINEDLLOVERRIDES presets for the settings' check list: the DLLs ("winhttp", "mscoree,mshtml") and
// the mode they get ("n,b": the game's own first; "": disabled), with what needs them.
struct DllPreset {
    const char* dlls;
    const char* mode;
    Text label;
};
std::span<const DllPreset> dllPresets();
// The mode WINEDLLOVERRIDES text gives a DLL (names compare without case and ".dll"; the last entry
// wins); nullopt when it is not named.
std::optional<std::string> dllOverride(std::string_view overrides, std::string_view dll);
// Whether every DLL of the preset has its mode.
bool presetSet(std::string_view overrides, const DllPreset& preset);
// The text with the DLLs (comma separated) set to `mode` in one entry at its end, or removed
// (nullopt); every other entry keeps its order.
std::string withDllOverride(std::string_view overrides, std::string_view dlls, std::optional<std::string_view> mode);
// The text's entries in order: their DLLs ("d3d9,dxgi", lowercase, without ".dll") and mode ("n,b";
// "" disabled).
struct DllEntry {
    std::string dlls, mode;
    bool operator==(const DllEntry&) const = default;
};
std::vector<DllEntry> dllEntries(std::string_view overrides);

// Popular environment variables the settings offer to add to a game's list.
struct VariablePreset {
    const char* name;
    const char* value;
    Text label;
};
std::span<const VariablePreset> variablePresets();
}
