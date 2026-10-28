#include "../src/library/battlenet.hpp"
#include "../src/library/pc_platforms.hpp"
#include "../src/platform/text_encoding.hpp"
#include <Windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::string number(uint64_t value) {
    std::string bytes;
    do { bytes += static_cast<char>((value & 127) | (value > 127 ? 128 : 0)); value >>= 7; } while (value);
    return bytes;
}
std::string field(unsigned id, const std::string& value) { return number((id << 3) | 2) + number(value.size()) + value; }
std::string flag(unsigned id, uint64_t value) { return number(id << 3) + number(value); }
std::string state(bool installed, bool playable, bool complete) { return flag(1, installed) + flag(2, playable) + flag(3, complete); }
std::string record(const std::string& uid, const std::string& code, const std::filesystem::path& path, const std::string& base) {
    // Completion=1 and large size must not fabricate download progress.
    const std::string completion = std::string("\x11\0\0\0\0\0\0\xf0\x3f", 9) + flag(4, 12428894444ull);
    return field(1, field(1, uid) + field(2, code) + field(3, field(1, text_encoding::utf8(path.wstring())) + field(2, "EU") + field(6, "enUS")) +
        field(4, field(1, base + field(7, "fixture")) + field(4, completion)));
}
void write(const std::filesystem::path& path, const std::string& bytes) { std::ofstream(path, std::ios::binary | std::ios::trunc) << bytes; }
}

