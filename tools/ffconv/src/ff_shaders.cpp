#include "ff_shaders.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <utility>
#include <vector>

#include <gfx_d3d9emu/shader_pack.h>

#include "ff_glsl.h"

namespace ff
{
namespace
{
struct Shader
{
    bool pixel;
    std::vector<uint8_t> bytecode;
};

std::map<uint64_t, Shader> g_shaders;

std::string FileName(uint64_t hash, const char *extension)
{
    char name[32];
    std::snprintf(name, sizeof(name), "%016llx.%s", static_cast<unsigned long long>(hash), extension);
    return name;
}

bool Exists(const std::string &path)
{
    FILE *f = std::fopen(path.c_str(), "rb");
    if (f)
        std::fclose(f);
    return f != nullptr;
}

bool Write(const std::string &path, const void *data, size_t size)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;
    const bool ok = std::fwrite(data, 1, size, f) == size;
    return std::fclose(f) == 0 && ok;
}

bool Translate(uint64_t hash, const Shader &shader, std::string &glsl, std::vector<std::string> &failures)
{
    std::string error;
    if (TranslateShader(shader.bytecode.data(), shader.bytecode.size(), shader.pixel, glsl, error))
        return true;
    failures.push_back(FileName(hash, shader.pixel ? "ps" : "vs") + ": " + error);
    return false;
}
} // namespace

void CollectShader(bool pixel, const uint32_t *program, uint32_t dwordCount)
{
    if (!program || !dwordCount)
        return;
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(program);
    const size_t size = dwordCount * 4u;
    g_shaders.try_emplace(ShaderPack_Hash(bytes, size), Shader{pixel, {bytes, bytes + size}});
}

size_t CollectedShaderCount()
{
    return g_shaders.size();
}

int WriteShaders(const std::string &dir)
{
    int written = 0;
    for (const auto &[hash, shader] : g_shaders)
    {
        const std::string path = dir + "/" + FileName(hash, shader.pixel ? "ps" : "vs");
        if (Exists(path))
            continue;
        if (!Write(path, shader.bytecode.data(), shader.bytecode.size()))
            return -1;
        ++written;
    }
    return written;
}

int WriteGlsl(const std::string &dir, std::vector<std::string> &failures)
{
    int written = 0;
    for (const auto &[hash, shader] : g_shaders)
    {
        const std::string path = dir + "/" + FileName(hash, shader.pixel ? "frag" : "vert");
        std::string glsl;
        if (Exists(path) || !Translate(hash, shader, glsl, failures))
            continue;
        if (!Write(path, glsl.data(), glsl.size()))
            return -1;
        ++written;
    }
    return written;
}

bool WriteShaderPack(const std::string &path, std::vector<std::string> &failures)
{
    std::vector<ShaderPackEntry> entries;
    std::vector<std::string> texts;
    for (const auto &[hash, shader] : g_shaders)
    {
        std::string glsl;
        if (!Translate(hash, shader, glsl, failures))
            continue;
        entries.push_back({hash, shader.pixel ? SHADER_PACK_PIXEL : SHADER_PACK_VERTEX, 0,
                           static_cast<uint32_t>(glsl.size()), 0});
        texts.push_back(std::move(glsl));
    }
    ShaderPackHeader header{};
    std::memcpy(header.magic, SHADER_PACK_MAGIC, sizeof(header.magic));
    header.version = SHADER_PACK_VERSION;
    header.count = static_cast<uint32_t>(entries.size());
    size_t offset = sizeof(header) + entries.size() * sizeof(ShaderPackEntry);
    for (ShaderPackEntry &entry : entries)
    {
        entry.offset = static_cast<uint32_t>(offset);
        offset += entry.size + 1;
    }
    std::vector<uint8_t> file(sizeof(header));
    std::memcpy(file.data(), &header, sizeof(header));
    const uint8_t *table = reinterpret_cast<const uint8_t *>(entries.data());
    file.insert(file.end(), table, table + entries.size() * sizeof(ShaderPackEntry));
    for (const std::string &text : texts)
        file.insert(file.end(), text.c_str(), text.c_str() + text.size() + 1);
    return Write(path, file.data(), file.size());
}
} // namespace ff
