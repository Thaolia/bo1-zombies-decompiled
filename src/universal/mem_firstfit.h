#pragma once
#include <cstddef>
#include <cstdint>
#include <platform/arch_size.h>
#include "mem_userhunk.h"

struct _firstfit_heapnode // sizeof=0x8
{                                       // XREF: FIRSTFIT_HEAPNODE/r
    _firstfit_heapnode *next;
    int size;
};

// The HU_SCHEME_FIRSTFIT hunk header (FIRSTFIT_HUNKUSER); the heap nodes follow it in the same buffer. The decompiled
// code addressed these fields as _user[1].scheme/flags/name/type.
struct _firstfit_hunkuser // sizeof=0x20
{
    HunkUser hunkUser;
    unsigned int size;
    _firstfit_heapnode *free_blocks;
    int unused;                         // set to -1 at init, never read
    int allocated;                      // bytes in use, this header included
};
static_assert(offsetof(_firstfit_hunkuser, size) == BO1_ARCH_SIZE(16, 24));
static_assert(offsetof(_firstfit_hunkuser, free_blocks) == BO1_ARCH_SIZE(20, 32));
static_assert(offsetof(_firstfit_hunkuser, unused) == BO1_ARCH_SIZE(24, 40));
static_assert(offsetof(_firstfit_hunkuser, allocated) == BO1_ARCH_SIZE(28, 44));
static_assert(sizeof(_firstfit_hunkuser) == BO1_ARCH_SIZE(32, 48));

HunkUser *__cdecl Hunk_FirstFitInit(
                _firstfit_hunkuser *buffer,
                unsigned int size,
                HU_ALLOCATION_SCHEME scheme,
                unsigned int flags,
                void *scheme_specific_data,
                const char *name,
                int type);
void __cdecl Hunk_FirstFitReset(HunkUser *_user);
void __cdecl Hunk_FirstFitDestroy(HunkUser *_user);
uintptr_t __cdecl Hunk_FirstFitAlloc(HunkUser *_user, int size, int alignment);
void __cdecl Hunk_FirstFitFree(HunkUser *_user, unsigned int *ptr);
