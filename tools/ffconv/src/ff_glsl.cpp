#include "ff_glsl.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

#include <mojoshader.h>

namespace ff
{
namespace
{
// The engine's material stream destinations (s_streamDestInfo in r_material.cpp): a vertex input's location is its
// destination index, so the GL backend binds streams the way Material_BuildVertexDecl does.
const std::pair<int, int> kStreamDest[20] = {{0, 0}, {3, 0},  {10, 0}, {10, 1}, {12, 0}, {5, 0},  {5, 1},
                                             {5, 2}, {5, 3},  {5, 4},  {5, 5},  {5, 6},  {5, 7},  {5, 8},
                                             {5, 9}, {5, 10}, {5, 11}, {5, 12}, {5, 13}, {1, 0}};
const int kUsageTexcoord = 5;
const int kUsageColor = 10;

using Usage = std::pair<int, int>;

// MojoShader's GLSL profile rejects centroid, which GLSL 4.60 has: clear the _centroid modifier on every dcl and
// return the pixel shader inputs (usage, index) that carried it.
std::set<Usage> StripCentroid(std::vector<uint32_t> &tokens, bool pixel)
{
    const uint32_t kCentroid = 4u << 20;
    std::set<Usage> centroid;
    for (size_t i = 1; i < tokens.size();)
    {
        const uint32_t token = tokens[i];
        if (token == 0x0000FFFFu)
            break;
        const uint32_t opcode = token & 0xFFFF;
        if (opcode == 0xFFFE)
        {
            i += 1 + ((token >> 16) & 0x7FFF);
            continue;
        }
        const uint32_t length = (token >> 24) & 0xF;
        if (opcode == 31 && length >= 2 && i + 2 < tokens.size() && (tokens[i + 2] & kCentroid))
        {
            const uint32_t dest = tokens[i + 2];
            tokens[i + 2] = dest & ~kCentroid;
            const uint32_t regType = ((dest >> 28) & 7) | ((dest >> 8) & 0x18);
            if (pixel && regType == 1)
                centroid.insert({static_cast<int>(tokens[i + 1] & 0x1F), static_cast<int>((tokens[i + 1] >> 16) & 0xF)});
        }
        i += 1 + length;
    }
    return centroid;
}

bool StartsWith(const std::string &s, const std::string &prefix)
{
    return s.compare(0, prefix.size(), prefix) == 0;
}

void ReplaceAll(std::string &s, const std::string &from, const std::string &to)
{
    for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size()))
        s.replace(at, from.size(), to);
}

// GLSL 1.10 built-ins MojoShader uses for varyings, renamed io_<usage>_<index> like its other varyings.
void RenameBuiltinVaryings(std::string &line)
{
    for (size_t at = line.find("gl_TexCoord["); at != std::string::npos; at = line.find("gl_TexCoord[", at))
    {
        const size_t close = line.find(']', at);
        const std::string index = line.substr(at + 12, close - at - 12);
        line.replace(at, close + 1 - at, "io_5_" + index);
    }
    ReplaceAll(line, "gl_FrontSecondaryColor", "io_10_1");
    ReplaceAll(line, "gl_SecondaryColor", "io_10_1");
    ReplaceAll(line, "gl_FrontColor", "io_10_0");
    ReplaceAll(line, "gl_Color", "io_10_0");
}

void ModernTextureCalls(std::string &line)
{
    static const char *const kCalls[][2] = {
        {"texture2DProjGrad(", "textureProjGrad("}, {"texture2DGrad(", "textureGrad("},
        {"texture2DProj(", "textureProj("},         {"texture2DLod(", "textureLod("},
        {"texture2D(", "texture("},                 {"texture3D(", "texture("},
        {"textureCubeLod(", "textureLod("},         {"textureCube(", "texture("},
    };
    for (const auto &call : kCalls)
        ReplaceAll(line, call[0], call[1]);
}

// Every io_<usage>_<index> the text names.
void CollectVaryings(const std::string &line, std::set<Usage> &varyings)
{
    for (size_t at = line.find("io_"); at != std::string::npos; at = line.find("io_", at + 3))
    {
        if (at && (std::isalnum(static_cast<unsigned char>(line[at - 1])) || line[at - 1] == '_'))
            continue;
        int usage;
        int index;
        int used = 0;
        if (std::sscanf(line.c_str() + at, "io_%d_%d%n", &usage, &index, &used) == 2 && used)
            varyings.insert({usage, index});
    }
}

