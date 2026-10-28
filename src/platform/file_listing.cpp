#include "file_listing.hpp"

#ifdef _WIN32
#include <Windows.h>
#include <ShlObj.h>
#else
#include "system.hpp"
#include "text_encoding.hpp"
#include "unicode_text.hpp"
#include <dirent.h>
#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <fstream>
#endif

#include <algorithm>

namespace file_listing {
namespace {
void sortItems(std::vector<Item>& items) {
    std::sort(items.begin(), items.end(), [](const Item& left, const Item& right) {
        if (left.folder != right.folder) return left.folder;
        return lessName(left.name, right.name);
    });
}
} // namespace

#ifdef _WIN32
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
    auto pattern = folder.wstring();
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
    sortItems(result.items);
    return result;
}

bool accessDenied(unsigned long error) { return error == ERROR_ACCESS_DENIED; }

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
#else
namespace {
// FILETIME ticks (100 ns since 1601), as on Windows.
uint64_t ticks(const timespec& time) {
    if (time.tv_sec < -11644473600LL) return 0;
    return static_cast<uint64_t>(time.tv_sec + 11644473600LL) * 10'000'000ULL + static_cast<uint64_t>(time.tv_nsec) / 100;
}
std::filesystem::path home() {
    if (const auto value = system_info::environment(L"HOME"); value && !value->empty() && value->front() == L'/') return *value;
    passwd entry{}; passwd* found = nullptr; char buffer[4096];
    if (getpwuid_r(getuid(), &entry, buffer, sizeof(buffer), &found) == 0 && found && found->pw_dir) return text_encoding::wideLenient(found->pw_dir);
    return {};
}
// /proc/self/mounts writes space, tab, newline and backslash as octal escapes (\040).
std::string unescape(std::string_view field) {
    std::string out;
    const auto octal = [](char ch) { return ch >= '0' && ch <= '7'; };
    for (size_t i = 0; i < field.size(); ++i) {
        if (field[i] == '\\' && i + 3 < field.size() && octal(field[i + 1]) && octal(field[i + 2]) && octal(field[i + 3])) {
            out += static_cast<char>((field[i + 1] - '0') * 64 + (field[i + 2] - '0') * 8 + (field[i + 3] - '0'));
            i += 3;
        } else out += field[i];
    }
    return out;
}
bool under(std::string_view path, std::string_view folder) {
    return path.size() > folder.size() && path.starts_with(folder) && path[folder.size()] == '/';
}
// Not volumes to browse: the system's own parts, virtual file systems, snaps and loop images.
bool systemMount(std::string_view source, std::string_view point) {
    for (const std::string_view folder : {"/", "/boot", "/efi", "/home", "/opt", "/root", "/srv", "/tmp", "/usr", "/var", "/nix", "/gnu", "/snap"})
        if (point == folder) return true;
    for (const std::string_view folder : {"/proc", "/sys", "/dev", "/boot", "/efi", "/usr", "/var", "/snap", "/nix", "/gnu", "/tmp"})
        if (under(point, folder)) return true;
    if ((point == "/run" || under(point, "/run")) && !under(point, "/run/media")) return true;
    return source.starts_with("/dev/loop");
}
bool remoteType(std::string_view type) {
    for (const std::string_view remote : {"nfs", "nfs4", "cifs", "smb3", "smbfs", "afs", "9p", "fuse.sshfs", "fuse.rclone", "davfs", "fuse.davfs2"})
        if (type == remote) return true;
    return false;
}
unsigned volumeType(std::string_view source, std::string_view type) {
    if (remoteType(type)) return drive_remote;
    if (source.starts_with("/dev/sr") || type == "iso9660" || type == "udf") return drive_optical;
    // A partition's disk says whether it is removable: /sys/class/block/sdb1/../removable.
    const std::filesystem::path block = std::filesystem::path("/sys/class/block") / std::string(source.substr(5));
    for (const auto& file : {block / "removable", block / ".." / "removable"}) {
        std::ifstream input(file);
        char flag = 0;
        if (input >> flag) return flag == '1' ? drive_removable : drive_fixed;
    }
    return drive_fixed;
}
void space(Drive& drive) {
    struct statvfs info{};
    if (statvfs(drive.root.c_str(), &info) != 0) return;
    drive.total = static_cast<uint64_t>(info.f_blocks) * info.f_frsize;
    drive.free = static_cast<uint64_t>(info.f_bavail) * info.f_frsize;
}
} // namespace

bool lessName(const std::wstring& left, const std::wstring& right) {
    return unicode_text::compareNatural(left, right) < 0;
}

