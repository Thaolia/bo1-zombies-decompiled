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
    // Join a dedicated server (docs/SERVER.md) instead of a solo game on `map`.
    bool join = false;
    std::string serverHost; // valid per Launcher_ValidServerHost, or empty
    int serverPort = 3074;  // the dedicated server's default net_port
    std::string playerName; // the name on the server, valid per Launcher_ValidPlayerName or empty (the game's own)
    int map = 0;            // index into kLauncherMaps
    bool aimAssist = true;
    bool aimSlowdown = true;
    bool aimLockon = true;
    bool aimAutoaim = false;
    bool god = false;
    bool tabInfo = true; // mods/zinfo: zombies alive / left this round beside the Tab scoreboard
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
// empty) goes before +devmap, which must stay last. A join with a server boots the game's menu and sets cl_autoConnect
// instead of +devmap; a join without one falls back to the solo line.
std::string Launcher_BuildLaunchLine(const LauncherSettings &s, const std::string &extra);

// What the menu accepts from the keyboard. A host is an IPv4 address or a host name, 1-63 letters, digits, '.' or '-';
// a port 1-65535, as text of digits only; a name 1-15 letters, digits, '_', '-' or '.'. No blank in any of them, since
// launch.cfg is one command line.
bool Launcher_ValidServerHost(const std::string &host);
bool Launcher_ParsePort(const std::string &text, int *port);
bool Launcher_ValidPlayerName(const std::string &name);

// "host:port", the address cl_autoConnect gets.
std::string Launcher_ServerAddress(const LauncherSettings &s);

// The gfx.cfg value switch_main.cpp turns into BO1_GFX_BACKEND.
const char *Launcher_BackendName(LauncherBackend backend);
const char *Launcher_TexFilterLabel(LauncherTexFilter filter);
