#pragma once

// Typed hooks that tools/ffconv/gen/transform_db_load.py puts into ffconv's copy of db_load.cpp.

#include <type_traits>

#include "ff_layout.h"

namespace ff
{
// Streams `size` bytes of `type` starting `startOffset` bytes into its first element (Load_Stream's semantics).
void LoadStream(bool atStreamStart, const TypeLayout &type, uint32_t startOffset, void *ptr, int size);
// The next DB_AllocStreamPos is for an object of this type.
void SetPendingAlloc(const TypeLayout &type);
} // namespace ff

// `var` is the var* global the streamed pointer comes from. When the pointer lies inside the object it points to,
// the object's type describes the bytes (`(byte *)varX`, `&varX->field`); otherwise the pointer is to data a field
// points to (`varX->_1`), described by the pointer's own type.
template <typename V, typename P> void FF_LoadStream(bool atStreamStart, V *var, P *ptr, int size)
{
    if constexpr (!std::is_void_v<V>)
    {
        const char *base = reinterpret_cast<const char *>(var);
        const char *p = reinterpret_cast<const char *>(ptr);
        if (p >= base && p < base + sizeof(V))
        {
            ff::LoadStream(atStreamStart, ff::Layout<V>(), static_cast<uint32_t>(p - base),
                           const_cast<void *>(static_cast<const void *>(ptr)), size);
            return;
        }
    }
    ff::LoadStream(atStreamStart, ff::Layout<P>(), 0, const_cast<void *>(static_cast<const void *>(ptr)), size);
}

template <typename T> void FF_NextAlloc(T *)
{
    ff::SetPendingAlloc(ff::Layout<T>());
}

// A pointer kept in an integer member (MaterialArgumentDef::codeSampler holds the literal's float[4]): the
// allocation holds plain data, so the retail alignment is the 64-bit one too.
template <typename T>
    requires std::is_integral_v<T>
void FF_NextAlloc(T)
{
    ff::SetPendingAlloc(ff::Layout<unsigned char>());
}
