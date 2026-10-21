#pragma once

// The D3D9 shaders the converted zones carry, collected during the walk (Load_CreateMaterial*Shader) and keyed by
// ShaderPack_Hash of their bytecode, so the same shader in many zones is translated once.

#include <cstdint>
#include <string>
#include <vector>

namespace ff
{
void CollectShader(bool pixel, const uint32_t *program, uint32_t dwordCount);
size_t CollectedShaderCount();

// Writes each collected shader not already there as DIR/<hash>.vs or DIR/<hash>.ps (raw bytecode); returns how many
// were new, or -1 on a write error.
int WriteShaders(const std::string &dir);
// Translates each collected shader not already there to DIR/<hash>.vert or DIR/<hash>.frag (ff_glsl.h), for
// glslangValidator. Shaders that fail are listed in `failures` as "<hash>: <reason>"; returns how many were new, or
// -1 on a write error.
int WriteGlsl(const std::string &dir, std::vector<std::string> &failures);
// Translates every collected shader into one shaders.pack (src/gfx_d3d9emu/shader_pack.h). Shaders that fail are
// listed in `failures` and left out.
bool WriteShaderPack(const std::string &path, std::vector<std::string> &failures);
} // namespace ff
