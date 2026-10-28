#pragma once
// A command-line helper Launchme downloaded itself (Nile for Amazon Games): started hidden in a
// kill-on-close job with NUL as its input, its standard output kept (up to 8 MiB) and its error
// stream handed over line by line (progress). Cancellation ends the whole job.
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace helper_process {
struct Result {
    bool started = false;
    bool ok = false;        // Started, exited with 0, not cancelled.
    unsigned long exit_code = 1;
    std::string output;     // Standard output.
};
using Environment = std::vector<std::pair<std::wstring, std::wstring>>;
// Inherits Launchme's environment; `environment` replaces or adds variables (names compared
// case-insensitively). Worker threads only: it waits for the helper.
Result run(const std::filesystem::path& executable, const std::filesystem::path& working_directory,
           const std::vector<std::wstring>& arguments, const Environment& environment,
           const std::function<bool()>& cancel, const std::function<void(std::string_view)>& error_line = {});
}
