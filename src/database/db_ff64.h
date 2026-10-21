#pragma once

// IWffs zones, written by tools/ffconv and read by 64-bit builds: the retail fastfile stream (same traversal, same
// zlib body after the header) with every struct in its 64-bit layout. ffconv and the 64-bit loader must agree on
// everything in this file; ffconv's layout tables (tools/ffconv/gen/layouts.py) give the 64-bit layouts.

#include <cstdint>

// File header: these 8 bytes, the version, the ABI, then the zlib stream (XFile header, then the body).
#define DB_FF64_MAGIC "IWffs100"
inline constexpr uint32_t DB_FF64_VERSION = 473;

// Struct layouts depend on the 64-bit ABI (bitfields, tail padding), so a zone is written for one of them.
enum DBFF64Abi : uint32_t
{
    DB_FF64_ABI_WIN64 = 1,   // MSVC x64 rules: Windows x64 builds (phase 3)
    DB_FF64_ABI_AARCH64 = 2, // AAPCS64 / LP64: the Switch
};

// Pointer slots are 8 bytes. 0 is NULL; the two markers mean "the data follows in the stream" (and, for the second,
// "allocate an insert pointer for it"); any other value is ((block << 32) | offset) + 1 into the 64-bit blocks.
inline constexpr uint64_t DB_FF64_FOLLOWS = ~0ull;
inline constexpr uint64_t DB_FF64_FOLLOWS_INSERT = ~1ull;

inline uint64_t DB_FF64_EncodeOffset(uint32_t block, uint32_t offset)
{
    return ((static_cast<uint64_t>(block) << 32) | offset) + 1;
}

inline uint32_t DB_FF64_OffsetBlock(uint64_t encoded)
{
    return static_cast<uint32_t>((encoded - 1) >> 32);
}

inline uint32_t DB_FF64_OffsetInBlock(uint64_t encoded)
{
    return static_cast<uint32_t>(encoded - 1);
}

// An AllocLoad_* aligns to its retail alignment or to the 64-bit alignment of the type it allocates, whichever is
// larger (retail aligns pointer arrays to 4 bytes).
inline uint32_t DB_FF64_Alignment(uint32_t retailAlignment, uint32_t align64)
{
    return retailAlignment > align64 ? retailAlignment : align64;
}

// Other rules ffconv applies that the 64-bit Load_* code must mirror:
// - stream positions are aligned as offsets from the block start, so each block must start on a 4096-byte boundary
//   (as VirtualAlloc gives the retail loader, whose alignments are on absolute addresses, up to 2048 bytes);
// - an insert pointer (DB_InsertPointer) is an 8-byte slot aligned to 8;
// - a Load_Stream that copies the first bytes of a record, up to a field the loader streams on its own (for example
//   XAnimDeltaPartQuat up to `u`), copies up to that field's 64-bit offset;
// - a pointer the retail stream stores as 0xFFFFFFFF or 0xFFFFFFFE becomes DB_FF64_FOLLOWS or
//   DB_FF64_FOLLOWS_INSERT; any other value the loader never converts is zero-extended (the loader only tests it
//   against 0).
