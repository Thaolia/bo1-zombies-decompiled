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
#include <mutex>

extern "C" void Switch_BootLogWrite(const char *text);

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

// Append one line to the SD boot log and echo it to stdout (an emulator captures stdout to its own log).
void BootLog(const char *fmt, ...)
{
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    std::printf("[bo1zm] %s\n", line);
    std::fflush(stdout);
    char out[sizeof(line) + 1];
    std::snprintf(out, sizeof(out), "%s\n", line);
    Switch_BootLogWrite(out);
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

// The one writer of the SD boot log, for switch_main, the engine (Switch_DebugLog) and the deko3d backend. Horizon's
// FS lets a single handle write a file: while the engine held its own "ab" handle, every other fopen of boot.log
// failed, so the backend's lines never reached the SD. Opened on first use and flushed per write; the mutex serialises
// the engine's and the backend's threads. fflush only hands the bytes to the FS service, which commits them on
// fsFileFlush or close: a console run that ended in a crash kept its log only up to a minute before it (no line of the
// lag or the crash). Committing costs an SD write, so it happens at most once a second.
extern "C" void Switch_BootLogWrite(const char *text)
{
    static std::mutex lock;
    const std::lock_guard<std::mutex> guard(lock);
    static FILE *const f = std::fopen(kBootLogPath, "ab");
    static u64 lastCommit;
    if (f)
    {
        std::fputs(text, f);
        std::fflush(f);
        const u64 now = armGetSystemTick();
        if (now - lastCommit >= armGetSystemTickFreq())
        {
            fsync(fileno(f));
            lastCommit = now;
        }
    }
}

extern "C" char _start[]; // libnx's crt0 entry, the module's first byte: ELF address 0

// libnx's diagAbortWithResult is weak; this one logs, then breaks as libnx's does. The release libdeko3d aborts
// through it on any error (its RaiseError: a failed block allocation, a swapchain acquire or present that fails, a
// queue flushed after a GPU fault), so without it the console's log just stops. The frames are the return addresses
// of the frame-record chain as ELF offsets (addr2line on the .elf, minus 4 for the call).
extern "C" NX_NORETURN void diagAbortWithResult(Result res)
{
    char line[512];
    int n = std::snprintf(line, sizeof(line), "*** abort: result 0x%x (module %u, description %u), frames:", res,
                          unsigned(R_MODULE(res)), unsigned(R_DESCRIPTION(res)));
    const uintptr_t base = reinterpret_cast<uintptr_t>(_start);
    const uintptr_t *fp = static_cast<const uintptr_t *>(__builtin_frame_address(0));
    for (int i = 0; i < 10 && fp && !(reinterpret_cast<uintptr_t>(fp) & 15) && n < int(sizeof(line)) - 24; ++i)
    {
        n += std::snprintf(line + n, sizeof(line) - n, " 0x%lx", static_cast<unsigned long>(fp[1] - base));
        const uintptr_t *next = reinterpret_cast<const uintptr_t *>(fp[0]);
        if (next <= fp || reinterpret_cast<uintptr_t>(next) - reinterpret_cast<uintptr_t>(fp) > (1u << 20))
            break;
        fp = next;
    }
    std::snprintf(line + n, sizeof(line) - n, "\n");
    Switch_BootLogWrite(line);
    svcOutputDebugString(line, std::strlen(line)); // an emulator dies on the break and loses its unflushed SD writes
    svcBreak(BreakReason_Panic, reinterpret_cast<uintptr_t>(&res), sizeof(res));
    __builtin_unreachable();
}

namespace
{
bool s_psmTried = false, s_psmUp = false;
} // namespace

// The battery charge for the on-screen counter (cl_scrn_mp.cpp), or -1 without the psm service. Each query is an IPC
// call, so the value is refreshed every 5 s rather than every frame.
extern "C" int Switch_BatteryPercent()
{
    static int percent = -1;
    static u64 lastTick = 0;
    if (!s_psmTried)
    {
        s_psmTried = true;
        const Result rc = psmInitialize();
        s_psmUp = R_SUCCEEDED(rc);
        BootLog("psmInitialize -> 0x%x", rc);
    }
    if (!s_psmUp)
        return -1;
    const u64 now = armGetSystemTick();
    if (percent < 0 || armTicksToNs(now - lastTick) >= 5000000000ull)
    {
        u32 charge = 0;
        if (R_SUCCEEDED(psmGetBatteryChargePercentage(&charge)))
            percent = int(charge);
        lastTick = now;
    }
    return percent;
}

int main(int, char **)
{
    // Make the data dir so even a first boot on a fresh SD can leave its log (EEXIST is fine).
    mkdir("sdmc:/switch", 0777);
    mkdir(kDataDir, 0777);
    // Start each launch with a fresh log instead of appending to every prior run: truncate it once here, before the
    // first BootLog opens the shared writer.
    if (FILE *fresh = std::fopen(kBootLogPath, "wb"))
        std::fclose(fresh);
    BootLog("=== BO1 Zombies (Switch) boot ===");

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
            // deko3d drives the console window (nwindowGetDefault) itself; SDL must not also grab it for an EGL/GL
            // context, or dkSwapchainCreate fails. The dummy video driver gives the engine its window object without
            // touching nwindow. GL path is unaffected (this only fires for deko3d).
            if (!std::strcmp(v, "deko3d"))
            {
                setenv("SDL_VIDEODRIVER", "dummy", 1);
                BootLog("deko3d backend -> SDL_VIDEODRIVER=dummy (deko3d owns nwindowGetDefault)");
            }
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
    // Which NRO wrote this log: the link time the build packs into romfs (CMakeLists.txt), not a compile-time
    // __DATE__, which only changes when this file recompiles.
    if (FILE *bf = std::fopen("romfs:/build.txt", "rb"))
    {
        char stamp[64] = {};
        const size_t n = std::fread(stamp, 1, sizeof(stamp) - 1, bf);
        std::fclose(bf);
        stamp[n && stamp[n - 1] == '\n' ? n - 1 : n] = '\0';
        BootLog("build %s", stamp);
    }
    else
        BootLog("build (no romfs:/build.txt)");

    char *const args = LoadLaunchArgs();
    BootLog("launch args = \"%s\"", args);
    BootLog("entering WinMain");
    const int rc = WinMain(GetModuleHandleA(nullptr), nullptr, args, 10 /* SW_SHOWDEFAULT */);
    BootLog("WinMain returned %d", rc);

    if (s_psmUp)
        psmExit();
    if (R_SUCCEEDED(rrc))
        romfsExit();
    return rc;
}
