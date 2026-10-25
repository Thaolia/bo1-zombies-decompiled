#pragma once
// aarch64 (Switch) has no x86 SSE header. The engine's `#include <xmmintrin.h>` resolves here and gets the SSE
// intrinsics it uses mapped to NEON by sse2neon (third_party/sse2neon). binklib's radbase.h defines lowercase type
// aliases (u8/u16/u32/...) that collide with sse2neon's identifier fragments, so they are suspended across the
// sse2neon include and restored after (handles either include order).
#pragma push_macro("u8")
#pragma push_macro("s8")
#pragma push_macro("u16")
#pragma push_macro("s16")
#pragma push_macro("u32")
#pragma push_macro("s32")
#pragma push_macro("u64")
#pragma push_macro("s64")
#pragma push_macro("f32")
#pragma push_macro("f64")
#undef u8
#undef s8
#undef u16
#undef s16
#undef u32
#undef s32
#undef u64
#undef s64
#undef f32
#undef f64
#include <sse2neon.h>
#pragma pop_macro("f64")
#pragma pop_macro("f32")
#pragma pop_macro("s64")
#pragma pop_macro("u64")
#pragma pop_macro("s32")
#pragma pop_macro("u32")
#pragma pop_macro("s16")
#pragma pop_macro("u16")
#pragma pop_macro("s8")
#pragma pop_macro("u8")
