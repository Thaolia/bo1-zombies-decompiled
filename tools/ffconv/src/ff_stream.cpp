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
    int64_t src64;    // narrowing: offset of the chunk's 64-bit bytes in the input, -1 if none
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
const TypeLayout *g_pending;
StreamStats g_stats;
std::string g_error;

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
           type.leaves[0].size32 == type.size32;
}

uint64_t WidenPointer(uint32_t value)
{
    if (value == 0xFFFFFFFFu)
        return DB_FF64_FOLLOWS;
    if (value == 0xFFFFFFFEu)
        return DB_FF64_FOLLOWS_INSERT;
    return value;
}

uint32_t NarrowPointer(uint64_t value)
{
    if (value == DB_FF64_FOLLOWS)
        return 0xFFFFFFFFu;
    if (value == DB_FF64_FOLLOWS_INSERT)
        return 0xFFFFFFFEu;
    return static_cast<uint32_t>(value);
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
                Fail(std::string(type.name) + ": a stream boundary cuts a pointer");
                continue;
            }
            if (g_narrow)
            {
                uint64_t wide;
                std::memcpy(&wide, at64, 8);
                const uint32_t value = leaf.kind == LEAF_PTR ? NarrowPointer(wide) : static_cast<uint32_t>(wide);
                std::memcpy(at32, &value, 4);
                g_wide[reinterpret_cast<uintptr_t>(at32)] = wide;
            }
            else
            {
                uint32_t value;
                std::memcpy(&value, at32, 4);
                const uint64_t wide = leaf.kind == LEAF_PTR ? WidenPointer(value) : value;
                std::memcpy(at64, &wide, 8);
            }
        }
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

// Stops at the first leaf that is not a described union; false if the byte is in no leaf.
bool Descend(const Chunk &chunk, size_t index, uintptr_t addr, Level &level)
{
    level = {chunk.type, chunk.start32 + (addr - chunk.addr32),
             -static_cast<int64_t>(Map64(*chunk.type, chunk.start32)), nullptr};
    for (;;)
    {
        level.leaf = LeafAt(*level.type, level.rel32);
        if (!level.leaf)
            return false;
        if (level.leaf->kind != LEAF_OPAQUE)
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
    if (g_narrow)
    {
        // Padding has no leaf: zero it as the retail stream does, so a padding byte that holds data shows up.
        chunk.src64 = in - g_in;
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
        if (chunk->src64 < 0)
        {
            Fail(std::string(type.name) + ": a union member is described before its bytes are read");
            return;
        }
        Convert(bytes, const_cast<uint8_t *>(g_in + chunk->src64 + rel64), type, 0, size);
        const size_t at = chunk->file + (addr - chunk->addr32);
        std::memcpy(g_out.data() + at, bytes, size);
        if (static_cast<uint32_t>(size) < level.leaf->size32)
            g_deadBytes.push_back({at + size, level.leaf->size32 - static_cast<size_t>(size)});
    }
    g_overlays.push_back({index, addr, &type, static_cast<uint32_t>(size), rel64, {bytes, bytes + size}});
    g_overlayAt.emplace(addr, g_overlays.size() - 1);
}

// The pointer slot at `slot` holds the encoded offset `value` of a byte of an earlier chunk. Widening records the
// encoded 64-bit offset of the same byte for the output; narrowing finds the slot's 64-bit value, puts the encoded
// 32-bit offset of the same byte in the slot and records it for the output. Returns the encoded 32-bit offset.
uint32_t Translate(unsigned int *slot)
{
    const uintptr_t slotAddr = reinterpret_cast<uintptr_t>(slot);
    size_t slotChunk;
    const Chunk *holder = Find(slotAddr, &slotChunk);
    Level where;
    if (!holder || !Descend(*holder, slotChunk, slotAddr, where) || where.leaf->kind == LEAF_OPAQUE)
    {
        Fail(std::string(holder ? holder->type->name : "?") + ": a converted pointer slot is not in a known field");
        return *slot;
    }
    if (where.leaf->kind == LEAF_BYTES || where.InLeaf() != 0)
    {
        Fail(std::string(where.type->name) + ": a converted pointer is not a pointer field");
        return *slot;
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
        const Chunk *target = targetAddr ? Find(targetAddr, &targetChunk) : nullptr;
        Level at;
        if (!target || target->block != block || !Descend(*target, targetChunk, targetAddr, at))
        {
            Fail(std::string(holder->type->name) + ": a pointer targets no streamed chunk");
            return value;
        }
        g_patches.push_back({slotChunk, where.Rel64(),
                             DB_FF64_EncodeOffset(block, static_cast<uint32_t>(target->pos64 + at.Rel64()))});
        return value;
    }
    const auto wide = g_wide.find(slotAddr);
    const uint32_t block = wide != g_wide.end() ? DB_FF64_OffsetBlock(wide->second) : 7;
    const Chunk *target = block < 7 ? Find64(block, DB_FF64_OffsetInBlock(wide->second)) : nullptr;
    if (!target)
    {
        Fail(std::string(holder->type->name) + ": a 64-bit pointer targets no streamed chunk");
        return *slot;
    }
    const uint64_t abs64 = DB_FF64_OffsetInBlock(wide->second) - target->pos64 + Map64(*target->type, target->start32);
    const uint32_t off32 = target->pos32 + static_cast<uint32_t>(Map32(*target->type, abs64) - target->start32);
    const uint32_t value = ((block << 29) | off32) + 1;
    *slot = value;
    g_patches.push_back({slotChunk, slotAddr - holder->addr32, value});
    return value;
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

void BeginZone(const uint8_t *body, size_t bodySize, bool narrow)
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
    g_pending = nullptr;
    g_stats = {};
    g_error.clear();
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

void SetPendingAlloc(const TypeLayout &type)
{
    g_pending = &type;
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
        static const char *const kinds[] = {"bytes", "pointer", "union of pointer", "opaque union"};
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
    uint32_t align64 = static_cast<uint32_t>(alignment) + 1;
    if (g_pending)
        align64 = DB_FF64_Alignment(align64, g_pending->align64);
    else
        ++g_stats.untypedAllocs;
    g_pending = nullptr;
    g_pos64 = AlignUp(g_pos64, align64);
    return g_streamPos;
}

const void **__cdecl DB_InsertPointer()
{
    DB_PushStreamPos(4u);
    SetPendingAlloc(Layout<void *>());
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

void __cdecl DB_ConvertOffsetToAlias(unsigned int *data)
{
    if (!*data || *data >= 0xFFFFFFFEu)
    {
        Fail("DB_ConvertOffsetToAlias on a value that is not an offset");
        return;
    }
    const unsigned int offset = Translate(data);
    *data = *reinterpret_cast<unsigned int *>(&g_streamBlocks[(offset - 1) >> 29].data[(offset - 1) & 0x1FFFFFFF]);
}

void __cdecl DB_ConvertOffsetToPointer(unsigned int *data)
{
    if (!*data || *data >= 0xFFFFFFFEu)
    {
        Fail("DB_ConvertOffsetToPointer on a value that is not an offset");
        return;
    }
    const unsigned int offset = Translate(data);
    *data = static_cast<unsigned int>(
        reinterpret_cast<uintptr_t>(&g_streamBlocks[(offset - 1) >> 29].data[(offset - 1) & 0x1FFFFFFF]));
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
