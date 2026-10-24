// The null backend: implements the whole of gfx_backend.h without a GPU. It draws nothing and keeps no texel, but it
// checks every call against the contract and ends the process on a violation, so a headless run with it proves the
// frontend's use of the contract. Results are those of a device that drew nothing: occlusion queries count 0
// samples, fences are reached at once, readbacks return zeros, timestamps read a monotonic clock.

#include "gb_null.h"

#include "../gb_backend.h"
#include "../gb_common.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>

#if defined(__unix__)
#include <unistd.h>
#endif

namespace gb::null
{
struct Buffer : gb::Buffer
{
    Buffer(int i, const BufferDesc &d) : id(i), desc(d) {}
    int id;
    BufferDesc desc;
};

struct Texture : gb::Texture
{
    Texture(int i, const TextureDesc &d) : id(i), desc(d) {}
    int id;
    TextureDesc desc;
};

struct Sampler : gb::Sampler
{
    Sampler(int i, const SamplerDesc &d) : id(i), desc(d) {}
    int id;
    SamplerDesc desc;
};

struct Shader : gb::Shader
{
    Shader(int i, Stage s, uint64_t h) : id(i), stage(s), hash(h) {}
    int id;
    Stage stage;
    uint64_t hash;
};

struct Pipeline : gb::Pipeline
{
    Pipeline(int i, const PipelineDesc &d) : id(i), desc(d) {}
    int id;
    PipelineDesc desc;
};

struct Query : gb::Query
{
    Query(int i, QueryType t) : id(i), type(t) {}
    int id;
    QueryType type;
    bool begun = false;
    bool ended = false;
    uint64_t value = 0;
};

struct Fence : gb::Fence
{
    explicit Fence(int i) : id(i) {}
    int id;
};

struct Swapchain : gb::Swapchain
{
    Swapchain(int i, const SwapchainDesc &d) : id(i), desc(d) {}
    int id;
    SwapchainDesc desc;
};

namespace
{
#define GB_NULL_CAST(T)                                                                                               \
    [[maybe_unused]] T *N(gb::T *p)                                                                                   \
    {                                                                                                                 \
        return static_cast<T *>(p);                                                                                   \
    }                                                                                                                 \
    [[maybe_unused]] const T *N(const gb::T *p)                                                                       \
    {                                                                                                                 \
        return static_cast<const T *>(p);                                                                             \
    }
GB_NULL_CAST(Buffer)
GB_NULL_CAST(Texture)
GB_NULL_CAST(Sampler)
GB_NULL_CAST(Shader)
GB_NULL_CAST(Pipeline)
GB_NULL_CAST(Query)
GB_NULL_CAST(Fence)
GB_NULL_CAST(Swapchain)
#undef GB_NULL_CAST

struct State
{
    bool initialized = false;
    Caps caps = {};
    int nextId = 1;
    bool recording = false;
    std::vector<std::string> log;
    std::unordered_map<std::string, std::unique_ptr<Sampler>> samplers;
    std::unordered_map<std::string, std::unique_ptr<Pipeline>> pipelines;
    bool inPass = false;
    PassDesc pass = {};
    uint32_t passWidth = 0, passHeight = 0;
    uint8_t passSamples = 1;
    Query *openOcclusion = nullptr;
    uint64_t uniformOffset = 0;
    uint64_t frame = 0;
};

State &S()
{
    static State *state = new State();
    return *state;
}

[[noreturn]] void Violation(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    std::fprintf(stderr, "gb null: contract violation: ");
    std::vfprintf(stderr, fmt, args);
    std::fprintf(stderr, "\n");
    va_end(args);
    std::fflush(nullptr);
    std::abort();
}

void Check(bool condition, const char *what)
{
    if (!condition)
        Violation("%s", what);
}

void Record(const char *fmt, ...)
{
    if (!S().recording)
        return;
    char line[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    S().log.emplace_back(line);
}

const char *FormatName(Format f)
{
    static const char *const names[] = {"Unknown", "BGRA8", "BGRX8", "RGBA8",   "B5G6R5", "B5G5R5A1", "R8",
                                        "RG8",     "RG16",  "RGBA16", "R16F",   "RG16F",  "RGBA16F",  "R32F",
                                        "BC1",     "BC2",   "BC3",    "D16",    "D24S8"};
    static_assert(sizeof(names) / sizeof(names[0]) == size_t(Format::Count), "format names");
    return f < Format::Count ? names[int(f)] : "?";
}

const char *UsageName(Usage u)
{
    static const char *const names[] = {"Static", "Dynamic", "RenderTarget", "DepthStencil"};
    return names[int(u)];
}

uint64_t FNV(const void *data, size_t size)
{
    uint64_t hash = 1469598103934665603ull;
    for (size_t i = 0; i < size; ++i)
    {
        hash ^= static_cast<const uint8_t *>(data)[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

uint64_t PhysicalMemory()
{
#if defined(__unix__)
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long pageSize = sysconf(_SC_PAGESIZE);
    if (pages > 0 && pageSize > 0)
        return uint64_t(pages) * uint64_t(pageSize);
#endif
    return 4ull << 30;
}

void CheckTextureLevel(const Texture *texture, uint8_t level, uint8_t layer, const char *call)
{
    if (!texture)
        Violation("%s: no texture", call);
    if (level >= texture->desc.levels)
        Violation("%s: T%d has %u levels, level %u", call, texture->id, texture->desc.levels, level);
    const uint8_t layers = texture->desc.type == TextureType::Cube ? 6 : 1;
    if (layer >= layers)
        Violation("%s: T%d has %u layers, layer %u", call, texture->id, layers, layer);
}

void CheckBox(const Texture *texture, uint8_t level, const Box &box, const char *call)
{
    const TextureDesc &d = texture->desc;
    const uint32_t w = LevelExtent(d.width, level), h = LevelExtent(d.height, level);
    const uint32_t depth = d.type == TextureType::Tex3D ? LevelExtent(d.depth, level) : 1;
    if (!box.width || !box.height || !box.depth || box.x + box.width > w || box.y + box.height > h ||
        box.z + box.depth > depth)
        Violation("%s: T%d level %u is %ux%ux%u, box %u,%u,%u %ux%ux%u", call, texture->id, level, w, h, depth, box.x,
                  box.y, box.z, box.width, box.height, box.depth);
    if (IsBlockFormat(d.format) && (box.x % 4 || box.y % 4 || (box.width % 4 && box.x + box.width != w) ||
                                    (box.height % 4 && box.y + box.height != h)))
        Violation("%s: T%d box %u,%u %ux%u is not made of whole blocks", call, texture->id, box.x, box.y, box.width,
                  box.height);
}

void CheckRect(const Texture *texture, uint8_t level, const Rect &r, const char *call)
{
    const uint32_t w = LevelExtent(texture->desc.width, level), h = LevelExtent(texture->desc.height, level);
    if (r.x < 0 || r.y < 0 || !r.width || !r.height || uint32_t(r.x) + r.width > w || uint32_t(r.y) + r.height > h)
        Violation("%s: T%d level %u is %ux%u, rect %d,%d %ux%u", call, texture->id, level, w, h, r.x, r.y, r.width,
                  r.height);
}

bool IsAttachment(const Texture *texture)
{
    const State &s = S();
    for (int i = 0; i < s.pass.colorCount; ++i)
        if (N(s.pass.color[i].texture) == texture)
            return true;
    return N(s.pass.depth) == texture;
}

void CheckDraw(const DrawState &state, const char *call)
{
    State &s = S();
    if (!s.inPass)
        Violation("%s outside a pass", call);
    if (!state.pipeline)
        Violation("%s without a pipeline", call);
    const Pipeline *pipeline = N(state.pipeline);
    const PipelineDesc &p = pipeline->desc;
    if (p.colorCount != s.pass.colorCount || p.samples != s.passSamples ||
        p.depthFormat != (s.pass.depth ? N(s.pass.depth)->desc.format : Format::Unknown))
        Violation("%s: pipeline P%d targets do not match the pass", call, pipeline->id);
    for (int i = 0; i < p.colorCount; ++i)
        if (p.colorFormats[i] != N(s.pass.color[i].texture)->desc.format)
            Violation("%s: pipeline P%d colour %d is %s, the pass has %s", call, pipeline->id, i,
                      FormatName(p.colorFormats[i]), FormatName(N(s.pass.color[i].texture)->desc.format));
    for (int i = 0; i < p.attribCount; ++i)
    {
        const VertexBinding &v = state.vertices[p.attribs[i].stream];
        if (!v.buffer || N(v.buffer)->desc.type != BufferType::Vertex)
            Violation("%s: attribute at location %u reads stream %u, which has no vertex buffer", call,
                      p.attribs[i].location, p.attribs[i].stream);
    }
    for (int i = 0; i < MAX_TEXTURES; ++i)
    {
        const Texture *texture = N(state.textures[i].texture);
        if (!texture)
            continue;
        if (!state.textures[i].sampler)
            Violation("%s: texture T%d at %d has no sampler", call, texture->id, i);
        if (!texture->desc.sampled)
            Violation("%s: texture T%d at %d is not sampled", call, texture->id, i);
        if (IsAttachment(texture))
            Violation("%s: texture T%d at %d is an attachment of the pass", call, texture->id, i);
    }
    if (!state.vertexConstants.buffer || !state.pixelConstants.buffer)
        Violation("%s without constants", call);
    if (state.viewport.width <= 0.0f || state.viewport.height <= 0.0f)
        Violation("%s: empty viewport", call);
}

const char *PrimitiveName(Primitive p)
{
    static const char *const names[] = {"PointList",    "LineList",      "LineStrip",
                                        "TriangleList", "TriangleStrip", "TriangleFan"};
    return names[int(p)];
}

void RecordDraw(const char *call, const DrawState &state, Primitive primitive, uint32_t count, uint32_t first,
                int32_t base)
{
    if (!S().recording)
        return;
    std::string textures;
    for (int i = 0; i < MAX_TEXTURES; ++i)
    {
        const TextureBinding &t = state.textures[i];
        if (t.texture)
            textures += " s" + std::to_string(i) + "=T" + std::to_string(N(t.texture)->id) + "/S" +
                        std::to_string(N(t.sampler)->id);
    }
    std::string streams;
    for (int i = 0; i < MAX_STREAMS; ++i)
    {
        const VertexBinding &v = state.vertices[i];
        if (v.buffer)
            streams += " v" + std::to_string(i) + "=B" + std::to_string(N(v.buffer)->id) + "+" +
                       std::to_string(v.offset) + "/" + std::to_string(v.stride);
    }
    Record("%s P%d %s count=%u first=%u base=%d%s%s%s viewport=%g,%g,%gx%g,%g-%g ref=%u", call, N(state.pipeline)->id,
           PrimitiveName(primitive), count, first, base, state.indices ? (" i=B" + std::to_string(N(state.indices)->id)).c_str()
                                                                       : "",
           streams.c_str(), textures.c_str(), state.viewport.x, state.viewport.y, state.viewport.width,
           state.viewport.height, state.viewport.minDepth, state.viewport.maxDepth, state.stencilRef);
}
} // namespace

void SetRecording(bool enabled)
{
    S().recording = enabled;
}

std::vector<std::string> TakeLog()
{
    std::vector<std::string> log;
    log.swap(S().log);
    return log;
}

bool Init()
{
    State &s = S();
    Check(!s.initialized, "Init twice");
    s.initialized = true;
    s.caps.device = "no GPU";
    s.caps.maxTextureSize = 16384;
    s.caps.maxSamples = 8;
    s.caps.memoryBytes = PhysicalMemory();
    for (int f = 1; f < int(Format::Count); ++f)
    {
        const Format format = Format(f);
        uint8_t flags = FORMAT_SAMPLED | FORMAT_FILTERABLE;
        flags |= IsDepthFormat(format) ? FORMAT_DEPTH_TARGET : IsBlockFormat(format) ? 0 : FORMAT_COLOR_TARGET;
        s.caps.formatFlags[f] = flags;
    }
    Record("Init");
    return true;
}

void Shutdown()
{
    State &s = S();
    Check(s.initialized, "Shutdown before Init");
    Check(!s.inPass, "Shutdown inside a pass");
    s.initialized = false;
    s.samplers.clear();
    s.pipelines.clear();
    Record("Shutdown");
}

const Caps &GetCaps()
{
    return S().caps;
}

const char *Name()
{
    return "null";
}

gb::Buffer *CreateBuffer(const BufferDesc &desc)
{
    if (!desc.size || (desc.usage != Usage::Static && desc.usage != Usage::Dynamic))
        Violation("CreateBuffer: size %u usage %s", desc.size, UsageName(desc.usage));
    Buffer *buffer = new Buffer(S().nextId++, desc);
    static const char *const types[] = {"Vertex", "Index16", "Index32"};
    Record("CreateBuffer B%d %s %s %s %u", buffer->id, types[int(desc.type)], UsageName(desc.usage),
           desc.memory == Memory::Device ? "Device" : "Shared", desc.size);
    return buffer;
}

gb::Texture *CreateTexture(const TextureDesc &desc)
{
    const char *problem = nullptr;
    const bool depth = IsDepthFormat(desc.format);
    if (desc.format == Format::Unknown || desc.format >= Format::Count)
        problem = "no format";
    else if (!desc.width || !desc.height || !desc.depth || !desc.levels || !desc.samples)
        problem = "a zero extent, level or sample count";
    else if (desc.width > S().caps.maxTextureSize || desc.height > S().caps.maxTextureSize)
        problem = "larger than maxTextureSize";
    else if ((desc.usage == Usage::Static || desc.usage == Usage::Dynamic) && (!desc.sampled || depth))
        problem = "a Static/Dynamic texture that is not a sampled colour texture";
    else if (desc.usage == Usage::RenderTarget && depth)
        problem = "a depth format as a colour attachment";
    else if (desc.usage == Usage::DepthStencil && !depth)
        problem = "a colour format as a depth attachment";
    else if (desc.samples > 1 && (desc.sampled || desc.levels != 1 || desc.type != TextureType::Tex2D))
        problem = "a multisampled texture that is sampled, mipmapped or not 2D";
    else if (desc.type != TextureType::Tex3D && desc.depth != 1)
        problem = "a depth on a texture that is not 3D";
    else if (desc.type == TextureType::Cube && desc.width != desc.height)
        problem = "a cube that is not square";
    else
    {
        uint32_t largest = desc.width > desc.height ? desc.width : desc.height;
        if (desc.type == TextureType::Tex3D && desc.depth > largest)
            largest = desc.depth;
        uint32_t levels = 1;
        while (largest > 1)
        {
            largest >>= 1;
            ++levels;
        }
        if (desc.levels > levels)
            problem = "more levels than the extent has";
    }
    if (problem)
        Violation("CreateTexture %s %ux%ux%u: %s", FormatName(desc.format), desc.width, desc.height, desc.depth,
                  problem);
    Texture *texture = new Texture(S().nextId++, desc);
    static const char *const types[] = {"2D", "Cube", "3D"};
    static const char sources[] = "RGBA01";
    const Swizzle &z = desc.swizzle;
    Record("CreateTexture T%d %s %s %s%s %ux%ux%u levels=%u samples=%u swizzle=%c%c%c%c", texture->id,
           types[int(desc.type)], FormatName(desc.format), UsageName(desc.usage), desc.sampled ? " sampled" : "",
           desc.width, desc.height, desc.depth, desc.levels, desc.samples, sources[int(z.r)], sources[int(z.g)],
           sources[int(z.b)], sources[int(z.a)]);
    return texture;
}

void DestroyBuffer(gb::Buffer *handle)
{
    Buffer *buffer = N(handle);
    if (!buffer)
        return;
    Record("DestroyBuffer B%d", buffer->id);
    delete buffer;
}

void DestroyTexture(gb::Texture *handle)
{
    Texture *texture = N(handle);
    if (!texture)
        return;
    if (S().inPass && IsAttachment(texture))
        Violation("DestroyTexture: T%d is an attachment of the current pass", texture->id);
    Record("DestroyTexture T%d", texture->id);
    delete texture;
}

void UploadBuffer(gb::Buffer *handle, uint32_t offset, const void *data, uint32_t size, Upload mode)
{
    const Buffer *buffer = N(handle);
    if (!buffer || !data || !size || uint64_t(offset) + size > buffer->desc.size)
        Violation("UploadBuffer: B%d of %u bytes, %u bytes at %u", buffer ? buffer->id : 0,
                  buffer ? buffer->desc.size : 0, size, offset);
    static const char *const modes[] = {"Ordered", "Discard", "NoOverwrite"};
    Record("UploadBuffer B%d %u+%u %s fnv=%016llx", buffer->id, offset, size, modes[int(mode)],
           static_cast<unsigned long long>(FNV(data, size)));
}

void UploadTexture(gb::Texture *handle, uint8_t level, uint8_t layer, const Box &box, const void *data,
                   uint32_t rowPitch, uint32_t slicePitch)
{
    const Texture *texture = N(handle);
    CheckTextureLevel(texture, level, layer, "UploadTexture");
    CheckBox(texture, level, box, "UploadTexture");
    if (texture->desc.samples > 1 || !data)
        Violation("UploadTexture: T%d is multisampled or the data is null", texture->id);
    const bool block = IsBlockFormat(texture->desc.format);
    const uint32_t rows = block ? (box.height + 3) / 4 : box.height;
    const uint32_t rowBytes = (block ? (box.width + 3) / 4 : box.width) * FormatBytes(texture->desc.format);
    if (rowPitch < rowBytes || (box.depth > 1 && slicePitch < rowPitch * rows))
        Violation("UploadTexture: T%d pitches %u/%u under the box's %u/%u", texture->id, rowPitch, slicePitch,
                  rowBytes, rowPitch * rows);
    uint64_t hash = 1469598103934665603ull;
    for (uint32_t z = 0; z < box.depth; ++z)
        for (uint32_t y = 0; y < rows; ++y)
        {
            const uint8_t *row = static_cast<const uint8_t *>(data) + size_t(z) * slicePitch + size_t(y) * rowPitch;
            for (uint32_t i = 0; i < rowBytes; ++i)
            {
                hash ^= row[i];
                hash *= 1099511628211ull;
            }
        }
    Record("UploadTexture T%d level=%u layer=%u box=%u,%u,%u %ux%ux%u fnv=%016llx", texture->id, level, layer, box.x,
           box.y, box.z, box.width, box.height, box.depth, static_cast<unsigned long long>(hash));
}

const gb::Sampler *GetSampler(const SamplerDesc &desc)
{
    std::unique_ptr<Sampler> &slot = S().samplers[SamplerKey(desc)];
    if (!slot)
    {
        slot.reset(new Sampler(S().nextId++, desc));
        Record("GetSampler S%d new mag=%d min=%d mip=%d address=%d,%d,%d aniso=%u compare=%d", slot->id,
               int(desc.magFilter), int(desc.minFilter), int(desc.mipFilter), int(desc.addressU), int(desc.addressV),
               int(desc.addressW), desc.maxAnisotropy, int(desc.depthCompare));
    }
    return slot.get();
}

gb::Shader *CreateShader(Stage stage, uint64_t hash, const char *code, uint32_t size)
{
    if (!code || !size)
        Violation("CreateShader %016llx without code", static_cast<unsigned long long>(hash));
    Shader *shader = new Shader(S().nextId++, stage, hash);
    Record("CreateShader H%d %s %016llx", shader->id, stage == Stage::Vertex ? "vertex" : "pixel",
           static_cast<unsigned long long>(hash));
    return shader;
}

void DestroyShader(gb::Shader *handle)
{
    Shader *shader = N(handle);
    if (!shader)
        return;
    auto &pipelines = S().pipelines;
    for (auto it = pipelines.begin(); it != pipelines.end();)
    {
        if (it->second->desc.vertex == handle || it->second->desc.pixel == handle)
        {
            Record("DestroyPipeline P%d", it->second->id);
            it = pipelines.erase(it);
        }
        else
            ++it;
    }
    Record("DestroyShader H%d", shader->id);
    delete shader;
}

const gb::Pipeline *GetPipeline(const PipelineDesc &desc)
{
    const Shader *vertex = N(desc.vertex), *pixel = N(desc.pixel);
    if (!vertex || !pixel || vertex->stage != Stage::Vertex || pixel->stage != Stage::Pixel)
        Violation("GetPipeline: needs a vertex and a pixel shader");
    if (desc.attribCount > MAX_ATTRIBS || desc.colorCount > MAX_COLOR_TARGETS || !desc.samples)
        Violation("GetPipeline: %u attributes, %u colour targets, %u samples", desc.attribCount, desc.colorCount,
                  desc.samples);
    for (int i = 0; i < desc.attribCount; ++i)
        for (int j = 0; j < i; ++j)
            if (desc.attribs[i].location == desc.attribs[j].location)
                Violation("GetPipeline: location %u is fed twice", desc.attribs[i].location);
    for (int i = 0; i < desc.attribCount; ++i)
        if (desc.attribs[i].stream >= MAX_STREAMS)
            Violation("GetPipeline: stream %u", desc.attribs[i].stream);
    std::unique_ptr<Pipeline> &slot = S().pipelines[PipelineKey(desc)];
    if (!slot)
    {
        slot.reset(new Pipeline(S().nextId++, desc));
        Record("GetPipeline P%d new H%d H%d attribs=%u colour=%u depth=%s", slot->id, vertex->id, pixel->id,
               desc.attribCount, desc.colorCount, FormatName(desc.depthFormat));
    }
    return slot.get();
}

void BeginPass(const PassDesc &desc)
{
    State &s = S();
    if (s.inPass)
        Violation("BeginPass inside a pass");
    if (desc.colorCount > MAX_COLOR_TARGETS || (!desc.colorCount && !desc.depth))
        Violation("BeginPass: %u colour attachments%s", desc.colorCount, desc.depth ? "" : " and no depth");
    uint32_t width = 0, height = 0;
    uint8_t samples = 0;
    auto attach = [&](const gb::Texture *handle, uint8_t level, uint8_t layer, Usage usage, const char *what) {
        const Texture *t = N(handle);
        if (!t)
            Violation("BeginPass: %s attachment missing", what);
        CheckTextureLevel(t, level, layer, "BeginPass");
        if (t->desc.usage != usage)
            Violation("BeginPass: T%d is not a %s attachment", t->id, what);
        const uint32_t w = LevelExtent(t->desc.width, level), h = LevelExtent(t->desc.height, level);
        if (!samples)
        {
            width = w;
            height = h;
            samples = t->desc.samples;
        }
        else if (t->desc.samples != samples)
            Violation("BeginPass: T%d has %u samples, the pass %u", t->id, t->desc.samples, samples);
        width = w < width ? w : width;
        height = h < height ? h : height;
    };
    std::string text;
    for (int i = 0; i < desc.colorCount; ++i)
    {
        attach(desc.color[i].texture, desc.color[i].level, desc.color[i].layer, Usage::RenderTarget, "colour");
        text += " c" + std::to_string(i) + "=T" + std::to_string(N(desc.color[i].texture)->id) + "/" +
                std::to_string(desc.color[i].level) + "/" + std::to_string(desc.color[i].layer);
        if (desc.color[i].load == LoadOp::Clear)
        {
            char clear[96];
            std::snprintf(clear, sizeof(clear), ":clear(%g,%g,%g,%g)", desc.color[i].clear[0], desc.color[i].clear[1],
                          desc.color[i].clear[2], desc.color[i].clear[3]);
            text += clear;
        }
        else if (desc.color[i].load == LoadOp::DontCare)
            text += ":dontcare";
    }
    if (desc.depth)
    {
        attach(desc.depth, 0, 0, Usage::DepthStencil, "depth");
        text += " d=T" + std::to_string(N(desc.depth)->id);
        static const char *const loads[] = {"load", "clear", "dontcare"};
        char loadText[96];
        std::snprintf(loadText, sizeof(loadText), ":%s(%g)/%s(%u)", loads[int(desc.depthLoad)], desc.clearDepth,
                      loads[int(desc.stencilLoad)], desc.clearStencil);
        text += loadText;
    }
    s.inPass = true;
    s.pass = desc;
    s.passWidth = width;
    s.passHeight = height;
    s.passSamples = samples;
    Record("BeginPass%s %ux%u", text.c_str(), width, height);
}

void EndPass()
{
    State &s = S();
    if (!s.inPass)
        Violation("EndPass outside a pass");
    if (s.openOcclusion)
        Violation("EndPass with occlusion query Q%d open", s.openOcclusion->id);
    s.inPass = false;
    s.pass = PassDesc();
    Record("EndPass");
}

void Clear(const Rect *rects, uint32_t count, bool color, bool depth, bool stencil, const float rgba[4],
           float depthValue, uint8_t stencilValue)
{
    State &s = S();
    if (!s.inPass)
        Violation("Clear outside a pass");
    if (!count || !rects)
        Violation("Clear without rects");
    if ((depth || stencil) && !s.pass.depth)
        Violation("Clear of depth/stencil in a pass without one");
    if (color && !s.pass.colorCount)
        Violation("Clear of colour in a pass without one");
    std::string text;
    for (uint32_t i = 0; i < count; ++i)
    {
        const Rect &r = rects[i];
        if (r.x < 0 || r.y < 0 || !r.width || !r.height || uint32_t(r.x) + r.width > s.passWidth ||
            uint32_t(r.y) + r.height > s.passHeight)
            Violation("Clear rect %d,%d %ux%u outside the %ux%u pass", r.x, r.y, r.width, r.height, s.passWidth,
                      s.passHeight);
        text += " " + std::to_string(r.x) + "," + std::to_string(r.y) + "," + std::to_string(r.width) + "x" +
                std::to_string(r.height);
    }
    Record("Clear%s%s%s%s (%g,%g,%g,%g) %g %u", text.c_str(), color ? " colour" : "", depth ? " depth" : "",
           stencil ? " stencil" : "", rgba[0], rgba[1], rgba[2], rgba[3], depthValue, stencilValue);
}

UniformSlice AllocUniforms(const void *data, uint32_t size)
{
    static Buffer uniforms{0, {BufferType::Vertex, Usage::Dynamic, Memory::Shared, 0}};
    if (!data || !size)
        Violation("AllocUniforms of nothing");
    const uint32_t offset = uint32_t(S().uniformOffset);
    S().uniformOffset += (size + 255u) & ~255u;
    Record("AllocUniforms %u fnv=%016llx", size, static_cast<unsigned long long>(FNV(data, size)));
    return UniformSlice{&uniforms, offset, size};
}

void Draw(const DrawState &state, Primitive primitive, uint32_t vertexCount, uint32_t firstVertex)
{
    CheckDraw(state, "Draw");
    if (!vertexCount)
        Violation("Draw of no vertex");
    RecordDraw("Draw", state, primitive, vertexCount, firstVertex, 0);
}

void DrawIndexed(const DrawState &state, Primitive primitive, uint32_t indexCount, uint32_t firstIndex,
                 int32_t baseVertex)
{
    CheckDraw(state, "DrawIndexed");
    const Buffer *indices = N(state.indices);
    if (!indices || indices->desc.type == BufferType::Vertex)
        Violation("DrawIndexed without an index buffer");
    const uint32_t indexBytes = indices->desc.type == BufferType::Index16 ? 2 : 4;
    if (!indexCount || (uint64_t(firstIndex) + indexCount) * indexBytes > indices->desc.size)
        Violation("DrawIndexed: %u indices from %u in B%d of %u bytes", indexCount, firstIndex, indices->id,
                  indices->desc.size);
    RecordDraw("DrawIndexed", state, primitive, indexCount, firstIndex, baseVertex);
}

void Blit(gb::Texture *srcHandle, uint8_t srcLevel, uint8_t srcLayer, const Rect &srcRect, gb::Texture *dstHandle,
          uint8_t dstLevel, uint8_t dstLayer, const Rect &dstRect, Filter filter)
{
    const Texture *src = N(srcHandle), *dst = N(dstHandle);
    if (S().inPass)
        Violation("Blit inside a pass");
    CheckTextureLevel(src, srcLevel, srcLayer, "Blit source");
    CheckTextureLevel(dst, dstLevel, dstLayer, "Blit destination");
    CheckRect(src, srcLevel, srcRect, "Blit source");
    CheckRect(dst, dstLevel, dstRect, "Blit destination");
    if (dst->desc.samples > 1)
        Violation("Blit into the multisampled T%d", dst->id);
    if (IsDepthFormat(src->desc.format) || IsDepthFormat(dst->desc.format))
    {
        if (src->desc.format != dst->desc.format || srcRect.width != dstRect.width ||
            srcRect.height != dstRect.height || filter != Filter::Nearest)
            Violation("Blit of depth T%d to T%d: formats, sizes or filter differ", src->id, dst->id);
    }
    else if (IsBlockFormat(dst->desc.format))
        Violation("Blit into the compressed T%d", dst->id);
    Record("Blit T%d/%u/%u %d,%d,%ux%u -> T%d/%u/%u %d,%d,%ux%u %s", src->id, srcLevel, srcLayer, srcRect.x, srcRect.y,
           srcRect.width, srcRect.height, dst->id, dstLevel, dstLayer, dstRect.x, dstRect.y, dstRect.width,
           dstRect.height, filter == Filter::Linear ? "linear" : "nearest");
}

void ReadTexture(gb::Texture *handle, uint8_t level, uint8_t layer, const Box &box, void *data, uint32_t rowPitch)
{
    const Texture *texture = N(handle);
    if (S().inPass)
        Violation("ReadTexture inside a pass");
    CheckTextureLevel(texture, level, layer, "ReadTexture");
    CheckBox(texture, level, box, "ReadTexture");
    if (texture->desc.samples > 1 || IsBlockFormat(texture->desc.format) || box.depth != 1 || !data)
        Violation("ReadTexture: T%d is multisampled or compressed, or the box is not one slice", texture->id);
    const uint32_t rowBytes = box.width * FormatBytes(texture->desc.format);
    if (rowPitch < rowBytes)
        Violation("ReadTexture: pitch %u under the row's %u bytes", rowPitch, rowBytes);
    for (uint32_t y = 0; y < box.height; ++y)
        std::memset(static_cast<uint8_t *>(data) + size_t(y) * rowPitch, 0, rowBytes);
    Record("ReadTexture T%d level=%u layer=%u box=%u,%u %ux%u", texture->id, level, layer, box.x, box.y, box.width,
           box.height);
}

gb::Query *CreateQuery(QueryType type)
{
    Query *query = new Query(S().nextId++, type);
    Record("CreateQuery Q%d %s", query->id, type == QueryType::Occlusion ? "occlusion" : "timestamp");
    return query;
}

void DestroyQuery(gb::Query *handle)
{
    Query *query = N(handle);
    if (!query)
        return;
    if (S().openOcclusion == query)
        Violation("DestroyQuery: Q%d is open", query->id);
    Record("DestroyQuery Q%d", query->id);
    delete query;
}

void BeginQuery(gb::Query *handle)
{
    Query *query = N(handle);
    State &s = S();
    if (!query || query->type != QueryType::Occlusion || !s.inPass || s.openOcclusion)
        Violation("BeginQuery: needs an occlusion query, inside a pass, with none open");
    query->begun = true;
    query->ended = false;
    s.openOcclusion = query;
    Record("BeginQuery Q%d", query->id);
}

void EndQuery(gb::Query *handle)
{
    Query *query = N(handle);
    State &s = S();
    if (!query)
        Violation("EndQuery of no query");
    if (query->type == QueryType::Occlusion)
    {
        if (s.openOcclusion != query || !s.inPass)
            Violation("EndQuery: Q%d is not the open occlusion query of this pass", query->id);
        s.openOcclusion = nullptr;
        query->value = 0;
    }
    else
        query->value = uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    std::chrono::steady_clock::now().time_since_epoch())
                                    .count());
    query->begun = false;
    query->ended = true;
    Record("EndQuery Q%d", query->id);
}

bool QueryResult(gb::Query *handle, uint64_t *value)
{
    const Query *query = N(handle);
    if (!query || !value)
        Violation("QueryResult without a query or a value");
    if (!query->ended)
        return false;
    *value = query->value;
    return true;
}

gb::Fence *InsertFence()
{
    Fence *fence = new Fence(S().nextId++);
    Record("InsertFence F%d", fence->id);
    return fence;
}

bool FenceReached(gb::Fence *fence)
{
    if (!fence)
        Violation("FenceReached of no fence");
    return true;
}

void DestroyFence(gb::Fence *fence)
{
    delete N(fence);
}

gb::Swapchain *CreateSwapchain(const SwapchainDesc &desc)
{
    if (!desc.width || !desc.height)
        Violation("CreateSwapchain %ux%u", desc.width, desc.height);
    Swapchain *swapchain = new Swapchain(S().nextId++, desc);
    Record("CreateSwapchain W%d %ux%u vsync=%d", swapchain->id, desc.width, desc.height, int(desc.vsync));
    return swapchain;
}

void ResizeSwapchain(gb::Swapchain *handle, uint16_t width, uint16_t height)
{
    Swapchain *swapchain = N(handle);
    if (!swapchain || !width || !height)
        Violation("ResizeSwapchain to %ux%u", width, height);
    swapchain->desc.width = width;
    swapchain->desc.height = height;
    Record("ResizeSwapchain W%d %ux%u", swapchain->id, width, height);
}

void DestroySwapchain(gb::Swapchain *handle)
{
    Swapchain *swapchain = N(handle);
    if (!swapchain)
        return;
    Record("DestroySwapchain W%d", swapchain->id);
    delete swapchain;
}

void SetGamma(gb::Swapchain *handle, const uint16_t red[256], const uint16_t green[256], const uint16_t blue[256])
{
    const Swapchain *swapchain = N(handle);
    if (!swapchain || !red || !green || !blue)
        Violation("SetGamma without a swapchain or a ramp");
    Record("SetGamma W%d %u,%u,%u", swapchain->id, red[255], green[255], blue[255]);
}

void Present(gb::Swapchain *swapchainHandle, gb::Texture *imageHandle)
{
    const Swapchain *swapchain = N(swapchainHandle);
    const Texture *image = N(imageHandle);
    State &s = S();
    if (s.inPass)
        Violation("Present inside a pass");
    if (!swapchain || !image || image->desc.usage != Usage::RenderTarget || image->desc.samples != 1 ||
        image->desc.width != swapchain->desc.width || image->desc.height != swapchain->desc.height)
        Violation("Present: the image must be a single-sampled colour attachment of the swapchain's size");
    s.uniformOffset = 0;
    ++s.frame;
    Record("Present W%d T%d", swapchain->id, image->id);
}

void WaitIdle()
{
    if (S().inPass)
        Violation("WaitIdle inside a pass");
    Record("WaitIdle");
}

const Backend &Table()
{
#define GB_NULL_ENTRY(ret, name, params, args) &null::name,
    static const Backend table = {&null::Init, &null::Name, GB_BACKEND_CALLS(GB_NULL_ENTRY)};
#undef GB_NULL_ENTRY
    return table;
}
} // namespace gb::null
