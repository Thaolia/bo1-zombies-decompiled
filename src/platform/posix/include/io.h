#pragma once

// MSVC's <io.h>: the directory search the engine uses (Sys_ListFiles). Declared only, the platform layer implements it.

#include <stdint.h>
#include "../msvc_intrinsics.h"

typedef uint32_t _fsize_t; // unsigned long on MSVC, 4 bytes

struct _finddata64i32_t
{
    unsigned attrib;
    __time64_t time_create;
    __time64_t time_access;
    __time64_t time_write;
    _fsize_t size;
    char name[260];
};

extern "C" {
intptr_t _findfirst64i32(const char *filespec, struct _finddata64i32_t *fileinfo);
int _findnext64i32(intptr_t handle, struct _finddata64i32_t *fileinfo);
int _findclose(intptr_t handle);
}
