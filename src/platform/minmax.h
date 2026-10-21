#pragma once

// The decompiled code calls min()/max() as the <windows.h> macros (same text, so either header may come first).
// Include this after the standard library headers: libstdc++ declares functions named min/max.
#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif
