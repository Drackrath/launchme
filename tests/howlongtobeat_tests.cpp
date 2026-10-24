#include "../plugins/howlongtobeat/hltb.hpp"
#include "../sdk/launchme_plugin.h"
#include "../src/platform/atomic_file.hpp"
#include "../src/platform/text_encoding.hpp"
#include "../src/model/key_codec.hpp"
#include "../src/plugins/time_comparison.hpp"
#include <Windows.h>
#include <chrono>
#include <iostream>

using namespace plugins::wire;
namespace {
HttpResponse response(uint32_t status, std::string body) { return {status, {body.begin(), body.end()}}; }
const std::string fixture = R"({"data":[{"game_id":123,"game_name":"Fixture","comp_main":3600,"comp_plus":5400,"comp_100":0}]})";
int pluginTest(const std::filesystem::path& dll) {
    const auto root = std::filesystem::temp_directory_path() / (L"launchme-hltb-" + std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { std::filesystem::path path; HMODULE module = nullptr; ~Cleanup() { if (module) FreeLibrary(module); std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup{root};
    cleanup.module = LoadLibraryW(dll.c_str()); if (!cleanup.module) return 20;
    const auto entry = reinterpret_cast<const LaunchmePluginV1* (__cdecl*)(uint32_t)>(GetProcAddress(cleanup.module, "launchme_plugin_v1"));
    const auto* api = entry ? entry(LAUNCHME_PLUGIN_ABI) : nullptr; if (!api) return 21;
    const auto invoke = [&](const std::string& request) {
        std::string out;
        const auto emit = [](void* data, const char* bytes, uint32_t size) { *static_cast<std::string*>(data) = std::string(bytes, size); };
        if (api->invoke(request.data(), static_cast<uint32_t>(request.size()), emit, &out) != 0) return std::string{};
        return out;
    };
    auto cache = *json::parse(fixture);
    cache.properties["name"] = string("Fixture"); cache.properties["selected"] = string("0");
    cache.properties["fetched"] = string(std::to_string(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()));
    if (!atomic_file::write(root / (key_codec::encode("gog:123") + ".json"), encode(cache))) return 22;
    if (invoke("{\"method\":\"activate\",\"params\":{\"dataDirectory\":" + quote(text_encoding::utf8(root.native())) + "}}") != "{}") return 23;
    const std::string lookup = R"({"method":"command","locale":"pl","params":{"command":"lookup","game":{"key":"gog:123","name":"Fixture"}}})";
    if (invoke(lookup).find("pick.123") == std::string::npos) return 24;
    const auto picked = invoke(R"({"method":"command","locale":"pl","params":{"command":"pick.123"}})");
    if (picked.find("Główna fabuła") == std::string::npos || picked.find("1 godz.") == std::string::npos || picked.find("/game/123") == std::string::npos) return 25;
    if (invoke(lookup).find("Główna fabuła") == std::string::npos) return 26; // Durable mapping, no network.
    const auto details = json::parse(invoke(R"({"method":"details","locale":"pl","params":{"game":{"key":"gog:123","name":"Fixture"},"playtimeSeconds":1800}})"));
    const auto comparison = details ? plugins::parseTimeComparison(*details, "gog:123") : std::nullopt;
    if (!comparison || comparison->main_seconds != 3600 || plugins::percentLabel(*comparison, 1800) != L"50%" || details->get("drawer")) return 27;
    auto second = *json::parse(R"({"data":[{"game_id":456,"game_name":"Another","comp_main":7200,"comp_plus":10800,"comp_100":14400}]})");
    second.properties["name"] = string("Another"); second.properties["selected"] = string("0"); second.properties["fetched"] = cache.properties["fetched"];
    if (!atomic_file::write(root / (key_codec::encode("custom:other") + ".json"), encode(second))) return 28;
    const auto active = invoke(R"({"method":"widget","locale":"en","params":{"game":{"key":"custom:other","name":"Another"},"playtimeSeconds":9000}})");
    if (active.find("125%") == std::string::npos || active.find("custom:other") == std::string::npos) return 29;
    // A passive widget for B must never replace the context of A's open picker.
    const auto still_a = invoke(R"({"method":"command","locale":"en","params":{"command":"pick.123"}})");
    if (still_a.find("/game/123") == std::string::npos || still_a.find("Another") != std::string::npos) return 30;
    if (invoke(R"({"method":"widget","locale":"en","params":{}})").find("No active game") == std::string::npos) return 31;
    // A widget with no cache is read-only: it cannot block on a network lookup.
    const auto missing = invoke(R"({"method":"widget","locale":"en","params":{"game":{"key":"custom:missing","name":"Missing cache"}}})");
    if (missing.find("Loading data") == std::string::npos || missing.find("100%") != std::string::npos) return 32;
    invoke(R"({"method":"deactivate","params":{}})");
    return 0;
}
}
int main(int argc, char** argv) {
    plugins::TimeComparison comparison{"fixture", 3600};
    if (plugins::timePercent(comparison, std::nullopt) || plugins::percentLabel(comparison, 0) != L"0%" || plugins::percentLabel(comparison, 5400) != L"150%") return 11;
    comparison.main_seconds = 0;
    if (plugins::timePercent(comparison, 100) || plugins::percentLabel(comparison, 100) != L"—") return 12;
    if (plugins::parseTimeComparison(*json::parse(R"({"comparison":{"gameKey":"wrong","mainSeconds":3600}})"), "fixture") ||
        plugins::parseTimeComparison(*json::parse(R"({"comparison":{"gameKey":"fixture","mainSeconds":-1}})"), "fixture")) return 13;
    if (argc == 2 && std::string_view(argv[1]) == "--live") {
        hltb::Client client; const auto result = client.search("Portal 2");
        std::cout << "Search valid=" << result.valid << " count=" << result.games.size() << " " << result.error << '\n';
        for (const auto& game : result.games) std::cout << game.id << " " << game.name << ": " << hltb::duration(game.main, false) << '\n';
        const auto page = client.game(7231);
        std::cout << "Game page valid=" << page.valid << " " << page.error << '\n';
        return result.valid && !result.games.empty() && page.valid ? 0 : 1;
    }
    const auto parsed = hltb::parseResults(fixture);
    if (!parsed.valid || parsed.games.size() != 1 || parsed.games[0].extra != 5400 || parsed.games[0].complete) return 1;
    if (hltb::parseResults("<html>unavailable</html>").valid || hltb::parseResults(R"({"error":"limited"})").valid) return 2;
    if (!hltb::parseResults(R"({"data":[]})").valid || !hltb::parseResults(R"({"data":[{"game_id":-1,"game_name":"bad"}]})").games.empty()) return 3;
    if (hltb::duration(0, false) != "—" || hltb::duration(5400, true) != "1 godz. 30 min") return 4;
    if (hltb::endpoint(R"(fetch("/api/other",{method:"POST"}))") != "") return 5;
    if (hltb::endpoint(R"(let searchTerms=[];fetch("/api/search/site",{method:"POST",body:JSON.stringify({searchTerms})}))") != "/api/search/site") return 6;
    const auto page = hltb::parseGamePage(R"(<script id="__NEXT_DATA__" type="application/json">{"props":{"pageProps":{"game":{"data":{"game":[{"game_id":123,"game_name":"Fixture","comp_main":3600}]}}}}}</script>)");
    if (!page.valid || page.games.size() != 1 || page.games[0].id != 123) return 7;
    hltb::Client client; int gets = 0, posts = 0;
    client.get = [&](const std::wstring& url, const std::wstring&, size_t) {
        ++gets;
        if (url == L"https://howlongtobeat.com/") return response(200, R"(<script src="/_next/static/chunks/app.js"></script>)");
        if (url.ends_with(L"app.js")) return response(200, R"(let searchTerms=[];fetch("/api/search/site",{method:"POST"}))");
        if (url.ends_with(L"/init")) return response(200, R"({"token":"fixture-token","hpKey":"fixture-key","hpValue":"fixture-value"})");
        return response(404, "");
    };
    client.post = [&](const std::wstring& url, const std::wstring& headers, const std::string& bytes, size_t) {
        ++posts; const auto request = json::parse(bytes);
        if (url != L"https://howlongtobeat.com/api/search/site" || headers.find(L"x-auth-token: fixture-token") == std::wstring::npos || !request ||
            text(*request, "fixture-key") != "fixture-value" || field(*request, "searchTerms").elements.size() != 2) return response(400, "");
        return response(200, fixture);
    };
    if (!client.search("Portal 2").valid || gets != 3 || posts != 1) return 8;
    if (!client.search("Portal 2").valid || gets != 4 || posts != 2) return 9; // Endpoint cached, fresh anonymous initialization.
    client.get = [](const auto&, const auto&, size_t) { return response(403, ""); };
    if (client.search("Portal 2").valid) return 10;
    if (argc == 3 && std::string_view(argv[1]) == "--plugin") {
        if (const int code = pluginTest(text_encoding::wide(argv[2]))) return code;
    }
    std::cout << "HLTB parsing, dynamic search, durations, failure handling and plugin cache passed\n";
}
