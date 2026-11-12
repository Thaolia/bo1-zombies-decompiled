#include "launcher_config.h"

#include <cstdlib>
#include <cstring>

// The labels of tools/mod-launcher.hta's MAPS, Kino first: it is the default and the map every console test used.
// Dead Ops Arcade is not in the .hta's list.
const LauncherMap kLauncherMaps[] = {
    {"zombie_theater", "Kino der Toten"},
    {"zombie_pentagon", "Five"},
    {"zombie_cosmodrome", "Ascension"},
    {"zombie_coast", "Call of the Dead"},
    {"zombie_temple", "Shangri-La"},
    {"zombie_moon", "Moon"},
    {"zombie_cod5_prototype", "Nacht der Untoten"},
    {"zombie_cod5_asylum", "Verruckt"},
    {"zombie_cod5_sumpf", "Shi No Numa"},
    {"zombie_cod5_factory", "Der Riese"},
    {"zombietron", "Dead Ops Arcade"},
};
const int kLauncherMapCount = int(sizeof(kLauncherMaps) / sizeof(kLauncherMaps[0]));

const int kLauncherStartPoints[] = {0, 1000, 2500, 5000, 10000, 25000, 50000, 100000};
const int kLauncherStartPointCount = int(sizeof(kLauncherStartPoints) / sizeof(kLauncherStartPoints[0]));
const LauncherPerk kLauncherPerks[] = {
    {"jugg", "Juggernog"},   {"revive", "Quick Revive"}, {"speed", "Speed Cola"},       {"doubletap", "Double Tap"},
    {"staminup", "Stamin-Up"}, {"phd", "PhD Flopper"},   {"deadshot", "Deadshot Daiquiri"}, {"mule", "Mule Kick"},
};
const int kLauncherPerkCount = int(sizeof(kLauncherPerks) / sizeof(kLauncherPerks[0]));

