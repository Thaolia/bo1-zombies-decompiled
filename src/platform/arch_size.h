#pragma once

// The decompiled code pins struct layouts and buffer sizes with the retail 32-bit numbers. On a 64-bit build pointers
// are 8 bytes and the structs that hold them grow; BO1_ARCH_SIZE gives the value for each pointer width, so a
// static_assert keeps catching an unintended layout change on both and a literal allocation stays big enough.
// The 64-bit value is the one measured on Windows x64; the Switch (LP64) agrees unless a struct holds `long` (the
// engine writes `double` for MSVC's `long double`), and the assert then says so.
#define BO1_ARCH_SIZE(size32, size64) (sizeof(void *) == 8 ? (size64) : (size32))

// A class with a base class can differ between compilers: the Itanium C++ ABI (GCC, and clang outside MSVC targets)
// places derived members in the base's tail padding, MSVC does not. Such a struct gets one size per ABI and width.
#if defined(_MSC_VER)
#define BO1_ABI_SIZE(msvc32, msvc64, itanium32, itanium64) BO1_ARCH_SIZE(msvc32, msvc64)
#else
#define BO1_ABI_SIZE(msvc32, msvc64, itanium32, itanium64) BO1_ARCH_SIZE(itanium32, itanium64)
#endif

// The save images (game_sp/g_sp_save*, clientscript/cscr_save.cpp) are the retail 32-bit layout, written through field
// tables with 4-byte pointers. 64-bit builds skip saves (G_SP_SaveGame) until that format has a 64-bit version, so the
// asserts that pin it only bind 32-bit builds.
#define BO1_SAVE_IMAGE_ASSERT(cond, msg) static_assert(sizeof(void *) == 8 || (cond), msg)

// MSVC's long: 4 bytes on every Windows target, 8 on LP64 (Linux, the Switch). Fields and Interlocked operands the
// decompiled code declares long mean 32 bits; they are spelled bo1_long, which is still long for MSVC.
#if defined(_WIN32)
typedef long bo1_long;
typedef unsigned long bo1_ulong;
#else
#include <stdint.h>
typedef int32_t bo1_long;
typedef uint32_t bo1_ulong;
#endif

// An allocation whose retail alignment is below its type's (MatchState is align(64), its snapshot blocks are allocated
// 4-aligned). MSVC copies such a struct with unaligned moves, so the retail value and hunk layout stay; GCC and clang
// copy it with aligned vector moves, which fault, so they get the type's alignment and the slack it can cost.
#if defined(_MSC_VER)
#define BO1_ALLOC_ALIGN(retail, T) (retail)
#define BO1_ALLOC_SLACK(retail, T) 0
#else
#define BO1_ALLOC_ALIGN(retail, T) (alignof(T) > (retail) ? alignof(T) : (retail))
#define BO1_ALLOC_SLACK(retail, T) (alignof(T) > (retail) ? alignof(T) : 0)
#endif
