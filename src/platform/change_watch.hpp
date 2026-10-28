#pragma once
// Kernel change notifications for workers that must not poll the disk or the registry:
// non-recursive directory watches (FindFirstChangeNotificationW) and registry key watches
// (RegNotifyChangeKeyValue). A worker blocks in wait() and rescans only after something
// it reads has really changed; a fired source is re-armed before wait() returns.
// Linux: inotify for folders (a subtree watches each subfolder too) and an eventfd for wakes;
// there is no registry.
#ifdef _WIN32
#include <Windows.h>
#else
#include <fnmatch.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <map>
#endif

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
// A request a sleeping worker must see at once: the flag says it was asked, the event wakes it
// (also used to wake it for shutdown).
struct WakeSignal {
    std::atomic_bool requested{false};
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    WakeSignal() = default;
    WakeSignal(const WakeSignal&) = delete;
    WakeSignal& operator=(const WakeSignal&) = delete;
    ~WakeSignal() { if (event) CloseHandle(event); }
    void request() { requested.store(true); wake(); }
    void wake() const { if (event) SetEvent(event); }
    bool load() const { return requested.load(); }
    bool take() { return requested.exchange(false); }
};

class ChangeWatch {
public:
    static constexpr DWORD file_changes = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                          FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE;
    ChangeWatch() = default;
    ChangeWatch(const ChangeWatch&) = delete;
    ChangeWatch& operator=(const ChangeWatch&) = delete;
    ~ChangeWatch() { clear(); }

    // A folder that does not exist (yet) is not watched; callers rebuild the set when the
    // things they read move (another library folder, another account).
    bool directory(const std::filesystem::path& folder, bool subtree = false, DWORD filter = file_changes) {
        if (folder.empty() || sources_.size() >= MAXIMUM_WAIT_OBJECTS - 1) return false;
        const HANDLE handle = FindFirstChangeNotificationW(folder.c_str(), subtree, filter);
        if (handle == INVALID_HANDLE_VALUE) return false;
        sources_.push_back({handle, nullptr, false});
        return true;
    }
    bool registry(HKEY root, const wchar_t* path, REGSAM view = 0, bool subtree = true) {
        if (sources_.size() >= MAXIMUM_WAIT_OBJECTS - 1) return false;
        HKEY key = nullptr;
        if (RegOpenKeyExW(root, path, 0, KEY_NOTIFY | view, &key) != ERROR_SUCCESS) return false;
        // Manual reset, like a directory watch: it stays signalled until re-armed.
        const HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!event) { RegCloseKey(key); return false; }
        Source source{event, key, subtree};
        if (!armKey(source)) { CloseHandle(event); RegCloseKey(key); return false; }
        sources_.push_back(source);
        return true;
    }
    // An extra event (shutdown, a requested rescan); it is not owned or re-armed here.
    void wake(HANDLE event) { wake_ = event; }
    void clear() {
        for (auto& source : sources_) {
            if (source.key) { RegCloseKey(source.key); CloseHandle(source.handle); }
            else FindCloseChangeNotification(source.handle);
        }
        sources_.clear();
    }
    bool empty() const { return sources_.empty(); }
    size_t size() const { return sources_.size(); }

    enum class Result { timeout, changed, woken };
    // Blocks up to `timeout_ms`. All sources that fired meanwhile are re-armed; a directory
    // whose watch broke (drive removed, folder deleted) is dropped and reported as a change.
    Result wait(DWORD timeout_ms) {
        std::vector<HANDLE> handles;
        handles.reserve(sources_.size() + 1);
        for (const auto& source : sources_) handles.push_back(source.handle);
        if (wake_) handles.push_back(wake_);
        if (handles.empty()) { Sleep(timeout_ms); return Result::timeout; }
        const DWORD result = WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), FALSE, timeout_ms);
        if (result == WAIT_TIMEOUT) return Result::timeout;
        if (result == WAIT_FAILED) { Sleep(timeout_ms); return Result::timeout; }
        const DWORD index = result - WAIT_OBJECT_0;
        if (wake_ && index == sources_.size()) {
            rearmSignalled();
            return Result::woken;
        }
        rearmSignalled();
        return Result::changed;
    }
    // After a change: wait until the sources stay quiet for `quiet_ms` (installers and clients
    // write many files and keys in a burst), at most `limit_ms`. False when woken meanwhile.
    bool settle(DWORD quiet_ms, DWORD limit_ms) {
        const auto start = GetTickCount64();
        while (GetTickCount64() - start < limit_ms) {
            const auto result = wait(quiet_ms);
            if (result == Result::timeout) return true;
            if (result == Result::woken) return false;
        }
        return true;
    }

