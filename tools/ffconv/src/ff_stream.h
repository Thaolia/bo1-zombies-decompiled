#pragma once

// ffconv's implementation of the engine's stream API (db_stream.h, db_stream_load.h). It keeps the 32-bit stream
// positions exactly as the engine does, so the transformed db_load.cpp runs unchanged, and in parallel the 64-bit
// positions of the IWffs stream. Widening reads a retail body and writes the IWffs body; narrowing reads an IWffs body
// and writes back the retail body it came from, which proves the conversion loses nothing.

#include <cstdint>
#include <string>
#include <vector>

#include "ff_layout.h"

namespace ff
{
struct StreamStats
{
    uint64_t chunks = 0;
    uint64_t pointersTranslated = 0;
    uint64_t valuesFixed = 0;       // integers rescaled by a value fixup (ff_layout.h)
    uint64_t misaligned64 = 0;      // a chunk whose 64-bit position is not aligned to its type
    uint64_t reinlined = 0;         // offset pointers streamed inline in the IWffs zone (Reinline)
    uint32_t end32[7] = {};         // highest 32-bit position reached in each block
    uint32_t end64[7] = {};         // same for the 64-bit stream: the IWffs block sizes
};

// The retail linker shares identical bytes between objects of different types: an offset pointer can target bytes
// that are not its pointee type (64 zero bytes read as XModel *[16]). Where the 64-bit layouts of the target and of
// the pointee differ, the IWffs zone holds the pointee inline instead (the slot says "follows", and the inline branch
// of the loader streams a widened copy of the target's bytes). Narrowing needs the retail value of each such slot.
struct Reinline
{
    uint64_t slot64;  // offset of the slot in the IWffs body
    uint32_t value32; // its retail value, an offset into the 32-bit blocks
};

// An offset pointer whose target does not hold its pointee in the 64-bit layout, for reports.
struct PointeeMismatch
{
    std::string site;    // the slot expression in db_load.cpp
    std::string pointee; // pointee type and element count
    std::string target;  // the streamed type the target bytes belong to
    std::string reason;
    uint32_t block;
    bool zero;           // every retail byte of the target is 0
    int asset;           // index in the zone's asset list
    std::string assetName;
};

// Starts a zone: `body` is the inflated stream after the XFile header (retail when widening, IWffs when narrowing).
// Narrowing takes the slots the widening streamed inline.
void BeginZone(const uint8_t *body, size_t bodySize, bool narrow, const std::vector<Reinline> &reinlined = {});
// The asset the walk is in, for reports: its index and its XAssetHeader, whose first field is the name of most types.
void SetAsset(int index, const void *header);
// Converts the XAssetList header the engine reads outside Load_Stream (Load_XAssetListCustom).
void LoadRaw(const TypeLayout &type, void *dst);
// Applies the overlays and pointer translations recorded during the walk; returns false and fills `error` on a
// failure.
bool EndZone(std::string &error);

size_t InputConsumed();
const std::vector<uint8_t> &Output();
const StreamStats &Stats();
// Which streamed type and leaf an output byte belongs to, for reports.
std::string DescribeOutputByte(size_t offset);

// Narrowing: output ranges of unions of pointers and wider data that lie outside the member the loader walked. The
// retail stream keeps leftover bytes there that no code reads, and the 64-bit union has no room for them.
struct ByteRange
{
    size_t offset;
    size_t size;
};
const std::vector<ByteRange> &DeadUnionBytes();

// Widening: the slots streamed inline. Both directions: the mismatches found.
const std::vector<Reinline> &Reinlined();
const std::vector<PointeeMismatch> &Mismatches();
} // namespace ff
