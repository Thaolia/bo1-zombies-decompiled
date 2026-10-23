#pragma once
#include <cstddef>
#include <platform/arch_size.h>
#include "mem_userhunk.h"

struct _fixed_heapnode // sizeof=0x4
{                                       // XREF: FIXED_HEAPNODE/r
    _fixed_heapnode *next;
};

// The HU_SCHEME_FIXED hunk header (FIXED_HUNKUSER); the blocks follow it in the same buffer. The decompiled code
// addressed these fields as _user[1].scheme/flags/name.
struct _fixed_hunkuser // sizeof=0x1C
{
    HunkUser hunkUser;
    unsigned int size;
    unsigned int block_size;
    _fixed_heapnode *free_blocks;
};
static_assert(offsetof(_fixed_hunkuser, size) == BO1_ARCH_SIZE(16, 24));
static_assert(offsetof(_fixed_hunkuser, block_size) == BO1_ARCH_SIZE(20, 28));
static_assert(offsetof(_fixed_hunkuser, free_blocks) == BO1_ARCH_SIZE(24, 32));
static_assert(sizeof(_fixed_hunkuser) == BO1_ARCH_SIZE(28, 40));

HunkUser *__cdecl Hunk_FixedInit(
                _fixed_hunkuser *buffer,
                unsigned int size,
                HU_ALLOCATION_SCHEME scheme,
                unsigned int flags,
                int *scheme_specific_data,
                const char *name,
                int type);


void __cdecl Hunk_FixedReset(HunkUser *_user);
void __cdecl Hunk_FixedDestroy(HunkUser *_user);
const char **__cdecl Hunk_FixedAlloc(HunkUser *_user);
void __cdecl Hunk_FixedFree(HunkUser *_user, char *ptr);
