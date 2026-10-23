// ffconv: converts retail PC fastfiles (IWffu100 v473, 32-bit) into IWffs zones for 64-bit builds.
//
//     ffconv.exe [--abi win64|aarch64] [--verify] --game INSTALL_DIR --out OUT_DIR
//     ffconv.exe [--abi win64|aarch64] [--verify] [--shader-dir DIR] [--glsl-dir DIR] INPUT.ff OUTPUT.ff
//     ffconv.exe --check [--shader-dir DIR] [--glsl-dir DIR] INPUT.ff
//
// With --game, every zone the zombies maps load (IsZombieZone) is converted from INSTALL_DIR/zone/<dir>/ to
// OUT_DIR/zone/<dir>/, and the GLSL of all their shaders goes to OUT_DIR/shaders.pack. The install is only read.
//
// A zone is walked with the engine's own Load_* code (a build-time copy of db_load.cpp with typed hooks), which
// streams every struct with its C++ type; ff_stream.cpp writes each one in its 64-bit layout. --check walks and
// reports without writing. --verify walks the written zone back to 32-bit and requires the retail stream, byte for
// byte. --shader-dir saves the D3D9 shaders, one file per bytecode hash (ff_shaders.h); --glsl-dir saves their
// GLSL 4.60 translations (ff_glsl.h), for glslangValidator.

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <zlib/zlib.h>

#include <database/db_ff64.h>
#include <database/db_file_load.h>
#include <database/db_load.h>
#include <database/db_registry.h>
#include <database/db_stream.h>
#include <database/db_stream_load.h>

#include "ff_hooks.h"
#include "ff_shaders.h"
#include "ff_stream.h"

// Defined with the other generated layouts in the transformed db_load.cpp.
template <> const ff::TypeLayout &ff::Layout<XAssetList>();
template <> const ff::TypeLayout &ff::Layout<XAsset>();

XAssetList *varXAssetList;

