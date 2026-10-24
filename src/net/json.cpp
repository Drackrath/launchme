#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace json {
const Value* Value::get(std::string_view name) const {
    if (type != Type::object) return nullptr;
    const auto found = properties.find(std::string(name));
    return found == properties.end() ? nullptr : &found->second;
}

std::string Value::stringOr(std::string fallback) const {
    return type == Type::string ? text : std::move(fallback);
}

// Out-of-range, infinite ("1e999") and NaN numbers fall back: converting them to int is undefined.
int Value::intOr(int fallback) const {
    return type == Type::number && numeric >= static_cast<double>(INT_MIN) && numeric <= static_cast<double>(INT_MAX) ?
           static_cast<int>(numeric) : fallback;
}

namespace {
void appendUtf8(std::string& output, uint32_t cp) {
    if (cp <= 0x7f) output += static_cast<char>(cp);
    else if (cp <= 0x7ff) {
        output += static_cast<char>(0xc0 | (cp >> 6));
        output += static_cast<char>(0x80 | (cp & 63));
    } else if (cp <= 0xffff) {
        output += static_cast<char>(0xe0 | (cp >> 12));
        output += static_cast<char>(0x80 | ((cp >> 6) & 63));
        output += static_cast<char>(0x80 | (cp & 63));
    } else if (cp <= 0x10ffff) {
        output += static_cast<char>(0xf0 | (cp >> 18));
        output += static_cast<char>(0x80 | ((cp >> 12) & 63));
        output += static_cast<char>(0x80 | ((cp >> 6) & 63));
        output += static_cast<char>(0x80 | (cp & 63));
    }
}

class Parser {
public:
    explicit Parser(std::string_view input, size_t max_bytes) : input_(input), max_bytes_(max_bytes) {}
    std::optional<Value> document() {
        if (input_.size() > max_bytes_) return std::nullopt;
        auto value = parseValue(0);
        spaces();
        return value && index_ == input_.size() ? std::move(value) : std::nullopt;
    }

private:
    void spaces() { while (index_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[index_]))) ++index_; }

    std::optional<uint32_t> hex4() {
        if (index_ + 4 > input_.size()) return std::nullopt;
        uint32_t result = 0;
        for (int i = 0; i < 4; ++i) {
            const char ch = input_[index_++];
            result <<= 4;
            if (ch >= '0' && ch <= '9') result |= ch - '0';
            else if (ch >= 'a' && ch <= 'f') result |= ch - 'a' + 10;
            else if (ch >= 'A' && ch <= 'F') result |= ch - 'A' + 10;
            else return std::nullopt;
        }
        return result;
    }

    std::optional<std::string> parseString() {
        if (index_ == input_.size() || input_[index_++] != '"') return std::nullopt;
        std::string output;
        while (index_ < input_.size()) {
            const char ch = input_[index_++];
            if (ch == '"') return output;
            if (static_cast<unsigned char>(ch) < 0x20) return std::nullopt;
            if (ch != '\\') { output += ch; continue; }
            if (index_ == input_.size()) return std::nullopt;
            const char escape = input_[index_++];
            switch (escape) {
            case '"': case '\\': case '/': output += escape; break;
            case 'b': output += '\b'; break;
            case 'f': output += '\f'; break;
            case 'n': output += '\n'; break;
            case 'r': output += '\r'; break;
            case 't': output += '\t'; break;
            case 'u': {
                auto cp = hex4();
                if (!cp) return std::nullopt;
                if (*cp >= 0xd800 && *cp <= 0xdbff) {
                    if (index_ + 2 > input_.size() || input_.substr(index_, 2) != "\\u") return std::nullopt;
                    index_ += 2;
                    auto low = hex4();
                    if (!low || *low < 0xdc00 || *low > 0xdfff) return std::nullopt;
                    cp = 0x10000 + ((*cp - 0xd800) << 10) + (*low - 0xdc00);
                } else if (*cp >= 0xdc00 && *cp <= 0xdfff) return std::nullopt;
                appendUtf8(output, *cp);
                break;
            }
            default: return std::nullopt;
            }
        }
        return std::nullopt;
    }

    std::optional<Value> parseValue(int depth) {
        if (depth > 32) return std::nullopt;
        spaces();
        if (index_ >= input_.size()) return std::nullopt;
        const char ch = input_[index_];
        Value value;
        if (ch == '"') {
            auto text = parseString();
            if (!text) return std::nullopt;
            value.type = Value::Type::string;
            value.text = std::move(*text);
            return value;
        }
        if (ch == '[' || ch == '{') {
            const bool array = ch == '[';
            value.type = array ? Value::Type::array : Value::Type::object;
            ++index_;
            spaces();
            if (index_ < input_.size() && input_[index_] == (array ? ']' : '}')) {
                ++index_;
                return value;
            }
            while (index_ < input_.size()) {
                if (array) {
                    auto item = parseValue(depth + 1);
                    if (!item) return std::nullopt;
                    value.elements.push_back(std::move(*item));
                } else {
                    spaces();
                    auto key = parseString();
                    if (!key) return std::nullopt;
                    spaces();
                    if (index_ == input_.size() || input_[index_++] != ':') return std::nullopt;
                    auto item = parseValue(depth + 1);
                    if (!item) return std::nullopt;
                    value.properties.emplace(std::move(*key), std::move(*item));
                }
                spaces();
                if (index_ == input_.size()) return std::nullopt;
                const char delimiter = input_[index_++];
                if (delimiter == (array ? ']' : '}')) return value;
                if (delimiter != ',') return std::nullopt;
            }
            return std::nullopt;
        }
        if (input_.substr(index_, 4) == "true") { value.type = Value::Type::boolean; value.flag = true; index_ += 4; return value; }
        if (input_.substr(index_, 5) == "false") { value.type = Value::Type::boolean; index_ += 5; return value; }
        if (input_.substr(index_, 4) == "null") { index_ += 4; return value; }
        const size_t begin = index_;
        if (ch == '-') ++index_;
        while (index_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[index_]))) ++index_;
        if (index_ < input_.size() && input_[index_] == '.') {
            ++index_;
            while (index_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[index_]))) ++index_;
        }
        if (index_ < input_.size() && (input_[index_] == 'e' || input_[index_] == 'E')) {
            ++index_;
            if (index_ < input_.size() && (input_[index_] == '+' || input_[index_] == '-')) ++index_;
            while (index_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[index_]))) ++index_;
        }
        if (index_ == begin || index_ - begin > 64) return std::nullopt;
        const std::string number(input_.substr(begin, index_ - begin));
        char* end = nullptr;
        value.numeric = std::strtod(number.c_str(), &end);
        if (end != number.c_str() + number.size()) return std::nullopt;
        value.type = Value::Type::number;
        value.text = number; // IDs can exceed the exact integer range of double.
        return value;
    }

    std::string_view input_;
    size_t max_bytes_;
    size_t index_ = 0;
};
} // namespace

