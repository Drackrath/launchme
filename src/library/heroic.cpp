#include "heroic.hpp"
#include "store_adapter.hpp"
#include "xdg_paths.hpp"
#include "../net/json.hpp"
#include "../platform/command_line.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>

namespace heroic {
namespace {
constexpr size_t max_bytes = 32 * 1024 * 1024; // gog_library.json of a large GOG account.
constexpr size_t max_games = 10000;

std::string readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() <= 0 || input.tellg() > static_cast<std::streamoff>(max_bytes)) return {};
    std::string bytes(static_cast<size_t>(input.tellg()), '\0');
    input.seekg(0);
    if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) return {};
    return bytes;
}
std::optional<json::Value> document(std::string_view bytes) {
    if (bytes.starts_with("\xef\xbb\xbf")) bytes.remove_prefix(3);
    return json::parse(bytes, max_bytes);
}
std::string field(const json::Value& value, const char* key) {
    const auto* found = value.get(key);
    return found && found->type == json::Value::Type::string ? found->text : std::string{};
}
bool flag(const json::Value& value, const char* key) { const auto* found = value.get(key); return found && found->flag; }
// Display text: no control characters or trademark signs (as the Windows scan), bounded.
std::wstring title(const std::string& text) {
    auto result = text_encoding::wideLenient(text).substr(0, 512);
    std::erase_if(result, [](wchar_t ch) { return ch < 0x20 || ch == L'\u00ae' || ch == L'\u2122' || ch == L'\u00a9'; });
    while (!result.empty() && result.back() == L' ') result.pop_back();
    return result;
}
Install install(const json::Value& entry, const char* app_key) {
    Install result;
    result.app = field(entry, app_key);
    result.title = title(field(entry, "title"));
    const std::filesystem::path folder(text_encoding::wideLenient(field(entry, "install_path")));
    if (folder.is_absolute()) result.directory = folder.lexically_normal();
    result.platform = field(entry, "platform");
    result.dlc = flag(entry, "is_dlc");
    return result;
}
std::filesystem::path absolutePath(const std::string& text) {
    const std::filesystem::path path(text_encoding::wideLenient(text));
    return path.is_absolute() ? path.lexically_normal() : std::filesystem::path{};
}
bool variableName(std::string_view key) {
    return !key.empty() && key.size() <= 128 && !(key.front() >= '0' && key.front() <= '9') &&
        std::all_of(key.begin(), key.end(), [](char ch) { return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_'; });
}
// POSIX shell quoting, as Python's shlex.quote: the backends split --wrapper with shlex.
std::string shellQuote(const std::string& text) {
    if (!text.empty() && std::all_of(text.begin(), text.end(), [](char ch) {
        return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || std::string_view("@%+=:,./-_").find(ch) != std::string_view::npos;
    })) return text;
    std::string result = "'";
    for (const char ch : text) result += ch == '\'' ? std::string("'\"'\"'") : std::string(1, ch);
    return result + "'";
}
bool isFolder(const std::filesystem::path& path) { std::error_code ec; return !path.empty() && path.is_absolute() && std::filesystem::is_directory(path, ec); }
// A title the GOG game names itself: goggame-<id>.info (Windows builds) or gameinfo's first line (Linux builds).
std::wstring gogTitle(const std::filesystem::path& folder, const std::string& id) {
    if (const auto info = document(readFile(folder / ("goggame-" + id + ".info")))) if (auto name = title(field(*info, "name")); !name.empty()) return name;
    const auto info = readFile(folder / "gameinfo");
    return title(info.substr(0, std::min(info.find('\n'), size_t{512})));
}
}

std::vector<Install> parseLegendaryInstalled(std::string_view bytes) {
    std::vector<Install> result;
    const auto root = document(bytes);
    if (!root || root->type != json::Value::Type::object) return result;
    for (const auto& [key, entry] : root->properties) {
        if (entry.type != json::Value::Type::object || result.size() >= max_games) continue;
        auto item = install(entry, "app_name");
        if (item.app.empty()) item.app = key;
        if (item.app == key) result.push_back(std::move(item));
    }
    std::sort(result.begin(), result.end(), [](const Install& a, const Install& b) { return a.app < b.app; });
    return result;
}

std::vector<Install> parseGogInstalled(std::string_view bytes) {
    std::vector<Install> result;
    const auto root = document(bytes);
    const auto* installed = root ? root->get("installed") : nullptr;
    if (!installed || installed->type != json::Value::Type::array) return result;
    for (const auto& entry : installed->elements)
        if (entry.type == json::Value::Type::object && result.size() < max_games) result.push_back(install(entry, "appName"));
    return result;
}

std::map<std::string, std::wstring> parseGogTitles(std::string_view bytes) {
    std::map<std::string, std::wstring> result;
    const auto root = document(bytes);
    const auto* games = root ? root->get("games") : nullptr;
    if (!games || games->type != json::Value::Type::array) return result;
    for (const auto& entry : games->elements) {
        auto name = title(field(entry, "title"));
        if (const auto app = field(entry, "app_name"); !app.empty() && !name.empty() && result.size() < max_games) result.emplace(app, std::move(name));
    }
    return result;
}

uint64_t isoTime(std::string_view text) {
    // YYYY-MM-DDTHH:MM:SS, then optional fraction and "Z" (Heroic writes UTC only).
    if (text.size() < 19 || text[4] != '-' || text[7] != '-' || text[10] != 'T' || text[13] != ':' || text[16] != ':') return 0;
    const auto number = [&](size_t at, size_t length) {
        int value = 0;
        const auto [end, error] = std::from_chars(text.data() + at, text.data() + at + length, value);
        return error == std::errc{} && end == text.data() + at + length ? value : -1;
    };
    const int year = number(0, 4), month = number(5, 2), day = number(8, 2), hour = number(11, 2), minute = number(14, 2), second = number(17, 2);
    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 60) return 0;
    auto rest = text.substr(19);
    if (rest.starts_with('.')) { rest.remove_prefix(1); while (!rest.empty() && rest.front() >= '0' && rest.front() <= '9') rest.remove_prefix(1); }
    if (rest != "Z" && !rest.empty()) return 0;
    // Days from the civil date (Howard Hinnant's algorithm).
    const int y = year - (month <= 2), era = y / 400, yoe = y - era * 400;
    const int doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const int64_t days = static_cast<int64_t>(era) * 146097 + doe - 719468;
    return static_cast<uint64_t>(days * 86400 + hour * 3600 + minute * 60 + second);
}

