#include "linux_hid.hpp"
#include <charconv>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

namespace linux_hid {
namespace {
std::string readText(const std::filesystem::path& file, size_t limit = 64 * 1024) {
    std::ifstream input(file, std::ios::binary);
    std::string text(limit, '\0');
    input.read(text.data(), static_cast<std::streamsize>(limit));
    text.resize(static_cast<size_t>(input.gcount()));
    while (!text.empty() && (text.back() == '\n' || text.back() == '\0')) text.pop_back();
    return text;
}
std::optional<uint32_t> hex(std::string_view text) {
    uint32_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (error != std::errc{} || end != text.data() + text.size() || text.empty()) return {};
    return value;
}
std::string field(std::string_view text, std::string_view key) {
    for (size_t at = 0; at < text.size();) {
        const auto end = std::min(text.find('\n', at), text.size());
        const auto line = text.substr(at, end - at);
        if (line.size() > key.size() && line.starts_with(key) && line[key.size()] == '=') return std::string(line.substr(key.size() + 1));
        at = end + 1;
    }
    return {};
}
std::filesystem::path resolved(const std::filesystem::path& path) {
    std::error_code error;
    auto result = std::filesystem::canonical(path, error);
    return error ? std::filesystem::path{} : result;
}
}

std::optional<Node> parseUevent(std::string_view text) {
    const auto id = field(text, "HID_ID"); // bus:vendor:product, 4 + 8 + 8 hex digits.
    const auto first = id.find(':'), second = id.find(':', first + 1);
    if (first == id.npos || second == id.npos) return {};
    const auto bus = hex(std::string_view(id).substr(0, first)), vendor = hex(std::string_view(id).substr(first + 1, second - first - 1)),
               product = hex(std::string_view(id).substr(second + 1));
    if (!bus || !vendor || !product) return {};
    Node node;
    node.bus = static_cast<uint16_t>(*bus); node.vendor = static_cast<uint16_t>(*vendor); node.product = static_cast<uint16_t>(*product);
    node.name = field(text, "HID_NAME"); node.uniq = field(text, "HID_UNIQ");
    return node;
}

std::vector<std::pair<uint16_t, uint16_t>> topCollections(std::span<const uint8_t> descriptor) {
    std::vector<std::pair<uint16_t, uint16_t>> result;
    uint16_t page = 0;
    std::optional<uint32_t> usage;
    int depth = 0;
    for (size_t at = 0; at < descriptor.size();) {
        const uint8_t prefix = descriptor[at];
        if (prefix == 0xfe) { // Long item: size, tag, data.
            if (at + 1 >= descriptor.size()) break;
            at += 3 + descriptor[at + 1];
            continue;
        }
        const size_t size = (prefix & 3) == 3 ? 4 : prefix & 3;
        if (at + 1 + size > descriptor.size()) break;
        uint32_t value = 0;
        for (size_t i = 0; i < size; ++i) value |= static_cast<uint32_t>(descriptor[at + 1 + i]) << (8 * i);
        const uint8_t item = prefix & 0xfc;
        if (item == 0x04) page = static_cast<uint16_t>(value);                 // Global: usage page.
        else if (item == 0x08) { if (!usage) usage = size == 4 ? value : (static_cast<uint32_t>(page) << 16 | value); } // Local: usage.
        else if (item == 0xa0) {                                                 // Main: collection.
            if (depth == 0 && usage) result.emplace_back(static_cast<uint16_t>(*usage >> 16), static_cast<uint16_t>(*usage));
            ++depth; usage.reset();
        } else if (item == 0xc0) { if (depth) --depth; usage.reset(); }        // End collection.
        else if (item == 0x80 || item == 0x90 || item == 0xb0) usage.reset();   // Input, output, feature.
        at += 1 + size;
    }
    return result;
}

std::optional<uint64_t> macAddress(std::string_view text) {
    if (text.size() != 17) return {};
    uint64_t result = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (i % 3 == 2) { if (text[i] != ':') return {}; continue; }
        const auto digit = hex(text.substr(i, 1));
        if (!digit) return {};
        result = result << 4 | *digit;
    }
    return result ? std::optional<uint64_t>{result} : std::nullopt;
}

std::vector<Hidraw> hidraws(const std::filesystem::path& sys, const std::filesystem::path& dev) {
    std::vector<Hidraw> result;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(sys / "class/hidraw", error)) {
        const auto device = resolved(entry.path() / "device");
        auto hid = device.empty() ? std::nullopt : parseUevent(readText(device / "uevent"));
        if (!hid) continue;
        hid->device = device;
        const auto bytes = readText(device / "report_descriptor", 4096);
        result.push_back({dev / entry.path().filename(), std::move(*hid), std::vector<uint8_t>(bytes.begin(), bytes.end())});
    }
    return result;
}

std::optional<Node> node(std::wstring_view path, const std::filesystem::path& sys) {
    const auto name = std::filesystem::path(path).filename();
    const auto text = name.string();
    if (text.starts_with("hidraw")) {
        const auto device = resolved(sys / "class/hidraw" / name / "device");
        auto hid = device.empty() ? std::nullopt : parseUevent(readText(device / "uevent"));
        if (hid) hid->device = device;
        return hid;
    }
    if (!text.starts_with("event")) return {};
    // eventN → its input device; the parent is the HID device (or the USB interface for xpad).
    const auto input = resolved(sys / "class/input" / name / "device");
    if (input.empty()) return {};
    const auto parent = resolved(input / "device");
    if (auto hid = parent.empty() ? std::nullopt : parseUevent(readText(parent / "uevent"))) { hid->device = parent; return hid; }
    const auto bus = hex(readText(input / "id/bustype")), vendor = hex(readText(input / "id/vendor")), product = hex(readText(input / "id/product"));
    if (!bus || !vendor || !product) return {};
    return Node{parent, static_cast<uint16_t>(*bus), static_cast<uint16_t>(*vendor), static_cast<uint16_t>(*product),
                readText(input / "name"), readText(input / "uniq")};
}

std::optional<Battery> battery(const std::filesystem::path& device) {
    std::error_code error;
    if (device.empty()) return {};
    for (const auto& entry : std::filesystem::directory_iterator(device / "power_supply", error)) {
        const auto capacity = readText(entry.path() / "capacity");
        int level = -1;
        if (std::from_chars(capacity.data(), capacity.data() + capacity.size(), level).ec != std::errc{} || level < 0 || level > 100) continue;
        return Battery{level, readText(entry.path() / "status") == "Charging"};
    }
    return {};
}

bool processRunning(std::string_view name, const std::filesystem::path& proc) {
    std::error_code error;
    const auto user = getuid();
    for (const auto& entry : std::filesystem::directory_iterator(proc, error)) {
        const auto pid = entry.path().filename().string();
        if (pid.empty() || pid.find_first_not_of("0123456789") != pid.npos) continue;
        struct stat info{};
        if (stat(entry.path().c_str(), &info) != 0 || info.st_uid != user) continue;
        if (readText(entry.path() / "comm", 64) == name) return true;
    }
    return false;
}
}
