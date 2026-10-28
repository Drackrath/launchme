// src/platform/updater.cpp without a network: GitHub's release answer, versions, the notes as text,
// the package unpacked with its paths checked, the swap of the files (the running EXE renamed)
// with its rollback, and the cleanup after the restart. Linux: the AppImage package, checked as an
// x86-64 ELF AppImage, swapped in over the running one ($APPIMAGE) with its rollback.
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
#ifdef _WIN32
const std::string suffix = "-win64.zip";
#else
const std::string suffix = "-x86_64.AppImage";
#endif
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
#ifdef _WIN32
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
#endif

int releases() {
    const std::string notes = R"("### Install\n\n1. Download the zip.\n\n### New\n\n- **Achievement card** (*Details*): a [larger](https://x.test) icon.\n  - Nested `code`.\n\n### Fixes\n\n- Something.\n\nSHA-256: `)" + hash + R"(`\n")";
    const auto parsed = parseRelease(release("v0.1.8", "launchme-0.1.8" + suffix, "sha256:" + hash, notes));
    CHECK(parsed && parsed->version == L"0.1.8" && parsed->package_size == 128219855 && parsed->sha256 == hash);
    CHECK(parsed->package_url == L"https://github.com/uxandai/launchme/releases/download/v0.1.8/launchme-0.1.8" + std::wstring(suffix.begin(), suffix.end()));
    CHECK(parsed->page == L"https://github.com/uxandai/launchme/releases/tag/v0.1.8");
    CHECK(parsed->notes == L"New\n\n\u2022 Achievement card (Details): a larger icon.\n  \u2022 Nested code.\n\nFixes\n\n\u2022 Something.");
    // No digest on the asset: the notes' own line; neither: no update to offer.
    CHECK(parseRelease(release("v0.1.8", "launchme-0.1.8" + suffix, "", notes))->sha256 == hash);
    CHECK(!parseRelease(release("v0.1.8", "launchme-0.1.8" + suffix, "", R"("No hash here")")));
    CHECK(!parseRelease(release("v0.1.8", "launchme-0.1.9" + suffix, "sha256:" + hash, notes))); // Not this version's package.
    CHECK(!parseRelease(release("v0.1.8", "launchme-0.1.8" + suffix, "sha256:" + hash, notes, true))); // A pre-release.
    CHECK(!parseRelease(release("nightly", "launchme-nightly" + suffix, "sha256:" + hash, notes)) && !parseRelease("not json") && !parseRelease("{}"));
    CHECK(allowedDownload(L"https://github.com/uxandai/launchme/releases/download/v1/a.zip") && allowedDownload(L"https://release-assets.githubusercontent.com/x"));
    CHECK(!allowedDownload(L"https://github.com/other/launchme/releases/download/v1/a.zip") && !allowedDownload(L"http://github.com/uxandai/launchme/releases/download/"));
    CHECK(newer(L"0.1.10", L"0.1.9") && newer(L"1.0.0", L"0.9.9") && !newer(L"0.1.7", L"0.1.7") && !newer(L"0.1.6", L"0.1.7"));
    CHECK(!newer(L"0.1.8", L"") && !newer(L"0.1", L"0.0.1") && !newer(L"0.1.8-beta", L"0.1.7"));
    return 0;
}

#ifdef _WIN32
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
#else
// An ELF header for x86-64 (`machine` 62) with the AppImage type 2 mark, then `body`.
std::string elf(const std::string& body, uint8_t machine = 62, bool mark = true) {
    std::string head(64, '\0');
    head.replace(0, 4, "\x7F" "ELF");
    head[4] = 2; head[5] = 1; head[6] = 1; // 64-bit, little-endian, version 1.
    if (mark) { head[8] = 'A'; head[9] = 'I'; head[10] = 2; }
    head[16] = 2; head[18] = static_cast<char>(machine); // ET_EXEC.
    return head + body;
}

