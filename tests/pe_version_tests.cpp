// Version resources read from a PE file's bytes (src/library/pe_version.hpp): a built image with
// a VS_VERSIONINFO, case-insensitive blocks, characters outside the BMP and damaged images.
#include "../src/library/pe_version.hpp"

#include <Windows.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <vector>

namespace {
void put16(std::string& out, size_t at, uint32_t value) { out[at] = static_cast<char>(value & 0xFF); out[at + 1] = static_cast<char>(value >> 8 & 0xFF); }
void put32(std::string& out, size_t at, uint32_t value) { put16(out, at, value & 0xFFFF); put16(out, at + 2, value >> 16); }
void pad(std::string& out) { while (out.size() % 4) out += '\0'; }
std::string utf16z(std::u16string_view text) {
    std::string out;
    for (const char16_t unit : text) { out += static_cast<char>(unit & 0xFF); out += static_cast<char>(unit >> 8); }
    return out + std::string(2, '\0');
}
// wLength, wValueLength, wType, szKey, padding, Value, padding, Children.
std::string node(std::u16string_view key, const std::string& value, uint16_t value_length, bool text, const std::vector<std::string>& children = {}) {
    std::string out(6, '\0');
    out += utf16z(key);
    pad(out);
    out += value;
    for (const auto& child : children) { pad(out); out += child; }
    put16(out, 0, static_cast<uint32_t>(out.size()));
    put16(out, 2, value_length);
    put16(out, 4, text ? 1 : 0);
    return out;
}
std::string string(std::u16string_view key, std::u16string_view value) {
    return node(key, utf16z(value), static_cast<uint16_t>(value.size() + 1), true);
}
std::string image() {
    std::string fixed(52, '\0');
    put32(fixed, 0, 0xFEEF04BD);
    const auto table = node(u"040904B0", {}, 0, true, {string(u"ProductName", u"Celeste \U0001F353"), string(u"FileDescription", u"Celeste"), string(u"Empty", u"")});
    std::string translation(4, '\0');
    put16(translation, 0, 0x409); put16(translation, 2, 0x4B0);
    const auto version = node(u"VS_VERSION_INFO", fixed, 52, false,
        {node(u"StringFileInfo", {}, 0, true, {table}), node(u"VarFileInfo", {}, 0, true, {node(u"Translation", translation, 4, false)})});

    std::string out(0x1000, '\0');
    out[0] = 'M'; out[1] = 'Z'; put32(out, 0x3C, 0x80);
    out[0x80] = 'P'; out[0x81] = 'E';
    put16(out, 0x84, 0x8664); put16(out, 0x86, 1); put16(out, 0x80 + 20, 0xF0); put16(out, 0x80 + 22, 0x22);
    const size_t optional = 0x98;
    put16(out, optional, 0x20B); put32(out, optional + 108, 16);
    // The resource tree: RT_VERSION (16) -> name 1 -> language 0x409 -> data.
    std::string tree(0x58, '\0');
    put16(tree, 0x0E, 1); put32(tree, 0x10, 16); put32(tree, 0x14, 0x80000018);
    put16(tree, 0x18 + 0x0E, 1); put32(tree, 0x28, 1); put32(tree, 0x2C, 0x80000030);
    put16(tree, 0x30 + 0x0E, 1); put32(tree, 0x40, 0x409); put32(tree, 0x44, 0x48);
    put32(tree, 0x48, 0x1000 + 0x58); put32(tree, 0x4C, static_cast<uint32_t>(version.size()));
    tree += version;
    put32(out, optional + 112 + 16, 0x1000); put32(out, optional + 112 + 20, static_cast<uint32_t>(tree.size()));
    const size_t section = optional + 0xF0;
    std::copy_n(".rsrc", 5, out.begin() + static_cast<std::ptrdiff_t>(section));
    put32(out, section + 8, static_cast<uint32_t>(tree.size())); put32(out, section + 12, 0x1000);
    put32(out, section + 16, static_cast<uint32_t>(tree.size())); put32(out, section + 20, 0x1000);
    return out + tree;
}
} // namespace

int main() {
    const auto bytes = image();
    const auto info = pe_version::parse(bytes);
    if (!info || info->translations.size() != 1 || info->translations[0] != std::pair<uint16_t, uint16_t>{0x409, 0x4B0}) return 1;
    const auto* product = info->find(L"040904b0", L"ProductName");
    if (!product || *product != std::wstring(L"Celeste ") + (sizeof(wchar_t) == 2 ? L"\xD83C\xDF53" : L"\U0001F353")) return 2;
    if (!info->find(L"040904B0", L"FileDescription") || info->find(L"040904b0", L"productname") || info->find(L"041504b0", L"ProductName")) return 3;
    if (!info->find(L"040904b0", L"Empty") || !info->find(L"040904b0", L"Empty")->empty()) return 4;
    // Every truncation and a few damaged bytes: no crash, no version.
    for (size_t size = 0; size < bytes.size(); size += 7) if (pe_version::parse(std::string_view(bytes).substr(0, size))) return 5;
    for (size_t at = 0x1000; at < bytes.size(); ++at) {
        auto damaged = bytes;
        damaged[at] = static_cast<char>(0xFF);
        (void)pe_version::parse(damaged);
    }
    if (pe_version::parse("MZ") || pe_version::parse(std::string(4096, 'x'))) return 6;
    const auto file = std::filesystem::temp_directory_path() / (L"launchme-pe-version-" + std::to_wstring(GetCurrentProcessId()) + L".exe");
    std::ofstream(file, std::ios::binary) << bytes;
    const auto read = pe_version::read(file);
    std::error_code ignored;
    std::filesystem::remove(file, ignored);
    if (!read || !read->find(L"040904b0", L"FileDescription") || *read->find(L"040904b0", L"FileDescription") != L"Celeste") return 7;
    if (pe_version::read(file)) return 8;
    std::cout << "PE version resources passed\n";
    return 0;
}
