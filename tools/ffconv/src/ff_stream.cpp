#include "ff_stream.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <unordered_map>

#include <database/db_ff64.h>
#include <database/db_stream.h>
#include <database/db_stream_load.h>

#include "ff_hooks.h"

// The engine's stream globals: db_load.cpp and db_stream.h name them.
XBlock *g_streamBlocks;
unsigned __int8 *g_streamPos;
unsigned int g_streamPosIndex;
unsigned int g_streamDelayIndex;
unsigned __int8 *g_streamPosArray[7];
unsigned int g_streamPosStackIndex;

namespace ff
{
namespace
{
// One Load_Stream (or string, insert pointer, raw header): where its bytes are in the 32-bit blocks and the 64-bit
// stream, where its 64-bit bytes came from (narrowing) and where its converted bytes went in the output.
struct Chunk
{
    uintptr_t addr32;
    uint32_t size32;
    const TypeLayout *type;
    uint32_t start32; // offset of the first byte inside the type's first element
    uint32_t block;
    uint32_t pos32;
    uint32_t pos64;
    uint32_t size64;
    int64_t src;      // offset of the chunk's bytes in the input (64-bit ones when narrowing), -1 if none
    int64_t file;     // offset in the output body, -1 while not written (runtime block, delayed)
    bool delayed;
};

// The member a union of pointers and wider data holds, as the loader reveals it by walking that member
// (`Load_Operand(0)` on an expressionRpnDataUnion). Its bytes are converted with the member's layout at the union's
// 64-bit offset.
struct Overlay
{
    size_t chunk;
    uintptr_t addr32;
    const TypeLayout *type;
    uint32_t size32;
    uint64_t rel64; // 64-bit offset of the union inside the chunk
    std::vector<uint8_t> bytes;
};

// A converted pointer value to write over the output once every chunk is placed: 8 bytes when widening, 4 when
// narrowing.
struct Patch
{
    size_t chunk;
    uint64_t rel;
    uint64_t value;
};

struct StackEntry
{
    unsigned __int8 *pos;
    unsigned int index;
    uint32_t pos64;
};

uint64_t Map64(const TypeLayout &type, uint64_t off32);

// A byte of a chunk, followed down through the overlays of the unions it falls in: the innermost type, the byte's
// offset in it, the 64-bit offset of that type's start in the chunk, and the leaf holding the byte.
struct Level
{
    const TypeLayout *type;
    uint64_t rel32;
    int64_t base64;
    const Leaf *leaf;

