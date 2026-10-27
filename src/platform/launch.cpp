#include "launch.hpp"
#ifdef _WIN32
#include <shellapi.h>
#endif

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <string>

namespace {
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
}

HANDLE takeLaunchedProcess() {
    const HANDLE process = launched_process;
    launched_process = nullptr;
    return process;
}

INT_PTR executeGameAction(HWND window, const GameAction& action) {
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
}
