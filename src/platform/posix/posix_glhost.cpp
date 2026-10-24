// The GL backend's host on posix (gfx_backend/gl/gb_gl_host.h): an SDL2 OpenGL 4.3 core context, made on a hidden
// 1x1 window of its own and then current on the game window it presents to. Once it exists the window layer makes
// every game window an OpenGL one (win32_gui.cpp), which SDL needs to make a context current on it. A headless run
// gets none and renders on the null backend, as its windows never show, unless BO1_GFX_BACKEND=gl asks for GL (its
// back buffer screenshots then show what it drew). Closing the context also stops SDL's video when no window uses it
// (Bo1_VideoRelease): the engine's first Direct3D object lives on another thread than its windows.

#include "posix_video.h"

#include <gfx_backend/gl/gb_gl_host.h>

#include <SDL.h>

#include <stdlib.h>
#include <string.h>

bool __cdecl Sys_IsHeadless();
void Com_Printf(int channel, const char *fmt, ...);

namespace gb::gl::host
{
namespace
{
constexpr int kConsoleChannelGfx = 8;

SDL_Window *s_window = nullptr;
SDL_GLContext s_context = nullptr;

void Fail(const char *what)
{
    Com_Printf(kConsoleChannelGfx, "gl: %s: %s\n", what, SDL_GetError());
}
} // namespace

bool Open()
{
    const char *wanted = getenv("BO1_GFX_BACKEND");
    if ((Sys_IsHeadless() && !(wanted && !strcmp(wanted, "gl"))) || !Bo1_VideoReady())
        return false;
    // The backend draws into its own attachments and only copies the frame into the window: no depth there.
    SDL_GL_ResetAttributes();
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    if (getenv("BO1_GL_DEBUG"))
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
    s_window = SDL_CreateWindow("", 0, 0, 1, 1, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!s_window)
    {
        Fail("no OpenGL window");
        return false;
    }
    s_context = SDL_GL_CreateContext(s_window);
    if (!s_context)
    {
        Fail("no OpenGL 4.3 core context");
        SDL_DestroyWindow(s_window);
        s_window = nullptr;
        return false;
    }
    SDL_GL_MakeCurrent(s_window, nullptr);
    Bo1_UseGlWindows();
    return true;
}

bool MakeCurrent(void *window)
{
    return SDL_GL_MakeCurrent(window ? static_cast<SDL_Window *>(window) : s_window, s_context) == 0;
}

void ReleaseCurrent()
{
    SDL_GL_MakeCurrent(s_window, nullptr);
}

void *ProcAddress(const char *name)
{
    return SDL_GL_GetProcAddress(name);
}

void DrawableSize(void *window, int *width, int *height)
{
    SDL_GL_GetDrawableSize(static_cast<SDL_Window *>(window), width, height);
}

void SetSwapInterval(int interval)
{
    SDL_GL_SetSwapInterval(interval);
}

void Swap(void *window)
{
    SDL_GL_SwapWindow(static_cast<SDL_Window *>(window));
}

void Close()
{
    SDL_GL_DeleteContext(s_context);
    SDL_DestroyWindow(s_window);
    s_context = nullptr;
    s_window = nullptr;
    Bo1_VideoRelease();
}

void Log(const char *line)
{
    Com_Printf(kConsoleChannelGfx, "%s\n", line);
}
} // namespace gb::gl::host
