#include "web_import.hpp"
#include "../library/store_adapter.hpp"
#include "../platform/protected_file.hpp"
#include "../platform/text_encoding.hpp"

#include <iomanip>
#include <set>
#include <sstream>

namespace web_import {
namespace {
constexpr const char* header = "launchme-import-v1";
bool valid(GameSource source, const Snapshot& snapshot) {
    if (snapshot.account.empty() || snapshot.account.size() > 128 || snapshot.games.size() > 10000 ||
        snapshot.account.find_first_of("\r\n\"") != std::string::npos) return false;
    std::set<std::string> keys;
    for (const auto& game : snapshot.games)
        if (game.source != source || !validStoreGameId(source, game.source_id) || game.name.empty() || game.name.size() > 512 ||
            !keys.insert(gameKey(game)).second) return false;
    return true;
}
}

std::string serialize(GameSource source, const Snapshot& snapshot) {
    if (!valid(source, snapshot)) return {};
    std::ostringstream data;
    data.imbue(std::locale::classic());
    data << header << ' ' << sourceSlug(source) << ' ' << std::quoted(snapshot.account) << ' ' << snapshot.synced_at << ' ' << snapshot.games.size() << '\n';
    for (const auto& game : snapshot.games) data << std::quoted(game.source_id) << ' ' << std::quoted(text_encoding::utf8(game.name)) << '\n';
    return data.str();
}

std::optional<Snapshot> deserialize(GameSource source, std::string_view text) {
    std::istringstream data{std::string(text)};
    data.imbue(std::locale::classic());
    std::string magic, slug;
    Snapshot snapshot;
    size_t count = 0;
    if (!(data >> magic >> slug >> std::quoted(snapshot.account) >> snapshot.synced_at >> count) ||
        magic != header || slug != sourceSlug(source) || count > 10000) return {};
    for (size_t i = 0; i < count; ++i) {
        Game game;
        std::string name;
        if (!(data >> std::quoted(game.source_id) >> std::quoted(name))) return {};
        game.source = source; game.kind = GameKind::store; game.installed = false;
        game.name = text_encoding::wide(name);
        game.account_id = snapshot.account;
        snapshot.games.push_back(std::move(game));
    }
    data >> std::ws;
    if (!data.eof() || !valid(source, snapshot)) return {};
    return snapshot;
}

std::filesystem::path file(const std::filesystem::path& root, GameSource source) {
    return root / L"accounts" / (text_encoding::wide(sourceSlug(source)) + L".library");
}

std::optional<Snapshot> load(const std::filesystem::path& root, GameSource source) {
    std::error_code ec;
    const auto path = file(root, source);
    if (!std::filesystem::exists(path, ec)) return {};
    const auto text = protected_file::read(path, 8 * 1024 * 1024);
    return text && !text->empty() ? deserialize(source, *text) : std::nullopt;
}

bool save(const std::filesystem::path& root, GameSource source, const Snapshot& snapshot) {
    const auto text = serialize(source, snapshot);
    return !text.empty() && protected_file::write(file(root, source), text, L"Launchme store library");
}

bool clear(const std::filesystem::path& root, GameSource source) {
    std::error_code ec;
    std::filesystem::remove(file(root, source), ec);
    return !ec;
}
}
