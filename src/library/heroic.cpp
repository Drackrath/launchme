#include "heroic.hpp"
#include "store_adapter.hpp"
#include "xdg_paths.hpp"
#include "../net/json.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
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

std::vector<Game> scan(const std::filesystem::path& config) {
    std::vector<Game> result;
    if (!isFolder(config)) return result;
    const auto times = parsePlayTimes(readFile(config / "store" / "timestamp.json"));
    const auto add = [&](GameSource source, const Install& install, std::wstring name, std::string_view runner) {
        if (install.dlc || !validStoreGameId(source, install.app) || !isFolder(install.directory)) return;
        Game game;
        game.source = source;
        game.kind = GameKind::store;
        game.source_id = install.app;
        game.name = name.empty() ? install.directory.filename().wstring() : std::move(name);
        configureStoreActions(game, {}, install.directory.wstring());
        game.play_action = launchAction(runner, install.app);
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
