#include "battlenet.hpp"
#include "store_adapter.hpp"
#include "../net/json.hpp"
#include "../platform/text_encoding.hpp"
#ifdef _WIN32
#include <Windows.h>
#endif
#include <algorithm>
#include <charconv>
#include <map>
#include <set>

namespace battlenet {
namespace {
constexpr size_t max_bytes = 8 * 1024 * 1024;
struct Field { unsigned id = 0, wire = 0; uint64_t number = 0; std::string_view bytes; };
using Fields = std::vector<Field>;

std::optional<Fields> decode(std::string_view bytes) {
    Fields fields;
    size_t cursor = 0;
    const auto varint = [&]() -> std::optional<uint64_t> {
        uint64_t value = 0;
        for (unsigned shift = 0; shift < 64 && cursor < bytes.size(); shift += 7) {
            const auto byte = static_cast<unsigned char>(bytes[cursor++]);
            if (shift == 63 && byte > 1) return {};
            value |= static_cast<uint64_t>(byte & 127) << shift;
            if (!(byte & 128)) return value;
        }
        return {};
    };
    while (cursor < bytes.size()) {
        if (fields.size() >= 10000) return {};
        const auto tag = varint();
        if (!tag || !(*tag >> 3) || (*tag >> 3) > 0x1fffffff) return {};
        Field f{static_cast<unsigned>(*tag >> 3), static_cast<unsigned>(*tag & 7)};
        if (!f.wire) {
            const auto value = varint();
            if (!value) return {};
            f.number = *value;
        } else {
            const auto size = f.wire == 2 ? varint() : f.wire == 1 ? std::optional<uint64_t>{8} :
                              f.wire == 5 ? std::optional<uint64_t>{4} : std::nullopt;
            if (!size || *size > bytes.size() - cursor) return {};
            f.bytes = bytes.substr(cursor, static_cast<size_t>(*size));
            cursor += static_cast<size_t>(*size);
        }
        fields.push_back(f);
    }
    return fields;
}
// Known singular fields must have the expected wire type and occur once.
// Ignoring a malformed state would otherwise turn a torn write into "ready".
bool schema(const Fields& fields, std::initializer_list<std::pair<unsigned, unsigned>> types) {
    for (const auto [id, wire] : types) {
        unsigned count = 0;
        for (const auto& f : fields) if (f.id == id && (++count > 1 || f.wire != wire)) return false;
    }
    return true;
}
std::string_view text(const Fields& fields, unsigned id) {
    for (const auto& f : fields) if (f.id == id && f.wire == 2) return f.bytes;
    return {};
}
std::optional<bool> flag(const Fields& fields, unsigned id) {
    for (const auto& f : fields) if (f.id == id && f.wire == 0 && f.number <= 1) return f.number == 1;
    return {};
}
std::string fold(std::string value) {
    for (char& c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return value;
}
bool identifier(std::string_view value) {
    return !value.empty() && value.size() <= 96 && std::all_of(value.begin(), value.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
    });
}
struct Product { const char* uid; const char* code; const char* family; const wchar_t* name; };
// Retail UID -> family from pinned Playnite; Agent codes from Lutris/Unifideck.
// No prefix matching (e.g. wow_beta != wow); preserve existing family keys.
constexpr Product products[] = {
    {"wow", "wow", "WoW", L"World of Warcraft"}, {"diablo3", "d3", "D3", L"Diablo III"},
    {"s2", "s2", "S2", L"StarCraft II"}, {"s1", "s1", "S1", L"StarCraft"},
    {"hs_beta", "hsb", "WTCG", L"Hearthstone"}, {"heroes", "hero", "Hero", L"Heroes of the Storm"},
    {"prometheus", "pro", "Pro", L"Overwatch 2"}, {"w3", "w3", "W3", L"Warcraft III: Reforged"},
    {"osi", "osi", "OSI", L"Diablo II: Resurrected"}, {"fenris", "fenris", "Fen", L"Diablo IV"},
    {"fen", "fenris", "Fen", L"Diablo IV"}, {"anbs", "anbs", "ANBS", L"Diablo Immortal"},
    {"viper", "viper", "VIPR", L"Call of Duty: Black Ops 4"},
    {"odin", "odin", "ODIN", L"Call of Duty: Modern Warfare"},
    {"lazarus", "lazarus", "LAZR", L"Call of Duty: Modern Warfare 2 Campaign Remastered"},
    {"zeus", "zeus", "ZEUS", L"Call of Duty: Black Ops Cold War"},
    {"fore", "fore", "FORE", L"Call of Duty: Vanguard"},
    {"auks", "auks", "AUKS", L"Call of Duty: Modern Warfare II"},
    {"pinta", "", "PNTA", L"Call of Duty: Modern Warfare III"},
    {"wlby", "wlby", "WLBY", L"Crash Bandicoot 4"},
    {"w1r", "w1r", "W1R", L"Warcraft: Remastered"}, {"w2r", "w2r", "W2R", L"Warcraft II: Remastered"},
    {"gryphon", "gryphon", "GRY", L"Warcraft Rumble"}, {"rtro", "rtro", "RTRO", L"Blizzard Arcade Collection"},
    {"d1", "", "D1", L"Diablo"}, {"w1", "w1", "W1", L"Warcraft: Orcs & Humans"},
    {"w2", "", "W2", L"Warcraft II: Battle.net Edition"},
    {"aris", "", "ARIS", L"Doom: The Dark Ages"}, {"scorpio", "", "SCOR", L"Sea of Thieves"},
    {"arkansas", "", "ARK", L"The Outer Worlds 2"}, {"libra", "", "LBRA", L"Tony Hawk's Pro Skater 3 + 4"},
    {"aqua", "", "AQUA", L"Avowed"}
};
const Product* byUid(const std::string& uid) {
    const auto key = fold(uid);
    for (const auto& p : products) if (key == p.uid) return &p;
    return nullptr;
}
const Product* byCode(const std::string& code) {
    const auto key = fold(code);
    for (const auto& p : products) if (*p.code && key == p.code) return &p;
    return nullptr;
}
std::optional<std::string> read(const std::filesystem::path& path) {
    // Sharing permits atomic replacement, but not an in-place concurrent writer.
    // A sharing violation is unknown, not an empty or completed installation.
    if (path.empty()) return {};
    const auto handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return {};
    struct Close { HANDLE handle; ~Close() { CloseHandle(handle); } } close{handle};
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(handle, &size) || size.QuadPart <= 0 || size.QuadPart > max_bytes) return {};
    std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
    DWORD count = 0;
    if (!ReadFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr) || count != bytes.size()) return {};
    return bytes;
}
bool directory(const std::filesystem::path& path) {
    std::error_code ec;
    return !path.empty() && path.is_absolute() && std::filesystem::is_directory(path, ec);
}
Game gameFor(const Product& product, const std::wstring& client, const std::filesystem::path& path) {
    Game game;
    game.source = GameSource::battlenet; game.kind = GameKind::store;
    game.source_id = product.family; game.name = product.name; game.installed = false;
    game.installation.phase = InstallPhase::unknown;
    configureStoreActions(game, client, path.wstring());
    return game;
}
}