int main() try {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / (L"launchme-battlenet-tests-" + std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { fs::path root; ~Cleanup() { std::error_code ec; fs::remove_all(root, ec); } } cleanup{root};
    fs::create_directories(root / "game");
    const auto database = root / "product.db", aggregate = root / "aggregate.json", folder = root / "game";
    const auto client = (root / "Battle.net.exe").wstring();
    const auto ready = record("hs_beta", "hsb", folder, state(true, true, true));
    const auto infrastructure = record("battle.net", "bna", root, state(true, true, true)) + record("agent", "agent", root, state(true, true, true));
    require(battlenet::parseProductDb(infrastructure).has_value(), "Windows infrastructure UID is not a launch family");
    auto parsed = battlenet::parseProductDb(ready + flag(6, 17) + field(5, "unknown"));
    require(parsed && parsed->size() == 1 && (*parsed)[0].ready(), "complete flags and unknown fields");
    require((*parsed)[0].uid == "hs_beta" && (*parsed)[0].code == "hsb" && (*parsed)[0].region == "EU", "distinct UID/code/settings");
    for (unsigned bits = 0; bits < 7; ++bits) {
        auto partial = battlenet::parseProductDb(record("hs_beta", "hsb", folder, state(bits & 1, bits & 2, bits & 4)));
        require(partial && !(*partial)[0].ready(), "all three flags required");
    }
    parsed = battlenet::parseProductDb(record("hs_beta", "hsb", folder, flag(1, 1) + flag(2, 1)));
    require(parsed && !(*parsed)[0].ready() && !(*parsed)[0].stateKnown(), "missing flag is unknown");
    parsed = battlenet::parseProductDb(record("hs_beta", "hsb", folder, flag(1, 2) + flag(2, 1) + flag(3, 1)));
    require(parsed && !(*parsed)[0].ready(), "non-boolean cannot grant readiness");
    require(!battlenet::parseProductDb(ready.substr(0, ready.size() - 1)), "truncated nested field");
    require(!battlenet::parseProductDb(ready + ready), "duplicate UID/code rejects snapshot");
    require(!battlenet::parseProductDb(record("hs_beta", "hsb", folder, field(1, "wrong-wire"))), "wrong state wire");
    require(!battlenet::parseProductDb(record("hs_beta", "hsb", folder, state(true, true, true) + flag(1, 0))), "duplicate flag");
    require(!battlenet::parseProductDb(std::string("\x0a\xff\xff\xff\xff\xff\xff\xff\xff\xff\x7f", 11)), "overflow length");
    require(!battlenet::parseProductDb({}), "empty write is unavailable");
    require(!battlenet::parseProductDb(field(99, "unrecognized schema")), "unknown schema is not an empty library");
    require(battlenet::uninstallUid(L"\"C:\\Battle.net\\Battle.net.exe\" --uid=\"hs_beta\"") == "hs_beta", "quoted UID");
    require(battlenet::uninstallUid(L"other.exe --uid=hs_beta").empty(), "unrelated uninstaller");
    write(database, ready);
    write(aggregate, R"({"installed":[{"product_id":"hsb","name":"Fixture title","last_played_timestamp":1720000000123,"icon_path":"C:/not-a-game.exe","launch_uri":"steam://install/123"}]})");
    auto scan = battlenet::scan(database, aggregate, client);
    require(scan.available && scan.games.size() == 1 && scan.games[0].installed, "ready native import");
    auto game = scan.games[0];
    require(gameKey(game) == "battlenet:wtcg" && game.source_id == "WTCG", "legacy family key");
    require(game.name == L"Fixture title" && game.last_played == 1720000000 && game.executable.empty(), "code join / milliseconds / untrusted icon");
    require(primaryGameAction(game).arguments == L"--exec=\"launch WTCG\"", "source launch command, ignore metadata URI");
    const auto key = gameKey(game);
    write(database, record("hs_beta", "hsb", folder, state(true, true, false)));
    scan = battlenet::scan(database, aggregate, client);
    require(scan.games.size() == 1 && !scan.games[0].installed && scan.games[0].installation.active(), "partial playable is not ready");
    require(!scan.games[0].installation.percent() && gameKey(scan.games[0]) == key, "completion is not live percent; stable key");
    require(primaryGameAction(scan.games[0]).target == client && primaryGameAction(scan.games[0]).arguments.empty() &&
        primaryGameAction(scan.games[0]).kind == ActionKind::open_client, "active installation opens Battle.net only");
    game.installed = true; game.installation.phase = InstallPhase::staging;
    require(primaryGameAction(game).kind == ActionKind::open_client, "active overrides stale installed flag");
    auto previous = scan.games;
    previous[0].visibility = GameVisibility::private_game;
    write(database, "\x0a\xff");
    scan = battlenet::scan(database, aggregate, client, {}, previous);
    require(!scan.available && scan.games.size() == 1 && !scan.games[0].installed && scan.games[0].visibility == GameVisibility::private_game &&
        scan.games[0].installation.phase == InstallPhase::unknown, "torn read retains membership and privacy without completing");
    write(database, ready);
#ifdef _WIN32 // Share modes: Windows only (Linux has no exclusive open to test).
    HANDLE lock = CreateFileW(database.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    require(lock != INVALID_HANDLE_VALUE, "test writer lock");
    scan = battlenet::scan(database, aggregate, client, {}, previous);
    CloseHandle(lock);
    require(!scan.available && scan.games.size() == 1 && !scan.games[0].installed, "sharing violation preserves unknown");
#endif
    scan = battlenet::scan(database, aggregate, client, {}, previous);
    require(scan.available && scan.games[0].installed, "retry restores confirmed ready");
    write(database, record("hs_beta", "hsb", root / "missing-drive", state(true, true, true)));
    require(!battlenet::scan(database, aggregate, client).games[0].installed, "missing installation directory");
    write(database, infrastructure); // Readable Agent snapshot with no games.
    scan = battlenet::scan(database, aggregate, client);
    require(scan.available && scan.games.size() == 1 && !scan.games[0].installed, "aggregate-only cannot mean ready");
    write(aggregate, R"({"installed":[]})");
    scan = battlenet::scan(database, aggregate, client, {{"hs_beta", folder}}, previous);
    require(scan.available && scan.games.empty(), "confirmed removal wins over stale registry/previous");
    fs::remove(database);
    scan = battlenet::scan(database, aggregate, client, {{"hs_beta", folder}});
    require(!scan.available && scan.games.size() == 1 && !scan.games[0].installed, "registry hint alone cannot mean ready");
    write(database, record("wow_beta", "wow", folder, state(true, true, true)));
    write(aggregate, R"({"installed":[{"product_id":"wow","name":"Beta"}]})");
    require(battlenet::scan(database, aggregate, client).games.empty(), "non-retail UID must not become retail through code");
    write(database, record("fenris", "hsb", folder, state(true, true, true)));
    write(aggregate, R"({"installed":[]})");
    require(battlenet::scan(database, aggregate, client).games.empty(), "conflicting UID/code mapping");
    write(database, record("fenris", "fenris", folder, state(true, true, true)));
    scan = battlenet::scan(database, aggregate, client);
    require(scan.games.size() == 1 && gameKey(scan.games[0]) == "battlenet:fen", "fenris preserves Fen privacy/history key");
    write(database, record("fenris", "fenris", folder, state(true, true, true)) + record("Fen", "fen", root, state(false, false, false)));
    scan = battlenet::scan(database, aggregate, client);
    require(scan.games.size() == 1 && !scan.games[0].installed && !scan.games[0].play_action, "ambiguous family does not select arbitrary variant");
    const auto catalogs = root / "catalogs"; fs::create_directories(catalogs);
    StoreLibrary native; native.visible = scan.games;
    write(catalogs / "battlenet.json", R"({"version":1,"games":[{"id":"fen","name":"Hidden fixture","installed":true,"visibility":"hidden"}]})");
    auto merged = mergePcLibraries(native, catalogs);
    require(merged.visible.empty() && merged.private_games.size() == 1 && !merged.private_games[0].installed, "catalog privacy without overriding incomplete state");
    write(catalogs / "battlenet.json", "{");
    merged = mergePcLibraries(native, catalogs);
    require(merged.visible.empty() && merged.private_games.empty() && !merged.errors.empty(), "broken privacy fails closed");
    require(!battlenet::parseAggregate(R"({"installed":[{"product_id":"hsb"},{"product_id":"HSB"}]})"), "ambiguous aggregate rejected");
    const auto duplicate = parseStoreCatalog(GameSource::battlenet,
        R"({"version":1,"games":[{"id":"Fen","name":"Public","installed":true,"visibility":"public"},{"id":"fen","name":"Private","installed":true,"visibility":"private"}]})");
    require(!duplicate.errors.empty() && duplicate.visible.empty() && duplicate.private_games.empty(), "canonical duplicate cannot bypass privacy");
    require(battlenet::scan(root / "absent.db", root / "absent.json", client).games.empty(), "no catalog/F2P imports");
    std::cout << "Battle.net schema, readiness, identity, lifecycle, unavailable reads, privacy and source actions passed\n";
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
