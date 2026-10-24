#pragma once
#include <string>
#include <string_view>

namespace command_line {
// One Windows CommandLineToArgvW-compatible argument, including embedded quotes
// and trailing backslashes. This is not quoting for cmd.exe or PowerShell.
inline std::wstring quoteArgument(std::wstring_view value) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (const wchar_t ch : value) {
        if (ch == L'\\') { ++slashes; continue; }
        result.append(ch == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0;
        result += ch;
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
}
}
