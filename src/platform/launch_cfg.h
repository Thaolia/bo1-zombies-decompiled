#pragma once

// launch.cfg (docs/SERVER.md): one engine command line, which may span several lines. The PC builds read it next to
// the program (win_main.cpp), the Switch on its SD card (switch_main.cpp, switch_launcher.cpp); all clean it here so
// that the same file means the same thing everywhere.

#include <stddef.h>
#include <string.h>

// Line breaks and tabs read as blanks, and the blanks at both ends go. Returns the new length and writes the
// terminating 0 there, so text must hold length + 1 bytes.
inline size_t LaunchCfg_Normalize(char *text, size_t length)
{
    for (size_t i = 0; i < length; ++i)
    {
        if (text[i] == '\r' || text[i] == '\n' || text[i] == '\t')
            text[i] = ' ';
    }
    while (length && text[length - 1] == ' ')
        --length;
    size_t start = 0;
    while (start < length && text[start] == ' ')
        ++start;
    if (start)
        memmove(text, text + start, length - start);
    length -= start;
    text[length] = 0;
    return length;
}
