#pragma once
// Save backups through Ludusavi (github.com/mtkennerly/ludusavi, MIT): its command line finds a game in
// its data set of save locations (from PCGamingWiki) and backs up or restores its saves, into the
// backup folder of Ludusavi's own configuration. Launchme finds Ludusavi (its own copy in
// <data>/tools/ludusavi, PATH, Flatpak) or downloads the newest release for this system. Portable.
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ludusavi {
std::filesystem::path ownFolder(const std::filesystem::path& data); // <data>/tools/ludusavi
// The program to run: Launchme's copy, else one on PATH, else Flatpak's wrapper; empty: none.
std::filesystem::path find(const std::filesystem::path& data);

// The latest release's asset for this system ("-win64.zip", "-linux.tar.gz"), from GitHub's JSON.
struct Release {
    std::string tag;
    std::wstring url;
    std::string asset;
};
std::optional<Release> parseLatest(std::string_view json);
// Downloads and unpacks it into ownFolder (tar); an error text, empty on success. Worker threads only.
std::wstring install(const std::filesystem::path& data);

// How Launchme names a game to Ludusavi: its Steam app ID, its GOG ID, else its title.
struct Query {
    uint32_t steam_id = 0;
    std::string gog_id;
    std::wstring title;
};
std::vector<std::wstring> findArguments(const Query& query); // find --api ...
std::optional<std::wstring> parseFind(std::string_view json); // The best title; nullopt: not in the data set.

// backup / restore of one game: --api --force [--wine-prefix <prefix>] <title>.
std::vector<std::wstring> operationArguments(bool restore, const std::wstring& title, const std::filesystem::path& wine_prefix);
struct Outcome {
    bool ok = false;
    uint32_t games = 0;  // Games processed.
    uint64_t bytes = 0;
    uint32_t files = 0;
};
Outcome parseOperation(std::string_view json);
}
