#include "db_stream_load.h"
#include "db_stream.h"
#include <universal/assertive.h>
#include <string.h>
#include "db_file_load.h"
#include "db_ff64.h"
#include <clientscript/cscr_stringlist.h>

StreamDelayInfo g_streamDelayArray[4096];

void __cdecl Load_Stream(bool atStreamStart, unsigned __int8 *ptr, int size)
{
    if (atStreamStart != (ptr == DB_GetStreamPos())
        && !Assert_MyHandler(
            "C:\\projects_pc\\cod\\codsrc\\src\\database\\db_stream_load.cpp",
            21,
            0,
            "%s",
            "atStreamStart == (ptr == DB_GetStreamPos())"))
    {
        __debugbreak();
    }
    if (atStreamStart && size)
    {
        if (g_streamPosIndex - 1 < 3)
        {
            if (g_streamPosIndex == 1)
            {
                memset(ptr, 0, size);
            }
            else
            {
                if (g_streamDelayIndex >= 0x1000
                    && !Assert_MyHandler(
                        "C:\\projects_pc\\cod\\codsrc\\src\\database\\db_stream_load.cpp",
                        36,
                        0,
                        "g_streamDelayIndex doesn't index ARRAY_COUNT( g_streamDelayArray )\n\t%i not in [0, %i)",
                        g_streamDelayIndex,
                        4096))
                {
                    __debugbreak();
                }
                g_streamDelayArray[g_streamDelayIndex].ptr = ptr;
                g_streamDelayArray[g_streamDelayIndex++].size = size;
            }
        }
        else
        {
            DB_LoadXFileData(ptr, size);
        }
        DB_IncStreamPos(size);
    }
}

void __cdecl Load_DelayStream()
{
    unsigned int index; // [esp+4h] [ebp-8h]

    for (index = 0; index < g_streamDelayIndex; ++index)
        DB_LoadXFileData((unsigned __int8 *)g_streamDelayArray[index].ptr, g_streamDelayArray[index].size);
}

// A stream offset is the encoded (block, offset) of a byte the stream already loaded: 3 + 29 bits in retail zones,
// 32 + 32 bits in IWffs zones (db_ff64.h).
static unsigned __int8 *DB_OffsetToAddress(uintptr_t offset)
{
    if constexpr (sizeof(uintptr_t) == 8)
        return &g_streamBlocks[DB_FF64_OffsetBlock(offset)].data[DB_FF64_OffsetInBlock(offset)];
    else
        return &g_streamBlocks[(offset - 1) >> 29].data[(offset - 1) & 0x1FFFFFFF];
}

void __cdecl DB_ConvertOffsetToAlias(void *data)
{
    uintptr_t offset; // [esp+0h] [ebp-8h]

    offset = *(uintptr_t *)data;
    if ( (!offset || offset >= (uintptr_t)-2)
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\database\\db_stream_load.cpp",
                    67,
                    0,
                    "%s\n\t(offset) = %i",
                    "(offset && (offset != 0xFFFFFFFF) && (offset != 0xFFFFFFFE))",
                    (int)offset) )
    {
        __debugbreak();
    }
    *(uintptr_t *)data = *(uintptr_t *)DB_OffsetToAddress(offset);
}

void __cdecl DB_ConvertOffsetToPointer(void *data)
{
    *(uintptr_t *)data = (uintptr_t)DB_OffsetToAddress(*(uintptr_t *)data);
}

void __cdecl Load_XStringCustom(const char **str)
{
    int numBytesLoaded; // [esp+4h] [ebp-8h]

    numBytesLoaded = DB_LoadXFileDataNullTerminated((unsigned __int8 *)*str);
    DB_IncStreamPos(numBytesLoaded);
}

void __cdecl Load_TempStringCustom(const char **str)
{
    const char *String; // [esp+0h] [ebp-4h]

    Load_XStringCustom(str);
    if ( *str )
        String = (const char *)(uintptr_t)SL_GetString((char *)*str, 4u, SCRIPTINSTANCE_SERVER);
    else
        String = 0;
    *str = String;
}