std::optional<Value> parse(std::string_view source) { return Parser(source, 2 * 1024 * 1024).document(); }
std::optional<Value> parse(std::string_view source, size_t max_bytes) { return Parser(source, max_bytes).document(); }

std::string quote(std::string_view text) {
    std::string out = "\"";
    out.reserve(text.size() + 2);
    constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char ch : text) {
        if (ch == '"' || ch == '\\') { out += '\\'; out += static_cast<char>(ch); }
        else if (ch == '\n') out += "\\n";
        else if (ch == '\r') out += "\\r";
        else if (ch == '\t') out += "\\t";
        else if (ch < 32) { out += "\\u00"; out += hex[ch >> 4]; out += hex[ch & 15]; }
        else out += static_cast<char>(ch);
    }
    return out + '"';
}

namespace {
void write(std::string& out, const Value& value, int depth) {
    if (depth > 32) { out += "null"; return; }
    switch (value.type) {
    case Value::Type::string: out += quote(value.text); return;
    case Value::Type::boolean: out += value.flag ? "true" : "false"; return;
    case Value::Type::number: {
        if (!value.text.empty()) { out += value.text; return; }
        if (!std::isfinite(value.numeric)) { out += "null"; return; }
        if (std::floor(value.numeric) == value.numeric && std::abs(value.numeric) < 9.0e15) { out += std::to_string(static_cast<long long>(value.numeric)); return; }
        char buffer[40];
        const int length = std::snprintf(buffer, sizeof(buffer), "%.10g", value.numeric);
        out.append(buffer, static_cast<size_t>(std::max(0, length)));
        return;
    }
    case Value::Type::array:
        out += '[';
        for (size_t i = 0; i < value.elements.size(); ++i) { if (i) out += ','; write(out, value.elements[i], depth + 1); }
        out += ']';
        return;
    case Value::Type::object: {
        std::vector<const std::pair<const std::string, Value>*> entries;
        entries.reserve(value.properties.size());
        for (const auto& entry : value.properties) entries.push_back(&entry);
        std::sort(entries.begin(), entries.end(), [](const auto* a, const auto* b) { return a->first < b->first; });
        out += '{';
        for (size_t i = 0; i < entries.size(); ++i) {
            if (i) out += ',';
            out += quote(entries[i]->first); out += ':';
            write(out, entries[i]->second, depth + 1);
        }
        out += '}';
        return;
    }
    default: out += "null"; return;
    }
}
}

std::string serialize(const Value& value) { std::string out; write(out, value, 0); return out; }
Value string(std::string text) { Value v; v.type = Value::Type::string; v.text = std::move(text); return v; }
Value number(double value) { Value v; v.type = Value::Type::number; v.numeric = value; return v; }
Value boolean(bool flag) { Value v; v.type = Value::Type::boolean; v.flag = flag; return v; }
Value array() { Value v; v.type = Value::Type::array; return v; }
Value object() { Value v; v.type = Value::Type::object; return v; }
} // namespace json
