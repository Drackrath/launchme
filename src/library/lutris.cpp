#include "lutris.hpp"
#include "../platform/ascii_text.hpp"
#include "../platform/file_io.hpp"
#include "battlenet.hpp"
#include "store_adapter.hpp"
#include "xdg_paths.hpp"
#include "../platform/text_encoding.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>

namespace lutris {
namespace {
constexpr size_t max_rows = 20000;

bool isFolder(const std::filesystem::path& path) { std::error_code ec; return !path.empty() && path.is_absolute() && std::filesystem::is_directory(path, ec); }
bool isFile(const std::filesystem::path& path) { std::error_code ec; return !path.empty() && path.is_absolute() && std::filesystem::is_regular_file(path, ec); }
std::string readFile(const std::filesystem::path& path, size_t limit = 1024 * 1024) { return file_io::readText(path, limit); }
std::string_view trim(std::string_view text) { return ascii_text::trim(text, " \t\r"); }
std::filesystem::path absolute(std::string_view text) {
    const std::filesystem::path path(text_encoding::wideLenient(text));
    return path.is_absolute() ? path.lexically_normal() : std::filesystem::path{};
}
struct Database {
    sqlite3* handle = nullptr;
    ~Database() { if (handle) sqlite3_close(handle); }
};
struct Statement {
    sqlite3_stmt* handle = nullptr;
    ~Statement() { if (handle) sqlite3_finalize(handle); }
};
std::string column(sqlite3_stmt* statement, int index) {
    const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(statement, index));
    return text ? std::string(text, static_cast<size_t>(sqlite3_column_bytes(statement, index))).substr(0, 4096) : std::string{};
}
// The folders of a database's game configs: beside it (Lutris 0.5.13+) and in its config folder (older).
std::vector<std::filesystem::path> gameConfigs(const std::filesystem::path& database) {
    const auto data = database.parent_path();
    std::vector<std::filesystem::path> result{data / "games"};
    if (data.parent_path().filename() == "data") result.push_back(data.parent_path().parent_path() / "config" / "lutris" / "games"); // Flatpak.
    else if (const auto config = xdg_paths::configHome(); !config.empty()) result.push_back(config / "lutris" / "games");
    return result;
}
}

