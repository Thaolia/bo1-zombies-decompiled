#include "launcher_config.h"

#include <cstdlib>
#include <cstring>

// The labels of tools/mod-launcher.hta's MAPS, Kino first: it is the default and the map every console test used.
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
};
const int kLauncherMapCount = int(sizeof(kLauncherMaps) / sizeof(kLauncherMaps[0]));

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

void Apply(LauncherSettings *s, const std::string &key, const std::string &value)
{
    if (key == "map")
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
    else if (key == "backend")
    {
        for (int i = 0; i < LAUNCHER_BACKEND_COUNT; ++i)
            if (value == kBackendNames[i])
                s->backend = LauncherBackend(i);
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
    return s;
}

std::string Launcher_FormatSettings(const LauncherSettings &s)
{
    std::string out;
    out += "map=" + std::string(kLauncherMaps[s.map].bsp) + "\n";
    out += std::string("aimassist=") + (s.aimAssist ? "1" : "0") + "\n";
    out += std::string("aim_slowdown=") + (s.aimSlowdown ? "1" : "0") + "\n";
    out += std::string("aim_lockon=") + (s.aimLockon ? "1" : "0") + "\n";
    out += std::string("aim_autoaim=") + (s.aimAutoaim ? "1" : "0") + "\n";
    out += std::string("god=") + (s.god ? "1" : "0") + "\n";
    out += std::string("backend=") + kBackendNames[s.backend] + "\n";
    out += "maxfps=" + std::to_string(s.maxFps) + "\n";
    out += std::string("shadows=") + (s.shadows ? "1" : "0") + "\n";
    out += std::string("fpscounter=") + (s.fpsCounter ? "1" : "0") + "\n";
    out += std::string("texfilter=") + kTexFilters[s.texFilter].key + "\n";
    return out;
}

std::string Launcher_BuildLaunchLine(const LauncherSettings &s, const std::string &extra)
{
    const TexFilterInfo &filter = kTexFilters[s.texFilter];
    std::string line = "+set bo1_zombies 1";
    // The three switches only act while bo1_aimassist is on (AimAssist_PlayerDisabledAutoAim), so they keep their
    // values when the assist is off and come back as they were.
    line += std::string(" +set bo1_aimassist ") + (s.aimAssist ? "1" : "0");
    line += std::string(" +set aim_slowdown_enabled ") + (s.aimSlowdown ? "1" : "0");
    line += std::string(" +set aim_lockon_enabled ") + (s.aimLockon ? "1" : "0");
    line += std::string(" +set aim_autoaim_enabled ") + (s.aimAutoaim ? "1" : "0");
    // Not the `god` command: it is a toggle that ClientSpawn clears. This dvar puts the god-mode bit back on every
    // playing client around each server frame (G_SP_HeadlessTestClientFrame).
    line += std::string(" +set bo1_testclient_god ") + (s.god ? "1" : "0");
    line += " +set com_maxfps " + std::to_string(s.maxFps);
    line += std::string(" +set sm_enable ") + (s.shadows ? "1" : "0");
    line += std::string(" +set bo1_fpscounter ") + (s.fpsCounter ? "1" : "0");
    line += " +set r_texFilterAnisoMax " + std::to_string(filter.anisoMax);
    line += " +set r_texFilterMipMode " + std::to_string(filter.mipMode);
    if (!extra.empty())
        line += " " + extra;
    line += " +devmap " + std::string(kLauncherMaps[s.map].bsp);
    return line;
}

const char *Launcher_BackendName(LauncherBackend backend)
{
    return kBackendNames[backend];
}

const char *Launcher_TexFilterLabel(LauncherTexFilter filter)
{
    return kTexFilters[filter].label;
}
