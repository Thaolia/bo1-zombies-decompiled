#include "mem_firstfit.h"
#include "assertive.h"
#include <win32/win_common.h>

HunkUser *__cdecl Hunk_FirstFitInit(
                _firstfit_hunkuser *buffer,
                unsigned int size,
                HU_ALLOCATION_SCHEME scheme,
                unsigned int flags,
                void *scheme_specific_data,
                const char *name,
                int type)
{
    if ( (flags & 2) == 0
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\universal\\mem_firstfit.cpp",
                    49,
                    0,
                    "%s",
                    "(flags & HF_FROMBUFFER)!=0") )
    {
        __debugbreak();
    }
    if ( size <= sizeof(_firstfit_hunkuser) + sizeof(_firstfit_heapnode)
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\universal\\mem_firstfit.cpp",
                    50,
                    0,
                    "%s",
                    "size>sizeof(FIRSTFIT_HUNKUSER)+sizeof(FIRSTFIT_HEAPNODE)") )
    {
        __debugbreak();
    }
    buffer->hunkUser.name = name;
    buffer->hunkUser.scheme = scheme;
    buffer->hunkUser.flags = flags;
    buffer->hunkUser.type = type;
    buffer->unused = -1;
    buffer->size = size;
    buffer->free_blocks = (_firstfit_heapnode *)(buffer + 1);
    buffer->free_blocks->next = 0;
    buffer->free_blocks->size = buffer->size - sizeof(_firstfit_hunkuser);
    buffer->allocated = sizeof(_firstfit_hunkuser);
    return &buffer->hunkUser;
}

void __cdecl Hunk_FirstFitReset(HunkUser *_user)
{
    _firstfit_hunkuser *hunk = (_firstfit_hunkuser *)_user;

    hunk->free_blocks = (_firstfit_heapnode *)(hunk + 1);
    hunk->free_blocks->next = 0;
    hunk->free_blocks->size = hunk->size - sizeof(_firstfit_hunkuser);
    hunk->allocated = sizeof(_firstfit_hunkuser);
}

void __cdecl Hunk_FirstFitDestroy(HunkUser *_user)
{
    _firstfit_hunkuser *hunk = (_firstfit_hunkuser *)_user;

    _user->scheme = HU_SCHEME_DEFAULT;
    _user->flags = 0;
    _user->name = 0;
    _user->type = 0;
    hunk->size = 0;
    hunk->free_blocks = 0;
    hunk->unused = 0;
    hunk->allocated = 0;
}

