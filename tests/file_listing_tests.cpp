// The file browser's listing (src/platform/file_listing.hpp): order, hidden files, links,
// errors, drives and the user's folders.
#include "../src/platform/file_listing.hpp"

#include <Windows.h>

#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

int main() {
    const auto root = fs::temp_directory_path() / (L"launchme-file-listing-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ignored;
    fs::remove_all(root, ignored);
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code e; fs::remove_all(path, e); } } cleanup{root};
    fs::create_directories(root / L"Game 10");
    fs::create_directories(root / L"game 2");
    fs::create_directories(root / L".config");
    std::ofstream(root / L"b.txt") << "12345";
    std::ofstream(root / L"A.txt") << "1";
    std::ofstream(root / L".hidden") << "";
    std::ofstream(root / L"zażółć.txt") << "";

    auto listing = file_listing::list(root);
    if (!listing.ok || listing.truncated || listing.items.size() != 7) return 1;
    // Folders first, natural order ignoring case, then files.
    const std::vector<std::wstring> expected{L".config", L"game 2", L"Game 10", L".hidden", L"A.txt", L"b.txt", L"zażółć.txt"};
    for (size_t i = 0; i < expected.size(); ++i)
        if (listing.items[i].name != expected[i]) { std::wcerr << L"item " << i << L": " << listing.items[i].name << L'\n'; return 2; }
    if (!listing.items[0].folder || listing.items[4].folder || listing.items[5].size != 5 || !listing.items[5].modified) return 3;
#ifndef _WIN32
    if (!listing.items[0].hidden || !listing.items[3].hidden || listing.items[1].hidden || listing.items[5].hidden) return 4;
    // A link to a folder is a folder; a broken link is still listed.
    fs::create_directory_symlink(root / L"Game 10", root / L"link");
    fs::create_symlink(root / L"missing", root / L"broken");
    listing = file_listing::list(root);
    const auto find = [&](std::wstring_view name) -> const file_listing::Item* {
        for (const auto& item : listing.items) if (item.name == name) return &item;
        return nullptr;
    };
    if (!listing.ok || !find(L"link") || !find(L"link")->folder || !find(L"broken") || find(L"broken")->folder) return 5;
#endif
    if (!file_listing::list(root, 2).truncated || file_listing::list(root, 2).items.size() != 2) return 6;
    const auto missing = file_listing::list(root / L"missing");
    if (missing.ok || !missing.error || file_listing::accessDenied(missing.error)) return 7;
    if (!file_listing::isFolder(root) || file_listing::isFolder(root / L"b.txt") || file_listing::isFolder({})) return 8;
    if (!file_listing::lessName(L"Part 2", L"part 10") || file_listing::lessName(L"b", L"A")) return 9;

    const auto drives = file_listing::drives();
    if (drives.empty()) return 10;
#ifndef _WIN32
    if (drives[0].root != L"/" || drives[0].type != file_listing::drive_fixed || !drives[0].total) return 11;
    for (const auto& drive : drives) if (!drive.root.is_absolute()) return 12;
#endif
    for (const auto known : {file_listing::Known::desktop, file_listing::Known::downloads, file_listing::Known::documents})
        if (const auto folder = file_listing::knownFolder(known); !folder.empty() && !file_listing::isFolder(folder)) return 13;
    std::cout << "File listing, drives and known folders passed\n";
    return 0;
}
