#pragma once
// Linux: the line protocol between BrowserWindow (browser_window_linux.cpp) and the `launchme-web`
// helper (src/linux/web_helper.cpp), over a socket pair that is descriptor 3 in the helper.
// One message per line, UTF-8, fields separated by tabs; a field escapes \ tab CR LF as \\ \t \r \n.
//
// Launcher -> helper:  navigate <url> | command <BrowserWindow::Command> | scroll <pixels> |
//                      low-memory <0|1> | present | script <id> <source> | capture <id> <png path> [window] | close
// Helper -> launcher:  status <BrowserWindow::Status> | page <url> <title> <loading> <back> <forward> <video> |
//                      error <text> | active <0|1> | external <url> | script <id> <json> |
//                      capture <id> <0|1> | closed <had focus 0|1>
// End of file on either side ends the window: the helper closes it, the launcher reports it closed.
#include <algorithm>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace browser_protocol {

inline std::string escape(std::string_view field) {
    std::string out;
    out.reserve(field.size());
    for (const char c : field) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '\t': out += "\\t"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        default: out += c;
        }
    }
    return out;
}

inline std::string unescape(std::string_view field) {
    std::string out;
    out.reserve(field.size());
    for (size_t i = 0; i < field.size(); ++i) {
        if (field[i] != '\\' || i + 1 == field.size()) { out += field[i]; continue; }
        const char next = field[++i];
        out += next == 't' ? '\t' : next == 'n' ? '\n' : next == 'r' ? '\r' : next;
    }
    return out;
}

// A message line (without its newline) from its fields.
inline std::string line(std::initializer_list<std::string_view> fields) {
    std::string out;
    bool first = true;
    for (const auto field : fields) {
        if (!first) out += '\t';
        out += escape(field);
        first = false;
    }
    out += '\n';
    return out;
}

inline std::vector<std::string> fields(std::string_view line) {
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        const auto tab = line.find('\t', start);
        out.push_back(unescape(line.substr(start, tab == std::string_view::npos ? std::string_view::npos : tab - start)));
        if (tab == std::string_view::npos) break;
        start = tab + 1;
    }
    return out;
}

// Splits complete lines off `buffer` (the rest stays for the next read).
inline std::vector<std::string> takeLines(std::string& buffer) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t end; (end = buffer.find('\n', start)) != std::string::npos; start = end + 1) out.push_back(buffer.substr(start, end - start));
    buffer.erase(0, start);
    return out;
}

inline bool asciiEqualIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
        if (lower(a[i]) != lower(b[i])) return false;
    }
    return true;
}

// The host of an absolute http(s) URL (empty when it is not one), as WinHttpCrackUrl splits it:
// no user name or password, a non-empty host.
inline std::string_view webHost(std::string_view url) {
    const auto colon = url.find("://");
    if (colon == std::string_view::npos) return {};
    const auto scheme = url.substr(0, colon);
    if (!asciiEqualIgnoreCase(scheme, "http") && !asciiEqualIgnoreCase(scheme, "https")) return {};
    auto authority = url.substr(colon + 3);
    authority = authority.substr(0, std::min(authority.find_first_of("/?#"), authority.size()));
    if (authority.find('@') != std::string_view::npos) return {};
    std::string_view host = authority;
    if (host.starts_with('[')) {
        const auto close = host.find(']');
        if (close == std::string_view::npos) return {};
        host = host.substr(0, close + 1);
        const auto rest = authority.substr(close + 1);
        if (!rest.empty() && !rest.starts_with(':')) return {};
    } else host = host.substr(0, std::min(host.find(':'), host.size()));
    if (const auto port = authority.substr(host.size()); port.starts_with(':')) {
        if (port.size() > 6) return {};
        for (const char c : port.substr(1)) if (c < '0' || c > '9') return {};
    }
    for (const char c : host) if (static_cast<unsigned char>(c) <= ' ' || c == '\\') return {};
    return host;
}

// BrowserWindow::supportedUrl: http or https, a host, no credentials, no control characters.
inline bool supportedUrl(std::string_view url) {
    if (url.empty() || url.size() > 8192) return false;
    for (const char c : url) if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) return false;
    return !webHost(url).empty();
}

}
