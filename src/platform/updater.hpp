#pragma once
// Launchme's own updates from its GitHub releases: the latest release's version, package and
// SHA-256, the package unpacked next to the running launcher and swapped in (the running EXE is
// renamed, never overwritten, so Windows lets it go on running), then the old files removed on the
// next start. No network or process here; src/app/updates.cpp drives it.
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace updater {
inline constexpr wchar_t latest_url[] = L"https://api.github.com/repos/uxandai/launchme/releases/latest";
inline constexpr wchar_t releases_page[] = L"https://github.com/uxandai/launchme/releases";
// Downloads only from the repository's releases and GitHub's asset storage it redirects to.
bool allowedDownload(std::wstring_view url);

struct Release {
    std::wstring version;     // "0.1.8" (the tag without its "v").
    std::wstring page;        // The release's page on GitHub.
    std::wstring package_url; // launchme-<version>-win64.zip
    uint64_t package_size = 0;
    std::string sha256;       // Lower-case hex; from the asset's digest, else the notes' "SHA-256:" line.
    std::wstring notes;       // The changelog as plain text (sections, bullets).
};
// GitHub's releases/latest answer. nullopt: not a release with the Windows package and its hash.
std::optional<Release> parseRelease(std::string_view json);
// Release notes (Markdown) as plain text: "### New" becomes "New", "- **X**" becomes "• X", links
// keep their text; the Install section and the SHA-256 line are left out.
std::wstring plainNotes(std::string_view markdown);
// "0.1.10" > "0.1.9"; anything that is not x.y.z is never newer.
bool newer(std::wstring_view candidate, std::wstring_view current);

// SHA-256 of a file in lower-case hex; empty when it cannot be read.
std::string sha256File(const std::filesystem::path& file);

// The package unpacked into `staging` (a new folder): its "launchme/" prefix stripped, paths checked
// (no absolute, drive or ".." parts), at most 2000 files and 1 GiB. Returns the relative paths of
// the files, or nullopt with `error`.
std::optional<std::vector<std::filesystem::path>> unpack(const std::filesystem::path& zip, const std::filesystem::path& staging, std::wstring& error);

// Swaps the staged files into `app`: each existing file becomes "<name>.update-old" first (the
// running EXE too), then the new one moves in. Any failure puts every file back. Files the package
// does not have (private plugins, settings) stay as they are.
bool apply(const std::filesystem::path& staging, const std::vector<std::filesystem::path>& files, const std::filesystem::path& app, std::wstring& error);
// The swapped-in files back out (the new EXE did not start): the .update-old ones return.
void rollback(const std::filesystem::path& app, const std::vector<std::filesystem::path>& files);
// After the restart: every "*.update-old" under `app` and the staging folder removed (best effort).
void cleanup(const std::filesystem::path& app);
inline constexpr wchar_t old_suffix[] = L".update-old";
inline constexpr wchar_t work_folder[] = L".update"; // <app>\.update: the package and its staging.
}
