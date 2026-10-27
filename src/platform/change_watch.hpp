#pragma once
// Kernel change notifications for workers that must not poll the disk or the registry:
// non-recursive directory watches (FindFirstChangeNotificationW) and registry key watches
// (RegNotifyChangeKeyValue). A worker blocks in wait() and rescans only after something
// it reads has really changed; a fired source is re-armed before wait() returns.
#ifdef _WIN32
#include <Windows.h>
#endif

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

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