namespace
{
const char *const kBackendNames[LAUNCHER_BACKEND_COUNT] = {"deko3d", "gl"};

struct TexFilterInfo
{
    const char *key;   // menu.cfg value
    const char *label; // menu text
    int anisoMax;      // r_texFilterAnisoMax
    int mipMode;       // r_texFilterMipMode: 0 Unchanged, 1 Force Trilinear, 2 Force Bilinear
};

const TexFilterInfo kTexFilters[LAUNCHER_TEXFILTER_COUNT] = {
    {"aniso16", "Anisotrope 16x", 16, 0}, {"aniso8", "Anisotrope 8x", 8, 0},
    {"aniso4", "Anisotrope 4x", 4, 0},    {"aniso2", "Anisotrope 2x", 2, 0},
    {"trilinear", "Trilineaire", 1, 1},   {"bilinear", "Bilineaire", 1, 2},
};

struct PadInfo
{
    const char *key;    // menu.cfg: pad_<key>=<key>
    const char *label;  // the button as printed on the Switch's pads
    const char *action; // what the game control does (ASCII: the console font has no accents)
};

// Bo1PadControl order. The actions are the Switch build's binds: retail buttons_default.cfg, then
// Switch_ApplyPadLayout (qcommon/cmd.cpp: Y reloads, L switches weapon, d-pad down throws the special grenade).
static_assert(BO1_PAD_COUNT == 16, "kPads lists every control");
const PadInfo kPads[BO1_PAD_COUNT] = {
    {"a", "A", "Sauter / valider"},
    {"b", "B", "S'accroupir / retour"},
    {"x", "X", "Utiliser / recharger"},
    {"y", "Y", "Recharger"},
    {"l", "L", "Changer d'arme"},
    {"r", "R", "Grenade"},
    {"zl", "ZL", "Viser"},
    {"zr", "ZR", "Tirer"},
    {"plus", "+", "Pause (menu du jeu)"},
    {"minus", "-", "Tableau des scores"},
    {"lstick", "Stick G", "Sprint"},
    {"rstick", "Stick D", "Couteau"},
    {"up", "Haut", "Emplacement 1"},
    {"down", "Bas", "Grenade speciale"},
    {"left", "Gauche", "Emplacement 3"},
    {"right", "Droite", "Emplacement 4"},
};

int PadIndex(const std::string &key)
{
    for (int i = 0; i < BO1_PAD_COUNT; ++i)
        if (key == kPads[i].key)
            return i;
    return -1;
}

bool ParseBool(const std::string &value, bool *out)
{
    if (value == "1")
        *out = true;
    else if (value == "0")
        *out = false;
    else
        return false;
    return true;
}

std::string Trim(const std::string &s)
{
    size_t b = 0, e = s.size();
    while (b < e && std::strchr(" \t\r\n", s[b]))
        ++b;
    while (e > b && std::strchr(" \t\r\n", s[e - 1]))
        --e;
    return s.substr(b, e - b);
}

bool AddressChar(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '-';
}

void Apply(LauncherSettings *s, const std::string &key, const std::string &value)
{
    if (key == "mode")
    {
        if (value == "solo" || value == "join")
            s->join = value == "join";
    }
    else if (key == "server")
    {
        if (Launcher_ValidServerHost(value))
            s->serverHost = value;
    }
    else if (key == "port")
        Launcher_ParsePort(value, &s->serverPort);
    else if (key == "name")
    {
        if (Launcher_ValidPlayerName(value))
            s->playerName = value;
    }
    else if (key == "map")
    {
        for (int i = 0; i < kLauncherMapCount; ++i)
            if (value == kLauncherMaps[i].bsp)
                s->map = i;
    }
    else if (key == "aimassist")
        ParseBool(value, &s->aimAssist);
    else if (key == "aim_slowdown")
        ParseBool(value, &s->aimSlowdown);
    else if (key == "aim_lockon")
        ParseBool(value, &s->aimLockon);
    else if (key == "aim_autoaim")
        ParseBool(value, &s->aimAutoaim);
    else if (key == "god")
        ParseBool(value, &s->god);
    else if (key == "zinfo")
        ParseBool(value, &s->tabInfo);
    else if (key == "start_points")
    {
        for (int i = 0; i < kLauncherStartPointCount; ++i)
            if (value == std::to_string(kLauncherStartPoints[i]))
                s->startPoints = kLauncherStartPoints[i];
    }
    else if (key == "start_perks")
    {
        // comma-separated names; unknown ones are skipped
        s->startPerks = 0;
        size_t pos = 0;
        while (pos <= value.size())
        {
            size_t end = value.find(',', pos);
            if (end == std::string::npos)
                end = value.size();
            const std::string name = Trim(value.substr(pos, end - pos));
            for (int i = 0; i < kLauncherPerkCount; ++i)
                if (name == kLauncherPerks[i].key)
                    s->startPerks |= 1u << i;
            pos = end + 1;
        }
    }
    else if (key == "backend")
    {
        for (int i = 0; i < LAUNCHER_BACKEND_COUNT; ++i)
            if (value == kBackendNames[i])
                s->backend = LauncherBackend(i);
    }
    else if (key == "start_power")
        ParseBool(value, &s->startPower);
    else if (key == "resolution")
    {
        if (value == "480" || value == "720")
            s->height = std::atoi(value.c_str());
    }
    else if (key == "maxfps")
    {
        if (value == "30" || value == "60")
            s->maxFps = std::atoi(value.c_str());
    }
    else if (key == "shadows")
        ParseBool(value, &s->shadows);
    else if (key == "fpscounter")
        ParseBool(value, &s->fpsCounter);
    else if (key.compare(0, 4, "pad_") == 0)
    {
        // checked as a whole once every line is read (Launcher_ParseSettings)
        const int control = PadIndex(key.substr(4));
        const int physical = PadIndex(value);
        if (control >= 0 && physical >= 0)
            s->pad.source[control] = (unsigned char)physical;
    }
    else if (key == "texfilter")
    {
        for (int i = 0; i < LAUNCHER_TEXFILTER_COUNT; ++i)
            if (value == kTexFilters[i].key)
                s->texFilter = LauncherTexFilter(i);
    }
}
} // namespace

