#include "win_shared.h"

#include <Windows.h>

#include <climits>
#include <cmath>

int sys_timeBase;

unsigned int __cdecl Sys_Milliseconds()
{
    static int initialized_1 = 0;
    if ( !initialized_1 )
    {
        sys_timeBase = timeGetTime();
        initialized_1 = 1;
    }
    return timeGetTime() - sys_timeBase;
}

unsigned int __cdecl Sys_MillisecondsRaw()
{
    return timeGetTime();
}

//void __cdecl Sys_SnapVector(float *v)
//{
//    float *va; // [esp+10h] [ebp+8h]
//
//    *v = (float)(int)*v;
//    va = v + 1;
//    *va = (float)(int)*va;
//    va[1] = (float)(int)va[1];
//}

// https://github.com/id-Software/Quake-III-Arena/blob/master/code/win32/win_shared.c
void Sys_SnapVector(float *v)
{
#if defined(_MSC_VER) && defined(_M_IX86)
    int i;
    float f;

    f = *v;
    __asm	fld		f;
    __asm	fistp	i;
    *v = i;
    v++;
    f = *v;
    __asm	fld		f;
    __asm	fistp	i;
    *v = i;
    v++;
    f = *v;
    __asm	fld		f;
    __asm	fistp	i;
    *v = i;
#else
    // FISTP of a float: round to nearest-even, integer indefinite (INT_MIN) when out of range. Precision control does
    // not matter here because nothing is added before the store.
    for (int k = 0; k < 3; ++k)
    {
        const float rounded = std::nearbyint(v[k]);
        v[k] = (rounded >= -2147483648.0f && rounded < 2147483648.0f) ? static_cast<float>(static_cast<int>(rounded))
                                                                       : static_cast<float>(INT_MIN);
    }
#endif
}