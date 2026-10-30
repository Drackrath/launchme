#pragma once
// Whole small files (settings, caches, manifests, pictures) and quiet path checks. A read returns
// nothing for a missing, unreadable, empty or oversized file: callers never parse a truncated one.
// Writes go through atomic_file.hpp.
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace file_io {
namespace detail {
template <class Bytes> Bytes read(const std::filesystem::path& file, size_t limit) {
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input) return {};
    const auto size = input.tellg();
    if (size <= 0 || static_cast<uint64_t>(size) > limit) return {};
    Bytes bytes(static_cast<size_t>(size), {});
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) return {};
    return bytes;
}
}
// The file's bytes as text (no decoding), "" when it cannot be read whole within `limit` bytes.
inline std::string readText(const std::filesystem::path& file, size_t limit) { return detail::read<std::string>(file, limit); }
// The same as raw bytes.
inline std::vector<uint8_t> readBytes(const std::filesystem::path& file, size_t limit) { return detail::read<std::vector<uint8_t>>(file, limit); }
// The text without a UTF-8 byte order mark (files edited in Notepad).
inline std::string readTextWithoutBom(const std::filesystem::path& file, size_t limit) {
    auto text = readText(file, limit);
    if (text.starts_with("\xef\xbb\xbf")) text.erase(0, 3);
    return text;
}

// Never throw, never report: an unreadable path is neither.
inline bool isFile(const std::filesystem::path& path) { std::error_code ec; return std::filesystem::is_regular_file(path, ec); }
inline bool isFolder(const std::filesystem::path& path) { std::error_code ec; return std::filesystem::is_directory(path, ec); }
}
