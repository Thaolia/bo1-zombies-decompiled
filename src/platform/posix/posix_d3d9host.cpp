// The D3D9 frontend's host on posix (gfx_d3d9emu/d3d9emu_host.h): the first SDL display and the game windows through
// the window layer (win32_gui.cpp), shaders.pack in the install folder next to zone/, and the engine's console.

#include "posix_video.h"
#include "win32_handles.h"

#include <gfx_d3d9emu/d3d9emu_host.h>

#include <SDL.h>

#include <stdio.h>

#include <string>

char *__cdecl Sys_DefaultInstallPath();
void Com_Printf(int channel, const char *fmt, ...);

namespace d3d9emu::host
{
namespace
{
constexpr int kAdapterDisplay = 0;
constexpr int kConsoleChannelGfx = 8;

// D3D9 lists the modes of its 32-bit display format.
bool Listed(const SDL_DisplayMode &mode)
{
    return SDL_BITSPERPIXEL(mode.format) >= 24;
}
} // namespace

uint32_t DisplayModes(DisplayMode *modes, uint32_t capacity)
{
    if (!Bo1_VideoReady())
        return 0;
    uint32_t count = 0;
    const int total = SDL_GetNumDisplayModes(kAdapterDisplay);
    for (int i = 0; i < total; ++i)
    {
        SDL_DisplayMode mode;
        if (SDL_GetDisplayMode(kAdapterDisplay, i, &mode) || !Listed(mode))
            continue;
        if (count < capacity)
            modes[count] = {uint32_t(mode.w), uint32_t(mode.h), uint32_t(mode.refresh_rate)};
        ++count;
    }
    return count;
}

bool DesktopMode(DisplayMode *mode)
{
    SDL_DisplayMode desktop;
    if (!Bo1_VideoReady() || SDL_GetDesktopDisplayMode(kAdapterDisplay, &desktop))
        return false;
    *mode = {uint32_t(desktop.w), uint32_t(desktop.h), uint32_t(desktop.refresh_rate)};
    return true;
}

HMONITOR AdapterMonitor()
{
    return Bo1_VideoReady() ? Bo1_DisplayMonitor(kAdapterDisplay) : nullptr;
}

void *NativeWindow(HWND hwnd)
{
    return Bo1_NativeWindow(hwnd);
}

bool ClientSize(HWND hwnd, uint32_t *width, uint32_t *height)
{
    RECT rect;
    if (!Bo1_ClientRect(hwnd, &rect))
        return false;
    *width = uint32_t(rect.right - rect.left);
    *height = uint32_t(rect.bottom - rect.top);
    return true;
}

bool ClientOrigin(HWND hwnd, int32_t *x, int32_t *y)
{
    RECT rect;
    SDL_Rect display;
    if (!Bo1_ClientRect(hwnd, &rect) || !Bo1_VideoReady() || SDL_GetDisplayBounds(kAdapterDisplay, &display))
        return false;
    *x = rect.left - display.x;
    *y = rect.top - display.y;
    return true;
}

bool WindowVisible(HWND hwnd)
{
    return Bo1_WindowShown(hwnd);
}

const char *ShaderPackPath()
{
    static const std::string path = Bo1_PosixPath(Sys_DefaultInstallPath()) + "/shaders.pack";
    return path.c_str();
}

bool SaveFile(const char *path, const void *data, size_t size)
{
    FILE *file = fopen(Bo1_PosixPath(path).c_str(), "wb");
    if (!file)
        return false;
    const bool written = fwrite(data, 1, size, file) == size;
    return fclose(file) == 0 && written;
}

void Log(const char *line)
{
    Com_Printf(kConsoleChannelGfx, "%s\n", line);
}
} // namespace d3d9emu::host