Listing list(const std::filesystem::path& folder, size_t limit) {
    Listing result;
    if (folder.empty()) { result.error = ENOENT; return result; }
    const int handle = ::open(folder.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    DIR* directory = handle >= 0 ? fdopendir(handle) : nullptr;
    if (!directory) {
        result.error = static_cast<unsigned long>(errno);
        if (handle >= 0) ::close(handle);
        return result;
    }
    while (const dirent* entry = readdir(directory)) {
        const std::string_view name = entry->d_name;
        if (name == "." || name == "..") continue;
        if (result.items.size() >= limit) { result.truncated = true; break; }
        Item item;
        item.name = text_encoding::wideLenient(name);
        item.hidden = name.front() == '.';
        // A link shows its target; a broken one stays a file.
        struct stat info{};
        if (fstatat(dirfd(directory), entry->d_name, &info, 0) == 0 || fstatat(dirfd(directory), entry->d_name, &info, AT_SYMLINK_NOFOLLOW) == 0) {
            item.folder = S_ISDIR(info.st_mode);
            item.size = item.folder ? 0 : static_cast<uint64_t>(info.st_size);
            item.modified = ticks(info.st_mtim);
        } else item.folder = entry->d_type == DT_DIR;
        result.items.push_back(std::move(item));
    }
    closedir(directory);
    result.ok = true;
    sortItems(result.items);
    return result;
}

bool accessDenied(unsigned long error) { return error == EACCES || error == EPERM; }

std::vector<Drive> drives() {
    std::vector<Drive> result;
    const auto add = [&](Drive drive) {
        if (std::any_of(result.begin(), result.end(), [&](const Drive& other) { return other.root == drive.root; })) return;
        if (drive.type == drive_fixed) space(drive); // Removable, optical and network volumes may block.
        result.push_back(std::move(drive));
    };
    add({"/", {}, drive_fixed});
    if (const auto folder = home(); !folder.empty() && isFolder(folder)) add({folder, {}, drive_fixed});
    // Mounted volumes: udisks puts them in /run/media/$USER (or /media), fstab often in /mnt.
    std::vector<Drive> volumes;
    std::ifstream mounts("/proc/self/mounts");
    for (std::string line; std::getline(mounts, line);) {
        const auto first = line.find(' '), second = first == std::string::npos ? first : line.find(' ', first + 1);
        const auto third = second == std::string::npos ? second : line.find(' ', second + 1);
        if (third == std::string::npos) continue;
        const auto source = unescape(std::string_view(line).substr(0, first));
        const auto point = unescape(std::string_view(line).substr(first + 1, second - first - 1));
        const auto type = std::string_view(line).substr(second + 1, third - second - 1);
        if ((!source.starts_with("/dev/") && !remoteType(type)) || !point.starts_with('/') || systemMount(source, point)) continue;
        Drive drive;
        drive.root = point;
        drive.type = volumeType(source, type);
        if (under(point, "/run/media") || under(point, "/media")) drive.label = drive.root.filename().wstring();
        // A later mount on the same folder covers the earlier one.
        std::erase_if(volumes, [&](const Drive& other) { return other.root == drive.root; });
        volumes.push_back(std::move(drive));
    }
    std::sort(volumes.begin(), volumes.end(), [](const Drive& a, const Drive& b) { return a.root < b.root; });
    for (auto& drive : volumes) add(std::move(drive));
    return result;
}

std::filesystem::path knownFolder(Known folder) {
    const char* key = "DESKTOP";
    const wchar_t* fallback = L"Desktop";
    switch (folder) {
    case Known::desktop: key = "DESKTOP"; fallback = L"Desktop"; break;
    case Known::downloads: key = "DOWNLOAD"; fallback = L"Downloads"; break;
    case Known::documents: key = "DOCUMENTS"; fallback = L"Documents"; break;
    case Known::pictures: key = "PICTURES"; fallback = L"Pictures"; break;
    case Known::music: key = "MUSIC"; fallback = L"Music"; break;
    case Known::videos: key = "VIDEOS"; fallback = L"Videos"; break;
    }
    const auto user = home();
    if (user.empty()) return {};
    // user-dirs.dirs (xdg-user-dirs): XDG_DOWNLOAD_DIR="$HOME/Pobrane"; one set to $HOME is disabled.
    const auto config = system_info::environment(L"XDG_CONFIG_HOME").value_or(L"");
    std::ifstream dirs((config.empty() || config.front() != L'/' ? user / L".config" : std::filesystem::path(config)) / L"user-dirs.dirs");
    const auto prefix = std::string("XDG_") + key + "_DIR=\"";
    std::filesystem::path result = user / fallback;
    for (std::string line; std::getline(dirs, line);) {
        if (!line.starts_with(prefix) || line.size() <= prefix.size() || line.back() != '"') continue;
        auto value = std::string_view(line).substr(prefix.size(), line.size() - prefix.size() - 1);
        if (value.starts_with("$HOME")) {
            value.remove_prefix(5);
            while (value.starts_with('/')) value.remove_prefix(1);
            if (value.empty()) return {};
            result = user / text_encoding::wideLenient(value);
        } else if (value.starts_with('/')) result = text_encoding::wideLenient(value);
    }
    return isFolder(result) ? result : std::filesystem::path{};
}

bool isFolder(const std::filesystem::path& path) {
    std::error_code error;
    return !path.empty() && std::filesystem::is_directory(path, error);
}
#endif
} // namespace file_listing
