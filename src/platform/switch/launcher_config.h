#pragma once

// The Switch launcher's settings (sdmc:/switch/bo1zm/menu.cfg) and the engine start-up line they produce. No libnx in
// here: tests/platform/launcher_config_test.cpp builds this part alone on the host. The menu itself is
// switch_launcher.cpp.

#include <string>

struct LauncherMap
{
    const char *bsp;   // the map name +devmap takes
    const char *label; // what the menu shows
};

extern const LauncherMap kLauncherMaps[];
extern const int kLauncherMapCount;

enum LauncherBackend
{
    LAUNCHER_BACKEND_DEKO3D,
    LAUNCHER_BACKEND_GL,
    LAUNCHER_BACKEND_COUNT
};

enum LauncherTexFilter
{
    LAUNCHER_TEXFILTER_ANISO16,
    LAUNCHER_TEXFILTER_ANISO8,
    LAUNCHER_TEXFILTER_ANISO4,
    LAUNCHER_TEXFILTER_ANISO2,
    LAUNCHER_TEXFILTER_TRILINEAR,
    LAUNCHER_TEXFILTER_BILINEAR,
    LAUNCHER_TEXFILTER_COUNT
};

struct LauncherSettings
{
    int map = 0; // index into kLauncherMaps
    bool aimAssist = true;
    bool aimSlowdown = true;
    bool aimLockon = true;
    bool aimAutoaim = false;
    bool god = false;
    LauncherBackend backend = LAUNCHER_BACKEND_DEKO3D;
    int maxFps = 60; // 30 or 60
    bool shadows = true;
    bool fpsCounter = true;
    LauncherTexFilter texFilter = LAUNCHER_TEXFILTER_ANISO16;
};

// menu.cfg: one `key=value` per line. Unknown keys are skipped and an invalid value keeps the default, so a file from
// an older or newer build still loads.
LauncherSettings Launcher_ParseSettings(const std::string &text);
std::string Launcher_FormatSettings(const LauncherSettings &s);

// The line written to launch.cfg. Every dvar is set explicitly, the defaults included: the graphics ones are archived
// in config.cfg, so leaving one out would keep whatever an earlier launch chose. `extra` (launch_extra.cfg, may be
// empty) goes before +devmap, which must stay last.
std::string Launcher_BuildLaunchLine(const LauncherSettings &s, const std::string &extra);

// The gfx.cfg value switch_main.cpp turns into BO1_GFX_BACKEND.
const char *Launcher_BackendName(LauncherBackend backend);
const char *Launcher_TexFilterLabel(LauncherTexFilter filter);