int VaryingLocation(Usage usage)
{
    if (usage.first == kUsageTexcoord && usage.second < 16)
        return usage.second;
    if (usage.first == kUsageColor && usage.second < 2)
        return 16 + usage.second;
    return -1;
}

int NumberAfter(const std::string &s, const char *prefix)
{
    return std::atoi(s.c_str() + std::strlen(prefix));
}

bool Normalize(const MOJOSHADER_parseData &parsed, bool pixel, const std::set<Usage> &centroid, std::string &glsl,
               std::string &error)
{
    const char *const stage = pixel ? "ps" : "vs";
    const std::string uniforms = std::string(stage) + "_uniforms_vec4";
    const std::string registers = std::string(stage) + "_c_reg";
    // Pixel shaders list their inputs (vPos and vFace included) as attributes too; only vertex inputs get locations.
    std::map<std::string, int> attributeLocation;
    for (int i = 0; !pixel && i < parsed.attribute_count; ++i)
    {
        const MOJOSHADER_attribute &a = parsed.attributes[i];
        int location = -1;
        for (int d = 0; d < 20; ++d)
            if (kStreamDest[d] == Usage{a.usage, a.index})
                location = d;
        if (location < 0)
        {
            error = "vertex input with D3D usage " + std::to_string(a.usage) + "/" + std::to_string(a.index) +
                    " has no material stream destination";
            return false;
        }
        attributeLocation[a.name] = location;
    }

    std::istringstream in(parsed.output);
    std::vector<std::string> body;
    std::set<int> literalRegisters;
    std::map<Usage, std::string> varyingType;
    std::set<Usage> varyings;
    for (std::string line; std::getline(in, line);)
    {
        if (StartsWith(line, "#version") || StartsWith(line, "#extension"))
            continue;
        char name[64];
        int count;
        int reg;
        if (std::sscanf(line.c_str(), "const vec4 %63[a-z_0-9] =", name) == 1 &&
            std::sscanf(name + 3, "c%d", &reg) == 1 && StartsWith(name, std::string(stage) + "_c"))
            literalRegisters.insert(reg);
        if (std::sscanf(line.c_str(), "uniform vec4 %63[a-z_0-9][%d];", name, &count) == 2 && uniforms == name)
            continue;
        // A range of def'd literals the shader indexes with a0: MojoShader leaves it as a uniform array for its own
        // GL glue to fill from the defs; here it is built from the literals it already declared.
        int base;
        if (std::sscanf(line.c_str(), "uniform vec4 %63[a-z_0-9][%d];", name, &count) == 2 &&
            std::sscanf(name, (std::string(stage) + "_const_array_%d_").c_str(), &base) == 1)
        {
            std::string init = "const vec4 " + std::string(name) + "[" + std::to_string(count) + "] = vec4[](";
            for (int i = 0; i < count; ++i)
            {
                if (!literalRegisters.count(base + i))
                {
                    error = std::string(name) + " indexes a register that is not a literal";
                    return false;
                }
                init += (i ? ", " : "") + std::string(stage) + "_c" + std::to_string(base + i);
            }
            body.push_back(init + ");");
            continue;
        }
        if (StartsWith(line, "uniform vec2 vposFlip;"))
        {
            body.push_back("#define vposFlip bo1_vpos_flip.xy");
            continue;
        }
        if (StartsWith(line, "#define " + std::string(stage) + "_c") &&
            line.find(uniforms + "[") != std::string::npos)
        {
            const std::string reg = line.substr(8, line.find(' ', 8) - 8);
            const int number = NumberAfter(reg, (std::string(stage) + "_c").c_str());
            body.push_back("#define " + reg + " " + registers + "[" + std::to_string(number) + "]");
            continue;
        }
        if (StartsWith(line, "#define ARRAYBASE_"))
        {
            const int base = NumberAfter(line, "#define ARRAYBASE_");
            body.push_back("#define ARRAYBASE_" + std::to_string(base) + " " + std::to_string(base));
            continue;
        }
        char samplerType[32];
        int samplerIndex;
        if (std::sscanf(line.c_str(), "uniform %31s ps_s%d;", samplerType, &samplerIndex) == 2 &&
            StartsWith(samplerType, "sampler"))
        {
            body.push_back("layout(binding = " + std::to_string(samplerIndex) + ") uniform " + samplerType + " ps_s" +
                           std::to_string(samplerIndex) + ";");
            continue;
        }
        char type[16];
        if (std::sscanf(line.c_str(), "attribute %15s %63[a-z_0-9];", type, name) == 2)
        {
            const auto location = attributeLocation.find(name);
            if (location == attributeLocation.end())
            {
                error = std::string("vertex input ") + name + " is not in MojoShader's attribute list";
                return false;
            }
            body.push_back("layout(location = " + std::to_string(location->second) + ") in " + type + " " + name +
                           ";");
            continue;
        }
        int usage;
        int index;
        if (std::sscanf(line.c_str(), "varying %15s io_%d_%d;", type, &usage, &index) == 3)
        {
            varyingType[{usage, index}] = type;
            continue;
        }
        ReplaceAll(line, uniforms + "[", registers + "[");
        RenameBuiltinVaryings(line);
        ModernTextureCalls(line);
        ReplaceAll(line, "gl_FragColor", "bo1_ps_color");
        CollectVaryings(line, varyings);
        body.push_back(line);
    }

    std::ostringstream out;
    out << "#version 460 core\n";
    out << "// Translated from D3D9 bytecode by ffconv (MojoShader + ff_glsl.cpp); see docs/RENDERER.md.\n";
    if (pixel)
        out << "layout(std140, binding = 1) uniform BO1PixelConstants\n{\n    vec4 ps_c_reg[256];\n"
               "    vec4 bo1_vpos_flip;\n    vec4 bo1_alpha_test;\n};\n";
    else
        out << "layout(std140, binding = 0) uniform BO1VertexConstants\n{\n    vec4 vs_c_reg[256];\n};\n";
    for (const Usage &usage : varyings)
    {
        const int location = VaryingLocation(usage);
        if (location < 0)
        {
            error = "varying with D3D usage " + std::to_string(usage.first) + "/" + std::to_string(usage.second) +
                    " has no location";
            return false;
        }
        const auto type = varyingType.find(usage);
        out << "layout(location = " << location << ") " << (pixel && centroid.count(usage) ? "centroid " : "")
            << (pixel ? "in " : "out ") << (type == varyingType.end() ? "vec4" : type->second) << " io_" << usage.first
            << "_" << usage.second << ";\n";
    }
    if (pixel)
        out << "layout(location = 0) out vec4 bo1_color0;\nvec4 bo1_ps_color;\n";

    size_t mainEnd = body.size();
    while (mainEnd && body[mainEnd - 1] != "}")
        --mainEnd;
    if (!mainEnd)
    {
        error = "MojoShader output has no closing brace for main";
        return false;
    }
    for (size_t i = 0; i < body.size(); ++i)
    {
        if (pixel && i == mainEnd - 1)
            // D3D9's fixed-function alpha test: x is 0 (off), 1 (alpha > ref) or 2 (alpha >= ref), y the 0-255 ref.
            out << "\tif (bo1_alpha_test.x != 0.0)\n\t{\n"
                   "\t\tfloat bo1_alpha = floor(clamp(bo1_ps_color.a, 0.0, 1.0) * 255.0 + 0.5);\n"
                   "\t\tif (bo1_alpha_test.x == 1.0 ? !(bo1_alpha > bo1_alpha_test.y) : "
                   "!(bo1_alpha >= bo1_alpha_test.y))\n\t\t\tdiscard;\n\t}\n\tbo1_color0 = bo1_ps_color;\n";
        out << body[i] << "\n";
    }
    glsl = out.str();
    return true;
}
} // namespace

bool TranslateShader(const uint8_t *bytecode, size_t size, bool pixel, std::string &glsl, std::string &error)
{
    std::vector<uint32_t> tokens(size / 4);
    std::memcpy(tokens.data(), bytecode, tokens.size() * 4);
    const std::set<Usage> centroid = StripCentroid(tokens, pixel);
    const MOJOSHADER_parseData *parsed =
        MOJOSHADER_parse(MOJOSHADER_PROFILE_GLSL, "main", reinterpret_cast<const unsigned char *>(tokens.data()),
                         static_cast<unsigned>(tokens.size() * 4), nullptr, 0, nullptr, 0, nullptr, nullptr, nullptr);
    bool ok;
    if (parsed->error_count)
    {
        error = std::string("MojoShader: ") + parsed->errors[0].error;
        ok = false;
    }
    else
        ok = Normalize(*parsed, pixel, centroid, glsl, error);
    MOJOSHADER_freeParseData(parsed);
    return ok;
}
} // namespace ff
