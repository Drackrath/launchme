#include "package.hpp"
#include "../platform/atomic_file.hpp"
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz.h>
#include <algorithm>
#include <fstream>
#include <set>

namespace plugins {
std::string readFile(const std::filesystem::path& file, size_t limit) {
    std::ifstream in(file, std::ios::binary | std::ios::ate);
    const auto size = in ? static_cast<int64_t>(in.tellg()) : -1;
    if (size < 0 || static_cast<uint64_t>(size) > limit) throw std::runtime_error("Cannot read file or size limit exceeded");
    std::string bytes(static_cast<size_t>(size), '\0'); in.seekg(0);
    if (size && !in.read(bytes.data(), size)) throw std::runtime_error("Cannot read file");
    return bytes;
}
void validateDll(const std::filesystem::path& dll) {
    const auto bytes = readFile(dll, 32 * 1024 * 1024);
    IMAGE_DOS_HEADER dos{};
    if (bytes.size() < sizeof(dos)) throw std::runtime_error("Invalid DLL");
    memcpy(&dos, bytes.data(), sizeof(dos));
    if (bytes.size() < sizeof(IMAGE_NT_HEADERS64) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0 || static_cast<size_t>(dos.e_lfanew) > bytes.size() - sizeof(IMAGE_NT_HEADERS64))
        throw std::runtime_error("Invalid PE header");
    IMAGE_NT_HEADERS64 pe{}; memcpy(&pe, bytes.data() + dos.e_lfanew, sizeof(pe));
    if (pe.Signature != IMAGE_NT_SIGNATURE || pe.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        !(pe.FileHeader.Characteristics & IMAGE_FILE_DLL) || pe.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        throw std::runtime_error("Plugin must be an x64 DLL");
}
Manifest inspectPackage(const std::filesystem::path& file) {
    const auto bytes = readFile(file, 64 * 1024 * 1024);
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0)) throw std::runtime_error("Invalid ZIP archive");
    struct Close { mz_zip_archive* zip; ~Close() { mz_zip_reader_end(zip); } } close{&zip};
    const auto index = mz_zip_reader_locate_file(&zip, "plugin.json", nullptr, MZ_ZIP_FLAG_CASE_SENSITIVE);
    mz_zip_archive_file_stat stat{};
    if (index < 0 || !mz_zip_reader_file_stat(&zip, index, &stat) || stat.m_uncomp_size > 64 * 1024) throw std::runtime_error("Invalid bundled manifest");
    std::string manifest(static_cast<size_t>(stat.m_uncomp_size), '\0');
    if (!mz_zip_reader_extract_to_mem(&zip, index, manifest.data(), manifest.size(), 0)) throw std::runtime_error("Invalid bundled manifest CRC");
    return parseManifest(manifest);
}
Manifest extractPackage(const std::filesystem::path& file, const std::filesystem::path& staging) {
    const auto bytes = readFile(file, 64 * 1024 * 1024);
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0)) throw std::runtime_error("Invalid ZIP archive");
    struct Close { mz_zip_archive* zip; ~Close() { mz_zip_reader_end(zip); } } close{&zip};
    const auto count = mz_zip_reader_get_num_files(&zip);
    if (!count || count > 256) throw std::runtime_error("ZIP must contain 1-256 entries");
    uint64_t total = 0;
    std::set<std::string> paths;
    // Validate the whole directory before creating any files.
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&zip, i, &stat) || stat.m_is_encrypted || !stat.m_is_supported)
            throw std::runtime_error("Unsupported or encrypted ZIP entry");
        std::string name = stat.m_filename;
        if (stat.m_is_directory && name.ends_with('/')) name.pop_back();
        // Unix symlinks, special files and Windows reparse attributes are never extracted.
        const auto type = (stat.m_external_attr >> 16) & 0170000;
        if ((type && type != 0100000 && type != 0040000) || (stat.m_external_attr & FILE_ATTRIBUTE_REPARSE_POINT) || !packagePath(name))
            throw std::runtime_error("Unsafe ZIP path or file type");
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
        if (!paths.insert(name).second) throw std::runtime_error("Duplicate ZIP path");
        if (stat.m_uncomp_size > 32 * 1024 * 1024 || (total += stat.m_uncomp_size) > 64 * 1024 * 1024)
            throw std::runtime_error("Uncompressed ZIP exceeds size limit");
    }
    if (!std::filesystem::create_directory(staging)) throw std::runtime_error("Staging directory already exists");
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat{}; mz_zip_reader_file_stat(&zip, i, &stat);
        const auto target = staging / stat.m_filename;
        if (stat.m_is_directory) { std::filesystem::create_directories(target); continue; }
        std::string data(static_cast<size_t>(stat.m_uncomp_size), '\0');
        if (!mz_zip_reader_extract_to_mem(&zip, i, data.data(), data.size(), 0) || !atomic_file::write(target, data))
            throw std::runtime_error("ZIP extraction or CRC verification failed");
    }
    auto manifest = parseManifest(readFile(staging / L"plugin.json", 64 * 1024));
    validateDll(staging / manifest.entry);
    return manifest;
}
}
