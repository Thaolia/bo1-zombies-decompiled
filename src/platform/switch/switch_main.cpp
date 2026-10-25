// The Switch entry (libnx). It mounts romfs for the game data, refuses applet mode (which gets a fraction of the RAM
// and cannot run the engine, switch-homebrew.md), then hands the engine's WinMain the same start as every platform.
// The Win32 shims and the minidump flag are shared with posix; only the entry and romfs differ, so this file stands
// in for posix_main.cpp on the switch build.

// libnx first: the Win32 shim's <windows.h> does `#define interface struct`, which breaks libnx headers that use
// `interface` as an identifier, so <switch.h> must be parsed before it.
#include <switch.h>

#include <win32/win_main.h>

#include <sys/stat.h>
#include <unistd.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
bool s_miniDumpStarted;
}

// As on posix: win_mini_dumper.cpp is Windows-only and writes nothing here, so starting the minidump is just the flag
// assertive.cpp reads.
void __cdecl Sys_StartMiniDump(bool)
{
    s_miniDumpStarted = true;
}

bool __cdecl Sys_IsMiniDumpStarted()
{
    return s_miniDumpStarted;
}

namespace
{
const char *const kDataDir = "sdmc:/switch/bo1zm";
const char *const kBootLogPath = "sdmc:/switch/bo1zm/boot.log";
// The console has no command line, so the engine's start-up arguments come from an optional text file on the SD card
// the user can edit without rebuilding (e.g. "+set bo1_zombies 1 +devmap zombie_cosmodrome"). Absent or empty, the
// default boots straight into solo zombies; the front-end command that lands on the map-select menu is pinned later.
const char *const kLaunchConfigPath = "sdmc:/switch/bo1zm/launch.cfg";
// Presence-only bring-up override: with this file on the SD card the engine runs even outside application mode, so a
// directly-loaded NRO (as an emulator loads it) can be exercised past the refusal screen. Hardware behaviour without
// the file is unchanged: applet mode is refused.
const char *const kAllowAppletPath = "sdmc:/switch/bo1zm/allow_applet";
const char *const kDefaultArgs = "+set bo1_zombies 1";
char s_commandLine[1024];

// Append one line to the SD boot log and echo it to stdout (an emulator captures stdout to its own log). Each call
// reopens and closes the file so a crash later in start-up still leaves every breadcrumb on disk. Best-effort: a
// missing or read-only SD path just means no file, never a failure.
void BootLog(const char *fmt, ...)
{
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    std::printf("[bo1zm] %s\n", line);
    std::fflush(stdout);
    FILE *f = std::fopen(kBootLogPath, "ab");
    if (f)
    {
        std::fprintf(f, "%s\n", line);
        std::fclose(f);
    }
}

const char *AppletTypeName(AppletType t)
{
    switch (t)
    {
    case AppletType_None:
        return "None";
    case AppletType_Default:
        return "Default";
    case AppletType_Application:
        return "Application";
    case AppletType_SystemApplet:
        return "SystemApplet";
    case AppletType_LibraryApplet:
        return "LibraryApplet";
    case AppletType_OverlayApplet:
        return "OverlayApplet";
    case AppletType_SystemApplication:
        return "SystemApplication";
    default:
        return "?";
    }
}

bool FileExists(const char *path)
{
    FILE *f = std::fopen(path, "rb");
    if (!f)
        return false;
    std::fclose(f);
    return true;
}

char *LoadLaunchArgs()
{
    FILE *f = std::fopen(kLaunchConfigPath, "rb");
    size_t n = 0;
    if (f)
    {
        n = std::fread(s_commandLine, 1, sizeof(s_commandLine) - 1, f);
        std::fclose(f);
    }
    s_commandLine[n] = '\0';
    while (n && (s_commandLine[n - 1] == '\n' || s_commandLine[n - 1] == '\r' || s_commandLine[n - 1] == ' ' ||
                 s_commandLine[n - 1] == '\t'))
        s_commandLine[--n] = '\0';
    if (n == 0)
        std::snprintf(s_commandLine, sizeof(s_commandLine), "%s", kDefaultArgs);
    return s_commandLine;
}

// Application mode gives the process the console's memory; applet mode (album, overlay) gives a fraction and cannot
// run the engine. Refuse it cleanly with an on-screen message instead of crashing later (switch-homebrew.md).
bool InApplicationMode()
{
    const AppletType type = appletGetAppletType();
    return type == AppletType_Application || type == AppletType_SystemApplication;
}

void ShowAppletModeMessage()
{
    consoleInit(nullptr);
    std::printf("\n  BO1 Zombies needs application mode.\n");
    std::printf("  Launch it over a game (title takeover): hold R while starting a game.\n\n");
    std::printf("  Press + to exit.\n");
    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    while (appletMainLoop())
    {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
            break;
        consoleUpdate(nullptr);
    }
    consoleExit(nullptr);
}
} // namespace

