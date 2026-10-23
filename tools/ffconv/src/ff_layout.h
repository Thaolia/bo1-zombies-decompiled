#pragma once

// Layout of a streamed type on the retail 32-bit target and on the 64-bit target, as flat leaf fields.
// Records come from ff_layouts.gen.h (tools/ffconv/gen/layouts.py); scalars, pointers and arrays are built here.

#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

namespace ff
{
enum LeafKind : uint8_t
{
    LEAF_BYTES,     // copied as is
    LEAF_PTR,       // 4-byte pointer slot, 8 bytes wide on 64-bit
    LEAF_UNION_PTR, // union of a pointer and data of 4 bytes or less
    LEAF_OPAQUE,    // union of pointers and wider data: only its active member may be streamed
};

struct Leaf
{
    uint32_t off32;
    uint32_t off64;
    uint32_t size32;
    uint32_t size64;
    LeafKind kind;
};

// A 4-byte signed integer inside a byte leaf that holds a distance in bytes between elements of one array of a unit
// type (GfxAabbTree::childrenOffset). The bytes are copied like the leaf, then the value is rescaled by the unit's
// size: value64 = value32 / unit32 * unit64, and the inverse when narrowing.
struct ValueFixup
{
    uint32_t off32;
    uint32_t off64;
    uint32_t unit32;
    uint32_t unit64;
    const char *field;
};

struct TypeLayout
{
    const char *name;
    uint32_t size32;
    uint32_t align32;
    uint32_t size64;
    uint32_t align64;
    const Leaf *leaves;
    uint32_t leafCount;
    const ValueFixup *fixups = nullptr;
    uint32_t fixupCount = 0;
};

template <typename T> const TypeLayout &Layout();

namespace detail
{
struct OwnedLayout
{
    std::vector<Leaf> leaves;
    TypeLayout layout;
    std::vector<ValueFixup> fixups;
};

inline const TypeLayout &Own(OwnedLayout &owned, const char *name, uint32_t size32, uint32_t align32, uint32_t size64,
                             uint32_t align64)
{
    owned.layout = {name,
                    size32,
                    align32,
                    size64,
                    align64,
                    owned.leaves.data(),
                    static_cast<uint32_t>(owned.leaves.size()),
                    owned.fixups.data(),
                    static_cast<uint32_t>(owned.fixups.size())};
    return owned.layout;
}

template <typename T> struct Builder
{
    static const TypeLayout &Get()
    {
        static_assert(std::is_arithmetic_v<T> || std::is_enum_v<T>,
                      "this record has no generated layout: add its var* global or check layouts.py");
        static OwnedLayout owned{{{0, 0, sizeof(T), sizeof(T), LEAF_BYTES}}, {}};
        static const TypeLayout &layout = Own(owned, "scalar", sizeof(T), alignof(T), sizeof(T), alignof(T));
        return layout;
    }
};

template <> struct Builder<void>
{
    static const TypeLayout &Get()
    {
        static OwnedLayout owned{{{0, 0, 1, 1, LEAF_BYTES}}, {}};
        static const TypeLayout &layout = Own(owned, "void", 1, 1, 1, 1);
        return layout;
    }
};

template <typename T> struct Builder<T *>
{
    static const TypeLayout &Get()
    {
        static OwnedLayout owned{{{0, 0, 4, 8, LEAF_PTR}}, {}};
        static const TypeLayout &layout = Own(owned, "pointer", 4, 4, 8, 8);
        return layout;
    }
};

template <typename T, size_t N> struct Builder<T[N]>
{
    static const TypeLayout &Get()
    {
        static OwnedLayout owned;
        static const TypeLayout &layout = [] () -> const TypeLayout & {
            const TypeLayout &elem = Layout<T>();
            for (size_t i = 0; i < N; ++i)
                for (uint32_t l = 0; l < elem.leafCount; ++l)
                {
                    Leaf leaf = elem.leaves[l];
                    leaf.off32 += static_cast<uint32_t>(i) * elem.size32;
                    leaf.off64 += static_cast<uint32_t>(i) * elem.size64;
                    owned.leaves.push_back(leaf);
                }
            for (size_t i = 0; i < N; ++i)
                for (uint32_t f = 0; f < elem.fixupCount; ++f)
                {
                    ValueFixup fixup = elem.fixups[f];
                    fixup.off32 += static_cast<uint32_t>(i) * elem.size32;
                    fixup.off64 += static_cast<uint32_t>(i) * elem.size64;
                    owned.fixups.push_back(fixup);
                }
            return Own(owned, elem.name, elem.size32 * N, elem.align32, elem.size64 * N, elem.align64);
        }();
        return layout;
    }
};
} // namespace detail

// Records are explicit specializations in ff_layouts.gen.h; a const record goes through its unqualified one.
template <typename T> const TypeLayout &Layout()
{
    if constexpr (!std::is_same_v<T, std::remove_cv_t<T>>)
        return Layout<std::remove_cv_t<T>>();
    else
        return detail::Builder<T>::Get();
}
} // namespace ff
