#pragma once

// MSVC's <intrin.h>. compat.h already provides the MSVC intrinsics the engine uses (posix/msvc_intrinsics.h); clang's
// own intrin.h is only meant for MSVC targets and conflicts with the engine's declarations.
// MSVC's intrin.h includes setjmp.h, which rb_backend.cpp relies on for jmp_buf.
#include <setjmp.h>
