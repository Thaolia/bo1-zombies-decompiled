#pragma once

struct StreamDelayInfo // sizeof=0x8
{                                       // XREF: .data:StreamDelayInfo * g_streamDelayArray/r
    const void *ptr;                    // XREF: Load_Stream(bool,void const *,int)+CE/w
                                        // Load_DelayStream(void)+26/r
    int size;                           // XREF: Load_Stream(bool,void const *,int)+DD/w
                                        // Load_DelayStream(void)+33/r
};

void __cdecl Load_Stream(bool atStreamStart, unsigned __int8 *ptr, int size);
void __cdecl Load_DelayStream();
// `data` is a pointer slot holding a stream offset (uintptr_t-sized).
void __cdecl DB_ConvertOffsetToAlias(void *data);
void __cdecl DB_ConvertOffsetToPointer(void *data);
void __cdecl Load_XStringCustom(const char **str);
void __cdecl Load_TempStringCustom(const char **str);
