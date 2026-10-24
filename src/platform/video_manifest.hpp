#pragma once

#include "atomic_file.hpp"
#include "text_encoding.hpp"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

// <video folder>\launchme-videos.txt: the videos Launchme downloaded into a folder it may share
// with ES-DE (downloaded_media), one relative path per line. Moving the folder (Biblioteka →
// Folder filmów) moves only these, never ES-DE's own.
namespace video_manifest {
inline constexpr const wchar_t* file_name = L"launchme-videos.txt";

inline std::mutex& guard() { static std::mutex mutex; return mutex; }

inline std::vector<std::filesystem::path> read(const std::filesystem::path& folder) {
    std::vector<std::filesystem::path> entries;
    std::ifstream input(folder / file_name, std::ios::binary);
    std::string line;
    while (std::getline(input, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        const std::filesystem::path entry(text_encoding::wideLenient(line));
        // Only plain relative paths inside the folder.
        if (line.empty() || entry.is_absolute() || entry.has_root_name() || entry.lexically_normal().native().starts_with(L"..")) continue;
        entries.push_back(entry.lexically_normal());
    }
    return entries;
}

inline bool write(const std::filesystem::path& folder, const std::vector<std::filesystem::path>& entries) {
    std::string text;
    for (const auto& entry : entries) text += text_encoding::utf8Lenient(entry.native()) + "\n";
    if (entries.empty()) { std::error_code ec; std::filesystem::remove(folder / file_name, ec); return true; }
    return atomic_file::write(folder / file_name, text);
}

// After a download: file (inside folder) is Launchme's.
inline void add(const std::filesystem::path& folder, const std::filesystem::path& file) {
    const auto relative = file.lexically_normal().lexically_relative(folder.lexically_normal());
    if (relative.empty() || relative.native().starts_with(L"..")) return;
    std::lock_guard lock(guard());
    auto entries = read(folder);
    for (const auto& entry : entries) if (_wcsicmp(entry.c_str(), relative.c_str()) == 0) return;
    entries.push_back(relative);
    write(folder, entries);
}
}