LauncherSettings Launcher_ParseSettings(const std::string &text)
{
    LauncherSettings s;
    size_t pos = 0;
    while (pos < text.size())
    {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos)
            end = text.size();
        const std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        const size_t eq = line.find('=');
        if (eq != std::string::npos)
            Apply(&s, Trim(line.substr(0, eq)), Trim(line.substr(eq + 1)));
    }
    // A layout that gives two game controls one button would leave another without any (perhaps the menus' A).
    if (!Bo1_PadRemapValid(s.pad))
        Bo1_PadRemapIdentity(&s.pad);
    return s;
}

std::string Launcher_FormatSettings(const LauncherSettings &s)
{
    std::string out;
    out += std::string("mode=") + (s.join ? "join" : "solo") + "\n";
    out += "server=" + s.serverHost + "\n";
    out += "port=" + std::to_string(s.serverPort) + "\n";
    out += "name=" + s.playerName + "\n";
    out += "map=" + std::string(kLauncherMaps[s.map].bsp) + "\n";
    out += std::string("aimassist=") + (s.aimAssist ? "1" : "0") + "\n";
    out += std::string("aim_slowdown=") + (s.aimSlowdown ? "1" : "0") + "\n";
    out += std::string("aim_lockon=") + (s.aimLockon ? "1" : "0") + "\n";
    out += std::string("aim_autoaim=") + (s.aimAutoaim ? "1" : "0") + "\n";
    out += std::string("god=") + (s.god ? "1" : "0") + "\n";
    out += std::string("zinfo=") + (s.tabInfo ? "1" : "0") + "\n";
    out += "start_points=" + std::to_string(s.startPoints) + "\n";
    out += "start_perks=" + Launcher_StartPerkList(s) + "\n";
    out += std::string("backend=") + kBackendNames[s.backend] + "\n";
    out += std::string("start_power=") + (s.startPower ? "1" : "0") + "\n";
    out += "resolution=" + std::to_string(s.height) + "\n";
    out += "maxfps=" + std::to_string(s.maxFps) + "\n";
    out += std::string("shadows=") + (s.shadows ? "1" : "0") + "\n";
    out += std::string("fpscounter=") + (s.fpsCounter ? "1" : "0") + "\n";
    out += std::string("texfilter=") + kTexFilters[s.texFilter].key + "\n";
    for (int i = 0; i < BO1_PAD_COUNT; ++i)
        out += std::string("pad_") + kPads[i].key + "=" + kPads[s.pad.source[i]].key + "\n";
    return out;
}

