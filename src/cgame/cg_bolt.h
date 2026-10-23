#pragma once

#include <cstdint>

void __cdecl CG_InitBolt(unsigned int localClientNum);
unsigned int __cdecl CG_AddBolt(
                int localClientNum,
                int targetEntityNum,
                unsigned int weapon,
                const float *position,
                const float *normal,
                unsigned __int8 tagIndex,
                intptr_t physUserBody);
void __cdecl CG_UpdateBolt(int localClientNum);