namespace
{
const char kRetailMagic[] = "IWffu100";
const uint32_t kRetailVersion = 473;
const size_t kRetailHeaderSize = 12;
const char *const kBlockNames[7] = {"temp", "runtime", "large_runtime", "physical_runtime", "virtual", "large",
                                    "physical"};

bool ReadFile(const char *path, std::vector<uint8_t> &data)
{
    FILE *f = std::fopen(path, "rb");
    if (!f)
        return false;
    std::fseek(f, 0, SEEK_END);
    data.resize(static_cast<size_t>(std::ftell(f)));
    std::fseek(f, 0, SEEK_SET);
    const bool ok = std::fread(data.data(), 1, data.size(), f) == data.size();
    std::fclose(f);
    return ok;
}

bool Inflate(const uint8_t *src, size_t size, std::vector<uint8_t> &out)
{
    z_stream z{};
    if (inflateInit(&z) != Z_OK)
        return false;
    z.next_in = const_cast<Bytef *>(src);
    z.avail_in = static_cast<uInt>(size);
    int status = Z_OK;
    while (status == Z_OK)
    {
        out.resize(out.size() + (16u << 20));
        z.next_out = out.data() + z.total_out;
        z.avail_out = static_cast<uInt>(out.size() - z.total_out);
        status = inflate(&z, Z_NO_FLUSH);
    }
    out.resize(z.total_out);
    inflateEnd(&z);
    return status == Z_STREAM_END;
}

bool Deflate(const std::vector<uint8_t> &src, std::vector<uint8_t> &out)
{
    uLongf size = compressBound(static_cast<uLong>(src.size()));
    out.resize(size);
    if (compress2(out.data(), &size, src.data(), static_cast<uLong>(src.size()), Z_BEST_COMPRESSION) != Z_OK)
        return false;
    out.resize(size);
    return true;
}

// Load_XAssetArrayCustom without the engine's material and mod hooks.
void LoadAssetArray(int count)
{
    FF_LoadStream(true, varXAsset, varXAsset, 8 * count);
    XAsset *var = varXAsset;
    for (int i = 0; i < count; ++i)
    {
        ff::SetAsset(i, &var->header);
        varXAsset = var++;
        Load_XAsset(false);
    }
}

struct Walk
{
    int assetCount = 0;
    bool ok = false;
    std::string error;
    ff::StreamStats stats;
    size_t consumed = 0;
    std::vector<uint8_t> output;
    std::vector<ff::Reinline> reinlined;
    std::vector<ff::PointeeMismatch> mismatches;
};

// DB_LoadXFileInternal's body walk, from the XAssetList on, over 32-bit blocks of the retail sizes.
Walk WalkZone(const uint8_t *body, size_t bodySize, const unsigned blockSize[7], bool narrow,
              const std::vector<ff::Reinline> &reinlined = {})
{
    // DB_AllocStreamPos aligns absolute addresses (up to 2048 bytes for sound data), and the engine's blocks come
    // from VirtualAlloc: page-aligned blocks give the same 32-bit positions. The slack keeps a walk that overruns a
    // block from corrupting the heap before the overrun is reported.
    const size_t kPage = 4096;
    const size_t kBlockSlack = 1 << 20;
    XBlock blocks[7];
    std::vector<std::vector<uint8_t>> blockData(7);
    for (int b = 0; b < 7; ++b)
    {
        blocks[b].size = blockSize[b];
        blocks[b].data = nullptr;
        if (!blockSize[b])
            continue;
        blockData[b].assign(blockSize[b] + kBlockSlack + kPage, 0);
        const uintptr_t raw = reinterpret_cast<uintptr_t>(blockData[b].data());
        blocks[b].data = reinterpret_cast<unsigned __int8 *>((raw + kPage - 1) & ~(kPage - 1));
    }
    ff::BeginZone(body, bodySize, narrow, reinlined);
    DB_InitStreams(blocks);

    XAssetList list{};
    varXAssetList = &list;
    ff::LoadRaw(ff::Layout<XAssetList>(), &list);
    DB_PushStreamPos(4u);
    varScriptStringList = &list.stringList;
    Load_ScriptStringList(false);
    DB_PopStreamPos();
    DB_PushStreamPos(4u);
    if (list.assets)
    {
        list.assets = AllocLoad_FxElemVisStateSample();
        varXAsset = list.assets;
        LoadAssetArray(list.assetCount);
    }
    DB_PopStreamPos();
    Load_DelayStream();

    Walk walk;
    walk.assetCount = list.assetCount;
    walk.ok = ff::EndZone(walk.error);
    walk.stats = ff::Stats();
    walk.consumed = ff::InputConsumed();
    walk.output = ff::Output();
    walk.reinlined = ff::Reinlined();
    walk.mismatches = ff::Mismatches();
    if (walk.ok && walk.consumed != bodySize)
    {
        walk.ok = false;
        walk.error = "the walk did not read the whole body";
    }
    for (int b = 0; walk.ok && b < 7; ++b)
        if (walk.stats.end32[b] > blockSize[b])
        {
            walk.ok = false;
            walk.error = std::string("the walk went past block ") + kBlockNames[b];
        }
    return walk;
}

void Report(const char *path, const XFile &header, size_t bodySize, const Walk &walk)
{
    std::printf("%s: %d assets, %zu of %zu body bytes read, %llu chunks, %llu pointers translated, %llu values "
                "fixed\n",
                path, walk.assetCount, walk.consumed, bodySize, static_cast<unsigned long long>(walk.stats.chunks),
                static_cast<unsigned long long>(walk.stats.pointersTranslated),
                static_cast<unsigned long long>(walk.stats.valuesFixed));
    for (int b = 0; b < 7; ++b)
        std::printf("  block %d %-16s retail %10u, walked to %10u, 64-bit %10u\n", b, kBlockNames[b],
                    header.blockSize[b], walk.stats.end32[b], walk.stats.end64[b]);
    std::printf("  misaligned 64-bit chunks %llu\n", static_cast<unsigned long long>(walk.stats.misaligned64));
}

// Offset pointers whose retail target is not their pointee on 64-bit (ff_stream.h, Reinline).
void ReportMismatches(const Walk &walk)
{
    for (size_t i = 0; i < walk.mismatches.size(); ++i)
    {
        const ff::PointeeMismatch &m = walk.mismatches[i];
        std::printf("  pointee mismatch: asset %d '%s', %s -> %s over %s, block %u, %s target: %s\n", m.asset,
                    m.assetName.c_str(), m.site.c_str(), m.pointee.c_str(), m.target.c_str(), m.block,
                    m.zero ? "zero" : "non-zero", m.reason.c_str());
    }
}

bool WriteZone(const char *path, DBFF64Abi abi, const std::vector<uint8_t> &compressed)
{
    FILE *f = std::fopen(path, "wb");
    if (!f)
        return false;
    const uint32_t version = DB_FF64_VERSION;
    const uint32_t abiTag = abi;
    const bool written = std::fwrite(DB_FF64_MAGIC, 1, 8, f) == 8 && std::fwrite(&version, 4, 1, f) == 1 &&
                         std::fwrite(&abiTag, 4, 1, f) == 1 &&
                         std::fwrite(compressed.data(), 1, compressed.size(), f) == compressed.size();
    return std::fclose(f) == 0 && written;
}

// Reads the written zone back and narrows it: the result must be the retail body, and the walk must end each block
// where the IWffs header says.
bool Verify(const char *path, const XFile &retail, const uint8_t *retailBody, size_t retailBodySize,
            const std::vector<ff::Reinline> &reinlined)
{
    std::vector<uint8_t> file;
    std::vector<uint8_t> stream;
    const size_t headerSize = 16;
    if (!ReadFile(path, file) || file.size() < headerSize || std::memcmp(file.data(), DB_FF64_MAGIC, 8) ||
        !Inflate(file.data() + headerSize, file.size() - headerSize, stream) || stream.size() < sizeof(XFile))
    {
        std::fprintf(stderr, "ffconv: verify: cannot read back %s\n", path);
        return false;
    }
    XFile header;
    std::memcpy(&header, stream.data(), sizeof(header));
    const Walk walk = WalkZone(stream.data() + sizeof(header), stream.size() - sizeof(header), retail.blockSize,
                               true, reinlined);
    if (!walk.ok)
    {
        ReportMismatches(walk);
        std::fprintf(stderr, "ffconv: verify: %s\n", walk.error.c_str());
        return false;
    }
    for (int b = 0; b < 7; ++b)
        if (walk.stats.end64[b] != header.blockSize[b])
        {
            std::fprintf(stderr, "ffconv: verify: block %s ends at %u, the header says %u\n", kBlockNames[b],
                         walk.stats.end64[b], header.blockSize[b]);
            return false;
        }
    // Leftover bytes outside a union's walked member are compared apart: the retail stream's become the expected
    // bytes there, and how many of them the 64-bit zone cannot hold is reported.
    std::vector<uint8_t> expected(retailBody, retailBody + retailBodySize);
    size_t deadBytes = 0;
    size_t deadDiffer = 0;
    for (const ff::ByteRange &range : ff::DeadUnionBytes())
    {
        if (range.offset + range.size > std::min(expected.size(), walk.output.size()))
            continue;
        deadBytes += range.size;
        for (size_t i = range.offset; i < range.offset + range.size; ++i)
            deadDiffer += expected[i] != walk.output[i];
        std::memcpy(expected.data() + range.offset, walk.output.data() + range.offset, range.size);
    }
    retailBody = expected.data();
    if (walk.output.size() != retailBodySize || std::memcmp(walk.output.data(), retailBody, retailBodySize))
    {
        size_t at = 0;
        while (at < std::min(walk.output.size(), retailBodySize) && walk.output[at] == retailBody[at])
            ++at;
        std::fprintf(stderr, "ffconv: verify: narrowed body differs from retail at byte %zu (%zu vs %zu bytes): %s\n",
                     at, walk.output.size(), retailBodySize, ff::DescribeOutputByte(at).c_str());
        std::fprintf(stderr, "  retail  :");
        for (size_t i = at - std::min<size_t>(at, 8); i < std::min(at + 8, retailBodySize); ++i)
            std::fprintf(stderr, " %02x", retailBody[i]);
        std::fprintf(stderr, "\n  narrowed:");
        for (size_t i = at - std::min<size_t>(at, 8); i < std::min(at + 8, walk.output.size()); ++i)
            std::fprintf(stderr, " %02x", walk.output[i]);
        std::fprintf(stderr, "\n");
        return false;
    }
    std::printf("  verify: narrowing the IWffs zone gives the retail body back (%zu bytes; %zu bytes outside walked "
                "union members, %zu of them leftover data; %llu inline pointees equal their retail targets)\n",
                retailBodySize, deadBytes, deadDiffer, static_cast<unsigned long long>(walk.stats.reinlined));
    return true;
}
// The zones the zombies maps load: code and patch zones, the zombie zones, and the menus. A language directory
// prefixes them with its language ("en_").
bool IsZombieZone(std::string name, bool languageDir)
{
    if (languageDir)
    {
        const size_t underscore = name.find('_');
        if (underscore == std::string::npos)
            return false;
        name.erase(0, underscore + 1);
    }
    static const char *const kPrefixes[] = {"code_pre_gfx", "code_post_gfx", "common_zombie", "patch",
                                            "frontend",     "so_narrative",  "ui_mp",         "zombie"};
    for (const char *prefix : kPrefixes)
        if (name.rfind(prefix, 0) == 0)
            return true;
    return false;
}

struct Options
{
    DBFF64Abi abi = DB_FF64_ABI_WIN64;
    bool check = false;
    bool verify = false;
    bool detailed = true; // the per-block report, for one zone
};

// Converts (or with check only walks) one zone; prints why it failed.
bool ConvertZone(const std::string &input, const std::string &output, const Options &options)
{
    std::vector<uint8_t> file;
    if (!ReadFile(input.c_str(), file))
    {
        std::fprintf(stderr, "ffconv: cannot read %s\n", input.c_str());
        return false;
    }
    uint32_t version = 0;
    if (file.size() < kRetailHeaderSize || std::memcmp(file.data(), kRetailMagic, 8) ||
        (std::memcpy(&version, file.data() + 8, 4), version != kRetailVersion))
    {
        std::fprintf(stderr, "ffconv: %s is not an unsigned PC fastfile (IWffu100 v%u)\n", input.c_str(),
                     kRetailVersion);
        return false;
    }
    std::vector<uint8_t> stream;
    if (!Inflate(file.data() + kRetailHeaderSize, file.size() - kRetailHeaderSize, stream) ||
        stream.size() < sizeof(XFile))
    {
        std::fprintf(stderr, "ffconv: %s: the zlib stream is damaged\n", input.c_str());
        return false;
    }
    file = {};
    XFile header;
    std::memcpy(&header, stream.data(), sizeof(header));
    const uint8_t *body = stream.data() + sizeof(header);
    const size_t bodySize = stream.size() - sizeof(header);

    const Walk walk = WalkZone(body, bodySize, header.blockSize, false);
    if (options.detailed)
        Report(input.c_str(), header, bodySize, walk);
    ReportMismatches(walk);
    if (!walk.ok)
    {
        std::fprintf(stderr, "ffconv: %s: %s\n", input.c_str(), walk.error.c_str());
        return false;
    }
    if (options.check)
        return true;

    XFile out = header;
    out.size = static_cast<unsigned>(walk.output.size());
    for (int b = 0; b < 7; ++b)
        out.blockSize[b] = walk.stats.end64[b];
    std::vector<uint8_t> payload(sizeof(XFile));
    std::memcpy(payload.data(), &out, sizeof(out));
    payload.insert(payload.end(), walk.output.begin(), walk.output.end());
    std::vector<uint8_t> compressed;
    if (!Deflate(payload, compressed) || !WriteZone(output.c_str(), options.abi, compressed))
    {
        std::fprintf(stderr, "ffconv: cannot write %s\n", output.c_str());
        return false;
    }
    std::printf("  %s: %d assets, %llu pointers, %llu values fixed, %llu pointees inline, %zu -> %zu body bytes, %zu "
                "compressed\n",
                output.c_str(), walk.assetCount, static_cast<unsigned long long>(walk.stats.pointersTranslated),
                static_cast<unsigned long long>(walk.stats.valuesFixed),
                static_cast<unsigned long long>(walk.stats.reinlined), bodySize, walk.output.size(),
                compressed.size());
    return !options.verify || Verify(output.c_str(), header, body, bodySize, walk.reinlined);
}

// Converts every zombies zone of an install; returns how many failed, or -1 if the install has no zone directory.
int ConvertInstall(const std::filesystem::path &game, const std::filesystem::path &out, const Options &options)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(game / "zone", ec))
        return -1;
    std::vector<fs::path> zones;
    for (const fs::directory_entry &dir : fs::directory_iterator(game / "zone", ec))
    {
        if (!dir.is_directory())
            continue;
        const bool languageDir = dir.path().filename() != "common";
        for (const fs::directory_entry &zone : fs::directory_iterator(dir.path(), ec))
            if (zone.path().extension() == ".ff" && IsZombieZone(zone.path().stem().string(), languageDir))
                zones.push_back(zone.path());
    }
    std::sort(zones.begin(), zones.end());
    int failed = 0;
    for (size_t i = 0; i < zones.size(); ++i)
    {
        const fs::path target = out / "zone" / zones[i].parent_path().filename() / zones[i].filename();
        fs::create_directories(target.parent_path(), ec);
        std::printf("[%zu/%zu] %s\n", i + 1, zones.size(), zones[i].string().c_str());
        if (!ConvertZone(zones[i].string(), target.string(), options))
            ++failed;
    }
    std::printf("%zu zones, %d failed, %zu unique shaders\n", zones.size(), failed, ff::CollectedShaderCount());
    return failed;
}
} // namespace

