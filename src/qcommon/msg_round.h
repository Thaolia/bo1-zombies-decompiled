#pragma once

#include <climits>
#include <cmath>

// Portable twin of the x87 MSG_RoundFloatToInt (FLD value; FADD bias; FISTP), used where inline x87 asm is not
// available. R_CreateDevice does not pass D3DCREATE_FPU_PRESERVE, so D3D9 leaves the main thread's x87 in 24-bit
// precision: the FADD result is rounded to float before FISTP rounds it to nearest-even. FISTP stores the integer
// indefinite (INT_MIN) for NaN and out-of-range values. tests/msg_round checks this against the x87 on all 2^32 floats.
inline int MSG_RoundFloatToInt_C(float value)
{
    const double bias = 9.313225746154785e-10;
    const float sum = static_cast<float>(static_cast<double>(value) + bias);
    const float rounded = std::nearbyint(sum);
    if (!(rounded >= -2147483648.0f && rounded < 2147483648.0f))
        return INT_MIN;
    return static_cast<int>(rounded);
}
