#include "../src/library/pc_platforms.hpp"
#include "../src/library/ubisoft.hpp"
#include <Windows.h>
#include <fstream>
#include <iostream>

namespace {
int failures = 0;
void check(bool condition, const char* what) {
    if (!condition) { std::cerr << "FAILED: " << what << '\n'; ++failures; }
}
std::string varint(uint64_t value) {
    std::string result;
    do { result += static_cast<char>((value & 127) | (value > 127 ? 128 : 0)); value >>= 7; } while (value);
    return result;
}
std::string number(unsigned field, uint64_t value) { return varint(field << 3) + varint(value); }
std::string bytes(unsigned field, const std::string& value) { return varint((field << 3) | 2) + varint(value.size()) + value; }
std::string u32(uint32_t value) { return std::string(reinterpret_cast<const char*>(&value), 4); }
// Client layout: <u32 signature size><signature><u32 payload size><payload>.
std::string ownershipFile(const std::string& payload) { return u32(256) + std::string(256, '\x5a') + u32(static_cast<uint32_t>(payload.size())) + payload; }

const std::string uuid_a = "aaaaaaaa-1111-4222-8333-444444444444";
const std::string uuid_b = "bbbbbbbb-1111-4222-8333-444444444444";
const std::string cd_key = "SECRET-CDKEY-0001";

std::string fixturePayload() {
    return bytes(1, number(1, 1234) + number(2, 1234) + bytes(4, cd_key) + number(6, 0) + bytes(19, "AAAAAAAA-1111-4222-8333-444444444444")) +
        bytes(1, number(1, 1235) + number(2, 1235) + number(6, 1)) +           // add-on of 1234
        bytes(1, number(1, 5555) + number(6, 0) + bytes(19, uuid_b)) +
        bytes(1, number(1, 77) + number(6, 0)) +                                // no catalogue UUID
        bytes(1, number(1, 1234) + number(6, 0)) +                              // duplicate
        number(2, 1234) + number(2, 1235) + number(2, 5555);
}
std::string recentlyPlayed() {
    const auto entry = [](uint64_t id, uint64_t time) { return bytes(1, number(1, id) + number(2, 0) + number(3, time)); };
    return bytes(1, "") + bytes(2, "") + bytes(4, entry(1234, 1786093268) + entry(5555, 1700000000) + entry(9, 12));
}
void write(const std::filesystem::path& file, const std::string& content) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary | std::ios::trunc) << content;
}
}

