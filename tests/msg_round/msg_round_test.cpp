// Exhaustive check of MSG_RoundFloatToInt_C (src/qcommon/msg_round.h) over all 2^32 float bit patterns.
//
// On x86 (32 or 64-bit, GCC/clang) every result is compared with the real x87 sequence the Windows build runs
// (FLD; FADD bias; FISTP) under 24-bit precision control, the state D3D9 leaves the main thread in. The same pass
// counts how many results would differ under 53-bit precision, to show the precision mode matters.
//
// On every architecture an FNV-1a hash of all results is checked against the x87 reference hash, so the test also
// proves the portable code on AArch64.

#include "qcommon/msg_round.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#if defined(__i386__) || defined(__x86_64__)
#define HAVE_X87 1
#endif

namespace
{
// Hash of the x87 results under 24-bit precision, in bit-pattern order. Recompute with this test on x86 if the
// hash function changes, never to make a failing port pass.
constexpr uint64_t kReferenceHash = 0xac2adb9bc5e25a37ull;

constexpr uint64_t kFnvOffset = 1469598103934665603ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

#ifdef HAVE_X87
constexpr uint16_t kCwPrecision24 = 0x007F;
constexpr uint16_t kCwPrecision53 = 0x027F;

void SetX87ControlWord(uint16_t cw)
{
    __asm__ volatile("fldcw %0" : : "m"(cw));
}

int X87RoundFloatToInt(float value)
{
    static const double bias = 9.313225746154785e-10;
    int result;
    __asm__ volatile("flds %1\n\t"
                     "faddl %2\n\t"
                     "fistpl %0"
                     : "=m"(result)
                     : "m"(value), "m"(bias)
                     : "st");
    return result;
}
#endif

struct Chunk
{
    uint64_t hash = kFnvOffset;
    uint64_t mismatches24 = 0;
    uint64_t differs53 = 0;
    uint32_t firstBad = 0;
    bool haveBad = false;
};

uint64_t HashInt(uint64_t hash, int value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    for (int i = 0; i < 4; ++i)
    {
        hash ^= (bits >> (8 * i)) & 0xFF;
        hash *= kFnvPrime;
    }
    return hash;
}

void RunChunk(uint64_t begin, uint64_t end, Chunk *out)
{
    Chunk c;
    for (uint64_t pattern = begin; pattern < end; ++pattern)
    {
        const uint32_t bits = static_cast<uint32_t>(pattern);
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        const int portable = MSG_RoundFloatToInt_C(value);
        c.hash = HashInt(c.hash, portable);
#ifdef HAVE_X87
        SetX87ControlWord(kCwPrecision24);
        const int ref24 = X87RoundFloatToInt(value);
        SetX87ControlWord(kCwPrecision53);
        const int ref53 = X87RoundFloatToInt(value);
        if (ref24 != portable)
        {
            if (!c.haveBad)
            {
                c.firstBad = bits;
                c.haveBad = true;
            }
            ++c.mismatches24;
        }
        if (ref53 != ref24)
            ++c.differs53;
#endif
    }
    *out = c;
}
} // namespace

int main()
{
    // Chunks are hashed in order so the combined hash does not depend on the thread count.
    constexpr unsigned kChunks = 256;
    constexpr uint64_t kTotal = 1ull << 32;
    std::vector<Chunk> chunks(kChunks);
    std::atomic<unsigned> next{0};
    unsigned workers = std::thread::hardware_concurrency();
    if (workers == 0)
        workers = 4;

    std::vector<std::thread> threads;
    for (unsigned t = 0; t < workers; ++t)
        threads.emplace_back([&] {
            for (unsigned i; (i = next.fetch_add(1)) < kChunks;)
                RunChunk(kTotal / kChunks * i, kTotal / kChunks * (i + 1), &chunks[i]);
        });
    for (std::thread &t : threads)
        t.join();

    uint64_t hash = kFnvOffset;
    uint64_t mismatches24 = 0;
    uint64_t differs53 = 0;
    bool haveBad = false;
    uint32_t firstBad = 0;
    for (const Chunk &c : chunks)
    {
        hash = (hash ^ c.hash) * kFnvPrime;
        mismatches24 += c.mismatches24;
        differs53 += c.differs53;
        if (c.haveBad && !haveBad)
        {
            haveBad = true;
            firstBad = c.firstBad;
        }
    }

    std::printf("results hash: 0x%016llx\n", static_cast<unsigned long long>(hash));
    bool ok = true;
#ifdef HAVE_X87
    std::printf("x87 24-bit mismatches: %llu\n", static_cast<unsigned long long>(mismatches24));
    std::printf("x87 53-bit results that differ from 24-bit: %llu\n", static_cast<unsigned long long>(differs53));
    if (haveBad)
    {
        std::printf("first mismatch: float bits 0x%08x\n", firstBad);
        ok = false;
    }
#endif
    if (hash != kReferenceHash)
    {
        std::printf("hash differs from the x87 reference 0x%016llx\n",
                    static_cast<unsigned long long>(kReferenceHash));
        ok = false;
    }
    std::printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