std::map<std::string, PlayTime> parsePlayTimes(std::string_view bytes) {
    std::map<std::string, PlayTime> result;
    const auto root = document(bytes);
    if (!root || root->type != json::Value::Type::object) return result;
    for (const auto& [app, entry] : root->properties) {
        if (entry.type != json::Value::Type::object || result.size() >= max_games) continue;
        PlayTime time{isoTime(field(entry, "lastPlayed"))};
        if (const auto* total = entry.get("totalPlayed"); total && total->type == json::Value::Type::number && total->numeric > 0)
            time.minutes = total->numeric >= 4294967295.0 ? UINT32_MAX : static_cast<uint32_t>(total->numeric);
        result.emplace(app, time);
    }
    return result;
}

EpicIds parseLegendaryMetadata(std::string_view bytes) {
    const auto root = document(bytes);
    const auto* metadata = root ? root->get("metadata") : nullptr;
    if (!metadata) return {};
    return {field(*metadata, "namespace"), field(*metadata, "id")};
}

GameAction launchAction(std::string_view runner, const std::string& app) {
    if (runner != "legendary" && runner != "gog") return {};
    return {L"heroic://launch?appName=" + text_encoding::wide(app) + L"&runner=" + text_encoding::wide(runner)};
}

Settings parseSettings(std::string_view bytes, std::string_view app) {
    Settings result;
    const auto root = document(bytes);
    const auto* game = root ? root->get(app) : nullptr;
    if (!game || game->type != json::Value::Type::object) return result;
    result.found = true;
    if (const auto* wine = game->get("wineVersion")) {
        result.wine_type = field(*wine, "type");
        // Older configs quote the binary ('...'), as Heroic itself tolerates.
        auto bin = field(*wine, "bin");
        if (bin.size() >= 2 && bin.front() == '\'' && bin.back() == '\'') bin = bin.substr(1, bin.size() - 2);
        result.wine_bin = absolutePath(bin);
    }
    result.wine_prefix = absolutePath(field(*game, "winePrefix"));
    result.launcher_args = field(*game, "launcherArgs");
    result.language = field(*game, "language");
    result.esync = flag(*game, "enableEsync");
    result.fsync = flag(*game, "enableFsync");
    if (const auto* options = game->get("enviromentOptions"); options && options->type == json::Value::Type::array)
        for (const auto& option : options->elements)
            if (const auto key = field(option, "key"); variableName(key) && result.environment.size() < 64)
                result.environment.emplace_back(key, field(option, "value"));
    if (const auto* options = game->get("wrapperOptions"); options && options->type == json::Value::Type::array)
        for (const auto& option : options->elements)
            if (const auto exe = field(option, "exe"); !exe.empty() && result.wrappers.size() < 16) {
                const auto args = field(option, "args");
                result.wrappers.push_back(shellQuote(exe) + (args.empty() ? "" : " " + args));
            }
    return result;
}