uintptr_t __cdecl Hunk_FirstFitAlloc(HunkUser *_user, int size, int alignment)
{
    _firstfit_heapnode *free_link; // [esp+10h] [ebp-20h]
    int adj_size; // [esp+18h] [ebp-18h]
    _firstfit_heapnode *last; // [esp+2Ch] [ebp-4h]
    _firstfit_hunkuser *hunk = (_firstfit_hunkuser *)_user;

    if ( alignment <= 0
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\universal\\mem_firstfit.cpp", 90, 0, "%s", "alignment>0") )
    {
        __debugbreak();
    }
    Sys_EnterCriticalSection(CRITSECT_MEMFIRSTFIT);
    free_link = hunk->free_blocks;
    last = 0;
    while ( 1 )
    {
        if ( !free_link )
            goto LABEL_16;
        adj_size = (~(uintptr_t)(alignment - 1) & ((uintptr_t)free_link + alignment + sizeof(_firstfit_heapnode) + sizeof(_firstfit_heapnode *) - 1)) - (uintptr_t)free_link + size;
        if ( free_link->size >= adj_size )
            break;
        last = free_link;
        free_link = free_link->next;
    }
    if ( !free_link->next && free_link->size - adj_size <= 1024 )
    {
LABEL_16:
        Sys_LeaveCriticalSection(CRITSECT_MEMFIRSTFIT);
        return 0;
    }
    if ( free_link->size - adj_size > 1024 )
    {
        ((_firstfit_heapnode *)((~(uintptr_t)(alignment - 1) & ((uintptr_t)free_link + alignment + sizeof(_firstfit_heapnode) + sizeof(_firstfit_heapnode *) - 1)) + size))->next = free_link->next;
        ((_firstfit_heapnode *)((char *)free_link + adj_size))->size = free_link->size - adj_size;
        free_link->next = (_firstfit_heapnode *)((~(uintptr_t)(alignment - 1) & ((uintptr_t)free_link + alignment + sizeof(_firstfit_heapnode) + sizeof(_firstfit_heapnode *) - 1)) + size);
        free_link->size = adj_size;
    }
    if ( last )
        last->next = free_link->next;
    else
        hunk->free_blocks = free_link->next;
    free_link->next = (_firstfit_heapnode *)-559038737;
    *((_firstfit_heapnode **)(~(uintptr_t)(alignment - 1) & ((uintptr_t)free_link + alignment + sizeof(_firstfit_heapnode) + sizeof(_firstfit_heapnode *) - 1)) - 1) = free_link;
    hunk->allocated += free_link->size;
    Sys_LeaveCriticalSection(CRITSECT_MEMFIRSTFIT);
    return ~(uintptr_t)(alignment - 1) & ((uintptr_t)free_link + alignment + sizeof(_firstfit_heapnode) + sizeof(_firstfit_heapnode *) - 1);
}

void __cdecl Hunk_FirstFitFree(HunkUser *_user, unsigned int *ptr)
{
    _firstfit_heapnode *free_link; // [esp+0h] [ebp-14h]
    _firstfit_heapnode *scan; // [esp+8h] [ebp-Ch]
    _firstfit_heapnode *last; // [esp+10h] [ebp-4h]
    _firstfit_hunkuser *hunk = (_firstfit_hunkuser *)_user;

    if ( ptr )
    {
        Sys_EnterCriticalSection(CRITSECT_MEMFIRSTFIT);
        last = 0;
        scan = hunk->free_blocks;
        free_link = *((_firstfit_heapnode **)ptr - 1);
        if ( free_link->next == (_firstfit_heapnode *)-559038737 )
        {
            hunk->allocated -= free_link->size;
            if ( !hunk->free_blocks
                && !Assert_MyHandler(
                            "C:\\projects_pc\\cod\\codsrc\\src\\universal\\mem_firstfit.cpp",
                            178,
                            0,
                            "%s",
                            "hunk->free_blocks!=NULL") )
            {
                __debugbreak();
            }
            while ( scan )
            {
                if ( scan > free_link )
                {
                    if ( last )
                    {
                        free_link->next = scan;
                        last->next = free_link;
                    }
                    else
                    {
                        free_link->next = hunk->free_blocks;
                        hunk->free_blocks = free_link;
                    }
                    if ( last && (_firstfit_heapnode *)((char *)last + last->size) == free_link )
                    {
                        last->size += free_link->size;
                        last->next = free_link->next;
                        free_link = last;
                    }
                    if ( free_link->next && (_firstfit_heapnode *)((char *)free_link + free_link->size) == free_link->next )
                    {
                        free_link->size += free_link->next->size;
                        free_link->next = free_link->next->next;
                    }
                    goto LABEL_24;
                }
                last = scan;
                scan = scan->next;
            }
            if ( !Assert_MyHandler(
                            "C:\\projects_pc\\cod\\codsrc\\src\\universal\\mem_firstfit.cpp",
                            220,
                            0,
                            "%s\n\t%s",
                            "0",
                            "invalid pointer passed into first fit free\n") )
                __debugbreak();
        }
        else if ( !Assert_MyHandler(
                                 "C:\\projects_pc\\cod\\codsrc\\src\\universal\\mem_firstfit.cpp",
                                 173,
                                 0,
                                 "%s\n\t%s",
                                 "0",
                                 "buffer overrun or underrun or illegal ptr detected") )
        {
            __debugbreak();
        }
LABEL_24:
        Sys_LeaveCriticalSection(CRITSECT_MEMFIRSTFIT);
    }
}

