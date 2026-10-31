#include "texture_cache.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <functional>
#include <string>
#include <thread>

namespace {
constexpr uint32_t file_magic = 0x58544D4C; // "LMTX"
// Raised whenever the blocks for the same input would differ (block_compression.cpp's encoder).
constexpr uint32_t file_version = 1;
struct Header {
    uint32_t magic = file_magic, version = file_version;
    uint64_t key = 0;
    uint32_t width = 0, height = 0;
    uint32_t format = 0; // block_compression::Format
    uint32_t ink_valid = 0;
    uint32_t ink[4]{};
    float accent[3]{};
    uint32_t reserved = 0;
    uint64_t bytes = 0;
};
static_assert(sizeof(Header) == 72, "The file header's layout is part of the format");
}

TextureCache::TextureCache(std::filesystem::path folder, uint64_t budget_bytes) : folder_(std::move(folder)), budget_(budget_bytes) {}

uint64_t TextureCache::key(const std::vector<uint8_t>& bytes, const DecodeKind& kind) {
    uint64_t hash = 14695981039346656037ull;
    const auto mix = [&](uint64_t value) {
        for (int i = 0; i < 8; ++i) hash = (hash ^ ((value >> (8 * i)) & 0xFF)) * 1099511628211ull;
    };
    mix(imageHash(bytes)); mix(bytes.size()); mix(file_version);
    mix(kind.accent); mix(kind.cover_fit); mix(kind.ink); mix(kind.short_edge); mix(kind.loose_edge); mix(kind.block_alpha);
    mix(kind.cover_fit ? kind.cover_width : 0); mix(kind.cover_fit ? kind.cover_height : 0);
    return hash;
}

std::filesystem::path TextureCache::file(uint64_t key) const {
    char name[17]{};
    std::snprintf(name, sizeof(name), "%016llx", static_cast<unsigned long long>(key));
    return folder_ / std::string(name, 2) / (std::string(name) + ".tex");
}

bool TextureCache::load(uint64_t key, DecodedImage& image) const {
    const auto path = file(key);
    {
        std::ifstream input(path, std::ios::binary);
        if (!input) return false;
        Header header;
        if (!input.read(reinterpret_cast<char*>(&header), sizeof(header)) || header.magic != file_magic ||
            header.version != file_version || header.key != key) return false;
        const auto format = static_cast<block_compression::Format>(header.format);
        if (!header.width || !header.height || header.width > 16384 || header.height > 16384 || !header.bytes ||
            header.bytes != block_compression::blockBytes(format, header.width, header.height)) return false;
        std::vector<uint8_t> blocks(static_cast<size_t>(header.bytes));
        if (!input.read(reinterpret_cast<char*>(blocks.data()), static_cast<std::streamsize>(blocks.size()))) return false;
        image.width = header.width; image.height = header.height;
        image.pixels.clear();
        image.blocks = std::move(blocks);
        image.block_format = format;
        std::copy(std::begin(header.accent), std::end(header.accent), image.accent);
        image.ink = {header.ink[0], header.ink[1], header.ink[2], header.ink[3], header.ink_valid != 0};
    }
    std::error_code touched; // Recently used: the last to go past the budget.
    std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now(), touched);
    return true;
}

void TextureCache::store(uint64_t key, const DecodedImage& image) {
    if (image.blocks.empty() || image.block_format == block_compression::Format::none || !image.valid()) return;
    const auto path = file(key);
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    auto temporary = path;
    temporary += "." + std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())) + ".tmp";
    bool written = false;
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        Header header;
        header.key = key;
        header.width = image.width; header.height = image.height;
        header.format = static_cast<uint32_t>(image.block_format);
        header.ink_valid = image.ink.valid ? 1 : 0;
        header.ink[0] = image.ink.left; header.ink[1] = image.ink.top; header.ink[2] = image.ink.right; header.ink[3] = image.ink.bottom;
        std::copy(std::begin(image.accent), std::end(image.accent), header.accent);
        header.bytes = image.blocks.size();
        written = output && output.write(reinterpret_cast<const char*>(&header), sizeof(header)) &&
                  output.write(reinterpret_cast<const char*>(image.blocks.data()), static_cast<std::streamsize>(image.blocks.size()));
    }
    if (written) std::filesystem::rename(temporary, path, ec);
    if (!written || ec) { std::error_code ignored; std::filesystem::remove(temporary, ignored); return; }
    if (++writes_ % 64 == 0) trim();
}

void TextureCache::trim() {
    std::unique_lock lock(trim_mutex_, std::try_to_lock);
    if (!lock) return; // Another worker is at it.
    struct Entry { std::filesystem::file_time_type time; uintmax_t size; std::filesystem::path path; };
    std::vector<Entry> entries;
    uint64_t total = 0;
    const auto now = std::filesystem::file_time_type::clock::now();
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(folder_, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code entry;
        if (!it->is_regular_file(entry)) continue;
        const auto time = it->last_write_time(entry);
        if (entry) continue;
        const auto& path = it->path();
        if (path.extension() == ".tmp") { // A write cut short (the launcher closed meanwhile).
            if (now - time > std::chrono::minutes(10)) std::filesystem::remove(path, entry);
            continue;
        }
        if (path.extension() != ".tex") continue;
        const auto size = it->file_size(entry);
        if (entry) continue;
        total += size;
        entries.push_back({time, size, path});
    }
    if (total <= budget_) return;
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.time < b.time; });
    const uint64_t target = budget_ / 10 * 9;
    for (const auto& entry : entries) {
        if (total <= target) break;
        std::error_code removed;
        if (std::filesystem::remove(entry.path, removed)) total -= entry.size;
    }
}
