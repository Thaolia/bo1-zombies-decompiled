#pragma once

#include <cstddef>

enum LauncherResult
{
    LAUNCHER_START,
    LAUNCHER_QUIT
};

// The start menu (switch_launcher.cpp), shown before the engine. On LAUNCHER_START it has saved the choices
// (menu.cfg), written launch.cfg and gfx.cfg, and returns the engine's start-up line and the backend name; the console
// is released either way, so the renderer can take the screen.
LauncherResult Launcher_Run(const char *buildStamp, char *line, size_t lineSize, char *backend, size_t backendSize);
