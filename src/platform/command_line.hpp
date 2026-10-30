#pragma once
#include <string>
#include <string_view>

namespace command_line {
namespace detail {
template <class Char> std::basic_string<Char> quote(std::basic_string_view<Char> value) {
    std::basic_string<Char> result(1, Char('"'));
    size_t slashes = 0;
    for (const Char ch : value) {
        if (ch == Char('\\')) { ++slashes; continue; }
        result.append(ch == Char('"') ? slashes * 2 + 1 : slashes, Char('\\'));
        slashes = 0;
        result += ch;
    }
    result.append(slashes * 2, Char('\\'));
    return result + Char('"');
}
}
// One Windows CommandLineToArgvW-compatible argument, including embedded quotes
// and trailing backslashes. This is not quoting for cmd.exe or PowerShell.
inline std::wstring quoteArgument(std::wstring_view value) { return detail::quote(value); }
// The same rules for UTF-8 (process_spawn::splitArguments reads them back on Linux).
inline std::string quoteArgument(std::string_view value) { return detail::quote(value); }
}
