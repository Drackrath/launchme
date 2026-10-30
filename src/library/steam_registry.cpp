#include "steam_registry.hpp"
#include "../platform/ascii_text.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/system.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>

namespace steam_registry {
namespace {
bool same(std::string_view a, std::string_view b) { return ascii_text::equalIgnoreCase(a, b); }
std::optional<std::string> readText(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() < 0 || input.tellg() > 8 * 1024 * 1024) return {};
    std::string text(static_cast<size_t>(input.tellg()), '\0');
    input.seekg(0);
    if (!input.read(text.data(), static_cast<std::streamsize>(text.size()))) return {};
    return text;
}
const Node* find(const Node& node, std::string_view key) {
    for (const auto& item : node.children) if (item.block && same(item.key, key)) return &item;
    return nullptr;
}
Node* child(Node& node, std::string_view key) {
    for (auto& item : node.children) if (item.block && same(item.key, key)) return &item;
    node.children.push_back({std::string(key), {}, true, {}});
    return &node.children.back();
}
const Node* steamKey(const Node& root, const std::vector<std::string>& keys) {
    const Node* node = &root;
    std::vector<std::string> path{"Registry", "HKCU", "Software", "Valve", "Steam"};
    path.insert(path.end(), keys.begin(), keys.end());
    for (const auto& key : path) {
        node = find(*node, key);
        if (!node) return nullptr;
    }
    return node;
}
std::string vdfQuoted(std::string_view text) {
    std::string result = "\"";
    for (const char ch : text) {
        if (ch == '"' || ch == '\\') result += '\\';
        result += ch;
    }
    return result + '"';
}
void write(const Node& node, int depth, std::string& out) {
    for (const auto& item : node.children) {
        const std::string indent(static_cast<size_t>(depth), '\t');
        if (item.block) {
            out += indent + vdfQuoted(item.key) + '\n' + indent + "{\n";
            write(item, depth + 1, out);
            out += indent + "}\n";
        } else out += indent + vdfQuoted(item.key) + "\t\t" + vdfQuoted(item.value) + '\n';
    }
}
}

std::filesystem::path file() {
    const std::filesystem::path home = system_info::environment(L"HOME").value_or(L"");
    if (home.empty()) return {};
    const auto native = home / L".steam" / L"registry.vdf";
    const auto flatpak = home / L".var" / L"app" / L"com.valvesoftware.Steam" / L".steam" / L"registry.vdf";
    std::error_code ec;
    if (!std::filesystem::exists(native, ec) && std::filesystem::exists(flatpak, ec)) return flatpak;
    return native;
}

std::optional<Node> parse(std::string_view text) {
    std::vector<Node> stack(1);
    std::optional<std::string> pending;
    for (size_t at = 0; at < text.size(); ++at) {
        const char ch = text[at];
        if (ch == '/' && at + 1 < text.size() && text[at + 1] == '/') { at = text.find('\n', at); if (at == std::string_view::npos) break; continue; }
        if (ch == '{') {
            if (!pending) return {};
            stack.push_back({std::move(*pending), {}, true, {}});
            pending.reset();
        } else if (ch == '}') {
            if (pending || stack.size() < 2) return {};
            auto done = std::move(stack.back());
            stack.pop_back();
            stack.back().children.push_back(std::move(done));
        } else if (ch == '"') {
            std::string token;
            bool closed = false;
            for (++at; at < text.size(); ++at) {
                if (text[at] == '\\' && at + 1 < text.size()) {
                    const char next = text[++at];
                    token += next == 'n' ? '\n' : next == 't' ? '\t' : next;
                    continue;
                }
                if (text[at] == '"') { closed = true; break; }
                token += text[at];
            }
            if (!closed) return {};
            if (!pending) pending = std::move(token);
            else { stack.back().children.push_back({std::move(*pending), std::move(token), false, {}}); pending.reset(); }
        }
    }
    if (pending || stack.size() != 1) return {};
    return std::move(stack.front());
}

std::string format(const Node& root) {
    std::string out;
    write(root, 0, out);
    return out;
}

std::optional<std::string> value(const std::filesystem::path& file, const std::vector<std::string>& keys, std::string_view name) {
    const auto text = readText(file);
    const auto root = text ? parse(*text) : std::nullopt;
    const auto* node = root ? steamKey(*root, keys) : nullptr;
    if (!node) return {};
    for (const auto& item : node->children) if (!item.block && same(item.key, name)) return item.value;
    return {};
}

std::optional<uint64_t> number(const std::filesystem::path& file, const std::vector<std::string>& keys, std::string_view name) {
    const auto text = value(file, keys, name);
    uint64_t result = 0;
    if (!text || text->empty()) return {};
    const auto parsed = std::from_chars(text->data(), text->data() + text->size(), result);
    return parsed.ec == std::errc{} && parsed.ptr == text->data() + text->size() ? std::optional<uint64_t>(result) : std::nullopt;
}

bool set(const std::filesystem::path& file, const std::vector<std::pair<std::string, std::string>>& values) {
    const auto text = readText(file);
    auto root = text ? parse(*text) : std::nullopt;
    if (!root) return false;
    Node* node = &*root;
    for (const auto* key : {"Registry", "HKCU", "Software", "Valve", "Steam"}) node = child(*node, key);
    for (const auto& [name, value] : values) {
        auto item = std::find_if(node->children.begin(), node->children.end(), [&](const Node& entry) { return !entry.block && same(entry.key, name); });
        if (item != node->children.end()) item->value = value;
        else node->children.push_back({name, value, false, {}});
    }
    return atomic_file::write(file, format(*root));
}
}
