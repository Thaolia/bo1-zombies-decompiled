#pragma once

// The Switch launcher's settings (sdmc:/switch/bo1zm/menu.cfg) and the engine start-up line they produce. No libnx in
// here: tests/platform/launcher_config_test.cpp builds this part alone on the host. The menu itself is
// switch_launcher.cpp.

#include "../gamepad_remap.h"

#include <string>

struct LauncherMap
{
    const char *bsp;    // the map name +devmap takes
    const char *label;  // what the menu shows
    const char *mapkit; // a MapKit layout (mods/mapkit/layouts/<it>.json) played on bsp, or null
    const char *zone;   // the layout's world: usermaps/<zone>/<zone>.ff and main/maps/<zone>.d3dbsp (bo1_mod_zones,
                        // bo1_bspworld), or null
};

extern const LauncherMap kLauncherMaps[];
extern const int kLauncherMapCount;

// menu.cfg's value for a map: its bsp, or "mapkit:<layout>" (ZmClient's spelling) for a layout, whose bsp is shared.
std::string Launcher_MapKey(int map);

// The files a map needs beside the game's own, relative to the game folder (sdmc:/switch/bo1zm/): the player's own
// extraction of another game (docs/BO2_MAPS.md), which the menu offers only when it is on the SD card. Retail maps:
// none.
int Launcher_MapFileCount(int map);
std::string Launcher_MapFile(int map, int i);

// The start kit of a solo game, given by mods/admin when the player first spawns (zm_startpoints, zm_startperks; the
// same choices as ZmClient's tools/zmclient/zmclient/profile.py). Points 0 keeps the game's own 500.
extern const int kLauncherStartPoints[];
extern const int kLauncherStartPointCount;
// mods/admin's zm_farrespawn choices (zm_where's units; 0 = off, 1500 = Moon's own), as ZmClient's FAR_RESPAWN_CHOICES.
extern const int kLauncherFarRespawns[];
extern const int kLauncherFarRespawnCount;
struct LauncherPerk
{
    const char *key;   // the name mods/admin's admin_perk_name takes, and menu.cfg's
    const char *label; // what the menu shows
};
extern const LauncherPerk kLauncherPerks[];
extern const int kLauncherPerkCount;

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
    bool lanAuto = true;    // the server view searches the local network for servers as it opens
    int map = 0;            // index into kLauncherMaps
    bool aimAssist = true;
    bool aimSlowdown = true;
    bool aimLockon = true;
    bool aimAutoaim = false;
    bool god = false;
    bool tabInfo = true; // mods/zinfo: zombies alive / left this round beside the Tab scoreboard
    int startPoints = 0;           // one of kLauncherStartPoints
    unsigned startPerks = 0;       // bit i: kLauncherPerks[i]
    bool startPower = false;       // mods/admin's zm_startpower: the map's power switch used at the start
    bool startPap = false;         // mods/admin's zm_startpap: a Pack-a-Punch, always on, at the start points
    int farRespawn = 0;            // one of kLauncherFarRespawns: mods/admin's zm_farrespawn
    LauncherBackend backend = LAUNCHER_BACKEND_DEKO3D;
    int height = 720; // 720 or 480: the game's frame (r_customMode), which the console scales to the screen
    int maxFps = 60;  // 30 or 60
    bool shadows = true;
    bool fpsCounter = true;
    LauncherTexFilter texFilter = LAUNCHER_TEXFILTER_ANISO16;
    // Which physical button drives each game control (gamepad_remap.h); the identity is the game's own layout.
    Bo1PadRemap pad = {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}};
};

// menu.cfg: one `key=value` per line. Unknown keys are skipped and an invalid value keeps the default, so a file from
// an older or newer build still loads. The pad lines (`pad_zr=zl`: the game's ZR comes from the ZL button) must make a
// permutation together, or the whole layout falls back to the game's own.
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

// The start perks as mods/admin's zm_startperks takes them: "jugg,revive" ("" for none).
std::string Launcher_StartPerkList(const LauncherSettings &s);

// "host:port", the address cl_autoConnect gets.
std::string Launcher_ServerAddress(const LauncherSettings &s);

// The gfx.cfg value switch_main.cpp turns into BO1_GFX_BACKEND.
const char *Launcher_BackendName(LauncherBackend backend);

// The Switch's label of a pad control (Bo1PadControl): "A", "ZR", "+", "Haut"...
const char *Launcher_PadButtonLabel(int control);
// What a game control does with the Switch build's binds (buttons_default.cfg, then Switch_ApplyPadLayout in
// qcommon/cmd.cpp): "Tirer", "Viser"...
const char *Launcher_PadActionLabel(int control);
const char *Launcher_TexFilterLabel(LauncherTexFilter filter);
