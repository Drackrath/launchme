#include "launch.hpp"
#ifdef _WIN32
#include <shellapi.h>
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <string>
#else
#include <cerrno>
#include <codecvt>
#include <filesystem>
#include <locale>
#include <spawn.h>
#include <string>
#include <unistd.h>
#include <vector>
#if defined(__GLIBC__)
#include <spawn.h>
#endif
extern char** environ;
#endif

namespace {
#ifdef _WIN32
thread_local HANDLE launched_process = nullptr;

std::wstring lowerExtension(const std::wstring& target) {
    std::error_code ec;
    auto extension = std::filesystem::path(target).extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return extension;
}

std::wstring systemPowerShell() {
    wchar_t folder[MAX_PATH]{};
    const UINT length = GetSystemDirectoryW(folder, MAX_PATH);
    if (!length || length >= MAX_PATH) return L"powershell.exe";
    return (std::filesystem::path(folder) / L"WindowsPowerShell" / L"v1.0" / L"powershell.exe").wstring();
}
#else
thread_local HANDLE launched_process = 0;

std::string utf8(const std::wstring& value) {
    return std::wstring_convert<std::codecvt_utf8<wchar_t>>{}.to_bytes(value);
}

std::vector<std::string> splitArguments(const std::string& command_line) {
    std::vector<std::string> result;
    std::string argument;
    bool quoted = false;
    for (size_t i = 0; i < command_line.size(); ++i) {
        const char character = command_line[i];
        if (character == '\\' && i + 1 < command_line.size() && command_line[i + 1] == '"') {
            argument += command_line[++i];
        } else if (character == '"') {
            quoted = !quoted;
        } else if (character == ' ' || character == '\t') {
            if (!argument.empty() || quoted) {
                result.push_back(std::move(argument));
                argument.clear();
            }
        } else {
            argument += character;
        }
    }
    if (!argument.empty() || quoted) result.push_back(std::move(argument));
    return result;
}

INT_PTR spawnError(int error) {
    switch (error) {
    case ENOENT: return launch_error::file_missing;
    case ENOTDIR: return launch_error::path_missing;
    case EACCES: case EPERM: return launch_error::denied;
    case ENOEXEC: return launch_error::no_handler;
    default: return launch_error::other;
    }
}
#endif
}

HANDLE takeLaunchedProcess() {
#ifdef _WIN32
    const HANDLE process = launched_process;
    launched_process = nullptr;
#else
    const HANDLE process = launched_process;
    launched_process = 0;
#endif
    return process;
}

INT_PTR executeGameAction(HWND window, const GameAction& action) {
#ifdef _WIN32
    if (launched_process) { CloseHandle(launched_process); launched_process = nullptr; }
    if (!action) return launch_error::other;
    // URI launches may go through an already running Steam/store process rather than a new
    // child. Let that broker hand foreground permission to its game (or its install dialog),
    // unless Launchme keeps the foreground and focuses the game window itself.
    if (!action.keep_focus) AllowSetForegroundWindow(ASFW_ANY);
    std::wstring target = action.target, arguments = action.arguments;
    // A PowerShell script runs; its "open" verb would only show it in Notepad.
    if (lowerExtension(target) == L".ps1") {
        arguments = L"-NoProfile -ExecutionPolicy Bypass -File \"" + target + L"\"" + (arguments.empty() ? L"" : L" " + arguments);
        target = systemPowerShell();
    }
    SHELLEXECUTEINFOW info{sizeof(info)};
    // NO_UI: a link no program handles fails here (and the launcher says which client is
    // missing) instead of Windows' "find an app" prompt, which a pad cannot answer.
    // NOZONECHECKS: a game the user added (often downloaded) starts without the modal
    // "Open File - Security Warning" that would block the launcher's frames and pad input.
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI | SEE_MASK_NOZONECHECKS | SEE_MASK_NOASYNC;
    info.hwnd = window;
    info.lpVerb = L"open";
    info.lpFile = target.c_str();
    info.lpParameters = arguments.empty() ? nullptr : arguments.c_str();
    const auto directory = action.working_directory.wstring();
    info.lpDirectory = directory.empty() ? nullptr : directory.c_str();
    info.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&info)) {
        launched_process = info.hProcess; // May be null: a running client took the link.
        return 33;
    }
    switch (GetLastError()) {
    case ERROR_CANCELLED: return launch_error::cancelled;
    case ERROR_FILE_NOT_FOUND: return launch_error::file_missing;
    case ERROR_PATH_NOT_FOUND: case ERROR_BAD_NETPATH: case ERROR_INVALID_DRIVE: return launch_error::path_missing;
    case ERROR_ACCESS_DENIED: return launch_error::denied;
    case ERROR_NO_ASSOCIATION: case ERROR_DDE_FAIL: return launch_error::no_handler;
    default: return launch_error::other;
    }
#else
    (void)window;
    launched_process = 0;
    if (!action) return launch_error::other;

    const bool is_uri = action.target.find(L"://") != std::wstring::npos;
    std::vector<std::string> arguments;
    std::string executable;
    if (is_uri) {
        executable = "xdg-open";
        arguments.push_back(utf8(action.target));
    } else {
        executable = utf8(action.target);
        arguments.push_back(executable);
        for (auto& argument : splitArguments(utf8(action.arguments))) arguments.push_back(std::move(argument));
    }

    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1);
    for (auto& argument : arguments) argv.push_back(argument.data());
    argv.push_back(nullptr);

    posix_spawn_file_actions_t file_actions;
    if (posix_spawn_file_actions_init(&file_actions) != 0) return launch_error::other;
#if defined(__GLIBC__)
    const auto directory = action.working_directory.string();
    if (!directory.empty() && posix_spawn_file_actions_addchdir_np(&file_actions, directory.c_str()) != 0) {
        posix_spawn_file_actions_destroy(&file_actions);
        return launch_error::path_missing;
    }
#endif
    pid_t process = 0;
    const int error = is_uri ? posix_spawnp(&process, executable.c_str(), &file_actions, nullptr, argv.data(), environ)
                             : posix_spawn(&process, executable.c_str(), &file_actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&file_actions);
    if (error != 0) return spawnError(error);
    launched_process = process;
    return 33;
#endif
}
