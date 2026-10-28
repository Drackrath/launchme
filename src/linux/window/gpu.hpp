#pragma once
// Linux only: the window layer's GPU (gpu.cpp) as window.cpp sees it.
#include <SDL3/SDL_video.h>

namespace lm_window::detail {
struct Window;
namespace gpu {
enum class Backend { none, vulkan, gl };
// Decided once, at the first window: Vulkan, OpenGL or none (LAUNCHME_RENDERER).
Backend backend();
// The flags a window needs for the backend (SDL_WINDOW_VULKAN / SDL_WINDOW_OPENGL).
SDL_WindowFlags windowFlags();
// A window was created (the OpenGL context comes with the first one).
void created(Window* target);
// A window is going: its swapchain and surfaces with it.
void release(Window* target);
// The GPU context and device, at the window layer's end.
void shutdown();
} // namespace gpu
} // namespace lm_window::detail
