#pragma once

// D3D9 vs_3_0 / ps_3_0 bytecode -> GLSL 4.60 core, with the fixed interface of docs/RENDERER.md ("Shader
// interface"): constants in a std140 block per stage, samplers at their s# binding, vertex inputs at their
// material stream destination, varyings at a location per D3D usage, alpha test in the pixel shader.

#include <cstddef>
#include <cstdint>
#include <string>

namespace ff
{
bool TranslateShader(const uint8_t *bytecode, size_t size, bool pixel, std::string &glsl, std::string &error);
} // namespace ff
