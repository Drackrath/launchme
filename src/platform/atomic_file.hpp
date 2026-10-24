#pragma once
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <string>
#include <filesystem>
#include <fstream>
#include <span>
#include <string_view>

namespace atomic_file {
// Same-directory temporary files keep replacement atomic. Unique names allow
// independent cache workers; a failed write/rename leaves the previous file intact.
inline bool write(const std::filesystem::path& file, std::span<const uint8_t> bytes, bool replace = true) {
    static std::atomic_uint64_t serial{0};
    std::error_code error;
    if (!file.parent_path().empty()) std::filesystem::create_directories(file.parent_path(), error);
    if (error) return false;
    const auto temporary = file.wstring() + L".tmp." + std::to_wstring(GetCurrentProcessId()) + L'.' + std::to_wstring(serial.fetch_add(1, std::memory_order_relaxed));
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (output) output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    output.flush(); const bool complete = output.good(); output.close();
    if (complete && !output.fail() && MoveFileExW(temporary.c_str(), file.c_str(), (replace ? MOVEFILE_REPLACE_EXISTING : 0) | MOVEFILE_WRITE_THROUGH)) return true;
    std::filesystem::remove(temporary, error);
    return false;
}
inline bool write(const std::filesystem::path& file, std::string_view text) {
    return write(file, std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
}
// For the user's own state (settings, library state): the new contents reach the disk before
// the rename, so a power cut leaves either the old file or the new one, never an empty file.
inline bool writeDurable(const std::filesystem::path& file, std::string_view text) {
    static std::atomic_uint64_t serial{0};
    std::error_code error;
    if (!file.parent_path().empty()) std::filesystem::create_directories(file.parent_path(), error);
    if (error) return false;
    const auto temporary = file.wstring() + L".tmp." + std::to_wstring(GetCurrentProcessId()) + L".d" + std::to_wstring(serial.fetch_add(1, std::memory_order_relaxed));
    const HANDLE handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    bool complete = true;
    for (size_t offset = 0; complete && offset < text.size();) {
        DWORD written = 0;
        const auto chunk = static_cast<DWORD>(std::min<size_t>(text.size() - offset, 1u << 20));
        complete = WriteFile(handle, text.data() + offset, chunk, &written, nullptr) && written == chunk;
        offset += written;
    }
    complete = complete && FlushFileBuffers(handle);
    CloseHandle(handle);
    if (complete && MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
    std::filesystem::remove(temporary, error);
    return false;
}
// Temporary files a crashed or killed process left next to `folder`'s files (*.tmp.<pid>.<n>).
// Only files of processes no longer running are removed.
inline void removeStaleTemporaries(const std::filesystem::path& folder) {
    std::error_code error;
    for (std::filesystem::directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
        const auto name = it->path().filename().wstring();
        const auto at = name.find(L".tmp.");
        if (at == std::wstring::npos || !it->is_regular_file(error)) continue;
        const auto pid_text = name.substr(at + 5, name.find(L'.', at + 5) - (at + 5));
        DWORD pid = 0;
        try { pid = static_cast<DWORD>(std::stoul(pid_text)); } catch (...) { continue; }
        if (pid == GetCurrentProcessId()) continue;
        if (const HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid)) {
            const bool running = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
            CloseHandle(process);
            if (running) continue;
        }
        std::filesystem::remove(it->path(), error);
        error.clear();
    }
}
}
