#pragma once

// shaders.pack, written by tools/ffconv next to the converted zones: the GLSL 4.60 translation of every D3D9
// shader in them, found by the hash of the D3D9 bytecode the zone hands to Load_CreateMaterial*Shader
// (docs/RENDERER.md, "Shader interface"). Little-endian, as on every target.
//
// Layout: ShaderPackHeader, `count` ShaderPackEntry sorted by hash, then the GLSL texts, each NUL-terminated, at
// their offsets from the start of the file.

#include <cstddef>
#include <cstdint>

#define SHADER_PACK_MAGIC "BO1GLSL1"
// Same layout, payloads are deko3d DKSH modules uam compiled offline (tools/ffconv/gen/make_dksh_pack.py)
// instead of GLSL; the deko3d backend loads it in place of the GLSL pack (host::ShaderPackPath picks by backend).
#define SHADER_PACK_MAGIC_DKSH "BO1DKSH1"
inline constexpr uint32_t SHADER_PACK_VERSION = 1;

enum ShaderPackStage : uint32_t
{
    SHADER_PACK_VERTEX = 0,
    SHADER_PACK_PIXEL = 1,
};

struct ShaderPackHeader
{
    char magic[8];
    uint32_t version;
    uint32_t count;
};

struct ShaderPackEntry
{
    uint64_t hash;
    uint32_t stage;
    uint32_t offset;
    uint32_t size; // without the terminating NUL
    uint32_t reserved;
};

static_assert(sizeof(ShaderPackHeader) == 16 && sizeof(ShaderPackEntry) == 24, "shaders.pack layout");

// The location of a vertex shader input, so that a stream reaches the input Material_BuildVertexDecl routes it to:
// the index of its D3D9 usage in the engine's material stream destinations (s_streamDestInfo in
// gfx_d3d/r_material.cpp), except that GL and uam accept 16 locations and there are 20 destinations. No vertex shader
// in the zombies zones reads TEXCOORD10-13, so they get none (-1, as a usage that is not a destination; ffconv
// rejects a shader that reads one), and BLENDWEIGHT takes 15 instead of 19 (uam and GL refused its 12 shaders).
inline int ShaderPack_InputLocation(int usage, int usageIndex)
{
    static constexpr uint8_t kDestinations[16][2] = {{0, 0}, {3, 0}, {10, 0}, {10, 1}, {12, 0}, {5, 0}, {5, 1}, {5, 2},
                                                     {5, 3}, {5, 4}, {5, 5}, {5, 6},  {5, 7},  {5, 8}, {5, 9}, {1, 0}};
    for (int d = 0; d < 16; ++d)
        if (kDestinations[d][0] == usage && kDestinations[d][1] == usageIndex)
            return d;
    return -1;
}

// FNV-1a 64 over the bytecode bytes.
inline uint64_t ShaderPack_Hash(const void *bytecode, size_t size)
{
    uint64_t hash = 1469598103934665603ull;
    for (size_t i = 0; i < size; ++i)
    {
        hash ^= static_cast<const uint8_t *>(bytecode)[i];
        hash *= 1099511628211ull;
    }
    return hash;
}