int packages(const std::filesystem::path& root) {
    CHECK(write(root / L"abc.txt", "abc") && sha256File(root / L"abc.txt") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256File(root / L"missing.bin").empty());
    // The running AppImage, under the name the user gave it.
    const auto app = root / L"Applications";
    const auto running = app / L"Launchme.AppImage";
    CHECK(write(running, elf("old")) && appImage() == running);
    const auto good = root / L"launchme-0.1.8-x86_64.AppImage";
    CHECK(write(good, elf("new")));
    std::wstring error;
    const auto staging = app / work_folder / L"staging";
    const auto files = unpack(good, staging, error);
    CHECK(files && files->size() == 1 && files->front() == L"Launchme.AppImage" && text(staging / L"Launchme.AppImage") == elf("new"));
    CHECK((std::filesystem::status(staging / L"Launchme.AppImage").permissions() & std::filesystem::perms::owner_exec) != std::filesystem::perms::none);
    // Not an AppImage for this machine: refused before anything is staged.
    for (const auto& bad : {std::string("#!/bin/sh\necho evil\n") + std::string(64, ' '), elf("arm", 183), elf("plain ELF", 62, false), elf("").substr(0, 20)}) {
        CHECK(write(root / L"bad.AppImage", bad));
        error.clear();
        CHECK(!unpack(root / L"bad.AppImage", root / L"bad-staging", error) && !error.empty());
        CHECK(!std::filesystem::exists(root / L"bad-staging" / L"Launchme.AppImage"));
    }
    // The swap: the new file renamed over the running one, the old kept as .update-old.
    const auto old = app / (std::wstring(L"Launchme.AppImage") + old_suffix);
    CHECK(apply(staging, *files, app, error));
    CHECK(text(running) == elf("new") && text(old) == elf("old"));
    // The new AppImage did not start: the old one back.
    rollback(app, *files);
    CHECK(text(running) == elf("old") && !std::filesystem::exists(old));
    // Nothing staged: nothing changes, nothing left behind.
    error.clear();
    CHECK(!apply(root / L"empty-staging", *files, app, error) && !error.empty() && text(running) == elf("old") && !std::filesystem::exists(old));
    // Several files (as on Windows): existing ones kept as .update-old, new ones added, the rest untouched.
    const auto many = root / L"many";
    CHECK(write(many / L"staging" / L"a.bin", "new a") && write(many / L"staging" / L"sub" / L"b.bin", "new b"));
    CHECK(write(many / L"app" / L"a.bin", "old a") && write(many / L"app" / L"private.bin", "private"));
    const std::vector<std::filesystem::path> both{L"a.bin", std::filesystem::path(L"sub") / L"b.bin"};
    CHECK(apply(many / L"staging", both, many / L"app", error));
    CHECK(text(many / L"app" / L"a.bin") == "new a" && text(many / L"app" / L"sub" / L"b.bin") == "new b" && text(many / L"app" / (std::wstring(L"a.bin") + old_suffix)) == "old a");
    rollback(many / L"app", both);
    CHECK(text(many / L"app" / L"a.bin") == "old a" && !std::filesystem::exists(many / L"app" / L"sub" / L"b.bin") && text(many / L"app" / L"private.bin") == "private");
    // Success, then the next start's cleanup: the old copy and the work folder go.
    CHECK(apply(staging, *unpack(good, staging, error), app, error));
    cleanup(app);
    CHECK(!std::filesystem::exists(old) && !std::filesystem::exists(app / work_folder) && text(running) == elf("new"));
    return 0;
}
#endif
}

int main() {
    const auto root = std::filesystem::temp_directory_path() / (L"launchme-updater-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(root);
    struct Cleanup { std::filesystem::path root; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(root, ec); } } cleanup{root};
#ifndef _WIN32
    // Not running as an AppImage: nothing to replace, so no update is offered.
    SetEnvironmentVariableW(L"APPIMAGE", nullptr);
    CHECK(!appImage() && !parseRelease(release("v0.1.8", "launchme-0.1.8" + suffix, "sha256:" + hash, R"("SHA-256: `)" + hash + R"(`")")));
    const auto running = root / L"Applications" / L"Launchme.AppImage";
    CHECK(write(running, "x") && SetEnvironmentVariableW(L"APPIMAGE", running.wstring().c_str()) && appImage() == running);
#endif
    if (const int failed = releases()) return failed;
    if (const int failed = packages(root)) return failed;
    std::cout << "Updater: releases, versions, notes, package, swap, rollback and cleanup passed\n";
    return 0;
}
