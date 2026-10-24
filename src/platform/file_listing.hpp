#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Folder contents and drives for the launcher's own file picker (src/app/file_browser.cpp).
namespace file_listing {
struct Item {
    std::wstring name;
    bool folder = false, hidden = false; // hidden: the hidden or system attribute.
    uint64_t size = 0, modified = 0;     // FILETIME ticks.
};
struct Listing {
    bool ok = false;
    unsigned long error = 0; // GetLastError() when !ok.
    bool truncated = false;  // More than `limit` items.
    std::vector<Item> items; // Folders first, then files; each by name with numbers in order.
};
Listing list(const std::filesystem::path& folder, size_t limit = 20000);

struct Drive {
    std::filesystem::path root; // "C:\".
    std::wstring label;         // Volume label of fixed drives; empty elsewhere (no slow spin-up).
    unsigned type = 0;          // GetDriveTypeW.
    uint64_t total = 0, free = 0;
};
std::vector<Drive> drives();

// Desktop, Downloads, Documents, Pictures, Music, Videos; empty when Windows has none.
enum class Known { desktop, downloads, documents, pictures, music, videos };
std::filesystem::path knownFolder(Known folder);

bool isFolder(const std::filesystem::path& path);
// Natural order, case-insensitive ("Game 2" before "Game 10").
bool lessName(const std::wstring& left, const std::wstring& right);
} // namespace file_listing
