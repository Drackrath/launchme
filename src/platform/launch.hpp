#pragma once
#include "../model/game.hpp"
#ifdef _WIN32
#include <Windows.h>
#else
#include <cstdint>
#include <sys/types.h>
using HWND = void*;
using HANDLE = pid_t;
using INT_PTR = std::intptr_t;
#endif

using ActionExecutor = INT_PTR (*)(HWND, const GameAction&);
// > 32: started. <= 32: not started, as one of launch_error (ShellExecute-compatible values).
INT_PTR executeGameAction(HWND window, const GameAction& action);

namespace launch_error {
inline constexpr INT_PTR other = 0;
inline constexpr INT_PTR cancelled = 1;    // The user declined (UAC prompt, a store's own dialog): no error to show.
inline constexpr INT_PTR file_missing = 2; // The file or its folder is gone.
inline constexpr INT_PTR path_missing = 3;
inline constexpr INT_PTR denied = 5;
inline constexpr INT_PTR no_handler = 31;  // Nothing handles the link (store client not installed).
}

// The process the last successful executeGameAction() on this thread started, when Windows
// reports one (a program or script; links handed to a running client have none). The caller
// owns the handle; empty after it was taken. On Linux it is the process ID (0: none) of a program
// started in a session of its own, already reaped when it exits: there is nothing to close.
HANDLE takeLaunchedProcess();