int main() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / (L"launchme-ubisoft-tests-" + std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{root};

    // Ownership: base games only, UUIDs normalised, CD keys never surface, duplicates collapse.
    const auto owned = ubisoft::parseOwnership(ownershipFile(fixturePayload()));
    check(owned && owned->size() == 3, "three base products");
    if (owned && owned->size() == 3) {
        check((*owned)[0].id == "1234" && (*owned)[0].uuid == uuid_a, "UUID lowercased");
        check((*owned)[1].id == "5555" && (*owned)[1].uuid == uuid_b, "second product");
        check((*owned)[2].id == "77" && (*owned)[2].uuid.empty(), "product without UUID");
        for (const auto& product : *owned) check(product.id.find("SECRET") == std::string::npos && product.uuid.find("SECRET") == std::string::npos, "no CD key");
    }
    auto torn = ownershipFile(fixturePayload());
    check(!ubisoft::parseOwnership(torn.substr(0, torn.size() - 3)), "truncated file is unavailable");
    check(!ubisoft::parseOwnership(u32(256) + std::string(256, 0) + u32(5) + "\x0a\x10"), "size mismatch rejected");
    check(!ubisoft::parseOwnership(ownershipFile(bytes(1, "\x08"))), "malformed record rejected");
    check(ubisoft::parseOwnership(ownershipFile(""))->empty(), "empty account is an empty list");

    const auto played = ubisoft::parseRecentlyPlayed(recentlyPlayed());
    check(played.size() == 2 && played.at("1234") == 1786093268 && played.at("5555") == 1700000000, "recently played, bogus time dropped");
    check(ubisoft::parseRecentlyPlayed("\xff\xff").empty(), "malformed settings ignored");

    // The newest account file wins; files that are not account GUIDs are ignored.
    const auto old_cache = root / "old-client" / "cache", cache = root / "appdata" / "cache";
    const std::string old_account = "11111111-2222-4333-8444-555555555555", account = "99999999-2222-4333-8444-555555555555";
    write(old_cache / "ownership" / old_account, ownershipFile(bytes(1, number(1, 4242) + number(6, 0))));
    write(cache / "ownership" / account, ownershipFile(fixturePayload()));
    write(cache / "ownership" / "not-an-account", ownershipFile(bytes(1, number(1, 1) + number(6, 0))));
    write(cache / "settings" / account, recentlyPlayed());
    const auto now = fs::file_time_type::clock::now();
    fs::last_write_time(old_cache / "ownership" / old_account, now - std::chrono::hours(48));
    fs::last_write_time(cache / "ownership" / account, now - std::chrono::hours(1));
    fs::last_write_time(cache / "ownership" / "not-an-account", now);
    auto read = ubisoft::readAccount({old_cache, cache});
    check(read && read->id == account && read->products.size() == 3 && read->last_played.size() == 2, "newest account and its settings");
    check(!ubisoft::readAccount({root / "missing"}), "no client cache");
    fs::last_write_time(old_cache / "ownership" / old_account, now);
    read = ubisoft::readAccount({old_cache, cache});
    check(read && read->id == old_account && read->products.size() == 1 && read->last_played.empty(), "account switch follows the newest file");
    write(old_cache / "ownership" / old_account, "torn");
    fs::last_write_time(old_cache / "ownership" / old_account, now);
    check(!ubisoft::readAccount({old_cache, cache}), "unreadable newest account never falls back to another account");
    fs::remove(old_cache / "ownership" / old_account);

    // Titles: cache file, public catalogues and the lookup order (UUID first).
    const auto names = ubisoft::parseNames("aaaaaaaa-1111-4222-8333-444444444444\tOwned Fixture\n5555\tBy ID\nbad key\tX\n77\t\x01" "control\n");
    check(names.size() == 2 && names.at(uuid_a) == L"Owned Fixture", "names cache parsed strictly");
    check(ubisoft::parseNames(ubisoft::serializeNames(names)) == names, "names cache round trip");
    const auto catalog = ubisoft::parseUuidCatalog(R"({"games":{")" + uuid_b + R"(":{"name":"Catalogue Game\u00ae"},"not-a-uuid":{"name":"X"}}})");
    check(catalog.size() == 1 && catalog.at(uuid_b) == L"Catalogue Game", "UUID catalogue, trademark removed");
    const auto list = ubisoft::parseIdList("\xef\xbb\xbf" "1, Example\r\n77, Far Cry\xc2\xae 3\n77, Far Cry 3 QC\n012, Leading zero\nnope\n");
    check(list.size() == 2 && list.at("77") == L"Far Cry 3", "ID list: first row wins, canonical IDs only");
    check(ubisoft::title(names, {"5555", uuid_b}) == L"By ID" && ubisoft::title(catalog, {"5555", uuid_b}) == L"Catalogue Game", "UUID preferred over ID");
    const auto missing = ubisoft::unnamed(names, *owned);
    check(missing.size() == 1 && missing[0].id == "77", "only unnamed products are requested");

    // Library: installed product keeps precedence, owned ones get uplay:// actions and account membership.
    write(root / "launchme" / "names.tsv", ubisoft::serializeNames({{uuid_a, L"Owned Fixture"}, {uuid_b, L"Library Fixture"}}));
    const auto installed = root / "games" / "Fixture";
    fs::create_directories(installed);
    PcDiscovery input;
    input.ubisoft_caches = {old_cache, cache};
    input.ubisoft_names = root / "launchme" / "names.tsv";
    input.installs.push_back({GameSource::ubisoft, "1234", {}, installed});
    const auto library = mergePcLibraries(scanNativePcLibraries(input), root / "catalogs");
    check(library.ubisoft_account && library.ubisoft_unnamed.size() == 1 && library.ubisoft_unnamed[0].first == "77" &&
          library.ubisoft_unnamed[0].second.empty(), "account flag and unnamed products survive the merge");
    check(library.visible.size() == 2, "installed + owned, unnamed hidden");
    for (const auto& game : library.visible) {
        check(game.source == GameSource::ubisoft && game.kind == GameKind::store && !game.app_id, "Ubisoft store game");
        if (game.source_id == "1234") {
            check(game.installed && game.name == L"Owned Fixture" && game.account_id.empty(), "local install titled from the catalogue");
            check(game.last_played == 1786093268 && game.play_action.target == L"uplay://launch/1234/0", "installed game plays via the client");
        } else {
            check(game.source_id == "5555" && !game.installed && game.name == L"Library Fixture" && game.account_id == account, "owned product");
            check(game.install_action.target == L"uplay://install/5555" && game.install_action.kind == ActionKind::install, "install through Ubisoft Connect");
            check(primaryGameAction(game).target == L"uplay://install/5555" && game.last_played == 1700000000, "primary action installs");
        }
    }
    input.ubisoft_caches = {root / "missing"};
    const auto without = scanNativePcLibraries(input);
    check(!without.ubisoft_account && without.visible.size() == 1 && without.ubisoft_unnamed.empty(), "without the client cache only installations remain");

    if (failures) return 1;
    std::cout << "Ubisoft Connect ownership cache, recently played, titles and library merge passed\n";
    return 0;
}
