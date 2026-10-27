// The system console on posix: the terminal the process runs in, in place of win32/win_syscon.cpp's console window.
// Console text goes to stdout without its colour codes; lines typed on stdin become console commands. Like the window,
// only the main thread writes to it.

#include <win32/win_syscon.h>

#include <qcommon/threads.h>
#include <universal/q_shared.h>

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

namespace
{
char s_errorString[512];
char s_inputLine[512];
size_t s_inputLength;
char s_returnedText[512];
bool s_inputClosed;

// Writes text without the ^0..^@ colour codes (Conbuf_CleanText's rule).
void WriteClean(FILE *stream, const char *text)
{
    for (const char *p = text; *p; ++p)
    {
        if (p[0] == '^' && p[1] && p[1] != '^' && p[1] >= '0' && p[1] <= '@')
        {
            ++p;
            continue;
        }
        fputc(*p, stream);
    }
    fflush(stream);
}
} // namespace

int __cdecl Conbuf_CleanText(const char *source, char *target, int sizeofTarget)
{
    char *start = target;
    char *last = target + sizeofTarget - 1;
    while (*source && target < last)
    {
        if (source[0] == '^' && source[1] && source[1] != '^' && source[1] >= '0' && source[1] <= '@')
        {
            source += 2;
            continue;
        }
        *target++ = *source++;
    }
    *target = 0;
    return (int)(target - start);
}

void __cdecl Sys_CreateConsole(HINSTANCE__ *)
{
}

void __cdecl Sys_DestroyConsole()
{
}

void __cdecl Sys_ShowConsole()
{
}

// One complete line from stdin per call, without blocking; nothing once stdin is at its end (a run with < /dev/null).
char *__cdecl Sys_ConsoleInput()
{
    while (!s_inputClosed)
    {
        pollfd input = {STDIN_FILENO, POLLIN, 0};
        if (poll(&input, 1, 0) <= 0 || !(input.revents & (POLLIN | POLLHUP)))
            return nullptr;
        char c;
        const ssize_t got = read(STDIN_FILENO, &c, 1);
        if (got < 0 && errno == EINTR)
            continue;
        if (got <= 0)
        {
            s_inputClosed = true;
            break;
        }
        if (c == '\r')
            continue;
        if (c != '\n')
        {
            if (s_inputLength < sizeof(s_inputLine) - 1)
                s_inputLine[s_inputLength++] = c;
            continue;
        }
        s_inputLine[s_inputLength] = 0;
        s_inputLength = 0;
        if (!s_inputLine[0])
            continue;
        // with its newline, as win_syscon.cpp returns it: Sys_GetEvent copies strlen - 1 characters
        I_strncpyz(s_returnedText, s_inputLine, sizeof(s_returnedText) - 1);
        I_strncat(s_returnedText, sizeof(s_returnedText), "\n");
        return s_returnedText;
    }
    return nullptr;
}

void __cdecl Conbuf_AppendText(char *pMsg)
{
    WriteClean(stdout, pMsg);
}

void __cdecl Conbuf_AppendTextInMainThread(char *msg)
{
    if (Sys_IsMainThread())
        Conbuf_AppendText(msg);
}

void __cdecl Sys_SetErrorText(const char *buf)
{
    I_strncpyz(s_errorString, buf, sizeof(s_errorString));
    fputs("ERROR: ", stderr);
    WriteClean(stderr, s_errorString);
    fputc('\n', stderr);
}
