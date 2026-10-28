#pragma once
// Linux only: the line protocol between the launcher's BrowserWindow (src/ui/browser_window_linux.cpp)
// and its reader-window helper `launchme-browser` (src/linux/browser/browser_main.cpp, GTK 4 and
// WebKitGTK 6.0 in a process of its own, so GTK's main loop never runs inside the SDL process).
//
// The launcher starts the helper with one end of a socketpair as the helper's stdout; the helper
// moves it off fd 1 at once. One message per line: fields separated by single spaces, each field
// escaped (`\\`, `\n`, `\r`, `\t`, `\s` for a space), UTF-8. The helper exits when the socket
// closes, so a launcher that dies never leaves its window behind.
//
// Launcher -> helper:
//   text <key> <value>        UI text (the launcher translates): reload, close, play, loading,
//                             hint, starting, unsupported, load-failed, downloads, crashed
//   fixture <html>            tests: every http:// request is answered with <html> by a loopback
//                             proxy, every other request fails (no real network)
//   page <url> <html>         the document for a https://launchme.invalid/ URL (the TV video player);
//                             empty <html>: the launcher has none
//   navigate <url>            open <url> (http or https)
//   show                      present the window (after the initial texts and navigation)
//   back | forward | reload | close | raise
//   scroll <pixels>           scroll the page (or its inner scroll container under the centre)
//   button previous|next|accept   pad focus on the toolbar buttons and pressing the chosen one
//   low-memory 0|1            AppSettings::low_memory
//   script <id> <javascript>  run in the page; answered by `result`
//   snapshot <id> page|window <png path>  answered by `snapshot`
//
// Helper -> launcher:
//   hello <pid> <webkit version>
//   status starting|ready|failed
//   loading 0|1
//   url <url>
//   title <text>
//   history <can go back 0|1> <can go forward 0|1>
//   error <text>              shown in the footer; empty clears it
//   focus 0|1                 the helper's window is (not) the active window
//   player <url>              a navigation to a launchme.invalid URL needs its `page`
//   result <id> 0|1 <json>    ExecuteScript-style JSON of the value (failure: 0, empty)
//   snapshot <id> 0|1
//   closed                    the window closed (Escape, B, the window manager); the helper exits
#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace browser_protocol {
// The helper's exit codes besides 0 (and 126/127 from the loader when WebKitGTK is missing).
constexpr int exit_usage = 64;
constexpr int exit_unavailable = 69; // GTK cannot open the display or WebKitGTK cannot start.
// The video player's documents are supplied by the launcher under this origin.
constexpr std::string_view player_origin = "https://launchme.invalid/";
// Longest accepted line (a fixture or player document, a script, a result).
constexpr size_t max_line = 16u << 20;

inline std::string escape(std::string_view field) {
    std::string result;
    result.reserve(field.size());
    for (const char ch : field) {
        switch (ch) {
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        case ' ': result += "\\s"; break;
        default: result += ch; break;
        }
    }
    return result;
}

inline std::string unescape(std::string_view field) {
    std::string result;
    result.reserve(field.size());
    for (size_t i = 0; i < field.size(); ++i) {
        if (field[i] != '\\' || i + 1 == field.size()) { result += field[i]; continue; }
        switch (field[++i]) {
        case 'n': result += '\n'; break;
        case 'r': result += '\r'; break;
        case 't': result += '\t'; break;
        case 's': result += ' '; break;
        default: result += field[i]; break;
        }
    }
    return result;
}

// One message, terminated by its newline.
inline std::string message(std::initializer_list<std::string_view> fields) {
    std::string line;
    for (const auto field : fields) {
        if (!line.empty()) line += ' ';
        line += escape(field);
    }
    line += '\n';
    return line;
}

// A received line (without its newline) as unescaped fields; the first is the verb.
inline std::vector<std::string> fields(std::string_view line) {
    std::vector<std::string> result;
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    size_t start = 0;
    for (;;) {
        const size_t space = line.find(' ', start);
        result.push_back(unescape(line.substr(start, space == line.npos ? line.npos : space - start)));
        if (space == line.npos) break;
        start = space + 1;
    }
    return result;
}

// The URLs the reader window opens: http or https with a host and no user name or password, no
// control characters (BrowserWindow::supportedUrl; WinHttpCrackUrl on Windows).
inline bool supportedUrl(std::string_view url) {
    if (url.empty() || url.size() > 8192) return false;
    for (const char ch : url) if (static_cast<unsigned char>(ch) < ' ' || ch == 0x7f) return false;
    const auto lower = [](char ch) { return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch; };
    const auto starts = [&](std::string_view prefix) {
        if (url.size() < prefix.size()) return false;
        for (size_t i = 0; i < prefix.size(); ++i) if (lower(url[i]) != prefix[i]) return false;
        return true;
    };
    size_t at = starts("https://") ? 8 : starts("http://") ? 7 : 0;
    if (!at) return false;
    const size_t end = url.find_first_of("/?#", at);
    const auto authority = url.substr(at, end == url.npos ? url.npos : end - at);
    if (authority.find('@') != authority.npos) return false;
    // The host, without a port; IPv6 literals keep their brackets.
    auto host = authority;
    if (host.starts_with('[')) {
        const size_t close = host.find(']');
        if (close == host.npos || close < 2) return false;
        const auto rest = host.substr(close + 1);
        if (!rest.empty() && rest[0] != ':') return false;
        return true;
    }
    if (const size_t colon = host.find(':'); colon != host.npos) {
        const auto port = host.substr(colon + 1);
        host = host.substr(0, colon);
        if (port.size() > 5) return false;
        for (const char ch : port) if (ch < '0' || ch > '9') return false;
    }
    return !host.empty();
}
}
