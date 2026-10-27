// The Switch launcher's settings and start-up line (src/platform/switch/launcher_config.cpp): pure, so it runs on the
// host. ./launcher_config_test in the build folder.

#include <platform/launch_cfg.h>
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
    return a.join == b.join && a.serverHost == b.serverHost && a.serverPort == b.serverPort && a.playerName == b.playerName && a.map == b.map && a.aimAssist == b.aimAssist && a.aimSlowdown == b.aimSlowdown &&
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

    LauncherSettings join;
    join.join = true;
    join.serverHost = "192.168.1.20";
    join.serverPort = 29070;
    join.playerName = "Switch1";
    join.god = true;
    CheckLine(Launcher_BuildLaunchLine(join, "+set bo1_frameperf 200"),
              "+set bo1_zombies 1 +set bo1_aimassist 1 +set aim_slowdown_enabled 1 +set aim_lockon_enabled 1"
              " +set aim_autoaim_enabled 0 +set bo1_testclient_god 0 +set com_maxfps 60 +set sm_enable 1"
              " +set bo1_fpscounter 1 +set r_texFilterAnisoMax 16 +set r_texFilterMipMode 0 +set name Switch1"
              " +set bo1_frameperf 200 +set cl_allowWanConnect 1 +set cl_autoConnect 192.168.1.20:29070",
              "a join: the game's menu, then cl_autoConnect, no +devmap and no local god mode");
    Check(SameSettings(Launcher_ParseSettings(Launcher_FormatSettings(join)), join), "menu.cfg round-trips a join");

    LauncherSettings noServer;
    noServer.join = true;
    noServer.god = true;
    CheckLine(Launcher_BuildLaunchLine(noServer, ""),
              "+set bo1_zombies 1 +set bo1_aimassist 1 +set aim_slowdown_enabled 1 +set aim_lockon_enabled 1"
              " +set aim_autoaim_enabled 0 +set bo1_testclient_god 1 +set com_maxfps 60 +set sm_enable 1"
              " +set bo1_fpscounter 1 +set r_texFilterAnisoMax 16 +set r_texFilterMipMode 0 +devmap zombie_theater",
              "a join without a server address starts the solo game");

    LauncherSettings noName = join;
    noName.playerName.clear();
    Check(Launcher_BuildLaunchLine(noName, "").find("+set name") == std::string::npos,
          "no name keeps the game's own");

    const LauncherSettings badJoin =
        Launcher_ParseSettings("mode=online\nserver=1.2.3.4 +set x\nport=70000\nname=Bad Name\n");
    Check(!badJoin.join && badJoin.serverHost.empty() && badJoin.serverPort == 3074 && badJoin.playerName.empty(),
          "an unknown mode, a host with a blank, a port past 65535 and a name with a blank keep the defaults");

    Check(Launcher_ValidServerHost("192.168.1.20") && Launcher_ValidServerHost("my-server.lan"),
          "an IPv4 address and a host name are valid hosts");
    Check(!Launcher_ValidServerHost("") && !Launcher_ValidServerHost("1.2.3.4:3074") &&
              !Launcher_ValidServerHost("1.2.3.4;quit") && !Launcher_ValidServerHost(std::string(64, '1')),
          "empty, with a port, with a semicolon and 64 characters are refused hosts");
    int port = 3074;
    Check(Launcher_ParsePort("29070", &port) && port == 29070, "a port parses");
    Check(!Launcher_ParsePort("", &port) && !Launcher_ParsePort("0", &port) && !Launcher_ParsePort("65536", &port) &&
              !Launcher_ParsePort("30a", &port) && !Launcher_ParsePort("123456", &port) && port == 29070,
          "empty, 0, past 65535, non-digits and six digits are refused ports, the value kept");
    Check(Launcher_ValidPlayerName("Player_2") && !Launcher_ValidPlayerName("") &&
              !Launcher_ValidPlayerName("sixteen_letters_") && !Launcher_ValidPlayerName("a\"b"),
          "player names: 1-15 safe characters");

    char cfg[64] = "\r\n  +set a 1\r\n+set\tb 2\n\n  ";
    const size_t cfgLen = LaunchCfg_Normalize(cfg, std::char_traits<char>::length(cfg));
    Check(cfgLen == 18 && std::string(cfg) == "+set a 1  +set b 2", "launch.cfg: line breaks read as blanks, ends trimmed");
    char blank[8] = " \r\n\t ";
    Check(LaunchCfg_Normalize(blank, 5) == 0 && blank[0] == 0, "launch.cfg: a blank file is empty");

    if (g_failures)
    {
        std::printf("launcher_config_test: %d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("launcher_config_test: all checks passed\n");
    return 0;
}
