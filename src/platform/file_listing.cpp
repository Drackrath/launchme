#include "file_listing.hpp"

#include <Windows.h>
#include <ShlObj.h>

#include <algorithm>

namespace file_listing {
namespace {
// No error box for an empty card reader or a disc drive without a disc (this thread only).
struct QuietErrors {
    DWORD previous = 0;
    QuietErrors() { SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, &previous); }
    ~QuietErrors() { SetThreadErrorMode(previous, nullptr); }
};
} // namespace

bool lessName(const std::wstring& left, const std::wstring& right) {
    const int order = CompareStringEx(LOCALE_NAME_USER_DEFAULT, LINGUISTIC_IGNORECASE | SORT_DIGITSASNUMBERS,
                                      left.c_str(), static_cast<int>(left.size()), right.c_str(), static_cast<int>(right.size()), nullptr, nullptr, 0);
    if (order) return order == CSTR_LESS_THAN;
    return _wcsicmp(left.c_str(), right.c_str()) < 0;
}

Listing list(const std::filesystem::path& folder, size_t limit) {
    Listing result;
    if (folder.empty()) { result.error = ERROR_PATH_NOT_FOUND; return result; }
    auto pattern = folder.native();
    if (!pattern.empty() && pattern.back() != L'\\' && pattern.back() != L'/') pattern += L'\\';
    pattern += L'*';
    WIN32_FIND_DATAW data{};
    const QuietErrors quiet;
    const HANDLE find = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (find == INVALID_HANDLE_VALUE) {
        result.error = GetLastError();
        // An empty drive root still lists (no "." entries): that is an empty folder, not an error.
        result.ok = result.error == ERROR_FILE_NOT_FOUND;
        return result;
    }
    do {
        const std::wstring_view name = data.cFileName;
        if (name == L"." || name == L"..") continue;
        if (result.items.size() >= limit) { result.truncated = true; break; }
        Item item;
        item.name = data.cFileName;
        item.folder = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        item.hidden = (data.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) != 0;
        item.size = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
        item.modified = (static_cast<uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime;
        result.items.push_back(std::move(item));
    } while (FindNextFileW(find, &data));
    FindClose(find);
    result.ok = true;
    std::sort(result.items.begin(), result.items.end(), [](const Item& left, const Item& right) {
        if (left.folder != right.folder) return left.folder;
        return lessName(left.name, right.name);
    });
    return result;
}

std::vector<Drive> drives() {
    std::vector<Drive> result;
    const DWORD mask = GetLogicalDrives();
    const QuietErrors quiet;
    for (int letter = 0; letter < 26; ++letter) {
        if (!(mask & (1u << letter))) continue;
        Drive drive;
        const wchar_t root[] = {static_cast<wchar_t>(L'A' + letter), L':', L'\\', 0};
        drive.root = root;
        drive.type = GetDriveTypeW(root);
        if (drive.type == DRIVE_NO_ROOT_DIR || drive.type == DRIVE_UNKNOWN) continue;
        if (drive.type == DRIVE_FIXED) {
            // Only local disks: removable, optical and network drives may block for seconds.
            wchar_t label[MAX_PATH + 1]{};
            if (GetVolumeInformationW(root, label, MAX_PATH + 1, nullptr, nullptr, nullptr, nullptr, 0)) drive.label = label;
            ULARGE_INTEGER free{}, total{};
            if (GetDiskFreeSpaceExW(root, &free, &total, nullptr)) { drive.free = free.QuadPart; drive.total = total.QuadPart; }
        }
        result.push_back(std::move(drive));
    }
    return result;
}

std::filesystem::path knownFolder(Known folder) {
    const KNOWNFOLDERID* id = &FOLDERID_Desktop;
    switch (folder) {
    case Known::desktop: id = &FOLDERID_Desktop; break;
    case Known::downloads: id = &FOLDERID_Downloads; break;
    case Known::documents: id = &FOLDERID_Documents; break;
    case Known::pictures: id = &FOLDERID_Pictures; break;
    case Known::music: id = &FOLDERID_Music; break;
    case Known::videos: id = &FOLDERID_Videos; break;
    }
    PWSTR path = nullptr;
    if (FAILED(SHGetKnownFolderPath(*id, KF_FLAG_DONT_VERIFY, nullptr, &path))) return {};
    std::filesystem::path result(path);
    CoTaskMemFree(path);
    return isFolder(result) ? result : std::filesystem::path{};
}

bool isFolder(const std::filesystem::path& path) {
    if (path.empty()) return false;
    const QuietErrors quiet;
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}
} // namespace file_listing
