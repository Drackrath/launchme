#pragma once
// The version resource (VS_VERSIONINFO) of a Windows executable, read from the file's bytes:
// what GetFileVersionInfoW and VerQueryValueW give, for systems without them (Linux, where
// Windows games run through Wine or Proton). Only the headers, the resource tree and that one
// resource are read, never the whole file.
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pe_version {
struct Table {
    std::wstring block; // "040904b0": language and code page, as written in the file.
    std::vector<std::pair<std::wstring, std::wstring>> values; // "ProductName" -> "Celeste".
};
struct Info {
    std::vector<std::pair<uint16_t, uint16_t>> translations; // \VarFileInfo\Translation.
    std::vector<Table> tables;                               // \StringFileInfo\<block>.
    // VerQueryValueW(L"\\StringFileInfo\\<block>\\<key>"): the block ignores case, the key does not.
    const std::wstring* find(std::wstring_view block, std::wstring_view key) const;
};
// nullopt: not a PE image, no version resource, or a damaged one.
std::optional<Info> read(const std::filesystem::path& file);
// The same from an image already in memory (tests).
std::optional<Info> parse(std::string_view image);
}
