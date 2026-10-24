#pragma once
#include "../net/json.hpp"
#include <cmath>
#include <map>
#include <stdexcept>

namespace plugins::wire {
inline std::string quote(std::string_view value) {
    std::string out = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char c : value) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else out += c;
    }
    return out + '"';
}
inline std::string encode(const json::Value& v) {
    switch (v.type) {
    case json::Value::Type::string: return quote(v.text);
    case json::Value::Type::boolean: return v.flag ? "true" : "false";
    case json::Value::Type::number: return std::isfinite(v.numeric) ? std::to_string(v.numeric) : "null";
    case json::Value::Type::array: {
        std::string out = "[";
        for (const auto& e : v.elements) { if (out.size() > 1) out += ','; out += encode(e); }
        return out + ']';
    }
    case json::Value::Type::object: {
        std::string out = "{";
        for (const auto& [key, value] : std::map<std::string, json::Value>(v.properties.begin(), v.properties.end())) {
            if (out.size() > 1) out += ',';
            out += quote(key) + ':' + encode(value);
        }
        return out + '}';
    }
    default: return "null";
    }
}
inline json::Value string(std::string value) { json::Value v; v.type = json::Value::Type::string; v.text = std::move(value); return v; }
inline json::Value boolean(bool flag) { json::Value v; v.type = json::Value::Type::boolean; v.flag = flag; return v; }
inline json::Value object() { json::Value v; v.type = json::Value::Type::object; return v; }
inline const json::Value& field(const json::Value& v, std::string_view key) {
    static const json::Value empty;
    const auto* p = v.get(key); return p ? *p : empty;
}
inline std::string text(const json::Value& v, std::string_view key) { return field(v, key).stringOr(); }
}