std::optional<std::vector<ProductInstall>> parseProductDb(std::string_view bytes) {
    if (bytes.empty() || bytes.size() > max_bytes) return {};
    const auto root = decode(bytes);
    if (!root) return {};
    std::vector<ProductInstall> result;
    std::set<std::string> uids, codes;
    for (const auto& field : *root) {
        if (field.id != 1) continue;
        if (field.wire != 2) return {};
        const auto record = decode(field.bytes);
        if (!record || !schema(*record, {{1, 2}, {2, 2}, {3, 2}, {4, 2}})) return {};
        ProductInstall p;
        p.uid = text(*record, 1); p.code = text(*record, 2);
        if (!identifier(p.uid) || !identifier(p.code) || !uids.insert(fold(p.uid)).second || !codes.insert(fold(p.code)).second) return {};
        const auto settings = decode(text(*record, 3));
        const auto cached = decode(text(*record, 4));
        if (!settings || !cached || !schema(*settings, {{1, 2}, {2, 2}, {6, 2}}) || !schema(*cached, {{1, 2}, {4, 2}})) return {};
        const auto base = decode(text(*cached, 1));
        if (!base || !schema(*base, {{1, 0}, {2, 0}, {3, 0}, {7, 2}})) return {};
        const auto path = text(*settings, 1);
        if (path.size() > 32767 || path.find_first_of(std::string_view("\0\r\n\"", 4)) != std::string_view::npos) return {};
        p.directory = text_encoding::wide(path);
        p.region = text(*settings, 2); p.language = text(*settings, 6); p.version = text(*base, 7);
        p.installed = flag(*base, 1); p.playable = flag(*base, 2); p.update_complete = flag(*base, 3);
        // Completion/size fields intentionally never become a live percentage.
        result.push_back(std::move(p));
    }
    return result.empty() ? std::nullopt : std::optional{std::move(result)};
}

