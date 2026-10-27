#include "store_adapter.hpp"
#include "steam.hpp"
#include "../net/json.hpp"
#include "../net/url_encoding.hpp"
#include "../platform/command_line.hpp"
#include "../platform/text_encoding.hpp"

#ifdef _WIN32
#include <Windows.h>
#endif

#include <algorithm>
#include <charconv>
#include <cwctype>
#include <fstream>
#include <map>

namespace {
// Snapshot JSON: strict, malformed input becomes empty.
std::wstring wide(const std::string& value) { return text_encoding::wide(value); }

std::string field(const json::Value& value, const char* key) {
    const auto* item = value.get(key);
    return item ? item->stringOr() : std::string{};
}

uint64_t timestamp(const json::Value& value, const char* key) {
    // Strings avoid rounding large integer values through JSON's double representation.
    const auto text = field(value, key);
    uint64_t number = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
    return error == std::errc{} && end == text.data() + text.size() ? number : 0;
}

bool validId(GameSource source, const std::string& id) {
    if (id.empty() || id.size() > 96) return false;
    for (unsigned char ch : id) {
        if (ch >= '0' && ch <= '9') continue;
        if (source == GameSource::gog || source == GameSource::ubisoft) return false;
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')) continue;
        if ((source == GameSource::ea || source == GameSource::epic) && (ch == ':' || ch == '-' || ch == '_' || ch == '.')) continue;
        // Xbox: the package family name, e.g. Publisher.Game_8wekyb3d8bbwe.
        if (source == GameSource::xbox && (ch == '-' || ch == '_' || ch == '.')) continue;
        return false;
    }
    // Keep numeric IDs canonical, while retaining their full precision as strings.
    return (source != GameSource::gog && source != GameSource::ubisoft) || id.front() != '0';
}

std::wstring uriId(const std::string& id) { return url_encoding::component(std::string_view(id)); }

bool clientPath(const std::wstring& value) {
    if (value.empty() || value.find_first_of(L"\r\n\"") != std::wstring::npos ||
        value.find(L'\0') != std::wstring::npos) return false;
    const std::filesystem::path path(value);
    auto extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
    return path.is_absolute() && extension == L".exe";
}

void planActions(Game& game, const std::wstring& client, const std::wstring& install_directory) {
    const auto id = uriId(game.source_id);
    switch (game.source) {
    case GameSource::gog:
        game.install_action = {L"goggalaxy://openGameView/" + id, {}, {}, ActionKind::open_client};
        if (clientPath(client) && !install_directory.empty())
            game.play_action = {client, L"/launchViaAutostart /gameId=" + id +
                                L" /command=runGame /path=" + command_line::quoteArgument(install_directory)};
        break;
    case GameSource::battlenet:
        game.install_action.kind = ActionKind::open_client;
        if (clientPath(client)) {
            game.play_action = {client, L"--exec=" + command_line::quoteArgument(L"launch " + id)};
            game.install_action = {client, {}, {}, ActionKind::open_client};
        }
        break;
    case GameSource::ea:
        game.install_action.kind = ActionKind::open_client;
        game.play_action = {L"origin2://game/launch?offerIds=" + id};
        if (clientPath(client)) game.install_action = {client, {}, {}, ActionKind::open_client};
        break;
    case GameSource::ubisoft:
        game.play_action = {L"uplay://launch/" + id + L"/0"};
        game.install_action = {L"uplay://install/" + id, {}, {}, ActionKind::install};
        break;
    case GameSource::epic:
        game.install_action = {L"com.epicgames.launcher://store/library", {}, {}, ActionKind::open_client};
        break;
    case GameSource::xbox:
        // Graj needs the package's application ID, which only the native scan knows (pc_platforms.cpp).
        game.install_action = {L"ms-windows-store://pdp/?PFN=" + id, {}, {}, ActionKind::open_client};
        break;
    default: break;
    }
}

template<GameSource Source>
class CatalogAdapter final : public StoreAdapter {
public:
    GameSource source() const override { return Source; }
    StoreLibrary scan(const std::filesystem::path& directory) const override {
        const auto file = directory / (std::string(sourceSlug(Source)) + ".json");
        std::error_code ec;
        if (!std::filesystem::exists(file, ec) && !ec) return {};
        std::ifstream input(file, std::ios::binary | std::ios::ate);
        if (!input || input.tellg() <= 0 || input.tellg() > 8 * 1024 * 1024)
            return {{}, {}, {std::wstring(sourceName(Source)) + tr(L": nie można odczytać katalogu")}};
        std::string bytes(static_cast<size_t>(input.tellg()), '\0');
        input.seekg(0);
        if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size())))
            return {{}, {}, {std::wstring(sourceName(Source)) + tr(L": niepełny odczyt katalogu")}};
        return parseStoreCatalog(Source, bytes);
    }
    GameAction action(const Game& game) const override {
        if (game.source != Source || !isGame(game) || !validId(Source, game.source_id)) return {};
        if (game.installation.active()) return game.install_action;
        return game.installed ? game.play_action : game.install_action;
    }
};
} // namespace

bool validStoreGameId(GameSource source, const std::string& id) { return validId(source, id); }

