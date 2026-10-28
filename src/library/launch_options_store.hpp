#pragma once
// Wine/Proton launch options (src/model/launch_options.hpp): the global default and each game's own,
// and the prefix assigned to each game. One file, <config>/linux/launch_options.json:
//   {"version":1,"prefix_root":"/home/u/Games/launchme/prefixes","default":{...},
//    "games":{"<game key>":{...}},"prefixes":{"<game key>":"/home/u/Games/launchme/prefixes/<slug>"}}
// Keys are gameKey(); an options object lists only the fields that differ from LaunchOptions{}, by
// their catalog ids (launch_options_catalog.hpp). A game without its own entry uses the default;
// "default"'s compat_tool is the global tool choice ("default" there: automatic).
// Thread-safe (one process-wide lock); every write is atomic and durable. Portable.
#include "../model/game.hpp"
#include "../model/launch_options.hpp"
#include "../net/json.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace launch_options_store {
struct Document {
    LaunchOptions defaults;
    std::map<std::string, LaunchOptions> games;
    std::map<std::string, std::filesystem::path> prefixes;
    std::filesystem::path prefix_root; // Empty: defaultPrefixRoot().
    bool operator==(const Document&) const = default;
};

// Pure: the document's JSON and back. parse() skips invalid entries and values (they keep their
// defaults); nullopt only when the text is not a version 1 document.
std::string serialize(const Document& document);
std::optional<Document> parse(std::string_view text);
json::Value toJson(const LaunchOptions& options);
LaunchOptions fromJson(const json::Value& value);
// A prefix folder name for a game: its title in lowercase ASCII words joined by '-' (diacritics
// dropped), else the key's, else "game"; at most 64 characters.
std::string slug(std::wstring_view title, std::string_view key);
// umu's STORE for a source: egs, gog, amazon, ubisoft, ea, battlenet, steam; empty for the others.
std::string umuStore(GameSource source);

// <config>/linux/launch_options.json (LAUNCHME_CONFIG_DIR moves <config>, as for every file).
std::filesystem::path file();
Document load(); // An empty document when the file is missing or unreadable.
bool save(const Document& document);

// For the settings UI.
LaunchOptions defaults();
bool setDefaults(const LaunchOptions& options);
// The global tool (compat_tools::Tool::id, or "default" for the automatic choice).
std::string defaultTool();
bool setDefaultTool(const std::string& id);
// The game's own options; nullopt while it follows the default.
std::optional<LaunchOptions> gameOptions(const std::string& key);
// The options a launch uses: the game's own, else the default (compat_tool not resolved).
LaunchOptions effectiveOptions(const std::string& key);
bool setGameOptions(const std::string& key, const LaunchOptions& options);
bool resetGameOptions(const std::string& key); // Back to the default.
// ~/Games/launchme/prefixes; the prefixes root in use (settable) and the folder of one game.
std::filesystem::path defaultPrefixRoot();
std::filesystem::path prefixRoot();
bool setPrefixRoot(const std::filesystem::path& root); // Empty: back to the default; else absolute.
// The game's prefix: its options' prefix, else the one assigned to it, else a new <root>/<slug>
// (another game's folder gets -2, -3...), remembered so a rename keeps the prefix.
std::filesystem::path gamePrefix(const Game& game);
// Keys of games whose own options name the tool ("default" listed when the global choice names it):
// the warning before removing a tool.
std::vector<std::string> gamesUsingTool(const std::string& tool_id);
// Keys of games whose prefix is `prefix` (shared prefixes; the warning before deleting one).
std::vector<std::string> gamesUsingPrefix(const std::filesystem::path& prefix);

// What GameAction::compat carries for a game's Windows program (src/platform/wine_runner.hpp):
// its identity for umu (STORE, store ID, Steam app ID), its install folder, its effective options
// with compat_tool "default" replaced by the global choice, and its prefix (assigned on first use).
std::shared_ptr<const CompatLaunch> compatLaunch(const Game& game);
// No game known: the default options in <root>/default.
CompatLaunch defaultCompatLaunch();
}