std::string Launcher_BuildLaunchLine(const LauncherSettings &s, const std::string &extra)
{
    const TexFilterInfo &filter = kTexFilters[s.texFilter];
    const bool join = s.join && Launcher_ValidServerHost(s.serverHost);
    std::string line = "+set bo1_zombies 1";
    // The three switches only act while bo1_aimassist is on (AimAssist_PlayerDisabledAutoAim), so they keep their
    // values when the assist is off and come back as they were.
    line += std::string(" +set bo1_aimassist ") + (s.aimAssist ? "1" : "0");
    line += std::string(" +set aim_slowdown_enabled ") + (s.aimSlowdown ? "1" : "0");
    line += std::string(" +set aim_lockon_enabled ") + (s.aimLockon ? "1" : "0");
    line += std::string(" +set aim_autoaim_enabled ") + (s.aimAutoaim ? "1" : "0");
    // Not the `god` command: it is a toggle that ClientSpawn clears. This dvar puts the god-mode bit back on every
    // playing client around each server frame (G_SP_HeadlessTestClientFrame).
    // Only a local server reads it: the dedicated server decides god mode for its players.
    line += std::string(" +set bo1_testclient_god ") + (s.god && !join ? "1" : "0");
    // The dedicated server stacks mods/zinfo for all its players itself (FS_ModStackNames), and decides its players'
    // start. Before the extra, so a launch_extra.cfg that sets its own fs_mods wins. mods/admin gives the start kit.
    const bool kit = !join && (s.startPoints > 0 || s.startPerks != 0 || s.startPower);
    if (!join && (s.tabInfo || kit))
        line += std::string(" +set fs_mods") + (s.tabInfo ? " zinfo" : "") + (kit ? " admin" : "");
    if (kit && s.startPower)
        line += " +set zm_startpower 1";
    if (kit && s.startPoints > 0)
        line += " +set zm_startpoints " + std::to_string(s.startPoints);
    if (kit && s.startPerks != 0)
        line += " +set zm_startperks " + Launcher_StartPerkList(s);
    // The game runs windowed on the console (r_fullscreen 0), so r_customMode sizes its back buffer; the swapchain
    // takes that size and the console's compositor scales the frame to the screen.
    line += std::string(" +set r_fullscreen 0 +set r_customMode ") + (s.height == 480 ? "854x480" : "1280x720");
    line += " +set com_maxfps " + std::to_string(s.maxFps);
    line += std::string(" +set sm_enable ") + (s.shadows ? "1" : "0");
    line += std::string(" +set bo1_fpscounter ") + (s.fpsCounter ? "1" : "0");
    line += " +set r_texFilterAnisoMax " + std::to_string(filter.anisoMax);
    line += " +set r_texFilterMipMode " + std::to_string(filter.mipMode);
    if (join && Launcher_ValidPlayerName(s.playerName))
        line += " +set name " + s.playerName;
    if (!extra.empty())
        line += " " + extra;
    if (join)
    {
        // The player typed the address, so a server beyond the LAN is meant. cl_autoConnect, not +connect: the menu's
        // own load undoes a connect from the command line, and it retries when the server is not up yet or goes away.
        line += " +set cl_allowWanConnect 1 +set cl_autoConnect " + Launcher_ServerAddress(s);
        return line;
    }
    line += " +devmap " + std::string(kLauncherMaps[s.map].bsp);
    return line;
}

bool Launcher_ValidServerHost(const std::string &host)
{
    if (host.empty() || host.size() > 63)
        return false;
    for (char c : host)
        if (!AddressChar(c))
            return false;
    return true;
}

bool Launcher_ParsePort(const std::string &text, int *port)
{
    if (text.empty() || text.size() > 5)
        return false;
    for (char c : text)
        if (c < '0' || c > '9')
            return false;
    const int value = std::atoi(text.c_str());
    if (value < 1 || value > 65535)
        return false;
    *port = value;
    return true;
}

std::string Launcher_StartPerkList(const LauncherSettings &s)
{
    std::string list;
    for (int i = 0; i < kLauncherPerkCount; ++i)
        if (s.startPerks & (1u << i))
            list += std::string(list.empty() ? "" : ",") + kLauncherPerks[i].key;
    return list;
}

std::string Launcher_ServerAddress(const LauncherSettings &s)
{
    return s.serverHost + ":" + std::to_string(s.serverPort);
}

bool Launcher_ValidPlayerName(const std::string &name)
{
    if (name.empty() || name.size() > 15)
        return false;
    for (char c : name)
        if (!AddressChar(c) && c != '_')
            return false;
    return true;
}

const char *Launcher_BackendName(LauncherBackend backend)
{
    return kBackendNames[backend];
}

const char *Launcher_TexFilterLabel(LauncherTexFilter filter)
{
    return kTexFilters[filter].label;
}

const char *Launcher_PadButtonLabel(int control)
{
    return control >= 0 && control < BO1_PAD_COUNT ? kPads[control].label : "?";
}

const char *Launcher_PadActionLabel(int control)
{
    return control >= 0 && control < BO1_PAD_COUNT ? kPads[control].action : "?";
}