    uint32_t InLeaf() const
    {
        return static_cast<uint32_t>(rel32 % type->size32) - leaf->off32;
    }
    uint64_t Rel64() const
    {
        return static_cast<uint64_t>(base64 + static_cast<int64_t>(Map64(*type, rel32)));
    }
};

bool g_narrow;
const uint8_t *g_in;
size_t g_inSize;
size_t g_inPos;
std::vector<uint8_t> g_out;
std::vector<Chunk> g_chunks;
std::map<uintptr_t, size_t> g_live;      // chunk start address -> chunk, for the bytes each address now holds
std::map<uint32_t, size_t> g_live64[7]; // narrowing: chunk start in the 64-bit block -> chunk
std::unordered_map<uintptr_t, uint64_t> g_wide; // narrowing: 64-bit value of each pointer slot
std::vector<Overlay> g_overlays;
std::multimap<uintptr_t, size_t> g_overlayAt;
std::vector<Patch> g_patches;
std::vector<ByteRange> g_deadBytes;
std::vector<size_t> g_delayed;
StackEntry g_stack[64];
uint32_t g_pos64;
uint32_t g_posArray64[7];
StreamStats g_stats;
std::string g_error;
int g_asset;
const void *g_assetHeader;

// A run of 32-bit bytes inside one chunk (index npos: bytes outside any chunk, which the retail stream leaves zero).
struct Segment
{
    size_t chunk;
    uintptr_t offset;
    size_t size;
};

// Narrowing: a pointee streamed inline, narrowed, and the retail bytes its slot's retail value targets, compared once
// the output is final.
struct ReinlineCheck
{
    std::string site;
    std::vector<uint8_t> narrowed;
    std::vector<Segment> target;
};

std::vector<Reinline> g_reinlines;                         // widening: the slots streamed inline
std::unordered_map<uint64_t, uint32_t> g_reinlineIn;       // narrowing: IWffs slot offset -> retail value
std::unordered_map<uintptr_t, uint32_t> g_reinlinedSlots;  // narrowing: such slots once converted -> retail value
std::vector<ReinlineCheck> g_reinlineChecks;
std::vector<PointeeMismatch> g_mismatches;

void Fail(const std::string &message)
{
    if (g_error.empty())
        g_error = message;
}

uint32_t AlignUp(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

// 64-bit offset of a byte offset into a run of `type` elements. Padding maps to the next leaf's 64-bit start, so the
// end of a partial copy lands where the 64-bit layout puts the next field.
uint64_t Map64(const TypeLayout &type, uint64_t off32)
{
    const uint64_t element = off32 / type.size32;
    const uint32_t inside = static_cast<uint32_t>(off32 % type.size32);
    for (uint32_t i = 0; i < type.leafCount; ++i)
    {
        const Leaf &leaf = type.leaves[i];
        if (inside < leaf.off32)
            return element * type.size64 + leaf.off64;
        if (inside < leaf.off32 + leaf.size32)
            return element * type.size64 + leaf.off64 + (inside - leaf.off32);
    }
    return (element + 1) * type.size64;
}

// The inverse of Map64 for an offset that starts a leaf or lies inside a byte leaf.
uint64_t Map32(const TypeLayout &type, uint64_t off64)
{
    const uint64_t element = off64 / type.size64;
    const uint32_t inside = static_cast<uint32_t>(off64 % type.size64);
    for (uint32_t i = 0; i < type.leafCount; ++i)
    {
        const Leaf &leaf = type.leaves[i];
        if (inside < leaf.off64)
            return element * type.size32 + leaf.off32;
        if (inside < leaf.off64 + leaf.size64)
            return element * type.size32 + leaf.off32 + std::min(inside - leaf.off64, leaf.size32);
    }
    return (element + 1) * type.size32;
}

const Leaf *LeafAt(const TypeLayout &type, uint64_t off32)
{
    const uint32_t inside = static_cast<uint32_t>(off32 % type.size32);
    for (uint32_t i = 0; i < type.leafCount; ++i)
        if (type.leaves[i].off32 <= inside && inside < type.leaves[i].off32 + type.leaves[i].size32)
            return &type.leaves[i];
    return nullptr;
}

bool IsPlainData(const TypeLayout &type)
{
    return type.size32 == type.size64 && type.leafCount == 1 && type.leaves[0].kind == LEAF_BYTES &&
           type.leaves[0].size32 == type.size32 && type.fixupCount == 0;
}

constexpr uint64_t WidenPointer(uint32_t value)
{
    if (value == 0xFFFFFFFFu)
        return DB_FF64_FOLLOWS;
    if (value == 0xFFFFFFFEu)
        return DB_FF64_FOLLOWS_INSERT;
    return value;
}

constexpr uint32_t NarrowPointer(uint64_t value)
{
    if (value == DB_FF64_FOLLOWS)
        return 0xFFFFFFFFu;
    if (value == DB_FF64_FOLLOWS_INSERT)
        return 0xFFFFFFFEu;
    return static_cast<uint32_t>(value);
}

// The sentinel widen/narrow is the hinge between the retail stream and the 64-bit one: the retail inline markers
// (0xFFFFFFFF "the data follows", 0xFFFFFFFE "follows, with an insert pointer") must widen to the exact 64-bit
// values the loader tests a pointer slot against, and narrow back unchanged. The loader compares a widened slot with
// `(T*)-1` / `(T*)-2` (db_load.cpp: Load_snd_alias_t, Load_SoundFileRef, ...), so the sentinels must be the 64-bit
// all-ones / all-ones-but-one patterns, not the zero-extended 32-bit ones. These round-trip checks run at every
// ffconv build so the encoding can never silently drift.
static_assert(DB_FF64_FOLLOWS == static_cast<uint64_t>(-1), "the inline sentinel must be 64-bit (T*)-1, not a zero-extended 0xFFFFFFFF");
static_assert(DB_FF64_FOLLOWS_INSERT == static_cast<uint64_t>(-2), "the insert sentinel must be 64-bit (T*)-2");
static_assert(WidenPointer(0xFFFFFFFFu) == DB_FF64_FOLLOWS, "0xFFFFFFFF widens to the follows sentinel");
static_assert(WidenPointer(0xFFFFFFFEu) == DB_FF64_FOLLOWS_INSERT, "0xFFFFFFFE widens to the insert sentinel");
static_assert(NarrowPointer(DB_FF64_FOLLOWS) == 0xFFFFFFFFu, "the follows sentinel narrows back to 0xFFFFFFFF");
static_assert(NarrowPointer(DB_FF64_FOLLOWS_INSERT) == 0xFFFFFFFEu, "the insert sentinel narrows back to 0xFFFFFFFE");
static_assert(NarrowPointer(WidenPointer(0xFFFFFFFFu)) == 0xFFFFFFFFu, "round-trip: the follows sentinel");
static_assert(NarrowPointer(WidenPointer(0xFFFFFFFEu)) == 0xFFFFFFFEu, "round-trip: the insert sentinel");
static_assert(NarrowPointer(WidenPointer(0u)) == 0u, "round-trip: a null slot");
static_assert(NarrowPointer(WidenPointer(0x00ABCDEFu)) == 0x00ABCDEFu, "round-trip: an ordinary slot value");
// A union of a pointer and 4-byte data is widened through its pointer member; the data members read the low 4 bytes,
// so widening must leave those untouched for every value, the sentinels included (~0ull and ~1ull keep 0xFFFFFFFF /
// 0xFFFFFFFE in their low word).
static_assert(static_cast<uint32_t>(WidenPointer(0xFFFFFFFFu)) == 0xFFFFFFFFu, "widen keeps the low 32 bits: follows");
static_assert(static_cast<uint32_t>(WidenPointer(0xFFFFFFFEu)) == 0xFFFFFFFEu, "widen keeps the low 32 bits: insert");
static_assert(static_cast<uint32_t>(WidenPointer(0x12345678u)) == 0x12345678u, "widen keeps the low 32 bits: data");

std::string AssetName();

// A LEAF_HANDLE integer holds a pointer at runtime. A retail zone can only stream it as 0: any other value would be a
// 32-bit runtime address, which the 64-bit engine cannot use, so widening refuses it. Narrowing requires the upper
// half to be the sign extension of the lower one.
void ConvertHandle(const TypeLayout &type, const Leaf &leaf, uint8_t *at32, uint8_t *at64)
{
    const auto where = [&] {
        return std::string(type.name) + " byte " + std::to_string(leaf.off32) + " (widened integer, asset " +
               std::to_string(g_asset) + " '" + AssetName() + "')";
    };
    if (g_narrow)
    {
        int64_t wide;
        std::memcpy(&wide, at64, 8);
        if (wide != static_cast<int32_t>(wide))
        {
            Fail(where() + ": " + std::to_string(wide) + " is not a sign-extended 4-byte value");
            return;
        }
        const int32_t value = static_cast<int32_t>(wide);
        std::memcpy(at32, &value, 4);
    }
    else
    {
        int32_t value;
        std::memcpy(&value, at32, 4);
        if (value != 0)
        {
            Fail(where() + ": the zone streams " + std::to_string(value) + ", a runtime handle must be 0");
            return;
        }
        const int64_t wide = value;
        std::memcpy(at64, &wide, 8);
    }
    ++g_stats.handles;
}

// Rescales the value fixups of `type`'s element `element` once its leaves are converted. A distance that is not a
// whole number of units, or that leaves the elements being converted, means the table describes the field wrongly.
void FixValues(uint8_t *p32, uint8_t *p64, const TypeLayout &type, uint64_t start, uint64_t end, uint64_t base64,
               uint64_t element)
{
    for (uint32_t i = 0; i < type.fixupCount; ++i)
    {
        const ValueFixup &fixup = type.fixups[i];
        const uint64_t at32 = element * type.size32 + fixup.off32;
        if (at32 + 4 <= start || at32 >= end)
            continue;
        if (at32 < start || at32 + 4 > end)
        {
            Fail(std::string(fixup.field) + ": a stream boundary cuts a value fixup");
            continue;
        }
        uint8_t *v32 = p32 + (at32 - start);
        uint8_t *v64 = p64 + (element * type.size64 + fixup.off64 - base64);
        const uint32_t from = g_narrow ? fixup.unit64 : fixup.unit32;
        const uint32_t to = g_narrow ? fixup.unit32 : fixup.unit64;
        int32_t value;
        std::memcpy(&value, g_narrow ? v64 : v32, 4);
        if (value % static_cast<int32_t>(from))
        {
            Fail(std::string(fixup.field) + ": " + std::to_string(value) + " is not a multiple of " +
                 std::to_string(from) + " bytes");
            continue;
        }
        const int64_t units = value / static_cast<int32_t>(from);
        // Only checkable when the unit is the converted type itself: the target must be one of the converted
        // elements, or the one just past them.
        if (fixup.unit32 == type.size32 && fixup.unit64 == type.size64)
        {
            const int64_t target = static_cast<int64_t>(element) + units;
            if (target < static_cast<int64_t>(start / type.size32) ||
                target > static_cast<int64_t>((end + type.size32 - 1) / type.size32))
            {
                Fail(std::string(fixup.field) + ": " + std::to_string(value) +
                     " bytes points outside the array being converted");
                continue;
            }
        }
        const int32_t scaled = static_cast<int32_t>(units * to);
        std::memcpy(g_narrow ? v32 : v64, &scaled, 4);
        ++g_stats.valuesFixed;
    }
}

// Converts `size` 32-bit bytes of `type`, starting `start` bytes into its first element, between the 32-bit bytes
// at `p32` and the 64-bit bytes at `p64` (zeroed by the caller when widening). A union of pointers and wider data
// keeps its 32-bit bytes at its start until an overlay says more. Narrowing keeps each pointer's 64-bit value.
void Convert(uint8_t *p32, uint8_t *p64, const TypeLayout &type, uint64_t start, uint64_t size)
{
    if (IsPlainData(type))
    {
        if (g_narrow)
            std::memcpy(p32, p64, size);
        else
            std::memcpy(p64, p32, size);
        return;
    }
    const uint64_t end = start + size;
    const uint64_t base64 = Map64(type, start);
    for (uint64_t element = start / type.size32; element * type.size32 < end; ++element)
    {
        for (uint32_t i = 0; i < type.leafCount; ++i)
        {
            const Leaf &leaf = type.leaves[i];
            const uint64_t leafBegin = element * type.size32 + leaf.off32;
            const uint64_t from = std::max(leafBegin, start);
            const uint64_t to = std::min(leafBegin + leaf.size32, end);
            if (from >= to)
                continue;
            uint8_t *at64 = p64 + (element * type.size64 + leaf.off64 + (from - leafBegin) - base64);
            uint8_t *at32 = p32 + (from - start);
            if (leaf.kind == LEAF_BYTES || leaf.kind == LEAF_OPAQUE)
            {
                if (g_narrow)
                    std::memcpy(at32, at64, to - from);
                else
                    std::memcpy(at64, at32, to - from);
                continue;
            }
            if (from != leafBegin || to != leafBegin + leaf.size32)
            {
                Fail(std::string(type.name) + ": a stream boundary cuts a " +
                     (leaf.kind == LEAF_HANDLE ? "widened integer" : "pointer"));
                continue;
            }
            if (leaf.kind == LEAF_HANDLE)
            {
                ConvertHandle(type, leaf, at32, at64);
                continue;
            }
            // A union of a pointer and 4-byte data is widened like the pointer: its data members read the low 4
            // bytes, which keep their value, and the pointer member keeps its markers.
            if (g_narrow)
            {
                uint64_t wide;
                std::memcpy(&wide, at64, 8);
                uint32_t value = NarrowPointer(wide);
                // A slot the widening streamed inline gets its retail offset back, so the loader takes the offset
                // branch, where ConvertOffsetToPointer skips the inline copy.
                if (wide == DB_FF64_FOLLOWS && !g_reinlineIn.empty())
                    if (const auto it = g_reinlineIn.find(static_cast<uint64_t>(at64 - g_in)); it != g_reinlineIn.end())
                    {
                        value = it->second;
                        g_reinlinedSlots[reinterpret_cast<uintptr_t>(at32)] = value;
                    }
                std::memcpy(at32, &value, 4);
                g_wide[reinterpret_cast<uintptr_t>(at32)] = wide;
            }
            else
            {
                uint32_t value;
                std::memcpy(&value, at32, 4);
                const uint64_t wide = WidenPointer(value);
                std::memcpy(at64, &wide, 8);
            }
        }
        FixValues(p32, p64, type, start, end, base64, element);
    }
}

size_t Record(const Chunk &chunk)
{
    const uintptr_t start = chunk.addr32;
    const uintptr_t end = start + chunk.size32;
    auto it = g_live.lower_bound(start);
    if (it != g_live.begin())
    {
        auto prev = std::prev(it);
        if (prev->first + g_chunks[prev->second].size32 > start)
            it = prev;
    }
    while (it != g_live.end() && it->first < end)
        it = g_live.erase(it);
    g_chunks.push_back(chunk);
    const size_t index = g_chunks.size() - 1;
    if (chunk.size32)
        g_live[start] = index;
    if (g_narrow && chunk.block < 7 && chunk.size64)
    {
        std::map<uint32_t, size_t> &live = g_live64[chunk.block];
        auto at = live.lower_bound(chunk.pos64);
        if (at != live.begin())
        {
            auto prev = std::prev(at);
            if (prev->first + g_chunks[prev->second].size64 > chunk.pos64)
                at = prev;
        }
        while (at != live.end() && at->first < chunk.pos64 + chunk.size64)
            at = live.erase(at);
        live[chunk.pos64] = index;
    }
    ++g_stats.chunks;
    return index;
}

const Chunk *Find(uintptr_t addr, size_t *index)
{
    auto it = g_live.upper_bound(addr);
    if (it == g_live.begin())
        return nullptr;
    --it;
    const Chunk &chunk = g_chunks[it->second];
    if (addr >= chunk.addr32 + chunk.size32)
        return nullptr;
    *index = it->second;
    return &chunk;
}

const Chunk *Find64(uint32_t block, uint32_t pos64)
{
    std::map<uint32_t, size_t> &live = g_live64[block];
    auto it = live.upper_bound(pos64);
    if (it == live.begin())
        return nullptr;
    --it;
    const Chunk &chunk = g_chunks[it->second];
    return pos64 < chunk.pos64 + chunk.size64 ? &chunk : nullptr;
}

const Overlay *FindOverlay(size_t chunk, uintptr_t unionStart)
{
    const Overlay *found = nullptr;
    for (auto [it, end] = g_overlayAt.equal_range(unionStart); it != end; ++it)
        if (g_overlays[it->second].chunk == chunk)
            found = &g_overlays[it->second];
    return found;
}

// Stops at the first leaf that is not a described union (at the outermost leaf without `overlays`); false if the byte
// is in no leaf.
bool Descend(const Chunk &chunk, size_t index, uintptr_t addr, Level &level, bool overlays = true)
{
    level = {chunk.type, chunk.start32 + (addr - chunk.addr32),
             -static_cast<int64_t>(Map64(*chunk.type, chunk.start32)), nullptr};
    for (;;)
    {
        level.leaf = LeafAt(*level.type, level.rel32);
        if (!level.leaf)
            return false;
        if (level.leaf->kind != LEAF_OPAQUE || !overlays)
            return true;
        const uint32_t inLeaf = level.InLeaf();
        const Overlay *overlay = FindOverlay(index, addr - inLeaf);
        if (!overlay)
            return true;
        level = {overlay->type, inLeaf, static_cast<int64_t>(overlay->rel64), nullptr};
    }
}

// The input bytes of the next `size` bytes of the stream, or null past its end.
const uint8_t *Take(size_t size)
{
    if (g_inPos + size > g_inSize)
    {
        Fail("read past the end of the zone body");
        return nullptr;
    }
    const uint8_t *data = g_in + g_inPos;
    g_inPos += size;
    return data;
}

// Converts the chunk's input bytes and appends the result to the output: 64-bit bytes when widening, the 32-bit
// bytes the loader now holds when narrowing.
void Emit(size_t index)
{
    Chunk &chunk = g_chunks[index];
    uint8_t *p32 = reinterpret_cast<uint8_t *>(chunk.addr32);
    const uint8_t *in = Take(g_narrow ? chunk.size64 : chunk.size32);
    if (!in)
    {
        std::memset(p32, 0, chunk.size32);
        return;
    }
    chunk.file = static_cast<int64_t>(g_out.size());
    chunk.src = in - g_in;
    if (g_narrow)
    {
        // Padding has no leaf: zero it as the retail stream does, so a padding byte that holds data shows up.
        std::memset(p32, 0, chunk.size32);
        Convert(p32, const_cast<uint8_t *>(in), *chunk.type, chunk.start32, chunk.size32);
        g_out.insert(g_out.end(), p32, p32 + chunk.size32);
    }
    else
    {
        std::memcpy(p32, in, chunk.size32);
        g_out.resize(g_out.size() + chunk.size64);
        Convert(p32, g_out.data() + chunk.file, *chunk.type, chunk.start32, chunk.size32);
    }
}

// A Load_Stream with atStreamStart false copies nothing; inside a union of pointers and wider data it tells which
// member the union holds. Narrowing converts that member again at once, since the loader reads it next.
void Describe(const TypeLayout &type, uint32_t startOffset, const void *ptr, int size)
{
    size_t index;
    const uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    const Chunk *chunk = Find(addr, &index);
    Level level;
    if (!chunk || startOffset != 0 || !Descend(*chunk, index, addr, level) || level.leaf->kind != LEAF_OPAQUE)
        return;
    if (level.InLeaf() != 0 || static_cast<uint32_t>(size) > level.leaf->size32)
    {
        Fail(std::string(level.type->name) + ": a union member is walked away from the union's start");
        return;
    }
    uint8_t *bytes = static_cast<uint8_t *>(const_cast<void *>(ptr));
    const uint64_t rel64 = level.Rel64();
    if (g_narrow)
    {
        if (chunk->src < 0)
        {
            Fail(std::string(type.name) + ": a union member is described before its bytes are read");
            return;
        }
        Convert(bytes, const_cast<uint8_t *>(g_in + chunk->src + rel64), type, 0, size);
        const size_t at = chunk->file + (addr - chunk->addr32);
        std::memcpy(g_out.data() + at, bytes, size);
        if (static_cast<uint32_t>(size) < level.leaf->size32)
            g_deadBytes.push_back({at + size, level.leaf->size32 - static_cast<size_t>(size)});
    }
    g_overlays.push_back({index, addr, &type, static_cast<uint32_t>(size), rel64, {bytes, bytes + size}});
    g_overlayAt.emplace(addr, g_overlays.size() - 1);
}

// An offset pointer slot and the streamed byte its value targets.
struct Target
{
    size_t slotChunk;
    size_t patch;     // the slot's entry in g_patches
    uint32_t block;
    uintptr_t addr32; // the targeted byte in the 32-bit blocks
    uint64_t pos64;   // its position in the 64-bit block
};

// The pointer slot at `slot` holds the encoded offset of a byte of an earlier chunk. Widening records the encoded
// 64-bit offset of the same byte for the output; narrowing finds the slot's 64-bit value, puts the encoded 32-bit
// offset of the same byte in the slot and records it for the output.
bool Translate(unsigned int *slot, Target &target)
{
    const uintptr_t slotAddr = reinterpret_cast<uintptr_t>(slot);
    size_t slotChunk;
    const Chunk *holder = Find(slotAddr, &slotChunk);
    Level where;
    if (!holder || !Descend(*holder, slotChunk, slotAddr, where) || where.leaf->kind == LEAF_OPAQUE)
    {
        Fail(std::string(holder ? holder->type->name : "?") + ": a converted pointer slot is not in a known field");
        return false;
    }
    if (where.leaf->kind == LEAF_BYTES || where.leaf->kind == LEAF_HANDLE || where.InLeaf() != 0)
    {
        Fail(std::string(where.type->name) + ": a converted pointer is not a pointer field");
        return false;
    }
    ++g_stats.pointersTranslated;
    if (!g_narrow)
    {
        const uint32_t value = *slot;
        const uint32_t block = (value - 1) >> 29;
        const uintptr_t targetAddr = block < 7 ? reinterpret_cast<uintptr_t>(g_streamBlocks[block].data +
                                                                             ((value - 1) & 0x1FFFFFFF))
                                               : 0;
        size_t targetChunk;
        const Chunk *chunk = targetAddr ? Find(targetAddr, &targetChunk) : nullptr;
        Level at;
        if (!chunk || chunk->block != block || !Descend(*chunk, targetChunk, targetAddr, at))
        {
            Fail(std::string(holder->type->name) + ": a pointer targets no streamed chunk");
            return false;
        }
        target = {slotChunk, g_patches.size(), block, targetAddr, chunk->pos64 + at.Rel64()};
        g_patches.push_back(
            {slotChunk, where.Rel64(), DB_FF64_EncodeOffset(block, static_cast<uint32_t>(target.pos64))});
        return true;
    }
    const auto wide = g_wide.find(slotAddr);
    const uint32_t block = wide != g_wide.end() ? DB_FF64_OffsetBlock(wide->second) : 7;
    const Chunk *chunk = block < 7 ? Find64(block, DB_FF64_OffsetInBlock(wide->second)) : nullptr;
    if (!chunk)
    {
        Fail(std::string(holder->type->name) + ": a 64-bit pointer targets no streamed chunk");
        return false;
    }
    const uint64_t abs64 = DB_FF64_OffsetInBlock(wide->second) - chunk->pos64 + Map64(*chunk->type, chunk->start32);
    const uint32_t off32 = chunk->pos32 + static_cast<uint32_t>(Map32(*chunk->type, abs64) - chunk->start32);
    const uint32_t value = ((block << 29) | off32) + 1;
    *slot = value;
    target = {slotChunk, g_patches.size(), block, reinterpret_cast<uintptr_t>(g_streamBlocks[block].data + off32),
              DB_FF64_OffsetInBlock(wide->second)};
    g_patches.push_back({slotChunk, slotAddr - holder->addr32, value});
    return true;
}

// Why the loader cannot read `count` elements of `type` at an offset pointer's target, or an empty string. The
// retail linker shares identical bytes between objects of any type, so the target can belong to another type: that
// is harmless only where every leaf of the pointee falls on a leaf of the same kind, at the 64-bit position the
// 64-bit loader reads it from.
std::string CheckPointee(const Target &target, const TypeLayout &type, uint64_t count)
{
    const bool plain = IsPlainData(type);
    const uint64_t elements = plain ? 1 : count;
    const uint32_t leafCount = plain ? 1 : type.leafCount;
    for (uint64_t e = 0; e < elements; ++e)
        for (uint32_t i = 0; i < leafCount; ++i)
        {
            const Leaf &leaf = type.leaves[i];
            uintptr_t addr = target.addr32 + static_cast<uintptr_t>(e * type.size32 + leaf.off32);
            uint64_t want64 = target.pos64 + e * type.size64 + leaf.off64;
            uint64_t left = plain ? count * type.size32 : leaf.size32;
            while (left)
            {
                const auto at = [&] { return "byte " + std::to_string(addr - target.addr32); };
                size_t index;
                const Chunk *chunk = Find(addr, &index);
                Level level;
                // A union of the pointee is compared as a whole: its retail bytes, and so the member the engine
                // reads, are the target's.
                if (!chunk || chunk->block != target.block ||
                    !Descend(*chunk, index, addr, level, leaf.kind != LEAF_OPAQUE))
                    return at() + " is not in a streamed field";
                const uint64_t at64 = chunk->pos64 + level.Rel64();
                if (at64 != want64)
                    return at() + " (" + level.type->name + ") is at 64-bit byte " +
                           std::to_string(at64 - target.pos64) + ", the pointee reads it at " +
                           std::to_string(want64 - target.pos64);
                const bool pointer = level.leaf->kind == LEAF_PTR || level.leaf->kind == LEAF_UNION_PTR;
                const bool handle = level.leaf->kind == LEAF_HANDLE;
                if (leaf.kind == LEAF_BYTES)
                {
                    if (pointer || handle)
                        return at() + " is in a " + (pointer ? "pointer" : "widened integer") + " of " +
                               level.type->name;
                    const uint64_t n = std::min<uint64_t>({left, level.leaf->size32 - level.InLeaf(),
                                                           chunk->addr32 + chunk->size32 - addr});
                    addr += static_cast<uintptr_t>(n);
                    want64 += n;
                    left -= n;
                    continue;
                }
                const bool same = level.InLeaf() == 0 &&
                                  (leaf.kind == LEAF_OPAQUE   ? level.leaf->kind == LEAF_OPAQUE &&
                                                                    level.leaf->size32 == leaf.size32 &&
                                                                    level.leaf->size64 == leaf.size64
                                   : leaf.kind == LEAF_HANDLE ? handle
                                                              : pointer);
                if (!same)
                    return at() + " is a " + (leaf.kind == LEAF_HANDLE ? "widened integer" : "pointer") +
                           " of the pointee, not of " + level.type->name;
                left = 0;
            }
        }
    return {};
}

// Widening: the retail bytes of [addr, addr + size) in a 32-bit block, from the input; bytes no chunk streams (block
// alignment, runtime blocks, insert pointers) are 0, as in the retail stream. False on a delayed chunk not read yet.
bool CopyInput(uintptr_t addr, uint32_t block, uint8_t *dst, size_t size)
{
    for (size_t done = 0; done < size;)
    {
        size_t index;
        const Chunk *chunk = Find(addr + done, &index);
        if (!chunk || chunk->block != block)
        {
            dst[done++] = 0;
            continue;
        }
        const size_t inChunk = addr + done - chunk->addr32;
        const size_t n = std::min<size_t>(size - done, chunk->size32 - inChunk);
        if (chunk->src >= 0)
            std::memcpy(dst + done, g_in + chunk->src + inChunk, n);
        else if (chunk->delayed)
            return false;
        else
            std::memset(dst + done, 0, n);
        done += n;
    }
    return true;
}

// Whether a leaf that the loader would follow or decode (a pointer, a union) is not zero in `count` elements. A
// widened integer is plain data to the loader; Convert checks its value.
bool HoldsPointers(const TypeLayout &type, uint64_t count, const uint8_t *bytes)
{
    if (IsPlainData(type))
        return false;
    for (uint64_t e = 0; e < count; ++e)
        for (uint32_t i = 0; i < type.leafCount; ++i)
        {
            const Leaf &leaf = type.leaves[i];
            if (leaf.kind == LEAF_BYTES || leaf.kind == LEAF_HANDLE)
                continue;
            const uint8_t *p = bytes + e * type.size32 + leaf.off32;
            if (std::any_of(p, p + leaf.size32, [] (uint8_t b) { return b != 0; }))
                return true;
        }
    return false;
}

// A pointer the walk loaded into a 32-bit block, or null.
const void *BlockPointer(const void *slot)
{
    const void *p;
    std::memcpy(&p, slot, sizeof(p));
    const uintptr_t addr = reinterpret_cast<uintptr_t>(p);
    for (int b = 0; b < 7; ++b)
    {
        const uintptr_t data = reinterpret_cast<uintptr_t>(g_streamBlocks[b].data);
        if (data && addr >= data && addr < data + g_streamBlocks[b].size)
            return p;
    }
    return nullptr;
}

// The name of the asset being loaded, when its header starts with a loaded string.
std::string AssetName()
{
    const void *header = g_assetHeader ? BlockPointer(g_assetHeader) : nullptr;
    const char *name = header ? static_cast<const char *>(BlockPointer(header)) : nullptr;
    std::string text;
    for (; name && *name && text.size() < 96; ++name)
        if (*name < 0x20 || *name > 0x7e)
            return "?";
        else
            text += *name;
    return name ? text : "?";
}

void Mismatch(const Target &target, const TypeLayout &pointee, uint64_t count, const char *site,
              const std::string &reason)
{
    PointeeMismatch mismatch{site,
                             std::string(pointee.name) + " (" + std::to_string(pointee.size32) + "/" +
                                 std::to_string(pointee.size64) + " bytes) x" + std::to_string(count),
                             "?", reason, target.block, false, g_asset, AssetName()};
    size_t index;
    const Chunk *chunk = Find(target.addr32, &index);
    Level level;
    if (chunk && Descend(*chunk, index, target.addr32, level))
        mismatch.target = std::string(level.type->name) + " (" + std::to_string(level.type->size32) + " bytes) +" +
                          std::to_string(level.rel32 % level.type->size32);
    if (!g_narrow)
    {
        std::vector<uint8_t> bytes(static_cast<size_t>(count * pointee.size32));
        mismatch.zero = CopyInput(target.addr32, target.block, bytes.data(), bytes.size()) &&
                        std::all_of(bytes.begin(), bytes.end(), [] (uint8_t b) { return b == 0; });
    }
    g_mismatches.push_back(mismatch);
}

// The 32-bit side of the walk while an inline branch streams a pointee that the retail stream does not hold: its
// positions are put back afterwards, and its chunks leave the 32-bit address map. The 64-bit positions keep the
// pointee, which the IWffs stream does hold.
struct Detour
{
    unsigned __int8 *pos;
    unsigned __int8 *posArray[7];
    unsigned int index;
    unsigned int stackIndex;
    uint32_t end32[7];
    size_t firstChunk;
    size_t delayed;
};

Detour BeginDetour()
{
    Detour detour{g_streamPos, {}, g_streamPosIndex, g_streamPosStackIndex, {}, g_chunks.size(), g_delayed.size()};
    std::copy(std::begin(g_streamPosArray), std::end(g_streamPosArray), detour.posArray);
    std::copy(std::begin(g_stats.end32), std::end(g_stats.end32), detour.end32);
    return detour;
}

void EndDetour(const Detour &detour, const char *site)
{
    if (g_streamPosIndex != detour.index || g_streamPosStackIndex != detour.stackIndex ||
        g_delayed.size() != detour.delayed)
        Fail(std::string(site) + ": the inline branch left the stream in another block or delayed data");
    g_streamPos = detour.pos;
    std::copy(std::begin(detour.posArray), std::end(detour.posArray), g_streamPosArray);
    std::copy(std::begin(detour.end32), std::end(detour.end32), g_stats.end32);
    for (size_t i = detour.firstChunk; i < g_chunks.size(); ++i)
    {
        const auto it = g_live.find(g_chunks[i].addr32);
        if (it != g_live.end() && it->second == i)
            g_live.erase(it);
    }
}

// Widening: the offset at `slot` targets retail bytes the 64-bit loader cannot read as the pointee. The IWffs slot
// says "follows" instead, and the loader's own inline branch streams the target's retail bytes, read as `count`
// elements of the pointee, at this point of the 64-bit stream. Only pointees whose pointers and unions are all zero
// qualify: the retail engine reads the target's loaded bytes there, the inline copy would be followed.
bool StreamInline(unsigned int *slot, const Target &target, const TypeLayout &pointee, uint64_t count, size_t size32,
              const char *site, void (*branch)(void *), void *context)
{
    const Chunk &holder = g_chunks[target.slotChunk];
    if (holder.file < 0 || g_streamPosIndex == 2 || g_streamPosIndex == 3)
    {
        Fail(std::string(site) + ": a pointee to stream inline is in a delayed or runtime block");
        return false;
    }
    std::vector<uint8_t> bytes(size32);
    if (!CopyInput(target.addr32, target.block, bytes.data(), bytes.size()))
    {
        Fail(std::string(site) + ": the target of a pointee to stream inline is not read yet");
        return false;
    }
    if (HoldsPointers(pointee, count, bytes.data()))
    {
        Fail(std::string(site) + ": the target of a pointee to stream inline holds non-zero pointers");
        return false;
    }
    Patch &patch = g_patches[target.patch];
    patch.value = DB_FF64_FOLLOWS;
    g_reinlines.push_back({static_cast<uint64_t>(holder.file) + patch.rel, *slot});

    const Detour detour = BeginDetour();
    const uint8_t *in = g_in;
    const size_t inSize = g_inSize;
    const size_t inPos = g_inPos;
    g_in = bytes.data();
    g_inSize = bytes.size();
    g_inPos = 0;
    branch(context);
    const bool whole = g_inPos == bytes.size();
    g_in = in;
    g_inSize = inSize;
    g_inPos = inPos;
    EndDetour(detour, site);
    if (!whole)
        Fail(std::string(site) + ": the inline branch did not stream the whole pointee");
    ++g_stats.reinlined;
    return true;
}

// Narrowing: a slot the widening streamed inline. The loader's inline branch reads the pointee from the IWffs stream
// as the 64-bit loader would; its 32-bit bytes leave the output, and must equal the retail bytes the slot's retail
// value targets (checked in EndZone, once pointers are patched).
void SkipReinlined(unsigned int *slot, uint32_t value32, const TypeLayout &pointee, int count, const char *site,
                   void (*branch)(void *), void *context)
{
    const uint32_t block = (value32 - 1) >> 29;
    if (block >= 7 || !g_streamBlocks[block].data)
    {
        Fail(std::string(site) + ": the retail value of an inline pointee targets no block");
        return;
    }
    const uintptr_t addr = reinterpret_cast<uintptr_t>(g_streamBlocks[block].data + ((value32 - 1) & 0x1FFFFFFF));
    const size_t size32 = count < 0 ? std::strlen(reinterpret_cast<const char *>(addr)) + 1
                                    : static_cast<size_t>(count) * pointee.size32;
    ReinlineCheck check{site, {}, {}};
    for (size_t done = 0; done < size32;)
    {
        size_t index;
        const Chunk *chunk = Find(addr + done, &index);
        if (!chunk || chunk->block != block)
        {
            check.target.push_back({static_cast<size_t>(-1), 0, 1});
            ++done;
            continue;
        }
        const uintptr_t inChunk = addr + done - chunk->addr32;
        const size_t n = std::min<size_t>(size32 - done, chunk->size32 - inChunk);
        check.target.push_back({index, inChunk, n});
        done += n;
    }

    const Detour detour = BeginDetour();
    const size_t outMark = g_out.size();
    const size_t deadMark = g_deadBytes.size();
    branch(context);
    check.narrowed.assign(g_out.begin() + static_cast<std::ptrdiff_t>(outMark), g_out.end());
    g_out.resize(outMark);
    g_deadBytes.resize(deadMark);
    for (size_t i = detour.firstChunk; i < g_chunks.size(); ++i)
        g_chunks[i].file = -1;
    EndDetour(detour, site);
    *slot = static_cast<unsigned int>(addr);
    g_reinlineChecks.push_back(std::move(check));
    ++g_stats.reinlined;
}

void Advance(uint32_t size32, uint32_t size64)
{
    g_streamPos += size32;
    g_pos64 += size64;
    const uint32_t b = g_streamPosIndex;
    g_stats.end32[b] = std::max<uint32_t>(g_stats.end32[b],
                                          static_cast<uint32_t>(g_streamPos - g_streamBlocks[b].data));
    g_stats.end64[b] = std::max(g_stats.end64[b], g_pos64);
}

uint32_t Pos32()
{
    return static_cast<uint32_t>(g_streamPos - g_streamBlocks[g_streamPosIndex].data);
}

// A null-terminated string read straight from the stream (Load_XStringCustom): the same bytes on both sides.
void LoadString(const char **str)
{
    uint8_t *dst = reinterpret_cast<uint8_t *>(const_cast<char *>(*str));
    const size_t start = g_inPos;
    size_t n = 0;
    do
    {
        if (g_inPos >= g_inSize)
        {
            Fail("a string runs past the end of the zone body");
            return;
        }
        dst[n] = g_in[g_inPos++];
    } while (dst[n++]);
    g_inPos = start;
    const size_t index = Record({reinterpret_cast<uintptr_t>(dst), static_cast<uint32_t>(n), &Layout<char>(), 0,
                                 g_streamPosIndex, Pos32(), g_pos64, static_cast<uint32_t>(n), -1, -1, false});
    Emit(index);
    Advance(static_cast<uint32_t>(n), static_cast<uint32_t>(n));
}
} // namespace

void BeginZone(const uint8_t *body, size_t bodySize, bool narrow, const std::vector<Reinline> &reinlined)
{
    g_narrow = narrow;
    g_in = body;
    g_inSize = bodySize;
    g_inPos = 0;
    g_out.clear();
    g_chunks.clear();
    g_live.clear();
    for (auto &live : g_live64)
        live.clear();
    g_wide.clear();
    g_overlays.clear();
    g_overlayAt.clear();
    g_patches.clear();
    g_deadBytes.clear();
    g_delayed.clear();
    g_stats = {};
    g_error.clear();
    g_asset = -1;
    g_assetHeader = nullptr;
    g_reinlines.clear();
    g_reinlineIn.clear();
    for (const Reinline &reinline : reinlined)
        g_reinlineIn[reinline.slot64] = reinline.value32;
    g_reinlinedSlots.clear();
    g_reinlineChecks.clear();
    g_mismatches.clear();
}

void SetAsset(int index, const void *header)
{
    g_asset = index;
    g_assetHeader = header;
}

void LoadRaw(const TypeLayout &type, void *dst)
{
    const size_t index = Record({reinterpret_cast<uintptr_t>(dst), type.size32, &type, 0, ~0u, 0, 0, type.size64,
                                 -1, -1, false});
    Emit(index);
}

void LoadStream(bool atStreamStart, const TypeLayout &type, uint32_t startOffset, void *ptr, int size)
{
    if (atStreamStart != (ptr == g_streamPos))
        Fail(std::string(type.name) + ": atStreamStart does not match the stream position");
    if (!atStreamStart)
    {
        Describe(type, startOffset, ptr, size);
        return;
    }
    if (!size)
        return;
    const uint32_t size64 = static_cast<uint32_t>(Map64(type, startOffset + static_cast<uint64_t>(size)) -
                                                  Map64(type, startOffset));
    if (startOffset == 0 && g_pos64 % type.align64)
        ++g_stats.misaligned64;
    const bool delayed = g_streamPosIndex == 2 || g_streamPosIndex == 3;
    const size_t index = Record({reinterpret_cast<uintptr_t>(ptr), static_cast<uint32_t>(size), &type, startOffset,
                                 g_streamPosIndex, Pos32(), g_pos64, size64, -1, -1, delayed});
    if (g_streamPosIndex == 1)
        std::memset(ptr, 0, size);
    else if (delayed)
        g_delayed.push_back(index);
    else
        Emit(index);
    Advance(static_cast<uint32_t>(size), size64);
}

bool EndZone(std::string &error)
{
    if (!g_narrow)
    {
        for (Overlay &overlay : g_overlays)
        {
            const Chunk &chunk = g_chunks[overlay.chunk];
            if (chunk.file < 0)
                continue;
            // A delayed chunk's bytes were read after the walk described it.
            if (chunk.delayed)
                overlay.bytes.assign(reinterpret_cast<const uint8_t *>(overlay.addr32),
                                     reinterpret_cast<const uint8_t *>(overlay.addr32) + overlay.size32);
            uint8_t *dst = g_out.data() + chunk.file + overlay.rel64;
            std::memset(dst, 0, Map64(*overlay.type, overlay.size32));
            Convert(overlay.bytes.data(), dst, *overlay.type, 0, overlay.size32);
        }
    }
    for (const Patch &patch : g_patches)
    {
        const Chunk &chunk = g_chunks[patch.chunk];
        if (chunk.file < 0)
        {
            Fail(std::string(chunk.type->name) + ": a translated pointer sits in data that is not in the file");
            continue;
        }
        if (g_narrow)
        {
            const uint32_t value = static_cast<uint32_t>(patch.value);
            std::memcpy(g_out.data() + chunk.file + patch.rel, &value, 4);
        }
        else
            std::memcpy(g_out.data() + chunk.file + patch.rel, &patch.value, 8);
    }
    for (const ReinlineCheck &check : g_reinlineChecks)
    {
        std::vector<uint8_t> retail;
        for (const Segment &segment : check.target)
        {
            const int64_t file = segment.chunk < g_chunks.size() ? g_chunks[segment.chunk].file : -1;
            if (file < 0)
                retail.insert(retail.end(), segment.size, 0);
            else
                retail.insert(retail.end(), g_out.begin() + file + static_cast<std::ptrdiff_t>(segment.offset),
                              g_out.begin() + file + static_cast<std::ptrdiff_t>(segment.offset + segment.size));
        }
        if (retail != check.narrowed)
            Fail(check.site + ": an inline pointee does not narrow to the retail bytes its slot targets");
    }
    if (!g_reinlinedSlots.empty())
        Fail("a slot streamed inline was never converted by the loader");
    error = g_error;
    return g_error.empty();
}

size_t InputConsumed()
{
    return g_inPos;
}

const std::vector<uint8_t> &Output()
{
    return g_out;
}

const StreamStats &Stats()
{
    return g_stats;
}

const std::vector<ByteRange> &DeadUnionBytes()
{
    return g_deadBytes;
}

const std::vector<Reinline> &Reinlined()
{
    return g_reinlines;
}

const std::vector<PointeeMismatch> &Mismatches()
{
    return g_mismatches;
}

void ConvertOffsetToPointer(unsigned int *slot, const TypeLayout &pointee, int count, const char *site,
                            void (*inlineBranch)(void *), void *context)
{
    if (!*slot || *slot >= 0xFFFFFFFEu)
    {
        Fail(std::string(site) + ": DB_ConvertOffsetToPointer on a value that is not an offset");
        return;
    }
    if (g_narrow)
        if (const auto it = g_reinlinedSlots.find(reinterpret_cast<uintptr_t>(slot)); it != g_reinlinedSlots.end())
        {
            const uint32_t value32 = it->second;
            g_reinlinedSlots.erase(it);
            SkipReinlined(slot, value32, pointee, count, site, inlineBranch, context);
            return;
        }
    Target target;
    if (!Translate(slot, target))
        return;
    const uint64_t elements = count < 0 ? std::strlen(reinterpret_cast<const char *>(target.addr32)) + 1
                                        : static_cast<uint64_t>(count);
    const std::string reason = elements ? CheckPointee(target, pointee, elements) : std::string();
    if (!reason.empty())
    {
        Mismatch(target, pointee, elements, site, reason);
        // Narrowing reads the IWffs zone: there, every target must hold its pointee.
        if (g_narrow)
            Fail(std::string(site) + ": the 64-bit target does not hold the pointee: " + reason);
        else if (!StreamInline(slot, target, pointee, elements, static_cast<size_t>(elements * pointee.size32), site,
                           inlineBranch, context))
            return;
    }
    *slot = static_cast<unsigned int>(target.addr32);
}

std::string DescribeOutputByte(size_t offset)
{
    for (const Chunk &chunk : g_chunks)
    {
        const size_t size = g_narrow ? chunk.size32 : chunk.size64;
        if (chunk.file < 0 || offset < static_cast<size_t>(chunk.file) || offset >= chunk.file + size)
            continue;
        const uint64_t rel = offset - chunk.file;
        const uint64_t rel32 = g_narrow ? chunk.start32 + rel : Map32(*chunk.type, rel + Map64(*chunk.type,
                                                                                                 chunk.start32));
        const Leaf *leaf = LeafAt(*chunk.type, rel32);
        static const char *const kinds[] = {"bytes", "pointer", "union of pointer", "opaque union", "widened integer"};
        char text[256];
        std::snprintf(text, sizeof(text), "%s (element size %u), byte %u of the element, %s leaf at %u, block %u",
                      chunk.type->name, chunk.type->size32, static_cast<unsigned>(rel32 % chunk.type->size32),
                      leaf ? kinds[leaf->kind] : "no", leaf ? leaf->off32 : 0, chunk.block);
        return text;
    }
    return "no chunk";
}
} // namespace ff

using namespace ff;

void __cdecl DB_InitStreams(XBlock *blocks)
{
    g_streamBlocks = blocks;
    g_streamPos = blocks->data;
    g_streamPosIndex = 0;
    g_streamDelayIndex = 0;
    g_streamPosArray[0] = nullptr;
    g_streamPosStackIndex = 0;
    for (int i = 1; i < 7; ++i)
        g_streamPosArray[i] = blocks[i].data;
    g_pos64 = 0;
    std::fill(std::begin(g_posArray64), std::end(g_posArray64), 0u);
}

void __cdecl DB_SetStreamIndex(unsigned int index)
{
    if (index != g_streamPosIndex)
    {
        g_streamPosArray[g_streamPosIndex] = g_streamPos;
        g_posArray64[g_streamPosIndex] = g_pos64;
        g_streamPosIndex = index;
        g_streamPos = g_streamPosArray[index];
        g_pos64 = g_posArray64[index];
        g_streamPosArray[index] = nullptr;
    }
}

void __cdecl DB_PushStreamPos(unsigned int index)
{
    if (g_streamPosStackIndex >= 64)
    {
        Fail("stream position stack overflow");
        return;
    }
    StackEntry &entry = g_stack[g_streamPosStackIndex];
    entry.index = g_streamPosIndex;
    DB_SetStreamIndex(index);
    entry.pos = g_streamPos;
    entry.pos64 = g_pos64;
    ++g_streamPosStackIndex;
}

// Leaving the temp block (0) rewinds it: temp data only lives until its push is popped.
void __cdecl DB_PopStreamPos()
{
    if (!g_streamPosStackIndex)
    {
        Fail("stream position stack underflow");
        return;
    }
    const StackEntry &entry = g_stack[--g_streamPosStackIndex];
    if (!g_streamPosIndex)
    {
        g_streamPos = entry.pos;
        g_pos64 = entry.pos64;
    }
    DB_SetStreamIndex(entry.index);
}

unsigned __int8 *__cdecl DB_GetStreamPos()
{
    return g_streamPos;
}

unsigned __int8 *__cdecl DB_AllocStreamPos(int alignment)
{
    g_streamPos = reinterpret_cast<unsigned __int8 *>(~static_cast<uintptr_t>(alignment) &
                                                      reinterpret_cast<uintptr_t>(&g_streamPos[alignment]));
    g_pos64 = AlignUp(g_pos64, std::max<uint32_t>(static_cast<uint32_t>(alignment) + 1, DB_FF64_MIN_ALIGNMENT));
    return g_streamPos;
}

const void **__cdecl DB_InsertPointer()
{
    DB_PushStreamPos(4u);
    const void **slot = reinterpret_cast<const void **>(DB_AllocStreamPos(3));
    Record({reinterpret_cast<uintptr_t>(slot), 4, &Layout<void *>(), 0, g_streamPosIndex, Pos32(), g_pos64, 8, -1, -1,
            false});
    Advance(4, 8);
    DB_PopStreamPos();
    return slot;
}

void __cdecl Load_DelayStream()
{
    for (size_t index : g_delayed)
        Emit(index);
}

// An alias targets the pointer slot that holds an asset (an insert pointer or the asset's own slot): on 64-bit too,
// the target must be a pointer.
void __cdecl DB_ConvertOffsetToAlias(void *slot)
{
    unsigned int *data = static_cast<unsigned int *>(slot);
    if (!*data || *data >= 0xFFFFFFFEu)
    {
        Fail("DB_ConvertOffsetToAlias on a value that is not an offset");
        return;
    }
    Target target;
    if (!Translate(data, target))
        return;
    const std::string reason = CheckPointee(target, Layout<void *>(), 1);
    if (!reason.empty())
    {
        Mismatch(target, Layout<void *>(), 1, "DB_ConvertOffsetToAlias", reason);
        Fail("an alias does not target a pointer on 64-bit: " + reason);
    }
    *data = *reinterpret_cast<unsigned int *>(target.addr32);
}

void __cdecl Load_XStringCustom(const char **str)
{
    LoadString(str);
}

// The engine replaces the string with a script-string handle; the converted zone keeps the stream's bytes.
void __cdecl Load_TempStringCustom(const char **str)
{
    LoadString(str);
}

// The engine replaces the zone's string index with a global handle; the converted zone keeps the index.
void __cdecl Load_ScriptStringCustom(unsigned __int16 *)
{
}
