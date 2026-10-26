// src/platform/updater.cpp without a network: GitHub's release answer, versions, the notes as text,
// the package unpacked with its paths checked, the swap of the files (the running EXE renamed)
// with its rollback, and the cleanup after the restart.
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz.h>

#include "../src/platform/updater.hpp"
#include "../src/platform/atomic_file.hpp"

#include <Windows.h>

#include <fstream>
#include <iostream>

#define CHECK(value) do { if (!(value)) { std::cerr << "updater check failed at line " << __LINE__ << ": " << #value << '\n'; return __LINE__; } } while (false)

namespace {
using namespace updater;
const std::string hash = "051ed4cc99727575f3ee8ddbbed99739a1af944a0b82f181f31979f4efc48133";
std::string release(const std::string& tag, const std::string& asset, const std::string& digest, const std::string& body, bool prerelease = false) {
    return R"({"tag_name":")" + tag + R"(","html_url":"https://github.com/uxandai/launchme/releases/tag/)" + tag + R"(","draft":false,"prerelease":)" +
           (prerelease ? "true" : "false") + R"(,"body":)" + body + R"(,"assets":[{"name":"notes.txt","browser_download_url":"https://github.com/uxandai/launchme/releases/download/x/notes.txt","size":3},
           {"name":")" + asset + R"(","browser_download_url":"https://github.com/uxandai/launchme/releases/download/)" + tag + "/" + asset +
           R"(","size":128219855,"digest":")" + digest + R"("}]})";
}
std::string text(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
bool write(const std::filesystem::path& file, const std::string& content) {
    std::filesystem::create_directories(file.parent_path());
    return atomic_file::write(file, content);
}
bool zip(const std::filesystem::path& file, const std::vector<std::pair<std::string, std::string>>& entries) {
    mz_zip_archive archive{};
    if (!mz_zip_writer_init_heap(&archive, 0, 0)) return false;
    for (const auto& [name, content] : entries)
        if (!mz_zip_writer_add_mem(&archive, name.c_str(), content.data(), content.size(), static_cast<mz_uint>(MZ_DEFAULT_LEVEL))) return false;
    void* data = nullptr; size_t size = 0;
    if (!mz_zip_writer_finalize_heap_archive(&archive, &data, &size)) return false;
    std::string bytes(static_cast<char*>(data), size);
    mz_zip_writer_end(&archive);
    return write(file, bytes);
}

int releases() {
    const std::string notes = R"("### Install\n\n1. Download the zip.\n\n### New\n\n- **Achievement card** (*Details*): a [larger](https://x.test) icon.\n  - Nested `code`.\n\n### Fixes\n\n- Something.\n\nSHA-256: `)" + hash + R"(`\n")";
    const auto parsed = parseRelease(release("v0.1.8", "launchme-0.1.8-win64.zip", "sha256:" + hash, notes));
    CHECK(parsed && parsed->version == L"0.1.8" && parsed->package_size == 128219855 && parsed->sha256 == hash);
    CHECK(parsed->package_url == L"https://github.com/uxandai/launchme/releases/download/v0.1.8/launchme-0.1.8-win64.zip");
    CHECK(parsed->page == L"https://github.com/uxandai/launchme/releases/tag/v0.1.8");
    CHECK(parsed->notes == L"New\n\n\u2022 Achievement card (Details): a larger icon.\n  \u2022 Nested code.\n\nFixes\n\n\u2022 Something.");
    // No digest on the asset: the notes' own line; neither: no update to offer.
    CHECK(parseRelease(release("v0.1.8", "launchme-0.1.8-win64.zip", "", notes))->sha256 == hash);
    CHECK(!parseRelease(release("v0.1.8", "launchme-0.1.8-win64.zip", "", R"("No hash here")")));
    CHECK(!parseRelease(release("v0.1.8", "launchme-0.1.9-win64.zip", "sha256:" + hash, notes))); // Not this version's package.
    CHECK(!parseRelease(release("v0.1.8", "launchme-0.1.8-win64.zip", "sha256:" + hash, notes, true))); // A pre-release.
    CHECK(!parseRelease(release("nightly", "launchme-nightly-win64.zip", "sha256:" + hash, notes)) && !parseRelease("not json") && !parseRelease("{}"));
    CHECK(allowedDownload(L"https://github.com/uxandai/launchme/releases/download/v1/a.zip") && allowedDownload(L"https://release-assets.githubusercontent.com/x"));
    CHECK(!allowedDownload(L"https://github.com/other/launchme/releases/download/v1/a.zip") && !allowedDownload(L"http://github.com/uxandai/launchme/releases/download/"));
    CHECK(newer(L"0.1.10", L"0.1.9") && newer(L"1.0.0", L"0.9.9") && !newer(L"0.1.7", L"0.1.7") && !newer(L"0.1.6", L"0.1.7"));
    CHECK(!newer(L"0.1.8", L"") && !newer(L"0.1", L"0.0.1") && !newer(L"0.1.8-beta", L"0.1.7"));
    return 0;
}

int packages(const std::filesystem::path& root) {
    CHECK(write(root / L"abc.txt", "abc") && sha256File(root / L"abc.txt") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256File(root / L"missing.bin").empty());
    // A good package: "launchme/" stripped, folders kept.
    const auto good = root / L"good.zip";
    CHECK(zip(good, {{"launchme/", ""}, {"launchme/launchme.exe", "new exe"}, {"launchme/plugin-packages/launchme.howlongtobeat.zip", "new hltb"},
                     {"launchme/assets/HorizonsTheme.mp3", "new music"}}));
    std::wstring error;
    const auto staging = root / L"app" / work_folder / L"staging";
    const auto files = unpack(good, staging, error);
    CHECK(files && files->size() == 3 && text(staging / L"launchme.exe") == "new exe" && text(staging / L"plugin-packages" / L"launchme.howlongtobeat.zip") == "new hltb");
    // Unsafe paths and packages without the launcher are refused before anything is written.
    for (const auto& name : {"launchme/../evil.exe", "launchme/C:/evil.exe", "other/launchme.exe", "launchme/a\\..\\b.exe"}) {
        const auto bad = root / L"bad.zip";
        CHECK(zip(bad, {{"launchme/launchme.exe", "x"}, {name, "evil"}}));
        error.clear();
        CHECK(!unpack(bad, root / L"bad-staging", error) && !error.empty());
        CHECK(!std::filesystem::exists(root / L"evil.exe"));
    }
    CHECK(zip(root / L"noexe.zip", {{"launchme/readme.txt", "x"}}) && !unpack(root / L"noexe.zip", root / L"noexe-staging", error));
    CHECK(!unpack(root / L"abc.txt", root / L"txt-staging", error)); // Not a ZIP.

    // The swap: existing files become .update-old, new ones move in; the private plugin stays.
    const auto app = root / L"app";
    CHECK(write(app / L"launchme.exe", "old exe") && write(app / L"plugin-packages" / L"launchme.howlongtobeat.zip", "old hltb") &&
          write(app / L"plugin-packages" / L"launchme.store.zip", "private"));
    CHECK(apply(staging, *files, app, error));
    CHECK(text(app / L"launchme.exe") == "new exe" && text(app / (std::wstring(L"launchme.exe") + old_suffix)) == "old exe");
    CHECK(text(app / L"plugin-packages" / L"launchme.howlongtobeat.zip") == "new hltb" && text(app / L"plugin-packages" / L"launchme.store.zip") == "private");
    CHECK(text(app / L"assets" / L"HorizonsTheme.mp3") == "new music"); // A file the old copy lacked.
    // The new EXE did not start: everything as it was.
    rollback(app, *files);
    CHECK(text(app / L"launchme.exe") == "old exe" && text(app / L"plugin-packages" / L"launchme.howlongtobeat.zip") == "old hltb");
    CHECK(!std::filesystem::exists(app / L"assets" / L"HorizonsTheme.mp3") && !std::filesystem::exists(app / (std::wstring(L"launchme.exe") + old_suffix)));
    // A file in use cannot be renamed away: nothing changes.
    const auto again = unpack(good, staging, error);
    CHECK(again);
    {
        const auto locked = app / L"plugin-packages" / L"launchme.howlongtobeat.zip";
        const HANDLE handle = CreateFileW(locked.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        CHECK(handle != INVALID_HANDLE_VALUE);
        error.clear();
        const bool applied = apply(staging, *again, app, error);
        CloseHandle(handle);
        CHECK(!applied && !error.empty() && text(app / L"launchme.exe") == "old exe" && text(locked) == "old hltb");
    }
    // Success, then the next start's cleanup: the old files and the work folder go.
    CHECK(apply(staging, *unpack(good, staging, error), app, error));
    cleanup(app);
    CHECK(!std::filesystem::exists(app / (std::wstring(L"launchme.exe") + old_suffix)) && !std::filesystem::exists(app / work_folder));
    CHECK(text(app / L"launchme.exe") == "new exe" && text(app / L"plugin-packages" / L"launchme.store.zip") == "private");
    return 0;
}
}

int main() {
    const auto root = std::filesystem::temp_directory_path() / (L"launchme-updater-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(root);
    struct Cleanup { std::filesystem::path root; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(root, ec); } } cleanup{root};
    if (const int failed = releases()) return failed;
    if (const int failed = packages(root)) return failed;
    std::cout << "Updater: releases, versions, notes, package, swap, rollback and cleanup passed\n";
    return 0;
}
