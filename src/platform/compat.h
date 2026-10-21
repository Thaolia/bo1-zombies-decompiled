#pragma once

// Force-included by CMake in front of every engine file when the compiler is GCC or clang (not MSVC or clang-cl).
// It maps the MSVC dialect the decompiled code is written in onto GNU equivalents without changing behaviour.

#if defined(__MINGW32__)
// MinGW already spells __int8..__int64, __forceinline and the calling conventions; the engine uses them in headers
// that include nothing else first.
#include <_mingw.h>
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
#endif
