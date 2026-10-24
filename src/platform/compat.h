#pragma once

// Force-included by CMake in front of every engine file when the compiler is GCC or clang (not MSVC or clang-cl).
// It maps the MSVC dialect the decompiled code is written in onto GNU equivalents without changing behaviour.

#if defined(__MINGW32__)
// MinGW already spells __int8..__int64, __forceinline and the calling conventions; the engine uses them in headers
// that include nothing else first.
#include <_mingw.h>
#elif !defined(_WIN32)
// posix and switch: the same spellings, with MSVC's sizes. long is 8 bytes here (LP64), so none of them uses it.
#define __int8 char
#define __int16 short
#define __int32 int
#define __int64 long long
#define __forceinline inline __attribute__((always_inline))
#define __inline inline
#define __cdecl
#define __stdcall
#define __fastcall
#define __thiscall
#define _cdecl
#define _stdcall
#define _fastcall
#define __ptr32
#define __ptr64
#endif

// __declspec: GCC's built-in mapping is __attribute__((x)), which silently drops align(N) because the GNU attribute
// is "aligned". Every struct layout pinned by a static_assert depends on it, so each form the engine and the MinGW
// headers use is spelled out, and an unknown form fails to compile instead of being ignored.
#undef __declspec
#define __declspec(x) BO1_DECLSPEC_##x
#define _declspec(x) __declspec(x)
#define BO1_DECLSPEC_align(n) __attribute__((aligned(n)))
#define BO1_DECLSPEC_noinline __attribute__((noinline))
#define BO1_DECLSPEC_noreturn __attribute__((noreturn))
#define BO1_DECLSPEC_nothrow __attribute__((nothrow))
#define BO1_DECLSPEC_deprecated __attribute__((deprecated))
#if defined(_WIN32)
#define BO1_DECLSPEC_dllimport __attribute__((dllimport))
#define BO1_DECLSPEC_dllexport __attribute__((dllexport))
#else
#define BO1_DECLSPEC_dllimport
#define BO1_DECLSPEC_dllexport
// Forms only the SDK's COM headers use (d3d9.h): no layout or code depends on them.
#define BO1_DECLSPEC_uuid(x)
#define BO1_DECLSPEC_novtable
#endif

#if !defined(_WIN32)
#include "posix/msvc_intrinsics.h"
// com_math.h's double random() would collide with the C library's long random() (stdlib.h, already included above).
#define random bo1_random
// The MSVC runtime's rand and srand (posix/msvc_crt.cpp): RAND_MAX is 0x7FFF, which the engine divides by everywhere
// (glibc's and newlib's is 2^31 - 1), and the same seed gives the same numbers, from one seed per thread that starts
// at 1, as in the UCRT.
#ifdef __cplusplus
extern "C" {
#endif
int bo1_rand(void);
void bo1_srand(unsigned int seed);
#ifdef __cplusplus
}
// For std::rand in the standard headers (<algorithm>'s random_shuffle), which <cstdlib> declared before this.
namespace std
{
using ::bo1_rand;
using ::bo1_srand;
} // namespace std
#endif
#define rand bo1_rand
#define srand bo1_srand
#undef RAND_MAX
#define RAND_MAX 0x7FFF
#endif
