#pragma once
// The handle of a launcher window as the platform knows it: an HWND on Windows, an SDL_Window* on
// Linux. Window-bound helpers (file_dialog, frame_clock, layered_surface) and the renderer take this
// type, so the same declarations serve both systems; on Windows it is exactly HWND.
#ifdef _WIN32
#include <Windows.h>
namespace platform { using NativeWindow = HWND; }
#else
struct SDL_Window;
namespace platform { using NativeWindow = SDL_Window*; }
#endif