std::optional<std::vector<Row>> readDatabase(const std::filesystem::path& path) {
    if (!isFile(path)) return std::nullopt;
    Database database;
    // Read-only, no URI parsing of the path; Lutris may hold a write lock for a moment.
    if (sqlite3_open_v2(path.string().c_str(), &database.handle, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_busy_timeout(database.handle, 250);
    Statement statement;
    if (sqlite3_prepare_v2(database.handle, "SELECT * FROM games", -1, &statement.handle, nullptr) != SQLITE_OK) return std::nullopt;
    // Columns by name: older and newer schemas add and drop some.
    std::vector<std::string> names;
    for (int i = 0; i < sqlite3_column_count(statement.handle); ++i) names.emplace_back(sqlite3_column_name(statement.handle, i));
    std::vector<Row> result;
    int status = SQLITE_ROW;
    while (result.size() < max_rows && (status = sqlite3_step(statement.handle)) == SQLITE_ROW) {
        Row row;
        for (int i = 0; i < static_cast<int>(names.size()); ++i) {
            const auto& name = names[static_cast<size_t>(i)];
            if (name == "id") row.id = sqlite3_column_int64(statement.handle, i);
            else if (name == "name") row.name = column(statement.handle, i);
            else if (name == "slug") row.slug = column(statement.handle, i);
            else if (name == "installer_slug") row.installer_slug = column(statement.handle, i);
            else if (name == "runner") row.runner = column(statement.handle, i);
            else if (name == "service") row.service = column(statement.handle, i);
            else if (name == "service_id") row.service_id = column(statement.handle, i);
            else if (name == "configpath") row.configpath = column(statement.handle, i);
            else if (name == "directory") row.directory = absolute(column(statement.handle, i));
            else if (name == "installed") row.installed = sqlite3_column_int64(statement.handle, i) == 1;
            else if (name == "lastplayed") row.last_played = static_cast<uint64_t>(std::max<int64_t>(0, sqlite3_column_int64(statement.handle, i)));
            else if (name == "playtime") row.playtime_hours = sqlite3_column_double(statement.handle, i);
        }
        result.push_back(std::move(row));
    }
    if (status != SQLITE_ROW && status != SQLITE_DONE) return std::nullopt;
    return result;
}

std::optional<GameSource> storeOf(std::string_view service) {
    if (service == "gog") return GameSource::gog;
    if (service == "egs") return GameSource::epic;
    if (service == "ea_app" || service == "origin") return GameSource::ea;
    if (service == "ubisoft") return GameSource::ubisoft;
    if (service == "battlenet") return GameSource::battlenet;
    return std::nullopt;
}

GameAction launchAction(const std::filesystem::path& lutris, int64_t id, ActionKind kind) {
    if (lutris.empty() || id <= 0) return {};
    return {lutris.wstring(), L"lutris:rungameid/" + std::to_wstring(id), {}, kind};
}

std::optional<Game> makeGame(const Row& row, const std::filesystem::path& lutris) {
    const auto source = storeOf(row.service);
    if (!source || !row.installed || row.id <= 0) return std::nullopt;
    Game game;
    game.source = *source;
    game.kind = GameKind::store;
    // Battle.net: Lutris keeps the Agent's product code ("pro"); the model the launch code ("Pro").
    game.source_id = *source == GameSource::battlenet ? battlenet::familyOfCode(row.service_id) : row.service_id;
    game.name = text_encoding::wideLenient(row.name).substr(0, 512);
    std::erase_if(game.name, [](wchar_t ch) { return ch < 0x20; });
    if (!validStoreGameId(game.source, game.source_id) || game.name.empty()) return std::nullopt;
    configureStoreActions(game, {}, isFolder(row.directory) ? row.directory.wstring() : std::wstring{});
    game.play_action = launchAction(lutris, row.id);
    if (!game.play_action) return std::nullopt;
    game.last_played = row.last_played;
    if (std::isfinite(row.playtime_hours) && row.playtime_hours > 0)
        game.store_playtime_minutes = static_cast<uint32_t>(std::min(row.playtime_hours * 60, 4294967295.0));
    return game;
}

std::filesystem::path winePrefix(std::string_view yml) {
    // The "game:" section's own "prefix:" key; Lutris writes plain or quoted scalars.
    bool section = false;
    size_t start = 0;
    while (start < yml.size()) {
        const auto end = std::min(yml.find('\n', start), yml.size());
        const auto line = yml.substr(start, end - start);
        start = end + 1;
        if (trim(line).empty() || trim(line).front() == '#') continue;
        if (line.front() != ' ' && line.front() != '\t') { section = trim(line) == "game:"; continue; }
        const auto entry = trim(line);
        if (!section || !entry.starts_with("prefix:")) continue;
        auto value = trim(entry.substr(7));
        if (value.size() >= 2 && (value.front() == '\'' || value.front() == '"') && value.back() == value.front()) value = value.substr(1, value.size() - 2);
        return absolute(value);
    }
    return {};
}

bool battlenetClient(const Row& row) {
    return row.installed && row.runner == "wine" && row.service.empty() &&
        (row.slug == "battlenet" || row.slug == "battle-net" || row.installer_slug.starts_with("battlenet"));
}

Scan scan(const std::vector<Row>& rows, const std::filesystem::path& lutris, const std::vector<std::filesystem::path>& game_configs) {
    Scan result;
    std::set<std::string> seen;
    for (const auto& row : rows)
        if (auto game = makeGame(row, lutris); game && seen.insert(gameKey(*game)).second) result.games.push_back(std::move(*game));
    for (const auto& row : rows) {
        if (!battlenetClient(row)) continue;
        std::filesystem::path prefix;
        if (!row.configpath.empty() && row.configpath.find('/') == std::string::npos)
            for (const auto& folder : game_configs)
                if (prefix.empty()) prefix = winePrefix(readFile(folder / (row.configpath + ".yml")));
        if (prefix.empty() && isFolder(row.directory / "drive_c")) prefix = row.directory;
        if (!isFolder(prefix)) continue;
        const auto agent = prefix / "drive_c" / "ProgramData" / "Battle.net" / "Agent";
        auto battle = battlenet::scan(agent / "product.db", agent / "aggregate.json", {}, {}, {}, prefix);
        result.battlenet |= battle.available;
        const auto client = launchAction(lutris, row.id, ActionKind::open_client);
        for (auto& game : battle.games) {
            // The client opens; the game is one click away in it.
            if (game.installed) game.play_action = client;
            game.install_action = client;
            if (seen.insert(gameKey(game)).second) result.games.push_back(std::move(game));
        }
    }
    return result;
}

Scan scan(const std::filesystem::path& database, const std::filesystem::path& lutris) {
    const auto rows = readDatabase(database);
    return rows ? scan(*rows, lutris, gameConfigs(database)) : Scan{};
}

std::vector<std::filesystem::path> databases() {
    std::vector<std::filesystem::path> result;
    for (const auto& folder : {xdg_paths::dataHome() / "lutris", xdg_paths::configHome() / "lutris", xdg_paths::flatpakHome(L"net.lutris.Lutris") / "data" / "lutris"})
        if (isFile(folder / "pga.db") && std::find(result.begin(), result.end(), folder / "pga.db") == result.end()) result.push_back(folder / "pga.db");
    return result;
}

std::filesystem::path executable() {
    if (auto found = xdg_paths::findProgram({L"lutris"}); !found.empty()) return found;
    return xdg_paths::flatpakWrapper(L"net.lutris.Lutris");
}
}
