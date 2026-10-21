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
