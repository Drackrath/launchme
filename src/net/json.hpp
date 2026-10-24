#pragma once

#include <optional>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace json {
struct Value {
    enum class Type { null_value, boolean, number, string, array, object } type = Type::null_value;
    bool flag = false;
    double numeric = 0;
    std::string text;
    std::vector<Value> elements;
    std::unordered_map<std::string, Value> properties;

    const Value* get(std::string_view name) const;
    std::string stringOr(std::string fallback = {}) const;
    int intOr(int fallback = 0) const;
};

std::optional<Value> parse(std::string_view source); // At most 2 MiB.
std::optional<Value> parse(std::string_view source, size_t max_bytes);

// Writing: a JSON string literal of UTF-8 text, and a compact document (object keys sorted;
// parsed numbers keep their original spelling, others print integers without a fraction).
std::string quote(std::string_view text);
std::string serialize(const Value& value);
Value string(std::string text);
Value number(double value);
Value boolean(bool flag);
Value array();
Value object();
} // namespace json
