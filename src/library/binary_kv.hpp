#pragma once
// Binary KeyValues as the Steam client writes them: appcache/stats (keys inline) and
// appcache/appinfo.vdf v29 (keys as int32 indices into the file's string table).
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace binary_kv {
struct Node {
    std::string name, text; // text: string values.
    int64_t number = 0;     // Integer values.
    bool object = false, numeric = false;
    std::vector<Node> children;
    const Node* child(std::string_view key) const; // Case-insensitive.
    // An integer value, also spelled as text ("1234"); -1 when it is neither.
    int64_t integer() const;
};
// `keys`: the string table of appinfo.vdf v29; keys are then int32 indices into it.
std::optional<Node> parse(std::span<const uint8_t> bytes, const std::vector<std::string>* keys = nullptr);
}
