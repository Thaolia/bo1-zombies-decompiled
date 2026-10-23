#include "mem_fixed.h"
#include "assertive.h"

HunkUser *__cdecl Hunk_FixedInit(
                _fixed_hunkuser *buffer,
                unsigned int size,
                HU_ALLOCATION_SCHEME scheme,
                unsigned int flags,
                int *scheme_specific_data,
                const char *name,
                int type)
{
    int v8; // [esp+0h] [ebp-1Ch]
    int i; // [esp+8h] [ebp-14h]
    _fixed_heapnode *node; // [esp+Ch] [ebp-10h]
    unsigned int count; // [esp+18h] [ebp-4h]

    if ( (flags & 2) == 0
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\universal\\mem_fixed.cpp",
                    28,
                    0,
                    "%s",
                    "(flags & HF_FROMBUFFER)!=0") )
    {
        __debugbreak();
    }
    if ( size <= sizeof(_fixed_hunkuser) + sizeof(_fixed_heapnode)
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\universal\\mem_fixed.cpp",
                    29,
                    0,
                    "%s",
                    "size>sizeof(FIXED_HUNKUSER)+sizeof(FIXED_HEAPNODE)") )
    {
        __debugbreak();
    }
    if ( *scheme_specific_data != (*scheme_specific_data & (*scheme_specific_data - 1))
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\universal\\mem_fixed.cpp",
                    30,
                    0,
                    "%s",
                    "scheme_data->block_size==(scheme_data->block_size&(scheme_data->block_size-1))") )
    {
        __debugbreak();
    }
    buffer->hunkUser.name = name;
    buffer->hunkUser.scheme = scheme;
    buffer->hunkUser.flags = flags;
    buffer->hunkUser.type = type;
    buffer->size = size;
    if ( *scheme_specific_data < (int)sizeof(_fixed_heapnode) )
        v8 = (int)sizeof(_fixed_heapnode);
    else
        v8 = *scheme_specific_data;
    buffer->block_size = v8;
    count = (size - sizeof(_fixed_hunkuser)) / (buffer->block_size + sizeof(_fixed_heapnode));
    node = (_fixed_heapnode *)(buffer + 1);
    buffer->free_blocks = (_fixed_heapnode *)(buffer + 1);
    for ( i = 0; i < (int)(count - 1); ++i )
        node->next = (_fixed_heapnode *)((char *)node + buffer->block_size);
    node->next = 0;
    return &buffer->hunkUser;
}

void __cdecl Hunk_FixedReset(HunkUser *_user)
{
    int i; // [esp+0h] [ebp-10h]
    _fixed_heapnode *node; // [esp+4h] [ebp-Ch]
    unsigned __int32 count; // [esp+Ch] [ebp-4h]
    _fixed_hunkuser *hunk = (_fixed_hunkuser *)_user;

    count = (hunk->size - sizeof(_fixed_hunkuser)) / (hunk->block_size + sizeof(_fixed_heapnode));
    node = (_fixed_heapnode *)(hunk + 1);
    hunk->free_blocks = (_fixed_heapnode *)(hunk + 1);
    for ( i = 0; i < (int)(count - 1); ++i )
        node->next = (_fixed_heapnode *)((char *)node + hunk->block_size + sizeof(_fixed_heapnode));
    node->next = 0;
}

void __cdecl Hunk_FixedDestroy(HunkUser *_user)
{
    _fixed_hunkuser *hunk = (_fixed_hunkuser *)_user;

    _user->scheme = HU_SCHEME_DEFAULT;
    _user->flags = 0;
    _user->name = 0;
    _user->type = 0;
    hunk->size = 0;
    hunk->block_size = 0;
    hunk->free_blocks = 0;
}

const char **__cdecl Hunk_FixedAlloc(HunkUser *_user)
{
    const char **result; // [esp+0h] [ebp-8h]
    _fixed_hunkuser *hunk = (_fixed_hunkuser *)_user;

    result = 0;
    if ( hunk->free_blocks )
    {
        result = (const char **)hunk->free_blocks;
        hunk->free_blocks = hunk->free_blocks->next;
    }
    return result;
}

void __cdecl Hunk_FixedFree(HunkUser *_user, char *ptr)
{
    _fixed_hunkuser *hunk = (_fixed_hunkuser *)_user;

    if ( ptr )
    {
        if ( ptr <= (char *)_user || ptr >= (char *)_user + hunk->size )
        {
            if ( !Assert_MyHandler(
                            "C:\\projects_pc\\cod\\codsrc\\src\\universal\\mem_fixed.cpp",
                            99,
                            0,
                            "%s\n\t%s",
                            "0",
                            "invalid pointer passed into first fit free\n") )
                __debugbreak();
        }
        else
        {
            ((_fixed_heapnode *)ptr)->next = hunk->free_blocks;
            hunk->free_blocks = (_fixed_heapnode *)ptr;
        }
    }
}

