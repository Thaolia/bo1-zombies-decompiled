// The Switch launcher's settings and start-up line (src/platform/switch/launcher_config.cpp): pure, so it runs on the
// host. ./launcher_config_test in the build folder.

#include <platform/launch_cfg.h>
#include <platform/switch/launcher_config.h>

#include <cstdio>
#include <cstring>
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
           a.aimLockon == b.aimLockon && a.aimAutoaim == b.aimAutoaim && a.god == b.god && a.tabInfo == b.tabInfo && a.backend == b.backend &&
           a.maxFps == b.maxFps && a.shadows == b.shadows && a.fpsCounter == b.fpsCounter &&
           a.texFilter == b.texFilter && a.startPoints == b.startPoints && a.startPerks == b.startPerks &&
           a.startPower == b.startPower && a.startPap == b.startPap && a.farRespawn == b.farRespawn && a.height == b.height &&
           std::memcmp(&a.pad, &b.pad, sizeof(a.pad)) == 0;
}
} // namespace

int main()
{
    const LauncherSettings defaults;
    Check(std::string(kLauncherMaps[defaults.map].bsp) == "zombie_theater", "the default map is Kino");
    Check(kLauncherMapCount == 12, "the ten zombies maps, Dead Ops Arcade and the native Nuketown are listed");
    Check(std::string(kLauncherMaps[10].bsp) == "zombietron", "Dead Ops Arcade follows the ten zombies maps");
    const int nuketown = kLauncherMapCount - 1;
    Check(Launcher_MapKey(nuketown) == "mapkit:bo2_zm_nuked_bsp" && Launcher_MapKey(1) == "zombie_pentagon",
          "the native Nuketown's menu.cfg key is not Five's, whose bsp it shares");
    Check(Launcher_MapFileCount(0) == 0 && Launcher_MapFileCount(nuketown) == 3 &&
              Launcher_MapFile(nuketown, 0) == "mods/mapkit/layouts/bo2_zm_nuked_bsp.json" &&
              Launcher_MapFile(nuketown, 1) == "usermaps/zombie_bo2nuked/zombie_bo2nuked.ff" &&
              Launcher_MapFile(nuketown, 2) == "main/maps/zombie_bo2nuked.d3dbsp",
          "the native Nuketown needs its layout, zone and BSP on the SD card; Kino needs nothing");
    Check(SameSettings(Launcher_ParseSettings(""), defaults), "an empty menu.cfg gives the defaults");

    CheckLine(Launcher_BuildLaunchLine(defaults, ""),
              "+set bo1_zombies 1 +set bo1_aimassist 1 +set aim_slowdown_enabled 1 +set aim_lockon_enabled 1"
              " +set aim_autoaim_enabled 0 +set bo1_testclient_god 0 +set fs_mods zinfo +set r_fullscreen 0"
              " +set r_customMode 1280x720 +set com_maxfps 60"
              " +set sm_enable 1 +set bo1_fpscounter 1 +set r_texFilterAnisoMax 16 +set r_texFilterMipMode 0"
              " +devmap zombie_theater",
              "the default line, with the Tab zombie counter");

    LauncherSettings custom;
    custom.map = 5; // Moon
    custom.aimAssist = false;
    custom.aimAutoaim = true;
    custom.god = true;
    custom.tabInfo = false;
    custom.backend = LAUNCHER_BACKEND_GL;
    custom.maxFps = 30;
    custom.height = 480;
    custom.shadows = false;
    custom.fpsCounter = false;
    custom.texFilter = LAUNCHER_TEXFILTER_BILINEAR;
    CheckLine(Launcher_BuildLaunchLine(custom, "+set bo1_frameperf 200"),
              "+set bo1_zombies 1 +set bo1_aimassist 0 +set aim_slowdown_enabled 1 +set aim_lockon_enabled 1"
              " +set aim_autoaim_enabled 1 +set bo1_testclient_god 1 +set r_fullscreen 0"
              " +set r_customMode 854x480 +set com_maxfps 30 +set sm_enable 0"
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

    const LauncherSettings bad = Launcher_ParseSettings("map=zombie_nowhere\r\nmaxfps=45\nshadows=yes\nresolution=1080\n"
                                                        "backend=vulkan\ntexfilter=aniso3\nunknown=1\nno equals sign\n");
    Check(SameSettings(bad, defaults), "unknown maps, invalid values and unknown keys keep the defaults");

    const LauncherSettings crlf = Launcher_ParseSettings(" map = zombie_pentagon \r\ngod=1\r\nmaxfps=30\r\nzinfo=0\r\n");
    Check(std::string(kLauncherMaps[crlf.map].bsp) == "zombie_pentagon" && crlf.god && crlf.maxFps == 30 && !crlf.tabInfo,
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

    LauncherSettings nuke;
    nuke.map = nuketown;
    Check(Launcher_ParseSettings(Launcher_FormatSettings(nuke)).map == nuketown, "menu.cfg keeps the native Nuketown");
    CheckLine(Launcher_BuildLaunchLine(nuke, ""),
              "+set bo1_zombies 1 +set bo1_aimassist 1 +set aim_slowdown_enabled 1 +set aim_lockon_enabled 1"
              " +set aim_autoaim_enabled 0 +set bo1_testclient_god 0 +set fs_game mods/mapkit"
              " +set bo1_mod_mapkit bo2_zm_nuked_bsp +set fs_mods mapkit zinfo +set bo1_mod_zones zombie_bo2nuked"
              " +set bo1_bspworld maps/zombie_bo2nuked.d3dbsp +set r_fullscreen 0 +set r_customMode 1280x720"
              " +set com_maxfps 60 +set sm_enable 1 +set bo1_fpscounter 1 +set r_texFilterAnisoMax 16"
              " +set r_texFilterMipMode 0 +devmap zombie_pentagon",
              "the native Nuketown: ZmClient's MapKit layout on Five, its zone and BSP world");
    nuke.tabInfo = false;
    nuke.startPoints = 5000;
    const std::string nukeKit = Launcher_BuildLaunchLine(nuke, "");
    Check(nukeKit.find(" +set fs_mods mapkit admin +set bo1_mod_zones") != std::string::npos &&
              nukeKit.find(" +set zm_startpoints 5000") != std::string::npos,
          "the start kit stacks mods/admin after mapkit");
    nuke.tabInfo = false;
    nuke.startPoints = 0;
    Check(Launcher_BuildLaunchLine(nuke, "").find("fs_mods") == std::string::npos,
          "without Tab info nor kit the native Nuketown sets fs_game only");
    nuke.join = true;
    nuke.serverHost = "192.168.1.10";
    Check(Launcher_BuildLaunchLine(nuke, "").find("mapkit") == std::string::npos,
          "a join ignores the native Nuketown's MapKit settings");

    LauncherSettings join;
    join.join = true;
    join.serverHost = "192.168.1.20";
    join.serverPort = 29070;
    join.playerName = "Switch1";
    join.god = true;
    CheckLine(Launcher_BuildLaunchLine(join, "+set bo1_frameperf 200"),
              "+set bo1_zombies 1 +set bo1_aimassist 1 +set aim_slowdown_enabled 1 +set aim_lockon_enabled 1"
              " +set aim_autoaim_enabled 0 +set bo1_testclient_god 0 +set r_fullscreen 0"
              " +set r_customMode 1280x720 +set com_maxfps 60 +set sm_enable 1"
              " +set bo1_fpscounter 1 +set r_texFilterAnisoMax 16 +set r_texFilterMipMode 0 +set name Switch1"
              " +set bo1_frameperf 200 +set cl_allowWanConnect 1 +set cl_autoConnect 192.168.1.20:29070",
              "a join: the game's menu, then cl_autoConnect, no +devmap, no local god mode, no fs_mods (the server's)");
    Check(SameSettings(Launcher_ParseSettings(Launcher_FormatSettings(join)), join), "menu.cfg round-trips a join");

    LauncherSettings noServer;
    noServer.join = true;
    noServer.god = true;
    CheckLine(Launcher_BuildLaunchLine(noServer, ""),
              "+set bo1_zombies 1 +set bo1_aimassist 1 +set aim_slowdown_enabled 1 +set aim_lockon_enabled 1"
              " +set aim_autoaim_enabled 0 +set bo1_testclient_god 1 +set fs_mods zinfo +set r_fullscreen 0"
              " +set r_customMode 1280x720 +set com_maxfps 60"
              " +set sm_enable 1 +set bo1_fpscounter 1 +set r_texFilterAnisoMax 16 +set r_texFilterMipMode 0"
              " +devmap zombie_theater",
              "a join without a server address starts the solo game");

    // The start kit: mods/admin stacked after zinfo, its two dvars, perks in kLauncherPerks order.
    LauncherSettings kit;
    kit.startPoints = 50000;
    kit.startPerks = (1u << 1) | (1u << 0); // revive, jugg
    const std::string kitLine = Launcher_BuildLaunchLine(kit, "");
    Check(kitLine.find(" +set fs_mods zinfo admin +set zm_startpoints 50000 +set zm_startperks jugg,revive ") !=
              std::string::npos,
          "a start kit stacks mods/admin and sets zm_startpoints / zm_startperks");
    Check(SameSettings(Launcher_ParseSettings(Launcher_FormatSettings(kit)), kit), "menu.cfg round-trips a start kit");
    LauncherSettings kitNoTab = kit;
    kitNoTab.tabInfo = false;
    kitNoTab.startPoints = 0;
    const std::string noTabLine = Launcher_BuildLaunchLine(kitNoTab, "");
    Check(noTabLine.find(" +set fs_mods admin +set zm_startperks jugg,revive ") != std::string::npos &&
              noTabLine.find("zm_startpoints") == std::string::npos,
          "perks alone: fs_mods admin, no zm_startpoints");
    LauncherSettings kitJoin = kit;
    kitJoin.join = true;
    kitJoin.serverHost = "192.168.1.20";
    Check(Launcher_BuildLaunchLine(kitJoin, "").find("zm_start") == std::string::npos,
          "a join leaves the start to the server");
    LauncherSettings power;
    power.tabInfo = false;
    power.startPower = true;
    const std::string powerLine = Launcher_BuildLaunchLine(power, "");
    Check(powerLine.find(" +set fs_mods admin +set zm_startpower 1 +set r_fullscreen 0 ") != std::string::npos &&
              Launcher_BuildLaunchLine(defaults, "").find("zm_startpower") == std::string::npos,
          "the start power alone stacks mods/admin and sets zm_startpower, off by default");
    Check(SameSettings(Launcher_ParseSettings(Launcher_FormatSettings(power)), power),
          "menu.cfg round-trips the start power");
    LauncherSettings pap;
    pap.tabInfo = false;
    pap.startPap = true;
    const std::string papLine = Launcher_BuildLaunchLine(pap, "");
    Check(papLine.find(" +set fs_mods admin +set zm_startpap 1 +set r_fullscreen 0 ") != std::string::npos &&
              Launcher_BuildLaunchLine(defaults, "").find("zm_startpap") == std::string::npos,
          "the start Pack-a-Punch alone stacks mods/admin and sets zm_startpap, off by default");
    Check(SameSettings(Launcher_ParseSettings(Launcher_FormatSettings(pap)), pap),
          "menu.cfg round-trips the start Pack-a-Punch");
    LauncherSettings far;
    far.tabInfo = false;
    far.farRespawn = 2500;
    const std::string farLine = Launcher_BuildLaunchLine(far, "");
    Check(farLine.find(" +set fs_mods admin +set zm_farrespawn 2500 +set r_fullscreen 0 ") != std::string::npos &&
              Launcher_BuildLaunchLine(defaults, "").find("zm_farrespawn") == std::string::npos,
          "the far zombies' distance alone stacks mods/admin and sets zm_farrespawn, off by default");
    Check(SameSettings(Launcher_ParseSettings(Launcher_FormatSettings(far)), far) &&
              Launcher_ParseSettings("far_respawn=1234\n").farRespawn == 0,
          "menu.cfg round-trips the far zombies' distance, and refuses one off the list");
    const LauncherSettings kitParsed =
        Launcher_ParseSettings("start_points=1234\nstart_perks= mule , nope,jugg\n");
    Check(kitParsed.startPoints == 0 && kitParsed.startPerks == ((1u << 7) | 1u),
          "an unknown point count keeps the default, unknown perks are skipped");

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

    // The pad layout in menu.cfg.
    {
        Check(Bo1_PadRemapIsIdentity(defaults.pad), "the default pad layout is the game's");
        LauncherSettings custom;
        Bo1_PadRemapAssign(&custom.pad, BO1_PAD_RTRIGGER, BO1_PAD_RSHOULDER); // fire on R, grenade on ZR
        Bo1_PadRemapAssign(&custom.pad, BO1_PAD_A, BO1_PAD_B);                // jump on B, crouch on A
        const std::string text = Launcher_FormatSettings(custom);
        Check(text.find("pad_zr=r\n") != std::string::npos && text.find("pad_r=zr\n") != std::string::npos &&
                  text.find("pad_a=b\n") != std::string::npos && text.find("pad_x=x\n") != std::string::npos,
              "menu.cfg lists every pad control with its button");
        Check(SameSettings(Launcher_ParseSettings(text), custom), "a custom pad layout round-trips through menu.cfg");
        const LauncherSettings dup = Launcher_ParseSettings("map=zombie_moon\npad_a=b\n");
        Check(Bo1_PadRemapIsIdentity(dup.pad) && dup.map == 5,
              "a layout with two controls on one button falls back to the game's, the other settings kept");
        const LauncherSettings swap = Launcher_ParseSettings("pad_a=b\npad_b=a\npad_nope=x\npad_x=nope\n");
        Check(swap.pad.source[BO1_PAD_A] == BO1_PAD_B && swap.pad.source[BO1_PAD_B] == BO1_PAD_A &&
                  swap.pad.source[BO1_PAD_X] == BO1_PAD_X,
              "a swap in two lines loads; unknown pad names are skipped");
        Check(std::string(Launcher_PadButtonLabel(BO1_PAD_RTRIGGER)) == "ZR" &&
                  std::string(Launcher_PadActionLabel(BO1_PAD_RTRIGGER)) == "Tirer" &&
                  std::string(Launcher_PadButtonLabel(BO1_PAD_COUNT)) == "?",
              "pad labels: ZR fires; past the list is \"?\"");
        bool ascii = true;
        for (int i = 0; i < BO1_PAD_COUNT; ++i)
            for (const char *t : {Launcher_PadButtonLabel(i), Launcher_PadActionLabel(i)})
                for (; *t; ++t)
                    ascii = ascii && (unsigned char)*t < 128;
        Check(ascii, "pad labels are ASCII (the console font has no accents)");
        // Cycling one line through every button keeps a permutation at each step.
        LauncherSettings cycle;
        bool perm = true;
        for (int step = 0; step < 40; ++step)
        {
            Bo1_PadRemapAssign(&cycle.pad, BO1_PAD_Y, (cycle.pad.source[BO1_PAD_Y] + 1) % BO1_PAD_COUNT);
            perm = perm && Bo1_PadRemapValid(cycle.pad);
        }
        Check(perm, "changing one line again and again keeps every control on its own button");
    }

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