void configureStoreActions(Game& game, const std::wstring& client, const std::wstring& directory,
                           const std::string& epic_namespace, const std::string& epic_item) {
    if (!validId(game.source, game.source_id)) return;
    if (std::filesystem::path(directory).is_absolute()) game.install_directory = directory;
    planActions(game, client, directory);
    if (game.source == GameSource::epic && validId(GameSource::epic, epic_namespace) && validId(GameSource::epic, epic_item))
        game.play_action = {L"com.epicgames.launcher://apps/" + uriId(epic_namespace) + L"%3A" + uriId(epic_item) +
                            L"%3A" + uriId(game.source_id) + L"?action=launch&silent=true"};
}

StoreLibrary parseStoreCatalog(GameSource source, std::string_view bytes) {
    StoreLibrary result;
    const auto error = [&] { result.errors.push_back(std::wstring(sourceName(source)) + tr(L": nieprawidłowy katalog v1")); };
    if (bytes.starts_with("\xef\xbb\xbf")) bytes.remove_prefix(3);
    const auto root = bytes.size() <= 8 * 1024 * 1024 ? json::parse(bytes) : std::nullopt;
    const auto* version = root ? root->get("version") : nullptr;
    const auto* games = root ? root->get("games") : nullptr;
    if (source == GameSource::steam || !version || version->type != json::Value::Type::number ||
        version->numeric != 1 || !games || games->type != json::Value::Type::array || games->elements.size() > 10000) {
        error(); return result;
    }
    const auto client = wide(field(*root, "client"));
    if (clientPath(client)) result.clients.push_back({source, client});
    std::map<std::string, Game> unique;
    for (const auto& entry : games->elements) {
        Game game;
        game.source = source;
        game.kind = GameKind::store;
        game.source_id = field(entry, "id");
        game.name = wide(field(entry, "name"));
        const auto* installed = entry.get("installed");
        const auto visibility = field(entry, "visibility");
        if (!validId(source, game.source_id) || game.name.empty() || game.name.size() > 512 ||
            game.name.find_first_of(std::wstring(L"\0\r\n", 3)) != std::wstring::npos ||
            !installed || installed->type != json::Value::Type::boolean ||
            (visibility != "public" && visibility != "hidden" && visibility != "private")) {
            // Reject the entire snapshot: an invalid private duplicate must never expose a public entry.
            error(); return result;
        }
        game.installed = installed->flag;
        game.visibility = visibility == "public" ? GameVisibility::public_game :
                          visibility == "hidden" ? GameVisibility::hidden : GameVisibility::private_game;
        game.last_played = timestamp(entry, "last_played");
        game.last_updated = timestamp(entry, "last_updated");
        const auto directory = wide(field(entry, "install_dir"));
        if (!directory.empty() && (!std::filesystem::path(directory).is_absolute() ||
            directory.find_first_of(std::wstring(L"\0\r\n\"", 4)) != std::wstring::npos)) {
            error(); return result;
        }
        configureStoreActions(game, client, directory, field(entry, "namespace"), field(entry, "catalog_id"));
        const auto key = gameKey(game);
        if (unique.contains(key)) { error(); return result; }
        unique.emplace(key, std::move(game));
    }
    for (auto& [id, game] : unique) {
        (void)id;
        (game.visibility == GameVisibility::public_game ? result.visible : result.private_games).push_back(std::move(game));
    }
    return result;
}

std::unique_ptr<StoreAdapter> makeStoreAdapter(GameSource source) {
    switch (source) {
    case GameSource::gog: return std::make_unique<CatalogAdapter<GameSource::gog>>();
    case GameSource::battlenet: return std::make_unique<CatalogAdapter<GameSource::battlenet>>();
    case GameSource::ea: return std::make_unique<CatalogAdapter<GameSource::ea>>();
    case GameSource::ubisoft: return std::make_unique<CatalogAdapter<GameSource::ubisoft>>();
    case GameSource::epic: return std::make_unique<CatalogAdapter<GameSource::epic>>();
    case GameSource::xbox: return std::make_unique<CatalogAdapter<GameSource::xbox>>();
    case GameSource::multimc: return std::make_unique<CatalogAdapter<GameSource::multimc>>(); // Play: src/library/multimc.cpp.
    default: return {};
    }
}

StoreLibrary scanStoreLibraries(const std::filesystem::path& directory) {
    StoreLibrary result;
    for (const auto source : pc_sources) {
        auto library = makeStoreAdapter(source)->scan(directory);
        result.visible.insert(result.visible.end(), library.visible.begin(), library.visible.end());
        result.private_games.insert(result.private_games.end(), library.private_games.begin(), library.private_games.end());
        result.errors.insert(result.errors.end(), library.errors.begin(), library.errors.end());
        result.clients.insert(result.clients.end(), library.clients.begin(), library.clients.end());
    }
    return result;
}

GameAction primaryGameAction(const Game& game) {
    if (!isGame(game)) return {};
    if (game.source == GameSource::steam)
        return {steamActionUrl(game), {}, {}, game.installation.active() ? ActionKind::open_client : game.installed ? ActionKind::play : ActionKind::install};
    // User-added games (src/library/custom_games.cpp): a missing file has nothing to open.
    if (localFileGame(game.source)) return game.installed ? game.play_action : GameAction{};
    const auto adapter = makeStoreAdapter(game.source);
    return adapter ? adapter->action(game) : GameAction{};
}