std::optional<std::vector<AggregateEntry>> parseAggregate(std::string_view bytes) {
    if (bytes.starts_with("\xef\xbb\xbf")) bytes.remove_prefix(3);
    const auto root = bytes.size() <= max_bytes ? json::parse(bytes) : std::nullopt;
    const auto* installed = root ? root->get("installed") : nullptr;
    if (!installed || installed->type != json::Value::Type::array || installed->elements.size() > 10000) return {};
    std::vector<AggregateEntry> result;
    std::set<std::string> seen;
    for (const auto& entry : installed->elements) {
        const auto* code = entry.get("product_id");
        if (!code || code->type != json::Value::Type::string || !identifier(code->text) || !seen.insert(fold(code->text)).second) return {};
        AggregateEntry item; item.code = code->text;
        if (const auto* name = entry.get("name"); name && name->type == json::Value::Type::string && name->text.size() <= 2048 &&
            name->text.find_first_of(std::string_view("\0\r\n", 3)) == std::string::npos) item.name = text_encoding::wide(name->text);
        if (const auto* played = entry.get("last_played_timestamp"); played && played->type == json::Value::Type::number) {
            uint64_t ms = 0;
            const auto& text = played->text;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), ms);
            if (error == std::errc{} && end == text.data() + text.size()) item.last_played = ms / 1000;
        }
        // icon_path can be a launcher, not the game executable. URLs and paths
        // from this metadata are never executed or treated as readiness evidence.
        result.push_back(std::move(item));
    }
    return result;
}

std::optional<std::vector<Game>> parseGamesAndSubs(std::string_view bytes) {
    // Title ID of a game account -> launch code, from pinned Playnite BattleNetGames.cs.
    // 1096108883 is shared by several Call of Duty releases and is deliberately absent.
    struct Title { uint64_t id; const char* family; };
    static constexpr Title titles[] = {
        {5730135, "WoW"}, {17459, "D3"}, {21298, "S2"}, {21297, "S1"}, {1465140039, "WTCG"}, {1214607983, "Hero"},
        {5272175, "Pro"}, {1447645266, "VIPR"}, {1329875278, "ODIN"}, {22323, "W3"}, {1279351378, "LAZR"},
        {1514493267, "ZEUS"}, {1464615513, "WLBY"}, {5198665, "OSI"}, {1381257807, "RTRO"}, {1179603525, "FORE"},
        {1095647827, "ANBS"}, {4613486, "Fen"}, {1146246220, "D1"}, {5714258, "W1R"}, {5714514, "W2R"},
        {1463898673, "W1"}, {1462911566, "W2"}, {4674137, "GRY"}, {1095911763, "ARIS"}, {1396920146, "SCOR"},
        {4280907, "ARK"}, {1279414849, "LBRA"}, {1095849281, "AQUA"},
    };
    if (bytes.starts_with("\xef\xbb\xbf")) bytes.remove_prefix(3);
    const auto root = bytes.size() <= max_bytes ? json::parse(bytes) : std::nullopt;
    const auto* accounts = root ? root->get("gameAccounts") : nullptr;
    if (!accounts || accounts->type != json::Value::Type::array || accounts->elements.size() > 10000) return {};
    std::vector<Game> result;
    std::set<std::string> seen;
    for (const auto& entry : accounts->elements) {
        const auto* title = entry.get("titleId");
        if (!title || title->type != json::Value::Type::number) continue;
        uint64_t id = 0;
        const auto [end, error] = std::from_chars(title->text.data(), title->text.data() + title->text.size(), id);
        if (error != std::errc{} || end != title->text.data() + title->text.size()) continue;
        const auto known = std::find_if(std::begin(titles), std::end(titles), [&](const Title& t) { return t.id == id; });
        if (known == std::end(titles) || !seen.insert(fold(known->family)).second) continue;
        const auto product = std::find_if(std::begin(products), std::end(products), [&](const Product& p) { return std::string_view(p.family) == known->family; });
        if (product == std::end(products)) continue;
        Game game;
        game.source = GameSource::battlenet; game.kind = GameKind::store; game.installed = false;
        game.source_id = product->family; game.name = product->name;
        result.push_back(std::move(game));
    }
    return result;
}

