#pragma once

#include "../model/game.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Lutris (Linux): its game database, pga.db (SQLite, read-only; never its CLI or a network).
// ~/.local/share/lutris/pga.db (older: ~/.config/lutris), Flatpak's ~/.var/app/net.lutris.Lutris/data/lutris.
// Only games of a store the model knows are listed: rows whose service is gog, egs (Epic), ea_app,
// ubisoft or battlenet, under that store's ID (service_id). Lutris starts them itself: its program
// with "lutris:rungameid/<row id>" (Lutris' own URI; the launcher opens it like a link). Steam games
// come from Steam's own scan; games without a store (Wine installers, emulators, native ports)
// have no source in the model and are not listed.
// Battle.net installed with Lutris' "battlenet" installer: the Agent's files in its Wine prefix
// (library/battlenet.hpp); its games open the client (no command line reaches a game through it).
namespace lutris {
struct Row {
    int64_t id = 0;
    std::string name, slug, installer_slug, runner, service, service_id, configpath;
    std::filesystem::path directory;
    uint64_t last_played = 0;     // Unix seconds.
    double playtime_hours = 0;
    bool installed = false;
};
struct Scan {
    std::vector<Game> games;
    bool battlenet = false; // A Battle.net prefix's product.db was read.
};

// The rows of the games table; nullopt when it cannot be opened or read.
std::optional<std::vector<Row>> readDatabase(const std::filesystem::path& database);
// GameSource of a Lutris service ("egs" → epic); nullopt for any other (steam, humblebundle, ...).
std::optional<GameSource> storeOf(std::string_view service);
GameAction launchAction(const std::filesystem::path& lutris, int64_t id, ActionKind kind = ActionKind::play);
// An installed store game of a row; nullopt when the row is none.
std::optional<Game> makeGame(const Row& row, const std::filesystem::path& lutris);
// The Wine prefix in a game's YAML config (game: prefix: ...); empty when it names none.
std::filesystem::path winePrefix(std::string_view yml);
bool battlenetClient(const Row& row);
// game_configs: the folders of the games' YAML configs (<configpath>.yml).
Scan scan(const std::vector<Row>& rows, const std::filesystem::path& lutris, const std::vector<std::filesystem::path>& game_configs);
Scan scan(const std::filesystem::path& database, const std::filesystem::path& lutris);

// Linux: the databases that exist and Lutris' program (PATH, Flatpak's wrapper); empty when none.
std::vector<std::filesystem::path> databases();
std::filesystem::path executable();
}
