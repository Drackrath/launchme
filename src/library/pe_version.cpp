#include "pe_version.hpp"

#include <algorithm>
#include <fstream>
#include <functional>

namespace pe_version {
namespace {
constexpr size_t max_resource = 1024 * 1024; // VS_VERSIONINFO is a few KiB.
using Reader = std::function<bool(uint64_t offset, size_t size, std::string& out)>;

uint16_t u16(std::string_view bytes, size_t at) {
    return at + 2 > bytes.size() ? 0 : static_cast<uint16_t>(static_cast<uint8_t>(bytes[at]) | static_cast<uint8_t>(bytes[at + 1]) << 8);
}
uint32_t u32(std::string_view bytes, size_t at) {
    return at + 4 > bytes.size() ? 0 : static_cast<uint32_t>(u16(bytes, at)) | static_cast<uint32_t>(u16(bytes, at + 2)) << 16;
}
// UTF-16LE up to a terminator or `end`; `at` moves past the terminator.
std::wstring utf16(std::string_view bytes, size_t& at, size_t end) {
    std::wstring out;
    end = std::min(end, bytes.size());
    while (at + 2 <= end) {
        uint32_t unit = u16(bytes, at);
        at += 2;
        if (!unit) break;
        if (unit >= 0xD800 && unit <= 0xDBFF && at + 2 <= end && u16(bytes, at) >= 0xDC00 && u16(bytes, at) <= 0xDFFF) {
            unit = 0x10000 + ((unit - 0xD800) << 10) + (u16(bytes, at) - 0xDC00u);
            at += 2;
        } else if (unit >= 0xD800 && unit <= 0xDFFF) unit = 0xFFFD;
        if constexpr (sizeof(wchar_t) == 2) {
            if (unit >= 0x10000) { out += static_cast<wchar_t>(0xD800 + ((unit - 0x10000) >> 10)); out += static_cast<wchar_t>(0xDC00 + ((unit - 0x10000) & 0x3FF)); }
            else out += static_cast<wchar_t>(unit);
        } else out += static_cast<wchar_t>(unit);
    }
    return out;
}
size_t align4(size_t at) { return (at + 3) & ~size_t{3}; }

// One VS_VERSIONINFO node: wLength, wValueLength, wType, szKey, padding, Value, padding, Children.
struct Node {
    size_t end = 0, value = 0, value_length = 0, children = 0;
    bool text = false;
    std::wstring key;
};
std::optional<Node> node(std::string_view bytes, size_t at, size_t limit) {
    const size_t length = u16(bytes, at);
    if (length < 6 || at + length > std::min(limit, bytes.size())) return std::nullopt;
    Node result;
    result.end = at + length;
    result.value_length = u16(bytes, at + 2);
    result.text = u16(bytes, at + 4) == 1;
    size_t cursor = at + 6;
    result.key = utf16(bytes, cursor, result.end);
    result.value = std::min(align4(cursor), result.end);
    // Text lengths count UTF-16 units (some tools write bytes; the terminator bounds both).
    const size_t value_bytes = result.text ? result.value_length * 2 : result.value_length;
    result.children = std::min(align4(result.value + value_bytes), result.end);
    return result;
}
template<class Visit> void children(std::string_view bytes, const Node& parent, Visit&& visit) {
    for (size_t at = parent.children; at + 6 <= parent.end;) {
        const auto child = node(bytes, at, parent.end);
        if (!child) break;
        visit(*child);
        at = align4(child->end);
    }
}

std::optional<Info> parseVersion(std::string_view bytes) {
    const auto root = node(bytes, 0, bytes.size());
    if (!root || root->key != L"VS_VERSION_INFO") return std::nullopt;
    Info info;
    children(bytes, *root, [&](const Node& section) {
        if (section.key == L"StringFileInfo") {
            children(bytes, section, [&](const Node& block) {
                Table table{block.key, {}};
                children(bytes, block, [&](const Node& item) {
                    size_t at = item.value;
                    auto value = item.value_length ? utf16(bytes, at, item.end) : std::wstring{};
                    if (!item.key.empty()) table.values.emplace_back(item.key, std::move(value));
                });
                info.tables.push_back(std::move(table));
            });
        } else if (section.key == L"VarFileInfo") {
            children(bytes, section, [&](const Node& item) {
                if (item.key != L"Translation") return;
                for (size_t at = item.value; at + 4 <= std::min(item.end, item.value + item.value_length); at += 4)
                    info.translations.emplace_back(u16(bytes, at), u16(bytes, at + 2));
            });
        }
    });
    return info;
}

std::optional<Info> readImage(const Reader& read) {
    std::string head;
    if (!read(0, 4096, head) || head.size() < 0x40 || head[0] != 'M' || head[1] != 'Z') return std::nullopt;
    const size_t pe = u32(head, 0x3C);
    if (pe + 24 > head.size() || head.compare(pe, 4, std::string_view("PE\0\0", 4)) != 0) return std::nullopt;
    const size_t sections = u16(head, pe + 6), optional_size = u16(head, pe + 20), optional = pe + 24;
    const uint16_t magic = u16(head, optional);
    const size_t directories = magic == 0x20B ? optional + 112 : magic == 0x10B ? optional + 96 : 0;
    const size_t directory_count = u32(head, magic == 0x20B ? optional + 108 : optional + 92);
    if (!directories || directory_count < 3 || directories + 24 > head.size() || sections > 96) return std::nullopt;
    const uint32_t resources = u32(head, directories + 16), resources_size = u32(head, directories + 20);
    if (!resources || !resources_size) return std::nullopt;
    const size_t table = optional + optional_size;
    if (table + sections * 40 > head.size()) return std::nullopt;
    const auto offset = [&](uint32_t rva) -> std::optional<uint64_t> {
        for (size_t i = 0; i < sections; ++i) {
            const size_t at = table + i * 40;
            const uint32_t address = u32(head, at + 12), size = std::max(u32(head, at + 8), u32(head, at + 16)), raw = u32(head, at + 20);
            if (rva >= address && rva - address < size) return uint64_t{raw} + (rva - address);
        }
        return std::nullopt;
    };
    const auto base = offset(resources);
    if (!base) return std::nullopt;
    // Resource directories: 16-byte header (named, then numbered entries), 8-byte entries; offsets from the tree's start.
    const auto directory = [&](uint32_t at, std::string& bytes) {
        return at < resources_size && read(*base + at, 16, bytes) && bytes.size() == 16;
    };
    const auto entry = [&](uint32_t at, std::optional<uint16_t> id) -> std::optional<uint32_t> {
        std::string header, entries;
        if (!directory(at, header)) return std::nullopt;
        const size_t count = static_cast<size_t>(u16(header, 12)) + u16(header, 14);
        if (!count || count > 4096 || !read(*base + at + 16, count * 8, entries) || entries.size() != count * 8) return std::nullopt;
        for (size_t i = 0; i < count; ++i) {
            const uint32_t name = u32(entries, i * 8);
            if (id && ((name & 0x80000000u) || name != *id)) continue;
            return u32(entries, i * 8 + 4);
        }
        return std::nullopt;
    };
    // Type RT_VERSION (16) -> its first name -> its first language -> the data entry.
    const auto type = entry(0, 16);
    if (!type || !(*type & 0x80000000u)) return std::nullopt;
    const auto name = entry(*type & 0x7FFFFFFFu, std::nullopt);
    if (!name || !(*name & 0x80000000u)) return std::nullopt;
    const auto language = entry(*name & 0x7FFFFFFFu, std::nullopt);
    if (!language || (*language & 0x80000000u)) return std::nullopt;
    std::string data;
    if (!read(*base + *language, 8, data) || data.size() != 8) return std::nullopt;
    const uint32_t size = u32(data, 4);
    const auto start = offset(u32(data, 0));
    if (!start || !size || size > max_resource) return std::nullopt;
    std::string version;
    if (!read(*start, size, version) || version.size() != size) return std::nullopt;
    return parseVersion(version);
}

bool sameBlock(std::wstring_view a, std::wstring_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const auto lower = [](wchar_t ch) { return ch >= L'A' && ch <= L'Z' ? static_cast<wchar_t>(ch - L'A' + L'a') : ch; };
        if (lower(a[i]) != lower(b[i])) return false;
    }
    return true;
}
} // namespace

const std::wstring* Info::find(std::wstring_view block, std::wstring_view key) const {
    for (const auto& item : tables) {
        if (!sameBlock(item.block, block)) continue;
        for (const auto& [name, value] : item.values) if (name == key) return &value;
    }
    return nullptr;
}

std::optional<Info> read(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    if (!input) return std::nullopt;
    return readImage([&](uint64_t offset, size_t size, std::string& out) {
        out.assign(size, '\0');
        input.clear();
        if (!input.seekg(static_cast<std::streamoff>(offset))) return false;
        input.read(out.data(), static_cast<std::streamsize>(size));
        out.resize(static_cast<size_t>(input.gcount()));
        return true;
    });
}

std::optional<Info> parse(std::string_view image) {
    return readImage([&](uint64_t offset, size_t size, std::string& out) {
        if (offset >= image.size()) return false;
        out.assign(image.substr(static_cast<size_t>(offset), size));
        return true;
    });
}
}
