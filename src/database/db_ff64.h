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
    DB_FF64_ABI_WIN64 = 1,   // MSVC x64 rules: Windows x64 builds (phase 3), the posix x86-64 build, and the Switch --
                             // every streamed struct lays out the same on all three (below)
    DB_FF64_ABI_AARCH64 = 2, // reserved for a distinct AArch64 layout; unused while AAPCS64 matches win64 for every
                             // streamed type (enforced by --check-switch, so a future divergence fails the build here)
};

// The ABI of this build; 0 on a target with no IWffs layout tables yet. The posix x86-64 build (SysV) and the Switch
// build (devkitA64, AAPCS64/LP64) both read the win64 zones ffconv emits: every streamed struct lays out there as
// with MSVC x64, which each build verifies before linking (CMakeLists.txt, tools/ffconv/gen/layouts.py
// --check-target64 for x86-64, --check-switch for the Switch). So they all tag as DB_FF64_ABI_WIN64.
#if defined(_WIN64)
inline constexpr uint32_t DB_FF64_BUILD_ABI = DB_FF64_ABI_WIN64;
#elif defined(__x86_64__)
inline constexpr uint32_t DB_FF64_BUILD_ABI = DB_FF64_ABI_WIN64;
#elif defined(__aarch64__)
inline constexpr uint32_t DB_FF64_BUILD_ABI = DB_FF64_ABI_WIN64; // verified layout-identical to win64 (--check-switch)
#else
inline constexpr uint32_t DB_FF64_BUILD_ABI = 0;
#endif

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

// Every stream allocation (DB_AllocStreamPos, so every AllocLoad_* and insert pointer) is aligned to its retail
// alignment or to 8, whichever is larger. Retail aligns pointer arrays to 4; no streamed type needs more than 8
// without already asking for it in retail, so the loader needs no type to align.
inline constexpr uint32_t DB_FF64_MIN_ALIGNMENT = 8;

// Other rules ffconv applies that the 64-bit Load_* code must mirror:
// - stream positions are aligned as offsets from the block start, so each block must start on a 4096-byte boundary
//   (as VirtualAlloc gives the retail loader, whose alignments are on absolute addresses, up to 2048 bytes);
// - an insert pointer (DB_InsertPointer) is an 8-byte slot;
// - a Load_Stream that copies the first bytes of a record, up to a field the loader streams on its own (for example
//   XAnimDeltaPartQuat up to `u`), copies up to that field's 64-bit offset;
// - a pointer the retail stream stores as 0xFFFFFFFF or 0xFFFFFFFE becomes DB_FF64_FOLLOWS or
//   DB_FF64_FOLLOWS_INSERT; any other value the loader never converts is zero-extended (the loader only tests it
//   against 0). A union of a pointer and 4-byte data (MaterialArgumentDef) is widened the same way: its data members
//   read the low 4 bytes, so the loader must test such a union through its pointer member.
