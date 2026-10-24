#pragma once

// What the GL backend needs from the platform it runs on: an OpenGL 4.3 core context, its entry points, the windows
// it presents to, and the log. The platform layer implements it (src/platform/posix/posix_glhost.cpp with SDL2; the
// Switch would with EGL on its native window), and so does tests/gfx_backend.

namespace gb::gl::host
{
// On the thread that calls gb::Init: makes the context and leaves it current on no thread. False, with the reason in
// the log, when there is none (no display, no OpenGL 4.3 core, a headless run). From then on the platform makes its
// windows so that the context can draw into them.
bool Open();
// On the backend's thread: makes the context current there, drawing into `window` (a SwapchainDesc::window), or
// into no window when it is null. False when that fails.
bool MakeCurrent(void *window);
// On the backend's thread, before it ends: leaves the context current on no thread.
void ReleaseCurrent();
void *ProcAddress(const char *name);
// A window's size in pixels, as the context draws into it.
void DrawableSize(void *window, int *width, int *height);
// For the window the context is current with: 1 waits for the vertical blank at each swap, 0 does not.
void SetSwapInterval(int interval);
// Shows what was drawn into `window`, the window the context is current with.
void Swap(void *window);
// On the thread that calls gb::Shutdown, once the backend's thread has ended: deletes the context.
void Close();
// One line, without the newline, for the engine's log. Only the frontend's threads call it.
void Log(const char *line);
} // namespace gb::gl::host
