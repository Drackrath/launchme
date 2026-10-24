#include "../src/library/owned_games.hpp"
#include "../src/net/json.hpp"

#include <Windows.h>

#include <chrono>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

int main() {
    const auto names = parseSteamAppListNames(
        R"({"applist":{"apps":[{"appid":620,"name":"Portal 2"},{"appid":999,"name":"Hidden"},{"appid":10,"name":"R\u00f3\u017ca"}]}})",
        {620, 10});
    if (names.size() != 2 || names.at(620) != L"Portal 2" || names.at(10) != L"Róża" ||
        names.contains(999)) {
        std::cerr << "Targeted Steam AppList lookup failed or resolved an excluded ID\n";
        return 1;
    }
    // Numbers outside int (and "1e999", parsed as infinity) fall back instead of undefined conversion.
    const auto numbers = json::parse(R"({"big":3000000000,"huge":1e999,"low":-5,"fraction":42.9})");
    if (!numbers || numbers->get("big")->intOr(-1) != -1 || numbers->get("huge")->intOr(-1) != -1 ||
        numbers->get("low")->intOr() != -5 || numbers->get("fraction")->intOr() != 42) {
        std::cerr << "json::Value::intOr accepted a number outside int\n";
        return 2;
    }
    // Names already cached are read back, never rewritten; oversized files are ignored.
    const auto cache = fs::temp_directory_path() / (L"launchme-owned-names-" + std::to_wstring(GetCurrentProcessId()));
    fs::create_directories(cache);
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); }
    } cleanup{cache};
    { std::ofstream file(cache / L"620.txt", std::ios::binary); file << "Portal 2"; }
    { std::ofstream file(cache / L"10.txt", std::ios::binary); file << std::string(501, 'x'); }
    const auto old = fs::last_write_time(cache / L"620.txt") - std::chrono::hours(48);
    fs::last_write_time(cache / L"620.txt", old);
    const auto cached = cachedCollectionGameNames({620, 10, 30}, cache);
    if (cached.size() != 1 || cached.at(620) != L"Portal 2") {
        std::cerr << "Cached Steam names were not read back or an oversized file was accepted\n";
        return 3;
    }
    const auto resolved = collectionGameNames(76561197960265728ULL + 42, {620}, cache); // All cached: no network.
    if (resolved.size() != 1 || resolved.at(620) != L"Portal 2" || fs::last_write_time(cache / L"620.txt") != old) {
        std::cerr << "A cached Steam name was written again\n";
        return 4;
    }
    return 0;
}
