#include "binary_kv.hpp"

#include <charconv>
#include <cstring>

namespace binary_kv {
namespace {
bool sameKey(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
        return (x >= 'A' && x <= 'Z' ? x - 'A' + 'a' : x) == (y >= 'A' && y <= 'Z' ? y - 'A' + 'a' : y);
    });
}
bool readString(std::span<const uint8_t> bytes, size_t& at, std::string& out) {
    const size_t start = at;
    while (at < bytes.size() && bytes[at]) ++at;
    if (at >= bytes.size()) return false;
    out.assign(reinterpret_cast<const char*>(bytes.data() + start), at - start);
    ++at;
    return true;
}
template <class T> bool readValue(std::span<const uint8_t> bytes, size_t& at, T& value) {
    if (at + sizeof(T) > bytes.size()) return false;
    std::memcpy(&value, bytes.data() + at, sizeof(T));
    at += sizeof(T);
    return true;
}
bool readKey(std::span<const uint8_t> bytes, size_t& at, std::string& out, const std::vector<std::string>* keys) {
    if (!keys) return readString(bytes, at, out);
    uint32_t index = 0;
    if (!readValue(bytes, at, index) || index >= keys->size()) return false;
    out = (*keys)[index];
    return true;
}
bool readObject(std::span<const uint8_t> bytes, size_t& at, Node& node, int depth, const std::vector<std::string>* keys) {
    if (depth > 32) return false;
    while (at < bytes.size()) {
        const uint8_t type = bytes[at++];
        if (type == 8 || type == 11) return true; // End of this object.
        Node entry;
        if (!readKey(bytes, at, entry.name, keys)) return false;
        switch (type) {
        case 0: entry.object = true; if (!readObject(bytes, at, entry, depth + 1, keys)) return false; break;
        case 1: if (!readString(bytes, at, entry.text)) return false; break;
        case 2: case 4: case 6: { int32_t value = 0; if (!readValue(bytes, at, value)) return false; entry.number = value; entry.numeric = type == 2; break; }
        case 3: { float value = 0; if (!readValue(bytes, at, value)) return false; entry.number = static_cast<int64_t>(value); entry.numeric = true; break; }
        case 5: { // UTF-16, zero-terminated: skipped (Steam's files do not use it).
            while (at + 1 < bytes.size() && (bytes[at] || bytes[at + 1])) at += 2;
            if (at + 1 >= bytes.size()) return false;
            at += 2; break;
        }
        case 7: case 10: { int64_t value = 0; if (!readValue(bytes, at, value)) return false; entry.number = value; entry.numeric = true; break; }
        default: return false;
        }
        if (node.children.size() >= 100000) return false;
        node.children.push_back(std::move(entry));
    }
    return depth == 0; // Only the root may end with the data.
}
}

const Node* Node::child(std::string_view key) const {
    for (const auto& entry : children) if (sameKey(entry.name, key)) return &entry;
    return nullptr;
}

int64_t Node::integer() const {
    if (object) return -1;
    if (numeric) return number;
    int64_t value = -1;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size() ? value : -1;
}

std::optional<Node> parse(std::span<const uint8_t> bytes, const std::vector<std::string>* keys) {
    Node root;
    root.object = true;
    size_t at = 0;
    if (bytes.empty() || !readObject(bytes, at, root, 0, keys)) return std::nullopt;
    return root;
}
}
