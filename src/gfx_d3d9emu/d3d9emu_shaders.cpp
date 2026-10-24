// Shaders and vertex declarations. A D3D9 shader becomes the backend shader of its translation in shaders.pack,
// found by the hash of its bytecode (shader_pack.h); the pack is opened at the first shader and read on demand.

#include "d3d9emu.h"

#include "d3d9emu_host.h"
#include "shader_pack.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace d3d9emu
{
namespace
{
constexpr DWORD kEndToken = 0x0000FFFF;
constexpr DWORD kCommentOpcode = 0xFFFE;
constexpr DWORD kDclOpcode = 31;
constexpr DWORD kRegisterSampler = 10;

class ShaderPack
{
public:
    // False (logged once) when the pack is missing or malformed.
    bool Open()
    {
        if (m_tried)
            return m_file != nullptr;
        m_tried = true;
        const char *path = host::ShaderPackPath();
        FILE *file = path ? std::fopen(path, "rb") : nullptr;
        if (!file)
        {
            Logf("d3d9emu: no shaders.pack at %s: nothing will be drawn", path ? path : "(no path)");
            return false;
        }
        const char *problem = Load(file);
        if (problem)
        {
            Logf("d3d9emu: %s is not a usable shaders.pack (%s): nothing will be drawn", path, problem);
            std::fclose(file);
            m_entries.clear();
            return false;
        }
        m_file = file;
        Logf("d3d9emu: shaders.pack holds %zu shaders", m_entries.size());
        return true;
    }

    const ShaderPackEntry *Find(uint64_t hash) const
    {
        size_t low = 0, high = m_entries.size();
        while (low < high)
        {
            const size_t middle = (low + high) / 2;
            if (m_entries[middle].hash < hash)
                low = middle + 1;
            else
                high = middle;
        }
        return low < m_entries.size() && m_entries[low].hash == hash ? &m_entries[low] : nullptr;
    }

    bool Read(const ShaderPackEntry &entry, std::string &text)
    {
        text.resize(size_t(entry.size) + 1);
        if (std::fseek(m_file, long(entry.offset), SEEK_SET) ||
            std::fread(&text[0], 1, text.size(), m_file) != text.size() || text.back() != '\0')
            return false;
        text.pop_back();
        return true;
    }

private:
    const char *Load(FILE *file)
    {
        if (std::fseek(file, 0, SEEK_END))
            return "cannot seek";
        const long size = std::ftell(file);
        std::rewind(file);
        ShaderPackHeader header;
        if (size < long(sizeof(header)) || std::fread(&header, sizeof(header), 1, file) != 1)
            return "too short";
        if (std::memcmp(header.magic, SHADER_PACK_MAGIC, sizeof(header.magic)))
            return "bad magic";
        if (header.version != SHADER_PACK_VERSION)
            return "unknown version";
        if (uint64_t(header.count) * sizeof(ShaderPackEntry) > uint64_t(size) - sizeof(header))
            return "entry table past the end";
        m_entries.resize(header.count);
        if (header.count && std::fread(m_entries.data(), sizeof(ShaderPackEntry), header.count, file) != header.count)
            return "cannot read the entries";
        for (size_t i = 0; i < m_entries.size(); ++i)
        {
            const ShaderPackEntry &e = m_entries[i];
            if (i && m_entries[i - 1].hash >= e.hash)
                return "entries not sorted";
            if (e.stage > SHADER_PACK_PIXEL || uint64_t(e.offset) + e.size + 1 > uint64_t(size))
                return "entry outside the file";
        }
        return nullptr;
    }

    bool m_tried = false;
    FILE *m_file = nullptr;
    std::vector<ShaderPackEntry> m_entries;
};

ShaderPack &Pack()
{
    static ShaderPack *pack = new ShaderPack();
    return *pack;
}

bool VersionOk(DWORD version)
{
    const DWORD type = version >> 16;
    return type == 0xFFFE || type == 0xFFFF;
}
} // namespace

size_t ShaderBytecodeSize(const DWORD *function, size_t maxBytes)
{
    const size_t tokens = maxBytes / 4;
    if (!function || tokens < 2 || !VersionOk(function[0]))
        return 0;
    for (size_t i = 1; i < tokens;)
    {
        const DWORD token = function[i];
        if (token == kEndToken)
            return (i + 1) * 4;
        if ((token & 0xFFFF) == kCommentOpcode)
            i += 1 + ((token >> 16) & 0x7FFF);
        else
            i += 1 + ((token >> 24) & 0xF);
    }
    return 0;
}

uint32_t PixelShaderSamplers(const DWORD *function, size_t bytes)
{
    uint32_t mask = 0;
    const size_t tokens = bytes / 4;
    for (size_t i = 1; i < tokens;)
    {
        const DWORD token = function[i];
        if (token == kEndToken)
            break;
        const DWORD opcode = token & 0xFFFF;
        if (opcode == kCommentOpcode)
        {
            i += 1 + ((token >> 16) & 0x7FFF);
            continue;
        }
        const DWORD length = (token >> 24) & 0xF;
        if (opcode == kDclOpcode && length >= 2 && i + 2 < tokens)
        {
            const DWORD destination = function[i + 2];
            const DWORD registerType = ((destination >> 28) & 7) | ((destination >> 8) & 0x18);
            const DWORD number = destination & 0x7FF;
            if (registerType == kRegisterSampler && number < gb::MAX_TEXTURES)
                mask |= 1u << number;
        }
        i += 1 + length;
    }
    return mask;
}

gb::Shader *CreatePackedShader(gb::Stage stage, uint64_t hash)
{
    ShaderPack &pack = Pack();
    if (!pack.Open())
        return nullptr;
    char key[48];
    std::snprintf(key, sizeof(key), "shader %016llx", static_cast<unsigned long long>(hash));
    const ShaderPackEntry *entry = pack.Find(hash);
    const uint32_t wanted = stage == gb::Stage::Pixel ? SHADER_PACK_PIXEL : SHADER_PACK_VERTEX;
    if (!entry || entry->stage != wanted)
    {
        WarnOnce(key, "d3d9emu: %s shader %016llx is not in shaders.pack: draws with it are skipped",
                 stage == gb::Stage::Pixel ? "pixel" : "vertex", static_cast<unsigned long long>(hash));
        return nullptr;
    }
    std::string text;
    if (!pack.Read(*entry, text))
    {
        WarnOnce(key, "d3d9emu: cannot read shader %016llx from shaders.pack", static_cast<unsigned long long>(hash));
        return nullptr;
    }
    gb::Shader *shader = gb::CreateShader(stage, hash, text.data(), uint32_t(text.size()));
    if (!shader)
        WarnOnce(key, "d3d9emu: the backend refused shader %016llx", static_cast<unsigned long long>(hash));
    return shader;
}

bool TranslateDeclaration(const D3DVERTEXELEMENT9 *elements, size_t count, gb::VertexAttrib *attribs,
                          uint8_t *attribCount)
{
    uint8_t n = 0;
    for (size_t i = 0; i < count; ++i)
    {
        const D3DVERTEXELEMENT9 &e = elements[i];
        if (e.Type == D3DDECLTYPE_UNUSED)
            continue;
        gb::AttribType type;
        switch (e.Type)
        {
        case D3DDECLTYPE_FLOAT1:
            type = gb::AttribType::Float1;
            break;
        case D3DDECLTYPE_FLOAT2:
            type = gb::AttribType::Float2;
            break;
        case D3DDECLTYPE_FLOAT3:
            type = gb::AttribType::Float3;
            break;
        case D3DDECLTYPE_FLOAT4:
            type = gb::AttribType::Float4;
            break;
        case D3DDECLTYPE_D3DCOLOR:
            type = gb::AttribType::UByte4NBGRA;
            break;
        case D3DDECLTYPE_UBYTE4:
            type = gb::AttribType::UByte4;
            break;
        case D3DDECLTYPE_SHORT2:
            type = gb::AttribType::Short2;
            break;
        case D3DDECLTYPE_SHORT4:
            type = gb::AttribType::Short4;
            break;
        case D3DDECLTYPE_UBYTE4N:
            type = gb::AttribType::UByte4N;
            break;
        case D3DDECLTYPE_SHORT2N:
            type = gb::AttribType::Short2N;
            break;
        case D3DDECLTYPE_SHORT4N:
            type = gb::AttribType::Short4N;
            break;
        case D3DDECLTYPE_USHORT2N:
            type = gb::AttribType::UShort2N;
            break;
        case D3DDECLTYPE_USHORT4N:
            type = gb::AttribType::UShort4N;
            break;
        case D3DDECLTYPE_FLOAT16_2:
            type = gb::AttribType::Half2;
            break;
        case D3DDECLTYPE_FLOAT16_4:
            type = gb::AttribType::Half4;
            break;
        default: // UDEC3 and DEC3N, which the caps leave out
            return false;
        }
        if (e.Method != D3DDECLMETHOD_DEFAULT || e.Stream >= gb::MAX_STREAMS)
            return false;
        const int location = ShaderPack_InputLocation(e.Usage, e.UsageIndex);
        if (location < 0)
            continue;
        for (uint8_t j = 0; j < n; ++j)
            if (attribs[j].location == location)
                return false;
        if (n == gb::MAX_ATTRIBS)
            return false;
        attribs[n++] = {uint8_t(location), uint8_t(e.Stream), e.Offset, type};
    }
    *attribCount = n;
    return true;
}

VertexShader::VertexShader(Device *device, gb::Shader *backendShader, uint64_t shaderHash)
    : shader(backendShader), hash(shaderHash), m_device(device)
{
    m_device->AddRef();
}

VertexShader::~VertexShader()
{
    gb::DestroyShader(shader);
}

IUnknown *VertexShader::Parent()
{
    return m_device;
}

HRESULT VertexShader::GetDevice(IDirect3DDevice9 **device)
{
    if (!device)
        return D3DERR_INVALIDCALL;
    m_device->AddRef();
    *device = m_device;
    return D3D_OK;
}

PixelShader::PixelShader(Device *device, gb::Shader *backendShader, uint64_t shaderHash, uint32_t samplers)
    : shader(backendShader), hash(shaderHash), samplerMask(samplers), m_device(device)
{
    m_device->AddRef();
}

PixelShader::~PixelShader()
{
    gb::DestroyShader(shader);
}

IUnknown *PixelShader::Parent()
{
    return m_device;
}

HRESULT PixelShader::GetDevice(IDirect3DDevice9 **device)
{
    if (!device)
        return D3DERR_INVALIDCALL;
    m_device->AddRef();
    *device = m_device;
    return D3D_OK;
}

VertexDeclaration::VertexDeclaration(Device *device, std::vector<D3DVERTEXELEMENT9> declElements,
                                     const gb::VertexAttrib *declAttribs, uint8_t count)
    : elements(std::move(declElements)), attribCount(count), m_device(device)
{
    std::memset(attribs, 0, sizeof(attribs));
    std::memcpy(attribs, declAttribs, sizeof(gb::VertexAttrib) * count);
    m_device->AddRef();
}

VertexDeclaration::~VertexDeclaration() = default;

IUnknown *VertexDeclaration::Parent()
{
    return m_device;
}

HRESULT VertexDeclaration::GetDevice(IDirect3DDevice9 **device)
{
    if (!device)
        return D3DERR_INVALIDCALL;
    m_device->AddRef();
    *device = m_device;
    return D3D_OK;
}
} // namespace d3d9emu