std::vector<std::filesystem::path> resourceFolders(const std::filesystem::path& config) {
    // Heroic's resources: beside its program (/opt/Heroic, a package's symlink on PATH), or in
    // Flatpak's installation (/app/bin/heroic/resources inside the sandbox).
    std::vector<std::filesystem::path> native, flatpak;
    std::error_code ec;
    if (const auto program = xdg_paths::findProgram({L"heroic"}); !program.empty())
        if (const auto real = std::filesystem::canonical(program, ec); !ec) native.push_back(real.parent_path() / "resources");
    native.push_back("/opt/Heroic/resources");
    const auto relative = std::filesystem::path("app") / "com.heroicgameslauncher.hgl" / "current" / "active" / "files" / "bin" / "heroic" / "resources";
    flatpak.push_back(std::filesystem::path("/var/lib/flatpak") / relative);
    if (const auto home = xdg_paths::home(); !home.empty()) flatpak.push_back(home / ".local" / "share" / "flatpak" / relative);
    const auto sandbox = xdg_paths::flatpakHome(L"com.heroicgameslauncher.hgl");
    const bool flatpak_config = !sandbox.empty() && config.lexically_normal().wstring().starts_with(sandbox.lexically_normal().wstring());
    auto& first = flatpak_config ? flatpak : native;
    auto& second = flatpak_config ? native : flatpak;
    first.insert(first.end(), second.begin(), second.end());
    return first;
}

Backends findBackends(const std::filesystem::path& config, const std::vector<std::filesystem::path>& resources) {
    Backends result;
    if (const auto root = document(readFile(config / "config.json")))
        if (const auto* settings = root->get("defaultSettings")) {
            if (const auto bin = absolutePath(field(*settings, "altLegendaryBin")); xdg_paths::executableFile(bin)) result.legendary = bin;
            if (const auto bin = absolutePath(field(*settings, "altGogdlBin")); xdg_paths::executableFile(bin)) result.gogdl = bin;
        }
    for (const auto& folder : resources) {
        const auto bin = folder / "app.asar.unpacked" / "build" / "bin" / "x64" / "linux";
        if (result.legendary.empty() && xdg_paths::executableFile(bin / "legendary")) result.legendary = bin / "legendary";
        if (result.gogdl.empty() && xdg_paths::executableFile(bin / "gogdl")) result.gogdl = bin / "gogdl";
    }
    if (result.legendary.empty()) result.legendary = xdg_paths::findProgram({L"legendary"});
    if (result.gogdl.empty()) result.gogdl = xdg_paths::findProgram({L"gogdl"});
    return result;
}

