#pragma once

// Typed hooks that tools/ffconv/gen/transform_db_load.py puts into ffconv's copy of db_load.cpp.

#include <type_traits>

#include "ff_layout.h"

namespace ff
{
// Streams `size` bytes of `type` starting `startOffset` bytes into its first element (Load_Stream's semantics).
void LoadStream(bool atStreamStart, const TypeLayout &type, uint32_t startOffset, void *ptr, int size);

// DB_ConvertOffsetToPointer on `slot`, whose inline branch streams `count` elements of `pointee` (-1: a string).
// `inlineBranch` runs that branch when the retail target cannot be read as the pointee on 64-bit (ff_stream.cpp).
void ConvertOffsetToPointer(unsigned int *slot, const TypeLayout &pointee, int count, const char *site,
                            void (*inlineBranch)(void *), void *context);

template <typename F>
void ConvertOffsetToPointer(unsigned int *slot, const TypeLayout &pointee, int count, const char *site, F &&branch)
{
    ConvertOffsetToPointer(
        slot, pointee, count, site, [](void *f) { (*static_cast<std::remove_reference_t<F> *>(f))(); }, &branch);
}
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
