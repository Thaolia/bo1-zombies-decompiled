// The Switch entry (libnx). It mounts romfs for the game data, refuses applet mode (which gets a fraction of the RAM
// and cannot run the engine, switch-homebrew.md), then hands the engine's WinMain the same start as every platform.
// The Win32 shims and the minidump flag are shared with posix; only the entry and romfs differ, so this file stands
// in for posix_main.cpp on the switch build.

// libnx first: the Win32 shim's <windows.h> does `#define interface struct`, which breaks libnx headers that use
// `interface` as an identifier, so <switch.h> must be parsed before it.
#include <switch.h>

#include <win32/win_main.h>

#include <cstdio>
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
// The console has no command line, so the engine's start-up arguments come from an optional text file on the SD card
// the user can edit without rebuilding (e.g. "+set bo1_zombies 1 +devmap zombie_cosmodrome"). Absent or empty, the
// default boots straight into solo zombies; the front-end command that lands on the map-select menu is pinned later.
const char *const kLaunchConfigPath = "sdmc:/switch/bo1zm/launch.cfg";
const char *const kDefaultArgs = "+set bo1_zombies 1";
char s_commandLine[1024];

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
    if (!InApplicationMode())
    {
        ShowAppletModeMessage();
        return 0;
    }

    romfsInit(); // romfs:/ holds the data the build packs into the .nro (converted zones stay on the SD card)

    const int rc = WinMain(GetModuleHandleA(nullptr), nullptr, LoadLaunchArgs(), 10 /* SW_SHOWDEFAULT */);

    romfsExit();
    return rc;
}
