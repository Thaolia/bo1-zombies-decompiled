// The posix entry point: it hands WinMain (win32/win_main.cpp) the command line as Windows does, and the engine starts
// the same way from there.

#include <win32/win_main.h>
#include <win32/win_mini_dumper.h>

#include <signal.h>

namespace
{
bool s_miniDumpStarted;
}

// win_mini_dumper.cpp stays Windows-only (dbghelp, token privileges). Its exception filter writes nothing in this source
// tree (miniDumper::writeMiniDump returns at once), so starting it comes down to the flag assertive.cpp reads.
void __cdecl Sys_StartMiniDump(bool)
{
    s_miniDumpStarted = true;
}

bool __cdecl Sys_IsMiniDumpStarted()
{
    return s_miniDumpStarted;
}

int main(int, char **)
{
    // A send to a peer that closed its socket fails with an error, as on Windows, instead of ending the process.
    signal(SIGPIPE, SIG_IGN);

    // As the MSVC CRT does for WinMain: the command line without the program name and the blanks after it.
    char *commandLine = GetCommandLineA();
    bool quoted = false;
    for (; *commandLine && (quoted || (*commandLine != ' ' && *commandLine != '\t')); ++commandLine)
    {
        if (*commandLine == '"')
            quoted = !quoted;
    }
    while (*commandLine == ' ' || *commandLine == '\t')
        ++commandLine;
    return WinMain(GetModuleHandleA(nullptr), nullptr, commandLine, 10); // SW_SHOWDEFAULT
}