private:
    struct Source { HANDLE handle = nullptr; HKEY key = nullptr; bool subtree = false; };
    std::vector<Source> sources_;
    HANDLE wake_ = nullptr;

    static bool armKey(const Source& source) {
        ResetEvent(source.handle);
        return RegNotifyChangeKeyValue(source.key, source.subtree,
            REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET | REG_NOTIFY_THREAD_AGNOSTIC, source.handle, TRUE) == ERROR_SUCCESS;
    }
    void rearmSignalled() {
        for (size_t i = 0; i < sources_.size();) {
            auto& source = sources_[i];
            if (WaitForSingleObject(source.handle, 0) != WAIT_OBJECT_0) { ++i; continue; }
            const bool rearmed = source.key ? armKey(source) : FindNextChangeNotification(source.handle) != FALSE;
            if (rearmed) { ++i; continue; }
            if (source.key) { RegCloseKey(source.key); CloseHandle(source.handle); }
            else FindCloseChangeNotification(source.handle);
            sources_.erase(sources_.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }
};

// Cheap identity of a file's contents for deciding whether a notification matters:
// size and last write time, 0 when the file is absent.
inline uint64_t fileStamp(const std::filesystem::path& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return 0;
    uint64_t hash = 1469598103934665603ULL;
    for (const uint64_t part : {static_cast<uint64_t>(data.nFileSizeHigh) << 32 | data.nFileSizeLow,
                                static_cast<uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32 | data.ftLastWriteTime.dwLowDateTime})
        hash = (hash ^ part) * 1099511628211ULL;
    return hash ? hash : 1;
}

// Names, sizes and write times of the files matching `pattern` in `folder`, in one directory
// listing (no file is opened). Missing folder: a distinct value, so a vanished drive differs.
inline uint64_t folderStamp(const std::filesystem::path& folder, const wchar_t* pattern) {
    uint64_t hash = 1469598103934665603ULL;
    const auto mix = [&](uint64_t value) { hash = (hash ^ value) * 1099511628211ULL; };
    WIN32_FIND_DATAW data{};
    const HANDLE find = FindFirstFileExW((folder / pattern).c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (find == INVALID_HANDLE_VALUE) { mix(GetLastError() == ERROR_FILE_NOT_FOUND ? 2 : 3); return hash; }
    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        for (const wchar_t* c = data.cFileName; *c; ++c) mix(static_cast<uint64_t>(*c));
        mix(static_cast<uint64_t>(data.nFileSizeHigh) << 32 | data.nFileSizeLow);
        mix(static_cast<uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32 | data.ftLastWriteTime.dwLowDateTime);
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return hash;
}
#else
// A request a sleeping worker must see at once: the flag says it was asked, the eventfd wakes it
// (also used to wake it for shutdown). Reading it resets it, like an auto-reset event.
struct WakeSignal {
    std::atomic_bool requested{false};
    int event = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    WakeSignal() = default;
    WakeSignal(const WakeSignal&) = delete;
    WakeSignal& operator=(const WakeSignal&) = delete;
    ~WakeSignal() { if (event >= 0) ::close(event); }
    void request() { requested.store(true); wake(); }
    void wake() const { if (event >= 0) { const uint64_t one = 1; [[maybe_unused]] const auto written = ::write(event, &one, sizeof(one)); } }
    bool load() const { return requested.load(); }
    bool take() { return requested.exchange(false); }
};

class ChangeWatch {
public:
    // Names created, removed or renamed, contents written; a watched folder that goes away always counts.
    static constexpr uint32_t file_changes = IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_MODIFY | IN_CLOSE_WRITE;
    ChangeWatch() = default;
    ChangeWatch(const ChangeWatch&) = delete;
    ChangeWatch& operator=(const ChangeWatch&) = delete;
    ~ChangeWatch() { clear(); }

    // A folder that does not exist (yet) is not watched; callers rebuild the set when the
    // things they read move (another library folder, another account).
    bool directory(const std::filesystem::path& folder, bool subtree = false, uint32_t filter = file_changes) {
        if (folder.empty() || size() >= max_sources) return false;
        if (inotify_ < 0) inotify_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (inotify_ < 0) return false;
        const int id = add(folder, filter | (subtree ? IN_CREATE | IN_MOVED_TO : 0u));
        if (id < 0) return false;
        watches_[id] = {folder, filter, subtree, true};
        if (subtree) addBelow(folder, filter);
        return true;
    }
    // An extra eventfd (shutdown, a requested rescan); it is not owned here.
    void wake(int event) { wake_ = event; }
    void clear() {
        if (inotify_ >= 0) ::close(inotify_); // Removes every watch with it.
        inotify_ = -1;
        watches_.clear();
    }
    bool empty() const { return size() == 0; }
    size_t size() const {
        size_t count = 0;
        for (const auto& [id, watch] : watches_) count += watch.root;
        return count;
    }

    enum class Result { timeout, changed, woken };
    // Blocks up to `timeout_ms`. All changes meanwhile are taken; a folder whose watch broke
    // (drive removed, folder deleted) is dropped and reported as a change.
    Result wait(uint32_t timeout_ms) {
        pollfd items[2]{};
        nfds_t count = 0;
        if (inotify_ >= 0 && !watches_.empty()) items[count++] = {inotify_, POLLIN, 0};
        if (wake_ >= 0) items[count++] = {wake_, POLLIN, 0};
        if (!count) { Sleep(timeout_ms); return Result::timeout; }
        const int ready = poll(items, count, static_cast<int>(std::min<uint32_t>(timeout_ms, INT32_MAX)));
        if (ready == 0) return Result::timeout;
        if (ready < 0) { Sleep(timeout_ms); return Result::timeout; }
        // Sources first, like WaitForMultipleObjects: a pending wake stays for the next wait.
        if (items[0].fd == inotify_ && (items[0].revents & POLLIN)) { drain(); return Result::changed; }
        uint64_t value = 0;
        [[maybe_unused]] const auto read = ::read(wake_, &value, sizeof(value));
        return Result::woken;
    }
    // After a change: wait until the sources stay quiet for `quiet_ms` (installers and clients
    // write many files in a burst), at most `limit_ms`. False when woken meanwhile.
    bool settle(uint32_t quiet_ms, uint32_t limit_ms) {
        const auto start = GetTickCount64();
        while (GetTickCount64() - start < limit_ms) {
            const auto result = wait(quiet_ms);
            if (result == Result::timeout) return true;
            if (result == Result::woken) return false;
        }
        return true;
    }

private:
    static constexpr size_t max_sources = 63, max_subfolders = 4096; // As MAXIMUM_WAIT_OBJECTS; inotify's per-user limit.
    struct Watch { std::filesystem::path folder; uint32_t filter = 0; bool subtree = false, root = false; };
    int inotify_ = -1, wake_ = -1;
    std::map<int, Watch> watches_; // By watch descriptor.

    int add(const std::filesystem::path& folder, uint32_t mask) {
        return inotify_add_watch(inotify_, folder.c_str(), mask | IN_ONLYDIR | IN_DELETE_SELF | IN_MOVE_SELF);
    }
    void addBelow(const std::filesystem::path& folder, uint32_t filter) {
        std::error_code error;
        const auto options = std::filesystem::directory_options::skip_permission_denied;
        for (std::filesystem::recursive_directory_iterator it(folder, options, error), end; !error && it != end; it.increment(error)) {
            if (watches_.size() >= max_subfolders) return;
            std::error_code type_error;
            if (it->is_symlink(type_error)) { it.disable_recursion_pending(); continue; }
            if (!it->is_directory(type_error)) continue;
            if (const int id = add(it->path(), filter | IN_CREATE | IN_MOVED_TO); id >= 0 && !watches_.contains(id)) watches_[id] = {it->path(), filter, true, false};
        }
    }
    void drain() {
        alignas(inotify_event) char buffer[16 * 1024];
        while (true) {
            const auto length = ::read(inotify_, buffer, sizeof(buffer));
            if (length <= 0) return;
            for (ssize_t at = 0; at < length;) {
                const auto* event = reinterpret_cast<const inotify_event*>(buffer + at);
                at += static_cast<ssize_t>(sizeof(inotify_event) + event->len);
                const auto found = watches_.find(event->wd);
                if (found == watches_.end()) continue;
                if (event->mask & IN_IGNORED) { watches_.erase(found); continue; } // Deleted, moved away or unmounted.
                if ((event->mask & IN_ISDIR) && (event->mask & (IN_CREATE | IN_MOVED_TO)) && found->second.subtree && event->len) {
                    const auto child = found->second.folder / event->name;
                    const auto filter = found->second.filter;
                    if (watches_.size() < max_subfolders)
                        if (const int id = add(child, filter | IN_CREATE | IN_MOVED_TO); id >= 0 && !watches_.contains(id)) watches_[id] = {child, filter, true, false};
                    addBelow(child, filter);
                }
            }
        }
    }
};

// Cheap identity of a file's contents for deciding whether a notification matters:
// size and last write time, 0 when the file is absent.
inline uint64_t fileStamp(const std::filesystem::path& path) {
    struct stat info{};
    if (::stat(path.c_str(), &info) != 0) return 0;
    uint64_t hash = 1469598103934665603ULL;
    for (const uint64_t part : {static_cast<uint64_t>(info.st_size), static_cast<uint64_t>(info.st_mtim.tv_sec * 1'000'000'000LL + info.st_mtim.tv_nsec)})
        hash = (hash ^ part) * 1099511628211ULL;
    return hash ? hash : 1;
}

// Names, sizes and write times of the files matching `pattern` in `folder`, in one directory
// listing (no file is opened). Missing folder: a distinct value, so a vanished drive differs.
// Linux lists in no particular order: the names are sorted first.
inline uint64_t folderStamp(const std::filesystem::path& folder, const wchar_t* pattern) {
    uint64_t hash = 1469598103934665603ULL;
    const auto mix = [&](uint64_t value) { hash = (hash ^ value) * 1099511628211ULL; };
    std::error_code error;
    std::filesystem::directory_iterator it(folder, error);
    if (error) { mix(3); return hash; } // ERROR_PATH_NOT_FOUND: not the same as no match.
    const auto wanted = std::filesystem::path(pattern).string();
    std::vector<std::pair<std::string, std::pair<uint64_t, uint64_t>>> files;
    for (std::filesystem::directory_iterator end; !error && it != end; it.increment(error)) {
        const auto name = it->path().filename().string();
        struct stat info{};
        if (fnmatch(wanted.c_str(), name.c_str(), 0) != 0 || ::stat(it->path().c_str(), &info) != 0 || S_ISDIR(info.st_mode)) continue;
        files.push_back({name, {static_cast<uint64_t>(info.st_size), static_cast<uint64_t>(info.st_mtim.tv_sec * 1'000'000'000LL + info.st_mtim.tv_nsec)}});
    }
    std::sort(files.begin(), files.end());
    for (const auto& [name, stamp] : files) {
        for (const unsigned char c : name) mix(c);
        mix(stamp.first);
        mix(stamp.second);
    }
    if (files.empty()) mix(2); // Like FindFirstFileExW's ERROR_FILE_NOT_FOUND.
    return hash;
}
#endif
