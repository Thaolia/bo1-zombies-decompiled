#pragma once

// MSVC's <direct.h>. Declared only, the platform layer implements them.

extern "C" {
int _mkdir(const char *dirname);
char *_getcwd(char *buffer, int maxlen);
}