int main(int, char **)
{
    // Make the data dir so even a first boot on a fresh SD can leave its log (EEXIST is fine).
    mkdir("sdmc:/switch", 0777);
    mkdir(kDataDir, 0777);
    // Start each launch with a fresh log instead of appending to every prior run: truncate it once here, before any
    // BootLog, so the switch_main and engine (Switch_DebugLog) "ab" writes all land in the new file.
    if (FILE *fresh = std::fopen(kBootLogPath, "wb"))
        std::fclose(fresh);
    BootLog("=== BO1 Zombies (Switch) boot === built %s %s", __DATE__, __TIME__);

    // The desktop builds run with the working directory set to the install folder, and the engine depends on it: it
    // opens a few files before the filesystem is up (localization.txt in Win_InitLocalization, read with a path
    // relative to the cwd via FileWrapper_Open), and fs_basepath defaults to Sys_Cwd(). There is no launcher to set
    // the cwd on the console, so set it to the data dir here; without it the language never resolves (the zone path
    // becomes zone\(null)\...) and fs_basepath comes out empty (the search path is //main).
    const int cdrc = chdir(kDataDir);
    char cwdbuf[256];
    BootLog("chdir(%s) -> %d; cwd = %s", kDataDir, cdrc, getcwd(cwdbuf, sizeof(cwdbuf)) ? cwdbuf : "(getcwd failed)");

    // Optional bring-up override: a file sdmc:/switch/bo1zm/gfx.cfg holding "null" or "gl" selects the graphics
    // backend (the dispatcher reads BO1_GFX_BACKEND) without a rebuild. Absent, the default applies (GL when it gets
    // a context). Lets the console be exercised headlessly (null) past the slow first-boot shader compile.
    if (FILE *gf = std::fopen("sdmc:/switch/bo1zm/gfx.cfg", "rb"))
    {
        char v[16] = {};
        const size_t n = std::fread(v, 1, sizeof(v) - 1, gf);
        std::fclose(gf);
        for (size_t i = 0; i < n; ++i)
            if (v[i] == '\r' || v[i] == '\n' || v[i] == ' ')
                v[i] = 0;
        if (v[0])
        {
            setenv("BO1_GFX_BACKEND", v, 1);
            BootLog("gfx.cfg -> BO1_GFX_BACKEND=%s", v);
        }
    }

    const AppletType at = appletGetAppletType();
    BootLog("applet type = %d (%s)", (int)at, AppletTypeName(at));

    const bool allowApplet = FileExists(kAllowAppletPath);
    if (!InApplicationMode() && !allowApplet)
    {
        BootLog("not application mode and no allow_applet sentinel -> refusal screen");
        ShowAppletModeMessage();
        return 0;
    }
    if (!InApplicationMode())
        BootLog("applet mode, but allow_applet present -> continuing (bring-up)");

    const Result rrc = romfsInit(); // romfs:/ holds no data here (data stays on the SD card); log whether it mounted
    BootLog("romfsInit -> 0x%x%s", rrc, R_SUCCEEDED(rrc) ? " (mounted)" : " (none; data from SD)");

    char *const args = LoadLaunchArgs();
    BootLog("launch args = \"%s\"", args);
    BootLog("entering WinMain");
    const int rc = WinMain(GetModuleHandleA(nullptr), nullptr, args, 10 /* SW_SHOWDEFAULT */);
    BootLog("WinMain returned %d", rc);

    if (R_SUCCEEDED(rrc))
        romfsExit();
    return rc;
}