GameAction backendAction(std::string_view runner, const Install& install, const std::filesystem::path& config,
                         const Backends& backends, const Settings& settings, const std::filesystem::path& home) {
    const bool gog = runner == "gog";
    const auto& backend = gog ? backends.gogdl : backends.legendary;
    const std::filesystem::path env("/usr/bin/env");
    if ((runner != "legendary" && !gog) || backend.empty() || !isFolder(install.directory) || !xdg_paths::executableFile(env)) return {};
    std::string platform = install.platform;
    std::transform(platform.begin(), platform.end(), platform.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    const bool native = platform == "linux";
    if (!native && platform != "windows") return {}; // macOS builds.
    // Environment, in Heroic's order (later wins): the game's own variables, Wine's, the backend's config.
    std::vector<std::pair<std::string, std::string>> environment = settings.environment;
    std::vector<std::string> flags;
    std::string wrapper;
    for (const auto& entry : settings.wrappers) wrapper += (wrapper.empty() ? "" : " ") + entry;
    if (!native) {
        if (!settings.found || !xdg_paths::executableFile(settings.wine_bin) || settings.wine_prefix.empty()) return {};
        const auto prefix = settings.wine_prefix.string();
        if (settings.wine_type == "wine") {
            environment.emplace_back("WINEPREFIX", prefix);
            // Heroic keeps winemenubuilder from touching file associations, after the user's own overrides.
            const auto overrides = std::find_if(environment.begin(), environment.end(), [](const auto& entry) {
                std::string key = entry.first;
                std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                return key == "winedlloverrides";
            });
            if (overrides != environment.end()) overrides->second += ";winemenubuilder.exe=d";
            else environment.emplace_back("WINEDLLOVERRIDES", "winemenubuilder.exe=d");
            if (settings.esync) environment.emplace_back("WINEESYNC", "1");
            if (settings.fsync) environment.emplace_back("WINEFSYNC", "1");
            flags = {"--wine", settings.wine_bin.string(), "--wine-prefix", prefix};
            if (!wrapper.empty()) flags.insert(flags.end(), {"--wrapper", wrapper});
        } else if (settings.wine_type == "proton") {
            if (home.empty()) return {};
            environment.emplace_back("STEAM_COMPAT_CLIENT_INSTALL_PATH", (home / ".steam" / "steam").string());
            environment.emplace_back("WINEPREFIX", prefix);
            environment.emplace_back("STEAM_COMPAT_DATA_PATH", prefix);
            environment.emplace_back("PROTONPATH", settings.wine_bin.parent_path().string());
            if (!settings.esync) environment.emplace_back("PROTON_NO_ESYNC", "1");
            if (!settings.fsync) environment.emplace_back("PROTON_NO_FSYNC", "1");
            flags = {"--no-wine", "--wrapper", (wrapper.empty() ? "" : wrapper + " ") + shellQuote(settings.wine_bin.string()) + " waitforexitandrun"};
        } else return {}; // Wine toolkit, CrossOver: macOS.
    } else if (!wrapper.empty()) flags = {"--wrapper", wrapper};
    std::vector<std::string> arguments;
    for (const auto& [key, value] : environment) arguments.push_back(key + "=" + value);
    if (gog) {
        arguments.push_back("GOGDL_CONFIG_PATH=" + (config / "gogdlConfig").string());
        arguments.insert(arguments.end(), {backend.string(), "--auth-config-path", (config / "gog_store" / "auth.json").string(),
                                           "launch", install.directory.string(), install.app});
        arguments.insert(arguments.end(), flags.begin(), flags.end());
        arguments.insert(arguments.end(), {"--platform", platform});
    } else {
        arguments.push_back("LEGENDARY_CONFIG_PATH=" + (config / "legendaryConfig" / "legendary").string());
        arguments.insert(arguments.end(), {backend.string(), "launch", install.app});
        arguments.insert(arguments.end(), flags.begin(), flags.end());
        if (!settings.language.empty()) arguments.insert(arguments.end(), {"--language", settings.language});
    }
    // launch.cpp splits on spaces and double quotes; anything it could not carry falls back to Heroic.
    std::wstring line;
    for (const auto& argument : arguments) {
        if (argument.empty() || argument.find_first_of(std::string_view("\"\\\0\r\n", 5)) != std::string::npos) return {};
        const auto wide = text_encoding::wide(argument);
        if (wide.empty()) return {};
        if (!line.empty()) line += L' ';
        line += wide.find_first_of(L" \t'") == std::wstring::npos ? wide : command_line::quoteArgument(wide);
    }
    if (!settings.launcher_args.empty()) {
        if (settings.launcher_args.find_first_of(std::string_view("\0\r\n", 3)) != std::string::npos) return {};
        line += L' ' + text_encoding::wideLenient(settings.launcher_args);
    }
    return {env.wstring(), std::move(line), install.directory, ActionKind::play};
}

std::vector<Game> scan(const std::filesystem::path& config) { return scan(config, findBackends(config, resourceFolders(config))); }

std::vector<Game> scan(const std::filesystem::path& config, const Backends& backends) {
    std::vector<Game> result;
    if (!isFolder(config)) return result;
    const auto home = xdg_paths::home();
    const auto times = parsePlayTimes(readFile(config / "store" / "timestamp.json"));
    const auto add = [&](GameSource source, const Install& install, std::wstring name, std::string_view runner) {
        if (install.dlc || !validStoreGameId(source, install.app) || !isFolder(install.directory)) return;
        Game game;
        game.source = source;
        game.kind = GameKind::store;
        game.source_id = install.app;
        game.name = name.empty() ? install.directory.filename().wstring() : std::move(name);
        configureStoreActions(game, {}, install.directory.wstring());
        game.play_action = backendAction(runner, install, config, backends,
            parseSettings(readFile(config / "GamesConfig" / (install.app + ".json")), install.app), home);
        if (!game.play_action) game.play_action = launchAction(runner, install.app);
        if (const auto time = times.find(install.app); time != times.end()) {
            game.last_played = time->second.last_played;
            game.store_playtime_minutes = time->second.minutes;
        }
        if (source == GameSource::epic) {
            const auto ids = parseLegendaryMetadata(readFile(config / "legendaryConfig" / "legendary" / "metadata" / (install.app + ".json")));
            if (validStoreGameId(GameSource::epic, ids.namespace_id) && validStoreGameId(GameSource::epic, ids.catalog_item)) {
                game.epic_namespace = ids.namespace_id; game.epic_catalog = ids.catalog_item;
            }
        }
        result.push_back(std::move(game));
    };
    for (const auto& install : parseLegendaryInstalled(readFile(config / "legendaryConfig" / "legendary" / "installed.json")))
        add(GameSource::epic, install, install.title, "legendary");
    auto titles = parseGogTitles(readFile(config / "store_cache" / "gog_library.json"));
    if (titles.empty()) titles = parseGogTitles(readFile(config / "gog_store" / "library.json"));
    for (const auto& install : parseGogInstalled(readFile(config / "gog_store" / "installed.json"))) {
        const auto known = titles.find(install.app);
        add(GameSource::gog, install, known != titles.end() ? known->second : gogTitle(install.directory, install.app), "gog");
    }
    return result;
}

std::vector<std::filesystem::path> configFolders() {
    std::vector<std::filesystem::path> result;
    for (const auto& base : {xdg_paths::configHome(), xdg_paths::flatpakHome(L"com.heroicgameslauncher.hgl") / "config"})
        if (!base.empty() && isFolder(base / "heroic")) result.push_back(base / "heroic");
    return result;
}

std::filesystem::path executable() {
    if (auto found = xdg_paths::findProgram({L"heroic"}); !found.empty()) return found;
    if (xdg_paths::executableFile("/opt/Heroic/heroic")) return "/opt/Heroic/heroic";
    return xdg_paths::flatpakWrapper(L"com.heroicgameslauncher.hgl");
}
}
