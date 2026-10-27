// The Switch launcher's settings and start-up line (src/platform/switch/launcher_config.cpp): pure, so it runs on the
// host. ./launcher_config_test in the build folder.

#include <platform/switch/launcher_config.h>

#include <cstdio>
#include <string>

namespace
{
int g_failures = 0;

void Check(bool ok, const char *what)
{
    if (!ok)
    {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

void CheckLine(const std::string &got, const std::string &want, const char *what)
{
    if (got != want)
        std::printf("  got:  %s\n  want: %s\n", got.c_str(), want.c_str());
    Check(got == want, what);
}

bool SameSettings(const LauncherSettings &a, const LauncherSettings &b)
{
    return a.map == b.map && a.aimAssist == b.aimAssist && a.aimSlowdown == b.aimSlowdown &&
           a.aimLockon == b.aimLockon && a.aimAutoaim == b.aimAutoaim && a.god == b.god && a.backend == b.backend &&
           a.maxFps == b.maxFps && a.shadows == b.shadows && a.fpsCounter == b.fpsCounter && a.texFilter == b.texFilter;
}
} // namespace

int main()
{
    const LauncherSettings defaults;
    Check(std::string(kLauncherMaps[defaults.map].bsp) == "zombie_theater", "the default map is Kino");
    Check(kLauncherMapCount == 10, "the ten zombies maps are listed");
    Check(SameSettings(Launcher_ParseSettings(""), defaults), "an empty menu.cfg gives the defaults");

    CheckLine(Launcher_BuildLaunchLine(defaults, ""),
              "+set bo1_zombies 1 +set bo1_aimassist 1 +set aim_slowdown_enabled 1 +set aim_lockon_enabled 1"
              " +set aim_autoaim_enabled 0 +set bo1_testclient_god 0 +set com_maxfps 60 +set sm_enable 1"
              " +set bo1_fpscounter 1 +set r_texFilterAnisoMax 16 +set r_texFilterMipMode 0 +devmap zombie_theater",
              "the default line");

    LauncherSettings custom;
    custom.map = 5; // Moon
    custom.aimAssist = false;
    custom.aimAutoaim = true;
    custom.god = true;
    custom.backend = LAUNCHER_BACKEND_GL;
    custom.maxFps = 30;
    custom.shadows = false;
    custom.fpsCounter = false;
    custom.texFilter = LAUNCHER_TEXFILTER_BILINEAR;
    CheckLine(Launcher_BuildLaunchLine(custom, "+set bo1_frameperf 200"),
              "+set bo1_zombies 1 +set bo1_aimassist 0 +set aim_slowdown_enabled 1 +set aim_lockon_enabled 1"
              " +set aim_autoaim_enabled 1 +set bo1_testclient_god 1 +set com_maxfps 30 +set sm_enable 0"
              " +set bo1_fpscounter 0 +set r_texFilterAnisoMax 1 +set r_texFilterMipMode 2 +set bo1_frameperf 200"
              " +devmap zombie_moon",
              "a custom line, with the extra arguments before +devmap");
    Check(std::string(Launcher_BackendName(custom.backend)) == "gl", "the GL backend's gfx.cfg value");
    Check(std::string(Launcher_BackendName(defaults.backend)) == "deko3d", "the deko3d backend's gfx.cfg value");

    LauncherSettings trilinear;
    trilinear.texFilter = LAUNCHER_TEXFILTER_TRILINEAR;
    const std::string triLine = Launcher_BuildLaunchLine(trilinear, "");
    Check(triLine.find(" +set r_texFilterAnisoMax 1 +set r_texFilterMipMode 1 ") != std::string::npos,
          "trilinear filtering is anisotropy 1 with forced trilinear mips");

    Check(SameSettings(Launcher_ParseSettings(Launcher_FormatSettings(custom)), custom),
          "menu.cfg round-trips a custom setting");
    Check(SameSettings(Launcher_ParseSettings(Launcher_FormatSettings(defaults)), defaults),
          "menu.cfg round-trips the defaults");

    const LauncherSettings bad = Launcher_ParseSettings("map=zombie_nowhere\r\nmaxfps=45\nshadows=yes\n"
                                                        "backend=vulkan\ntexfilter=aniso3\nunknown=1\nno equals sign\n");
    Check(SameSettings(bad, defaults), "unknown maps, invalid values and unknown keys keep the defaults");

    const LauncherSettings crlf = Launcher_ParseSettings(" map = zombie_pentagon \r\ngod=1\r\nmaxfps=30\r\n");
    Check(std::string(kLauncherMaps[crlf.map].bsp) == "zombie_pentagon" && crlf.god && crlf.maxFps == 30,
          "spaces and CRLF line ends are tolerated");

    for (int i = 0; i < kLauncherMapCount; ++i)
    {
        LauncherSettings s;
        s.map = i;
        const std::string line = Launcher_BuildLaunchLine(s, "+set x 1");
        const std::string tail = " +devmap " + std::string(kLauncherMaps[i].bsp);
        Check(line.size() > tail.size() && line.compare(line.size() - tail.size(), tail.size(), tail) == 0,
              "+devmap <map> ends every line");
    }

    if (g_failures)
    {
        std::printf("launcher_config_test: %d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("launcher_config_test: all checks passed\n");
    return 0;
}