std::string uninstallUid(std::wstring_view command) {
    auto text = std::wstring(command);
    for (auto& c : text) if (c >= L'A' && c <= L'Z') c += L'a' - L'A';
    const auto begin = text.find(L"--uid=");
    if (text.find(L"battle.net") == std::wstring::npos || begin == std::wstring::npos) return {};
    auto value = std::wstring_view(text).substr(begin + 6);
    if (value.starts_with(L'"')) value.remove_prefix(1);
    value = value.substr(0, value.find_first_of(L" \t\""));
    const auto uid = text_encoding::utf8(value);
    return identifier(uid) ? uid : std::string{};
}

Scan scan(const std::filesystem::path& database, const std::filesystem::path& aggregate,
          const std::wstring& client, const std::vector<RegistryHint>& hints, const std::vector<Game>& previous) {
    Scan result;
    const auto bytes = read(database);
    const auto records = bytes ? parseProductDb(*bytes) : std::nullopt;
    const auto metadata_bytes = read(aggregate);
    const auto metadata = metadata_bytes ? parseAggregate(*metadata_bytes) : std::nullopt;
    result.available = records.has_value();
    std::map<std::string, Game> games;
    const auto enrich = [&](Game& game, const std::string& code) {
        if (metadata) for (const auto& entry : *metadata) if (entry.code == code) {
            if (!entry.name.empty()) game.name = entry.name;
            game.last_played = entry.last_played;
        }
    };
    if (records) for (const auto& record : *records) {
        if (record.code == "agent" || record.code == "bna") continue;
        const auto* product = byUid(record.uid);
        if (!product) continue;
        const auto* code_product = byCode(record.code);
        if (code_product && std::string_view(code_product->family) != product->family) continue;
        auto game = gameFor(*product, client, record.directory);
        game.manifest_path = database;
        game.installed = record.ready() && directory(record.directory);
        game.installation.phase = game.installed ? InstallPhase::none :
            record.stateKnown() && !record.ready() ? InstallPhase::staging : InstallPhase::unknown;
        enrich(game, record.code);
        const auto key = gameKey(game);
        if (const auto found = games.find(key); found != games.end()) {
            // Ambiguous aliases/variants cannot promote one another to "Play".
            found->second.installed = false; found->second.installation.phase = InstallPhase::unknown;
            found->second.play_action = {};
        } else games.emplace(key, std::move(game));
    }
    if (!records) {
        for (const auto& old : previous) if (old.source == GameSource::battlenet) {
            auto retained = old;
            retained.installed = false; retained.installation = {InstallPhase::unknown};
            games.emplace(gameKey(retained), std::move(retained));
        }
        for (const auto& hint : hints) if (const auto* product = byUid(hint.uid); product && directory(hint.directory)) {
            auto game = gameFor(*product, client, hint.directory);
            games.try_emplace(gameKey(game), std::move(game));
        }
    }
    if (metadata) for (const auto& entry : *metadata) if (const auto* product = byCode(entry.code)) {
        // aggregate.json can precede the product record. Show presence, never Play.
        // A known non-retail UID with the same code must not create a retail tile.
        if (records && std::any_of(records->begin(), records->end(), [&](const auto& p) { return p.code == entry.code; })) continue;
        auto game = gameFor(*product, client, {}); enrich(game, entry.code);
        games.try_emplace(gameKey(game), std::move(game));
    }
    for (auto& [key, game] : games) { (void)key; game.manifest_path = database; result.games.push_back(std::move(game)); }
    return result;
}
}