int main(int argc, char **argv)
{
    // Unbuffered, so a crash still leaves the report of what was walked.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    Options options;
    const char *shaderDir = nullptr;
    const char *glslDir = nullptr;
    const char *game = nullptr;
    const char *out = nullptr;
    std::vector<const char *> paths;
    for (int i = 1; i < argc; ++i)
    {
        const bool hasValue = i + 1 < argc;
        if (!std::strcmp(argv[i], "--check"))
            options.check = true;
        else if (!std::strcmp(argv[i], "--verify"))
            options.verify = true;
        else if (!std::strcmp(argv[i], "--shader-dir") && hasValue)
            shaderDir = argv[++i];
        else if (!std::strcmp(argv[i], "--glsl-dir") && hasValue)
            glslDir = argv[++i];
        else if (!std::strcmp(argv[i], "--game") && hasValue)
            game = argv[++i];
        else if (!std::strcmp(argv[i], "--out") && hasValue)
            out = argv[++i];
        else if (!std::strcmp(argv[i], "--abi") && hasValue)
        {
            ++i;
            if (!std::strcmp(argv[i], "win64"))
                options.abi = DB_FF64_ABI_WIN64;
            else if (!std::strcmp(argv[i], "aarch64"))
                options.abi = DB_FF64_ABI_AARCH64;
            else
            {
                std::fprintf(stderr, "ffconv: unknown ABI '%s'\n", argv[i]);
                return 2;
            }
        }
        else
            paths.push_back(argv[i]);
    }
    const bool install = game || out;
    const bool usage = install ? !game || !out || !paths.empty() || options.check
                               : paths.size() != (options.check ? 1u : 2u) || (options.check && options.verify);
    if (usage)
    {
        std::fprintf(stderr, "usage: ffconv [--abi win64|aarch64] [--verify] --game INSTALL_DIR --out OUT_DIR\n"
                             "       ffconv [--abi win64|aarch64] [--verify] [--shader-dir DIR] [--glsl-dir DIR] "
                             "INPUT.ff OUTPUT.ff\n"
                             "       ffconv --check [--shader-dir DIR] [--glsl-dir DIR] INPUT.ff\n");
        return 2;
    }
    if (options.abi != DB_FF64_ABI_WIN64)
    {
        // The layout tables are generated for one 64-bit target; another ABI needs its own tables.
        std::fprintf(stderr, "ffconv: this build only has the win64 layout tables\n");
        return 2;
    }

    if (install)
    {
        options.detailed = false;
        const int failed = ConvertInstall(game, out, options);
        if (failed < 0)
        {
            std::fprintf(stderr, "ffconv: %s has no zone directory\n", game);
            return 1;
        }
        std::vector<std::string> failures;
        const std::string pack = (std::filesystem::path(out) / "shaders.pack").string();
        if (!ff::WriteShaderPack(pack, failures))
        {
            std::fprintf(stderr, "ffconv: cannot write %s\n", pack.c_str());
            return 1;
        }
        std::printf("%s: %zu shaders translated, %zu left out\n", pack.c_str(),
                    ff::CollectedShaderCount() - failures.size(), failures.size());
        for (const std::string &failure : failures)
            std::printf("  %s\n", failure.c_str());
        return failed || !failures.empty() ? 1 : 0;
    }

    if (!ConvertZone(paths[0], options.check ? std::string() : paths[1], options))
        return 1;
    if (shaderDir)
    {
        const int written = ff::WriteShaders(shaderDir);
        if (written < 0)
        {
            std::fprintf(stderr, "ffconv: cannot write shaders to %s\n", shaderDir);
            return 1;
        }
        std::printf("  %d new shaders in %s\n", written, shaderDir);
    }
    if (glslDir)
    {
        std::vector<std::string> failures;
        const int written = ff::WriteGlsl(glslDir, failures);
        if (written < 0)
        {
            std::fprintf(stderr, "ffconv: cannot write GLSL to %s\n", glslDir);
            return 1;
        }
        std::printf("  %d new GLSL shaders in %s, %zu not translated\n", written, glslDir, failures.size());
        for (const std::string &failure : failures)
            std::printf("    %s\n", failure.c_str());
    }
    return 0;
}
