#include "../src/library/ludusavi.hpp"

#include <iostream>

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) { std::cerr << "FAILED: " << what << '\n'; ++failures; }
}
}

int main() {
    // The latest release's package for this system; legacy builds and foreign hosts are skipped.
    const auto release = ludusavi::parseLatest(R"({"tag_name":"v0.31.0","assets":[
        {"name":"ludusavi-v0.31.0-legal.zip","browser_download_url":"https://github.com/mtkennerly/ludusavi/releases/download/v0.31.0/ludusavi-v0.31.0-legal.zip"},
        {"name":"ludusavi-v0.31.0-legacy-linux.tar.gz","browser_download_url":"https://github.com/mtkennerly/ludusavi/releases/download/v0.31.0/ludusavi-v0.31.0-legacy-linux.tar.gz"},
        {"name":"ludusavi-v0.31.0-linux.tar.gz","browser_download_url":"https://github.com/mtkennerly/ludusavi/releases/download/v0.31.0/ludusavi-v0.31.0-linux.tar.gz"},
        {"name":"ludusavi-v0.31.0-win64.zip","browser_download_url":"https://github.com/mtkennerly/ludusavi/releases/download/v0.31.0/ludusavi-v0.31.0-win64.zip"},
        {"name":"ludusavi-v0.31.0-win32.zip","browser_download_url":"https://evil.example/ludusavi-v0.31.0-win32.zip"}]})");
#ifdef _WIN32
    check(release && release->asset == "ludusavi-v0.31.0-win64.zip" && release->tag == "v0.31.0", "Windows package");
#else
    check(release && release->asset == "ludusavi-v0.31.0-linux.tar.gz", "Linux package");
#endif
    check(!ludusavi::parseLatest(R"({"tag_name":"v1","assets":[{"name":"ludusavi-v1-win64.zip","browser_download_url":"https://evil.example/x.zip"},
        {"name":"ludusavi-v1-linux.tar.gz","browser_download_url":"https://evil.example/y.tar.gz"}]})"), "only GitHub's own downloads");

    // Finding the game: by ID, else by its title.
    ludusavi::Query steam{292030, "", L"The Witcher 3"};
    check(ludusavi::findArguments(steam) == std::vector<std::wstring>{L"find", L"--api", L"--steam-id", L"292030"}, "Steam ID");
    ludusavi::Query gog{0, "1207664663", L"The Witcher 3"};
    check(ludusavi::findArguments(gog) == std::vector<std::wstring>{L"find", L"--api", L"--gog-id", L"1207664663"}, "GOG ID");
    ludusavi::Query title{0, "", L"The Witcher 3 Wild Hunt"};
    check(ludusavi::findArguments(title) == std::vector<std::wstring>{L"find", L"--api", L"--normalized", L"--", L"The Witcher 3 Wild Hunt"}, "title");
    check(ludusavi::parseFind(R"({"games":{"The Witcher 3: Wild Hunt":{"score":1.0}}})") == std::optional<std::wstring>(L"The Witcher 3: Wild Hunt"), "found");
    check(ludusavi::parseFind(R"({"games":{"A":{"score":0.5},"B":{"score":0.9}}})") == std::optional<std::wstring>(L"B"), "best score");
    check(!ludusavi::parseFind(R"({"errors":{"unknownGames":["x"]},"games":{}})") && !ludusavi::parseFind("No info"), "unknown game");

    // Backups and restores.
    check(ludusavi::operationArguments(false, L"Game", "/pfx") == std::vector<std::wstring>{L"backup", L"--api", L"--force", L"--wine-prefix", L"/pfx", L"--", L"Game"},
          "backup with a prefix");
    check(ludusavi::operationArguments(true, L"Game", "/pfx") == std::vector<std::wstring>{L"restore", L"--api", L"--force", L"--", L"Game"}, "restore");
    const auto done = ludusavi::parseOperation(R"({"overall":{"totalGames":1,"totalBytes":150,"processedGames":1,"processedBytes":150},
        "games":{"Game":{"decision":"Processed","files":{"/a/save1":{"bytes":100},"/a/save2":{"bytes":50}},"registry":{}}}})");
    check(done.ok && done.games == 1 && done.bytes == 150 && done.files == 2, "backup outcome");
    const auto nothing = ludusavi::parseOperation(R"({"overall":{"totalGames":0,"totalBytes":0,"processedGames":0,"processedBytes":0},"games":{}})");
    check(nothing.ok && !nothing.games, "nothing to back up");
    check(!ludusavi::parseOperation("garbage").ok, "not Ludusavi's answer");

    if (failures) { std::cerr << failures << " failure(s)\n"; return 1; }
    std::cout << "Ludusavi requests and answers passed\n";
    return 0;
}
