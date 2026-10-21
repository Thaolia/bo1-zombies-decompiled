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
    uint64_t untypedAllocs = 0;     // DB_AllocStreamPos without a known type (aligned to the retail value only)
    uint64_t misaligned64 = 0;      // a chunk whose 64-bit position is not aligned to its type
    uint32_t end32[7] = {};         // highest 32-bit position reached in each block
    uint32_t end64[7] = {};         // same for the 64-bit stream: the IWffs block sizes
};

// Starts a zone: `body` is the inflated stream after the XFile header (retail when widening, IWffs when narrowing).
void BeginZone(const uint8_t *body, size_t bodySize, bool narrow);
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
} // namespace ff
