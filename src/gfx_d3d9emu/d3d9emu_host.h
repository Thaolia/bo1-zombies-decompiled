#pragma once

// What the D3D9 frontend needs from the platform it runs on: the display, the windows, files and the log. The
// platform layer implements it (src/platform/posix/posix_d3d9host.cpp), and so does tests/gfx_d3d9emu.

#include <windows.h>

#include <cstddef>
#include <cstdint>

namespace d3d9emu::host
{
struct DisplayMode
{
    uint32_t width;
    uint32_t height;
    uint32_t refreshRate; // Hz, 0 when unknown
};

// The display the adapter stands for: its modes (at most `capacity`, the count returned) and its current mode. 0 and
// false without a display.
uint32_t DisplayModes(DisplayMode *modes, uint32_t capacity);
bool DesktopMode(DisplayMode *mode);
HMONITOR AdapterMonitor();

// The native window behind a game window, for the backend's swapchain (gb::SwapchainDesc::window); its client size
// and the client area's top-left corner on the adapter's display; whether any of it can be seen (a hidden or
// minimised window cannot).
void *NativeWindow(HWND hwnd);
bool ClientSize(HWND hwnd, uint32_t *width, uint32_t *height);
bool ClientOrigin(HWND hwnd, int32_t *x, int32_t *y);
bool WindowVisible(HWND hwnd);

// The path of shaders.pack (src/gfx_d3d9emu/shader_pack.h), next to the converted zones.
const char *ShaderPackPath();
// Writes a whole file at a path the engine built (D3DXSaveSurfaceToFile's screenshots); false when it cannot.
bool SaveFile(const char *path, const void *data, size_t size);

// One line, without the newline, for the engine's log.
void Log(const char *line);
} // namespace d3d9emu::host
