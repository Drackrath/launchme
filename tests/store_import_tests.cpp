#include "../src/library/battlenet.hpp"
#include "../src/library/pc_platforms.hpp"
#include "../src/store/ea_account.hpp"
#include "../src/store/web_import.hpp"
#include <Windows.h>
#include <fstream>
#include <iostream>

namespace {
int failures = 0;
void check(bool condition, const char* what) {
    if (!condition) { std::cerr << "FAILED: " << what << '\n'; ++failures; }
}
HttpResponse reply(uint32_t status, std::string_view body = {}) {
    HttpResponse response; response.status = status;
    response.body.assign(body.begin(), body.end());
    return response;
}
const std::string token = "QVQxOmExMjM0NTY3ODkwYWJjZGVm";

// Fake EA GraphQL service: identity, two pages of entitlements, then offer details.
struct FakeEa {
    std::vector<std::wstring> headers;
    std::vector<std::string> bodies;
    uint32_t status = 200;
    bool unauthenticated = false;
    HttpResponse operator()(const std::wstring& url, const std::wstring& header, const std::string& body) {
        headers.push_back(header); bodies.push_back(body);
        if (url != L"https://service-aggregation-layer.juno.ea.com/graphql") return reply(404);
        if (status != 200) return reply(status);
        if (unauthenticated) return reply(200, R"({"errors":[{"message":"no","extensions":{"code":"UNAUTHENTICATED"}}],"data":null})");
        if (body.find("player") != std::string::npos) return reply(200, R"({"data":{"me":{"player":{"pd":1000123456789,"psd":7,"displayName":"Tester"}}}})");
        if (body.find("getEntitlements") != std::string::npos) {
            if (body.find("\"next\":\"page2\"") != std::string::npos)
                return reply(200, R"({"data":{"me":{"ownedGameProducts":{"next":null,"items":[
                    {"originOfferId":"Origin.OFR.50.0003","product":{"baseItem":{"gameType":"BASE_GAME"}}}]}}}})");
            return reply(200, R"({"data":{"me":{"ownedGameProducts":{"next":"page2","items":[
                {"originOfferId":"Origin.OFR.50.0001","product":{"baseItem":{"gameType":"BASE_GAME"}}},
                {"originOfferId":"Origin.OFR.50.0002","product":{"baseItem":{"gameType":"EXTRA_CONTENT"}}},
                {"originOfferId":"Origin.OFR.50.0001","product":{"baseItem":{"gameType":"BASE_GAME"}}},
                {"originOfferId":"Origin.OFR.50.0009","product":null}]}}}})");
        }
        if (body.find("getOffers") != std::string::npos)
            return reply(200, R"({"data":{"legacyOffers":[
                {"offerId":"Origin.OFR.50.0001","contentId":"196102"},
                {"offerId":"Origin.OFR.50.0003","contentId":"1026480"},
                {"offerId":"Origin.OFR.50.0004","contentId":"55"}],
              "gameProducts":{"items":[
                {"id":"x1","originOfferId":"Origin.OFR.50.0001","baseItem":{"title":"Mass Effect\u2122 Legendary Edition"}},
                {"id":"1026480","originOfferId":"","baseItem":{"title":"Dead Space"}}]}}})");
        return reply(500);
    }
};
}

int main() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / (L"launchme-import-tests-" + std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{root};

    // Battle.net: account page -> known launch codes only; the paste field drops spaces and non-ASCII.
    const auto page = std::string(R"({"gameAccounts":[{"titleId":5730135,"localizedGameName":"WorldofWarcraft","gameAccountStatus":"Good"},)") +
        R"({"titleId":4613486,"localizedGameName":"DiabloIV"},{"titleId":4613486},{"titleId":1096108883},{"titleId":999},{"titleId":"x"}],"subscriptions":[]})";
    const auto games = battlenet::parseGamesAndSubs(page);
    check(games && games->size() == 2, "two known Battle.net titles");
    if (games && games->size() == 2) {
        check(gameKey((*games)[0]) == "battlenet:wow" && (*games)[0].name == L"World of Warcraft" && !(*games)[0].installed, "WoW family and name");
        check(gameKey((*games)[1]) == "battlenet:fen" && (*games)[1].name == L"Diablo IV", "Diablo IV family");
    }
    check(!battlenet::parseGamesAndSubs("<html>401</html>") && !battlenet::parseGamesAndSubs(R"({"error":"unauthorized"})"), "sign-in page is not a list");
    check(battlenet::parseGamesAndSubs(R"({"gameAccounts":[]})")->empty(), "empty account");

    // EA: the pasted sign-in page yields the token; the library comes from GraphQL, the token is not kept.
    check(ea::accessToken(R"( {"access_token":")" + token + R"(","token_type":"Bearer","expires_in":"3599"} )") == token, "token from JSON");
    check(ea::accessToken(token) == token && ea::accessToken("short").empty() && ea::accessToken("{\"access_token\":\"a b\"}").empty(), "bare token, rejects");
    check(ea::signedOut(R"({"error_code":"login_required","error":"login_required","error_number":"102100"})"), "login_required");
    FakeEa fake;
    const auto post = [&](const std::wstring& url, const std::wstring& header, const std::string& body) { return fake(url, header, body); };
    check(ea::importLibrary(R"({"error":"login_required"})", post).failure == ea::Failure::signed_out && fake.bodies.empty(), "signed out: no request");
    check(ea::importLibrary("garbage", post).failure == ea::Failure::input, "not a token");
    auto imported = ea::importLibrary(R"({"access_token":")" + token + R"("})", post);
    check(imported.failure == ea::Failure::none && imported.snapshot.account == "1000123456789", "EA account");
    check(imported.snapshot.games.size() == 2, "two EA base games with titles");
    for (const auto& game : imported.snapshot.games) {
        check(game.source == GameSource::ea && !game.installed && game.account_id == "1000123456789", "EA game fields");
        check((game.source_id == "196102" && game.name == L"Mass Effect Legendary Edition") || (game.source_id == "1026480" && game.name == L"Dead Space"), "content ID and title");
    }
    check(fake.bodies.size() == 4 && fake.bodies[2].find("\"next\":\"page2\"") != std::string::npos, "identity, two pages, one offer batch");
    check(fake.headers[0].find(L"Authorization: Bearer " + std::wstring(token.begin(), token.end())) != std::wstring::npos, "bearer header");
    fake.status = 401;
    check(ea::importLibrary(token, post).failure == ea::Failure::expired, "401 is an expired token");
    fake.status = 0;
    check(ea::importLibrary(token, post).failure == ea::Failure::network, "no response is a network failure");
    fake.status = 200; fake.unauthenticated = true;
    check(ea::importLibrary(token, post).failure == ea::Failure::expired, "UNAUTHENTICATED is an expired token");

    // Saved list: DPAPI round trip, no token inside, strict format.
    const auto text = web_import::serialize(GameSource::ea, imported.snapshot);
    check(!text.empty() && text.find(token) == std::string::npos, "serialized without the token");
    const auto back = web_import::deserialize(GameSource::ea, text);
    check(back && back->games.size() == 2 && back->account == imported.snapshot.account && back->games[0].account_id == back->account, "round trip");
    check(!web_import::deserialize(GameSource::battlenet, text), "source mismatch rejected");
    check(!web_import::deserialize(GameSource::ea, text + "\"extra\""), "trailing data rejected");
    auto duplicate = imported.snapshot; duplicate.games.push_back(duplicate.games[0]);
    check(web_import::serialize(GameSource::ea, duplicate).empty(), "duplicate IDs rejected");
    check(web_import::save(root, GameSource::ea, imported.snapshot), "save");
    const auto loaded = web_import::load(root, GameSource::ea);
    check(loaded && loaded->games.size() == 2 && !web_import::load(root, GameSource::battlenet), "load per source");
    std::string raw;
    { std::ifstream input(web_import::file(root, GameSource::ea), std::ios::binary); raw.assign(std::istreambuf_iterator<char>(input), {}); }
    check(raw.find("Dead Space") == std::string::npos, "file is DPAPI-protected");
    check(web_import::clear(root, GameSource::ea) && !web_import::load(root, GameSource::ea), "clear");

    // Merge: a local installation keeps precedence; imported titles install through the client.
    fs::create_directories(root / "clients");
    const auto battle_client = root / "clients" / "Battle.net.exe";
    std::ofstream(battle_client) << "fixture";
    StoreLibrary native;
    native.clients.push_back({GameSource::battlenet, battle_client});
    Game installed; installed.source = GameSource::ea; installed.kind = GameKind::store; installed.source_id = "196102"; installed.name = L"Mass Effect (local)";
    native.visible.push_back(installed);
    auto everything = imported.snapshot.games;
    for (auto game : *games) { game.account_id = "battlenet"; everything.push_back(game); }
    Game stray = everything.back(); stray.source_id = "S2"; stray.account_id.clear(); everything.push_back(stray); // No account: ignored.
    const auto merged = mergeImportedLibrary(native, everything);
    check(merged.visible.size() == 4, "local EA + imported EA + two Battle.net");
    for (const auto& game : merged.visible) {
        if (game.source == GameSource::ea && game.source_id == "196102") check(game.installed && game.name == L"Mass Effect (local)", "local installation kept");
        else if (game.source == GameSource::ea) {
            check(!game.installed && game.install_action.target == L"origin2://game/launch?offerIds=1026480&autoDownload=1" &&
                  game.install_action.kind == ActionKind::install && !game.play_action, "EA installs through the EA app");
            check(std::wstring(primaryActionLabel(game)) == L"Instaluj", "EA label");
        } else check(game.source == GameSource::battlenet && !game.installed && game.install_action.target == battle_client.wstring() &&
                     primaryGameAction(game).target == battle_client.wstring(), "Battle.net opens the client");
    }

    if (failures) return 1;
    std::cout << "EA sign-in page import, Battle.net game list, DPAPI snapshot and library merge passed\n";
    return 0;
}
