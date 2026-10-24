// The GL backend's thread: it makes the platform's context current (gb_gl_host.h) and executes the command chunks
// the frontend side records (gb_gl.cpp), in order, serving the build lane between commands.
//
// D3D9's conventions (gfx_backend.h) on GL 4.3 core:
// - glClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE): clip-space depth from 0 to 1, and window row 0 is the first row of
//   an attachment, so rows count from the top as in D3D9. Uploads, viewports, scissors, blits and readbacks use D3D9's
//   rows unchanged; only Present, drawing into the window's bottom-up framebuffer, turns the image over.
// - Depth bias: D3DRS_DEPTHBIAS is a depth offset; glPolygonOffset's units are steps of the depth format.

#include "gb_gl.h"

#include "../gb_common.h"
#include "gb_gl_functions.h"
#include "gb_gl_host.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#if defined(__unix__)
#include <unistd.h>
#endif

namespace gb::gl
{
#define GB_GL_DEFINE(type, name) type name = nullptr;
GB_GL_FUNCTIONS(GB_GL_DEFINE)
#undef GB_GL_DEFINE
PFNGLBUFFERSTORAGEPROC glBufferStorage = nullptr;

bool LoadFunctions(const char **missing)
{
#define GB_GL_LOAD(type, name)                                                                                        \
    name = reinterpret_cast<type>(host::ProcAddress(#name));                                                          \
    if (!name)                                                                                                        \
    {                                                                                                                 \
        *missing = #name;                                                                                             \
        return false;                                                                                                 \
    }
    GB_GL_FUNCTIONS(GB_GL_LOAD)
#undef GB_GL_LOAD
    return true;
}

struct Vao
{
    GLuint name = 0;
    uint8_t streams = 0;     // the streams the layout reads
    uint64_t generation = 0; // the buffer generation the bindings below belong to
    const Buffer *buffers[MAX_STREAMS] = {};
    uint32_t offsets[MAX_STREAMS] = {}, strides[MAX_STREAMS] = {};
    const Buffer *indices = nullptr;
};

namespace
{
// EXT_texture_compression_s3tc, which glcorearb.h leaves out.
constexpr GLenum kDxt1 = 0x83F1, kDxt3 = 0x83F2, kDxt5 = 0x83F3;
// The unit uploads bind textures to: no draw samples it.
constexpr GLuint kUploadUnit = MAX_TEXTURES;
// D3D9's front faces are the triangles clockwise on screen. The upper-left origin turns the rows over but GL still
// judges facing as with its own origin, so they are GL_CW ones (tests/gfx_backend checks it).
constexpr GLenum kFrontFace = GL_CW;
constexpr int kMaxDebugMessages = 64;

#define GB_GL_CAST(T)                                                                                                 \
    [[maybe_unused]] T *G(gb::T *p)                                                                                   \
    {                                                                                                                 \
        return static_cast<T *>(p);                                                                                   \
    }                                                                                                                 \
    [[maybe_unused]] const T *G(const gb::T *p)                                                                       \
    {                                                                                                                 \
        return static_cast<const T *>(p);                                                                             \
    }
GB_GL_CAST(Buffer)
GB_GL_CAST(Texture)
GB_GL_CAST(Sampler)
GB_GL_CAST(Pipeline)
#undef GB_GL_CAST

struct FormatInfo
{
    GLenum internal;
    GLenum format, type; // for uploads and readbacks; 0 for the compressed formats
};

const FormatInfo &Info(Format f)
{
    static const FormatInfo table[] = {
        {0, 0, 0},                                            // Unknown
        {GL_RGBA8, GL_BGRA, GL_UNSIGNED_BYTE},                // BGRA8
        {GL_RGB8, GL_BGRA, GL_UNSIGNED_BYTE},                 // BGRX8: alpha reads 1, as D3D9's X8 formats do
        {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE},                // RGBA8
        {GL_RGB565, GL_RGB, GL_UNSIGNED_SHORT_5_6_5},         // B5G6R5
        {GL_RGB5_A1, GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV}, // B5G5R5A1
        {GL_R8, GL_RED, GL_UNSIGNED_BYTE},                    // R8
        {GL_RG8, GL_RG, GL_UNSIGNED_BYTE},                    // RG8
        {GL_RG16, GL_RG, GL_UNSIGNED_SHORT},                  // RG16
        {GL_RGBA16, GL_RGBA, GL_UNSIGNED_SHORT},              // RGBA16
        {GL_R16F, GL_RED, GL_HALF_FLOAT},                     // R16F
        {GL_RG16F, GL_RG, GL_HALF_FLOAT},                     // RG16F
        {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT},                 // RGBA16F
        {GL_R32F, GL_RED, GL_FLOAT},                          // R32F
        {kDxt1, 0, 0},                                        // BC1
        {kDxt3, 0, 0},                                        // BC2
        {kDxt5, 0, 0},                                        // BC3
        {GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT}, // D16
        {GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8}, // D24S8
    };
    static_assert(sizeof(table) / sizeof(table[0]) == size_t(Format::Count), "format table");
    return table[int(f)];
}

GLenum TextureTarget(const TextureDesc &d)
{
    switch (d.type)
    {
    case TextureType::Cube:
        return GL_TEXTURE_CUBE_MAP;
    case TextureType::Tex3D:
        return GL_TEXTURE_3D;
    case TextureType::Tex2D:
        break;
    }
    return d.samples > 1 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
}

GLenum DepthAttachment(Format f)
{
    return f == Format::D24S8 ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
}

GLenum ToCompare(Compare c)
{
    static const GLenum table[] = {GL_NEVER,   GL_LESS,     GL_EQUAL,  GL_LEQUAL,
                                   GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS};
    return table[int(c)];
}

GLenum ToStencilOp(StencilOp op)
{
    static const GLenum table[] = {GL_KEEP, GL_ZERO,   GL_REPLACE,   GL_INCR,
                                   GL_DECR, GL_INVERT, GL_INCR_WRAP, GL_DECR_WRAP};
    return table[int(op)];
}

GLenum ToBlend(Blend b)
{
    static const GLenum table[] = {GL_ZERO,
                                   GL_ONE,
                                   GL_SRC_COLOR,
                                   GL_ONE_MINUS_SRC_COLOR,
                                   GL_SRC_ALPHA,
                                   GL_ONE_MINUS_SRC_ALPHA,
                                   GL_DST_ALPHA,
                                   GL_ONE_MINUS_DST_ALPHA,
                                   GL_DST_COLOR,
                                   GL_ONE_MINUS_DST_COLOR,
                                   GL_SRC_ALPHA_SATURATE,
                                   GL_CONSTANT_COLOR,
                                   GL_ONE_MINUS_CONSTANT_COLOR};
    return table[int(b)];
}

GLenum ToBlendOp(BlendOp op)
{
    static const GLenum table[] = {GL_FUNC_ADD, GL_FUNC_SUBTRACT, GL_FUNC_REVERSE_SUBTRACT, GL_MIN, GL_MAX};
    return table[int(op)];
}

GLenum ToMode(Primitive p)
{
    static const GLenum table[] = {GL_POINTS,    GL_LINES,          GL_LINE_STRIP,
                                   GL_TRIANGLES, GL_TRIANGLE_STRIP, GL_TRIANGLE_FAN};
    return table[int(p)];
}

GLenum ToSource(Source s)
{
    static const GLenum table[] = {GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA, GL_ZERO, GL_ONE};
    return table[int(s)];
}

void AttribFormat(const VertexAttrib &a)
{
    GLint size = 4;
    GLenum type = GL_FLOAT;
    GLboolean normalized = GL_FALSE;
    switch (a.type)
    {
    case AttribType::Float1:
        size = 1;
        break;
    case AttribType::Float2:
        size = 2;
        break;
    case AttribType::Float3:
        size = 3;
        break;
    case AttribType::Float4:
        break;
    case AttribType::UByte4:
        type = GL_UNSIGNED_BYTE;
        break;
    case AttribType::UByte4N:
        type = GL_UNSIGNED_BYTE;
        normalized = GL_TRUE;
        break;
    case AttribType::UByte4NBGRA:
        // ARB_vertex_array_bgra: bytes B, G, R, A read as (R, G, B, A), as D3DCOLOR.
        size = GL_BGRA;
        type = GL_UNSIGNED_BYTE;
        normalized = GL_TRUE;
        break;
    case AttribType::Short2:
        size = 2;
        type = GL_SHORT;
        break;
    case AttribType::Short4:
        type = GL_SHORT;
        break;
    case AttribType::Short2N:
        size = 2;
        type = GL_SHORT;
        normalized = GL_TRUE;
        break;
    case AttribType::Short4N:
        type = GL_SHORT;
        normalized = GL_TRUE;
        break;
    case AttribType::UShort2N:
        size = 2;
        type = GL_UNSIGNED_SHORT;
        normalized = GL_TRUE;
        break;
    case AttribType::UShort4N:
        type = GL_UNSIGNED_SHORT;
        normalized = GL_TRUE;
        break;
    case AttribType::Half2:
        size = 2;
        type = GL_HALF_FLOAT;
        break;
    case AttribType::Half4:
        type = GL_HALF_FLOAT;
        break;
    }
    glVertexAttribFormat(a.location, size, type, normalized, a.offset);
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

const char kPresentVertex[] = R"(#version 430 core
void main()
{
    vec2 corner = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
)";

// The window's framebuffer counts rows from the bottom, the image from the top; the ramp is D3D9's gamma ramp.
const char kPresentPixel[] = R"(#version 430 core
layout(binding = 0) uniform sampler2D image;
layout(binding = 1) uniform sampler2D ramp;
layout(location = 0) uniform vec4 frame; // 1 / width, 1 / height, height
layout(location = 0) out vec4 color;
void main()
{
    vec2 uv = vec2(gl_FragCoord.x, frame.z - gl_FragCoord.y) * frame.xy;
    ivec3 i = ivec3(clamp(texture(image, uv).rgb, 0.0, 1.0) * 255.0 + 0.5);
    color = vec4(texelFetch(ramp, ivec2(i.r, 0), 0).r, texelFetch(ramp, ivec2(i.g, 0), 0).g,
                 texelFetch(ramp, ivec2(i.b, 0), 0).b, 1.0);
}
)";

struct UniformBlock
{
    GLuint name = 0;
    uint8_t *mapped = nullptr; // persistently mapped, when the context has buffer storage
};

struct FrameBlocks
{
    GLsync sync;
    std::vector<UniformBlock *> blocks;
};

struct Attachment
{
    const Texture *texture;
    uint8_t level, layer;
    bool operator==(const Attachment &) const = default;
};

struct Fbo
{
    GLuint name;
    uint8_t colorCount;
    Attachment color[MAX_COLOR_TARGETS];
    const Texture *depth;
};

// What the context has bound or set, so that a draw makes only the calls that change something. Sentinels that no
// real value has make the first use set the state.
struct Bound
{
    GLuint program = ~0u, vao = ~0u, drawFbo = ~0u, readFbo = ~0u;
    GLuint activeUnit = ~0u;
    GLuint unitTexture[MAX_TEXTURES + 1] = {};
    GLuint unitSampler[MAX_TEXTURES + 1] = {};
    GLuint uboBuffer[2] = {~0u, ~0u};
    GLintptr uboOffset[2] = {-1, -1};
    GLsizeiptr uboSize[2] = {-1, -1};
    int blend = -1;
    GLenum blendFunc[4] = {}, blendOp[2] = {};
    uint8_t colorMask = 0xFF;
    float blendColor[4] = {-1.0f, -1.0f, -1.0f, -1.0f};
    int depthTest = -1, depthMask = -1;
    GLenum depthFunc = 0;
    int stencilTest = -1;
    GLenum stencilFunc[2] = {};
    GLint stencilRef[2] = {-1, -1};
    GLuint stencilReadMask[2] = {~0u, ~0u};
    GLenum stencilOps[2][3] = {};
    GLuint stencilWriteMask = ~0u;
    int cull = -1;
    GLenum cullFace = 0, polygonMode = 0;
    int scissorTest = -1;
    GLint scissor[4] = {-1, -1, -1, -1};
    int offset = -1;
    float offsetFactor = -1.0f, offsetUnits = -1.0f;
    float viewport[4] = {-1.0f, -1.0f, -1.0f, -1.0f};
    float depthRange[2] = {-1.0f, -1.0f};
};

class Device
{
public:
    explicit Device(Queue &queue) : q(queue) {}
    bool Start(DeviceInfo &info);
    void Run();
    void Stop();

private:
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    void Log(const char *fmt, ...);
    static void APIENTRY OnDebug(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
                                 const GLchar *message, const void *user);
    bool HasExtension(const char *name) const;
    bool Version(int major, int minor) const;
    void CheckFormats(DeviceInfo &info);
    GLuint BuildProgram(const char *vertex, const char *pixel);

    // state
    void UseProgram(GLuint program);
    void BindVao(GLuint vao);
    void BindDrawFbo(GLuint fbo);
    void BindReadFbo(GLuint fbo);
    void ActiveUnit(GLuint unit);
    void BindTexture(GLuint unit, GLenum target, GLuint texture);
    void EditTexture(GLenum target, GLuint texture);
    void BindSampler(GLuint unit, GLuint sampler);
    void BindUbo(GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size);
    void SetBlend(const PipelineGl &p);
    void SetBlendOff();
    void SetColorMask(uint8_t mask);
    void SetBlendColor(const float rgba[4]);
    void SetDepth(bool test, bool write, GLenum func);
    void SetDepthMask(bool write);
    void SetStencil(const PipelineGl &p, uint8_t ref);
    void SetStencilOff();
    void SetStencilWriteMask(GLuint mask);
    void SetCull(bool cull, GLenum face);
    void SetPolygonMode(GLenum mode);
    void SetScissorTest(bool enable);
    void SetScissor(const Rect &r);
    void SetOffset(bool enable, float factor, float units);
    void SetViewport(float x, float y, float width, float height);
    void SetDepthRange(float minDepth, float maxDepth);

    // objects
    void Attach(GLenum target, GLenum attachment, const Texture *texture, uint8_t level, uint8_t layer);
    void Detach(GLenum target, GLenum attachment);
    Fbo &FboFor(const PassDesc &desc);
    void ForgetTexture(const Texture *texture);
    Vao *VaoFor(const PipelineDesc &desc);
    PipelineGl &Translate(const Pipeline *pipeline);
    UniformBlock *Block(uint32_t index);
    UniformBlock *TakeBlock();
    void EndFrame();
    GLuint ResolveScratch(Format format, uint32_t width, uint32_t height);

    // the lanes
    void ServeBuilds();
    void Compile(Shader *shader, const std::string &source);
    void Link(Program *program);
    void ReportShader(const Shader *shader);
    void Execute(const Chunk &chunk);
    bool Polling() const;
    void Poll();

    void Do(const cmd::CreateBuffer &c, const Chunk &);
    void Do(const cmd::DestroyBuffer &c, const Chunk &);
    void Do(const cmd::UploadBuffer &c, const Chunk &chunk);
    void Do(const cmd::CreateTexture &c, const Chunk &);
    void Do(const cmd::DestroyTexture &c, const Chunk &);
    void Do(const cmd::UploadTexture &c, const Chunk &chunk);
    void Do(const cmd::CreateSampler &c, const Chunk &);
    void Do(const cmd::DestroyShader &c, const Chunk &);
    void Do(const cmd::DestroyProgram &c, const Chunk &);
    void Do(const cmd::DestroyPipeline &c, const Chunk &);
    void Do(const cmd::BeginPass &c, const Chunk &);
    void Do(const cmd::EndPass &c, const Chunk &);
    void Do(const cmd::Clear &c, const Chunk &chunk);
    void Do(const cmd::Uniforms &c, const Chunk &chunk);
    void Do(const cmd::Draw &c, const Chunk &);
    void Do(const cmd::Blit &c, const Chunk &);
    void Do(const cmd::ReadTexture &c, const Chunk &);
    void Do(const cmd::CreateQuery &c, const Chunk &);
    void Do(const cmd::DestroyQuery &c, const Chunk &);
    void Do(const cmd::BeginQuery &c, const Chunk &);
    void Do(const cmd::EndQuery &c, const Chunk &);
    void Do(const cmd::InsertFence &c, const Chunk &);
    void Do(const cmd::DestroyFence &c, const Chunk &);
    void Do(const cmd::CreateSwapchain &c, const Chunk &);
    void Do(const cmd::DestroySwapchain &c, const Chunk &);
    void Do(const cmd::SetGamma &c, const Chunk &chunk);
    void Do(const cmd::Present &c, const Chunk &);
    void Do(const cmd::Finish &c, const Chunk &);

    void ClearAttachments(bool color, bool depth, bool stencil, const float rgba[4], float depthValue,
                          uint8_t stencilValue);

    Queue &q;
    int m_major = 0, m_minor = 0;
    std::unordered_set<std::string> m_extensions;
    bool m_bufferStorage = false, m_mirrorOnce = false, m_anisotropy = false;
    float m_maxAnisotropy = 1.0f;
    uint32_t m_uniformAlign = 256;
    int m_debugMessages = 0;
    Bound m_bound;
    std::vector<Build> m_builds;
    bool m_flushed = true;

    void *m_window = nullptr; // the window the context draws into, or null
    bool m_reportedWindow = false;
    GLuint m_present = 0, m_emptyVao = 0, m_readFbo = 0, m_drawFbo = 0;
    GLuint m_linearClamp = 0, m_nearestClamp = 0;

    // The current pass, for Clear.
    uint8_t m_passColors = 0;
    const Texture *m_passDepth = nullptr;

    std::vector<Fbo> m_fbos;
    std::unordered_map<std::string, std::unique_ptr<Vao>> m_vaos;
    uint64_t m_bufferGeneration = 1;

    std::vector<UniformBlock *> m_frameBlocks;
    std::vector<UniformBlock *> m_freeBlocks;
    std::deque<FrameBlocks> m_inFlight;
    std::vector<std::unique_ptr<UniformBlock>> m_blocks;

    struct Scratch
    {
        GLuint name;
        Format format;
        uint32_t width, height;
    };
    std::vector<Scratch> m_resolves;

    std::vector<std::pair<Query *, uint64_t>> m_queries; // ended, result not read yet
    std::vector<Fence *> m_fences;                       // inserted, not reached yet
    std::vector<uint8_t> m_readScratch;
};

void Device::Log(const char *fmt, ...)
{
    char line[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    std::lock_guard<std::mutex> lock(q.lock);
    q.log.emplace_back(line);
}

void APIENTRY Device::OnDebug(GLenum, GLenum type, GLuint id, GLenum severity, GLsizei, const GLchar *message,
                               const void *user)
{
    Device *device = static_cast<Device *>(const_cast<void *>(user));
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION || device->m_debugMessages >= kMaxDebugMessages)
        return;
    if (++device->m_debugMessages == kMaxDebugMessages)
        device->Log("gl debug: no more messages");
    else
        device->Log("gl debug: type 0x%x id %u: %s", type, id, message);
}

bool Device::HasExtension(const char *name) const
{
    return m_extensions.count(name) != 0;
}

bool Device::Version(int major, int minor) const
{
    return m_major > major || (m_major == major && m_minor >= minor);
}

GLuint Device::BuildProgram(const char *vertex, const char *pixel)
{
    const GLuint vs = glCreateShader(GL_VERTEX_SHADER), ps = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(vs, 1, &vertex, nullptr);
    glShaderSource(ps, 1, &pixel, nullptr);
    glCompileShader(vs);
    glCompileShader(ps);
    const GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, ps);
    glLinkProgram(program);
    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char text[1024] = {};
        glGetProgramInfoLog(program, sizeof(text), nullptr, text);
        Log("gl: the backend's own program does not link: %s", text);
        glDeleteProgram(program);
    }
    glDeleteShader(vs);
    glDeleteShader(ps);
    return ok ? program : 0;
}

void Device::CheckFormats(DeviceInfo &info)
{
    const bool s3tc = HasExtension("GL_EXT_texture_compression_s3tc");
    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    BindDrawFbo(fbo);
    std::string missing;
    for (int i = 1; i < int(Format::Count); ++i)
    {
        const Format format = Format(i);
        if (IsBlockFormat(format) && !s3tc)
            continue;
        while (glGetError() != GL_NO_ERROR)
        {
        }
        GLuint texture;
        glGenTextures(1, &texture);
        EditTexture(GL_TEXTURE_2D, texture);
        glTexStorage2D(GL_TEXTURE_2D, 1, Info(format).internal, 4, 4);
        if (glGetError() != GL_NO_ERROR)
        {
            missing += missing.empty() ? "" : ", ";
            missing += std::to_string(i);
            glDeleteTextures(1, &texture);
            m_bound.unitTexture[kUploadUnit] = 0;
            continue;
        }
        uint8_t flags = FORMAT_SAMPLED | FORMAT_FILTERABLE;
        if (!IsBlockFormat(format))
        {
            const GLenum attachment = IsDepthFormat(format) ? DepthAttachment(format) : GL_COLOR_ATTACHMENT0;
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, attachment, GL_TEXTURE_2D, texture, 0);
            glDrawBuffer(IsDepthFormat(format) ? GL_NONE : GL_COLOR_ATTACHMENT0);
            glReadBuffer(GL_NONE);
            if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE)
                flags |= IsDepthFormat(format) ? FORMAT_DEPTH_TARGET : FORMAT_COLOR_TARGET;
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, attachment, GL_TEXTURE_2D, 0, 0);
        }
        info.caps.formatFlags[i] = flags;
        glDeleteTextures(1, &texture);
        m_bound.unitTexture[kUploadUnit] = 0;
    }
    BindDrawFbo(0);
    glDeleteFramebuffers(1, &fbo);
    if (!s3tc)
        Log("gl: no EXT_texture_compression_s3tc: the BC formats are missing");
    if (!missing.empty())
        Log("gl: formats %s cannot be made", missing.c_str());
}

bool Device::Start(DeviceInfo &info)
{
    if (!host::MakeCurrent(nullptr))
    {
        Log("gl: the context cannot be made current on the backend's thread");
        return false;
    }
    const char *missing = nullptr;
    if (!LoadFunctions(&missing))
    {
        Log("gl: the context has no %s", missing);
        return false;
    }
    glGetIntegerv(GL_MAJOR_VERSION, &m_major);
    glGetIntegerv(GL_MINOR_VERSION, &m_minor);
    GLint extensions = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &extensions);
    for (GLint i = 0; i < extensions; ++i)
        m_extensions.insert(reinterpret_cast<const char *>(glGetStringi(GL_EXTENSIONS, GLuint(i))));
    const char *renderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
    const char *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
    const char *glsl = reinterpret_cast<const char *>(glGetString(GL_SHADING_LANGUAGE_VERSION));
    info.device = std::string(renderer ? renderer : "?") + ", OpenGL " + (version ? version : "?");
    Log("gl: %s, GLSL %s", info.device.c_str(), glsl ? glsl : "?");
    if (!Version(4, 3))
    {
        Log("gl: OpenGL 4.3 is needed");
        return false;
    }
    if (!Version(4, 5) && !HasExtension("GL_ARB_clip_control"))
    {
        Log("gl: glClipControl (OpenGL 4.5 or ARB_clip_control) is needed");
        return false;
    }
    m_bufferStorage = Version(4, 4) || HasExtension("GL_ARB_buffer_storage");
    if (m_bufferStorage)
        glBufferStorage = reinterpret_cast<PFNGLBUFFERSTORAGEPROC>(host::ProcAddress("glBufferStorage"));
    m_bufferStorage = m_bufferStorage && glBufferStorage;
    m_mirrorOnce = Version(4, 4) || HasExtension("GL_ARB_texture_mirror_clamp_to_edge") ||
                   HasExtension("GL_EXT_texture_mirror_clamp");
    m_anisotropy = Version(4, 6) || HasExtension("GL_ARB_texture_filter_anisotropic") ||
                   HasExtension("GL_EXT_texture_filter_anisotropic");
    if (m_anisotropy)
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &m_maxAnisotropy);
    if (std::getenv("BO1_GL_DEBUG") && (Version(4, 3) || HasExtension("GL_KHR_debug")))
    {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(&Device::OnDebug, this);
        Log("gl: debug output on");
    }

    GLint value = 0;
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &value);
    m_uniformAlign = value > 0 ? uint32_t(value) : 256u;
    info.uniformAlign = m_uniformAlign;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &value);
    info.caps.maxTextureSize = uint16_t(value < 32768 ? value : 32768);
    GLint samples = 0, colorSamples = 0, depthSamples = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &samples);
    glGetIntegerv(GL_MAX_COLOR_TEXTURE_SAMPLES, &colorSamples);
    glGetIntegerv(GL_MAX_DEPTH_TEXTURE_SAMPLES, &depthSamples);
    samples = samples < colorSamples ? samples : colorSamples;
    samples = samples < depthSamples ? samples : depthSamples;
    info.caps.maxSamples = uint8_t(samples < 16 ? (samples > 1 ? samples : 1) : 16);
    info.caps.memoryBytes = PhysicalMemory();
    Log("gl: uniform alignment %u, buffer storage %s", m_uniformAlign, m_bufferStorage ? "yes" : "no");

    glClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE);
    glFrontFace(kFrontFace);
    glDisable(GL_DITHER);
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    CheckFormats(info);

    m_present = BuildProgram(kPresentVertex, kPresentPixel);
    if (!m_present)
        return false;
    glGenVertexArrays(1, &m_emptyVao);
    glGenFramebuffers(1, &m_readFbo);
    glGenFramebuffers(1, &m_drawFbo);
    glGenSamplers(1, &m_linearClamp);
    glGenSamplers(1, &m_nearestClamp);
    for (GLuint sampler : {m_linearClamp, m_nearestClamp})
    {
        const GLint filter = sampler == m_linearClamp ? GL_LINEAR : GL_NEAREST;
        glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, filter);
        glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, filter);
        glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    const GLenum error = glGetError();
    if (error != GL_NO_ERROR)
    {
        Log("gl: error 0x%x while setting up", error);
        return false;
    }
    return true;
}

void Device::Stop()
{
    for (auto &entry : m_vaos)
        glDeleteVertexArrays(1, &entry.second->name);
    for (const Fbo &fbo : m_fbos)
        glDeleteFramebuffers(1, &fbo.name);
    for (const Scratch &scratch : m_resolves)
        glDeleteTextures(1, &scratch.name);
    for (FrameBlocks &frame : m_inFlight)
        glDeleteSync(frame.sync);
    for (auto &block : m_blocks)
        glDeleteBuffers(1, &block->name);
    for (Fence *fence : m_fences)
        glDeleteSync(static_cast<GLsync>(fence->sync));
    glDeleteProgram(m_present);
    glDeleteVertexArrays(1, &m_emptyVao);
    glDeleteFramebuffers(1, &m_readFbo);
    glDeleteFramebuffers(1, &m_drawFbo);
    glDeleteSamplers(1, &m_linearClamp);
    glDeleteSamplers(1, &m_nearestClamp);
    glFinish();
}

// ---- state

void Device::UseProgram(GLuint program)
{
    if (m_bound.program != program)
    {
        glUseProgram(program);
        m_bound.program = program;
    }
}

void Device::BindVao(GLuint vao)
{
    if (m_bound.vao != vao)
    {
        glBindVertexArray(vao);
        m_bound.vao = vao;
    }
}

void Device::BindDrawFbo(GLuint fbo)
{
    if (m_bound.drawFbo != fbo)
    {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
        m_bound.drawFbo = fbo;
    }
}

void Device::BindReadFbo(GLuint fbo)
{
    if (m_bound.readFbo != fbo)
    {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        m_bound.readFbo = fbo;
    }
}

void Device::ActiveUnit(GLuint unit)
{
    if (m_bound.activeUnit != unit)
    {
        glActiveTexture(GL_TEXTURE0 + unit);
        m_bound.activeUnit = unit;
    }
}

void Device::BindTexture(GLuint unit, GLenum target, GLuint texture)
{
    if (m_bound.unitTexture[unit] != texture)
    {
        ActiveUnit(unit);
        glBindTexture(target, texture);
        m_bound.unitTexture[unit] = texture;
    }
}

// Binds `texture` on the upload unit and makes that unit active, for the calls that act on a target of the active
// unit (storage, uploads, parameters): a draw leaves another unit active.
void Device::EditTexture(GLenum target, GLuint texture)
{
    BindTexture(kUploadUnit, target, texture);
    ActiveUnit(kUploadUnit);
}

void Device::BindSampler(GLuint unit, GLuint sampler)
{
    if (m_bound.unitSampler[unit] != sampler)
    {
        glBindSampler(unit, sampler);
        m_bound.unitSampler[unit] = sampler;
    }
}

void Device::BindUbo(GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size)
{
    if (m_bound.uboBuffer[index] != buffer || m_bound.uboOffset[index] != offset || m_bound.uboSize[index] != size)
    {
        glBindBufferRange(GL_UNIFORM_BUFFER, index, buffer, offset, size);
        m_bound.uboBuffer[index] = buffer;
        m_bound.uboOffset[index] = offset;
        m_bound.uboSize[index] = size;
    }
}

void Device::SetBlend(const PipelineGl &p)
{
    if (!p.blend)
    {
        SetBlendOff();
        return;
    }
    if (m_bound.blend != 1)
    {
        glEnable(GL_BLEND);
        m_bound.blend = 1;
    }
    const GLenum funcs[4] = {p.srcColor, p.dstColor, p.srcAlpha, p.dstAlpha};
    if (std::memcmp(funcs, m_bound.blendFunc, sizeof(funcs)))
    {
        glBlendFuncSeparate(funcs[0], funcs[1], funcs[2], funcs[3]);
        std::memcpy(m_bound.blendFunc, funcs, sizeof(funcs));
    }
    if (m_bound.blendOp[0] != p.opColor || m_bound.blendOp[1] != p.opAlpha)
    {
        glBlendEquationSeparate(p.opColor, p.opAlpha);
        m_bound.blendOp[0] = p.opColor;
        m_bound.blendOp[1] = p.opAlpha;
    }
}

void Device::SetBlendOff()
{
    if (m_bound.blend != 0)
    {
        glDisable(GL_BLEND);
        m_bound.blend = 0;
    }
}

void Device::SetColorMask(uint8_t mask)
{
    if (m_bound.colorMask != mask)
    {
        glColorMask(mask & 1 ? GL_TRUE : GL_FALSE, mask & 2 ? GL_TRUE : GL_FALSE, mask & 4 ? GL_TRUE : GL_FALSE,
                    mask & 8 ? GL_TRUE : GL_FALSE);
        m_bound.colorMask = mask;
    }
}

void Device::SetBlendColor(const float rgba[4])
{
    if (std::memcmp(rgba, m_bound.blendColor, sizeof(m_bound.blendColor)))
    {
        glBlendColor(rgba[0], rgba[1], rgba[2], rgba[3]);
        std::memcpy(m_bound.blendColor, rgba, sizeof(m_bound.blendColor));
    }
}

void Device::SetDepth(bool test, bool write, GLenum func)
{
    // GL writes no depth without the test: a write without a test is a test that always passes.
    if (!test && write)
    {
        test = true;
        func = GL_ALWAYS;
    }
    if (m_bound.depthTest != int(test))
    {
        if (test)
            glEnable(GL_DEPTH_TEST);
        else
            glDisable(GL_DEPTH_TEST);
        m_bound.depthTest = int(test);
    }
    if (!test)
        return;
    SetDepthMask(write);
    if (m_bound.depthFunc != func)
    {
        glDepthFunc(func);
        m_bound.depthFunc = func;
    }
}

void Device::SetDepthMask(bool write)
{
    if (m_bound.depthMask != int(write))
    {
        glDepthMask(write ? GL_TRUE : GL_FALSE);
        m_bound.depthMask = int(write);
    }
}

void Device::SetStencil(const PipelineGl &p, uint8_t ref)
{
    if (!p.stencil)
    {
        SetStencilOff();
        return;
    }
    if (m_bound.stencilTest != 1)
    {
        glEnable(GL_STENCIL_TEST);
        m_bound.stencilTest = 1;
    }
    static const GLenum faces[2] = {GL_FRONT, GL_BACK};
    for (int f = 0; f < 2; ++f)
    {
        if (m_bound.stencilFunc[f] != p.stencilFunc[f] || m_bound.stencilRef[f] != ref ||
            m_bound.stencilReadMask[f] != p.stencilReadMask)
        {
            glStencilFuncSeparate(faces[f], p.stencilFunc[f], ref, p.stencilReadMask);
            m_bound.stencilFunc[f] = p.stencilFunc[f];
            m_bound.stencilRef[f] = ref;
            m_bound.stencilReadMask[f] = p.stencilReadMask;
        }
        const GLenum ops[3] = {p.stencilFail[f], p.stencilDepthFail[f], p.stencilPass[f]};
        if (std::memcmp(ops, m_bound.stencilOps[f], sizeof(ops)))
        {
            glStencilOpSeparate(faces[f], ops[0], ops[1], ops[2]);
            std::memcpy(m_bound.stencilOps[f], ops, sizeof(ops));
        }
    }
    SetStencilWriteMask(p.stencilWriteMask);
}

void Device::SetStencilOff()
{
    if (m_bound.stencilTest != 0)
    {
        glDisable(GL_STENCIL_TEST);
        m_bound.stencilTest = 0;
    }
}

void Device::SetStencilWriteMask(GLuint mask)
{
    if (m_bound.stencilWriteMask != mask)
    {
        glStencilMask(mask);
        m_bound.stencilWriteMask = mask;
    }
}

void Device::SetCull(bool cull, GLenum face)
{
    if (m_bound.cull != int(cull))
    {
        if (cull)
            glEnable(GL_CULL_FACE);
        else
            glDisable(GL_CULL_FACE);
        m_bound.cull = int(cull);
    }
    if (cull && m_bound.cullFace != face)
    {
        glCullFace(face);
        m_bound.cullFace = face;
    }
}

void Device::SetPolygonMode(GLenum mode)
{
    if (m_bound.polygonMode != mode)
    {
        glPolygonMode(GL_FRONT_AND_BACK, mode);
        m_bound.polygonMode = mode;
    }
}

void Device::SetScissorTest(bool enable)
{
    if (m_bound.scissorTest != int(enable))
    {
        if (enable)
            glEnable(GL_SCISSOR_TEST);
        else
            glDisable(GL_SCISSOR_TEST);
        m_bound.scissorTest = int(enable);
    }
}

void Device::SetScissor(const Rect &r)
{
    const GLint box[4] = {r.x, r.y, GLint(r.width), GLint(r.height)};
    if (std::memcmp(box, m_bound.scissor, sizeof(box)))
    {
        glScissor(box[0], box[1], box[2], box[3]);
        std::memcpy(m_bound.scissor, box, sizeof(box));
    }
}

void Device::SetOffset(bool enable, float factor, float units)
{
    if (m_bound.offset != int(enable))
    {
        for (GLenum mode : {GL_POLYGON_OFFSET_FILL, GL_POLYGON_OFFSET_LINE, GL_POLYGON_OFFSET_POINT})
        {
            if (enable)
                glEnable(mode);
            else
                glDisable(mode);
        }
        m_bound.offset = int(enable);
    }
    if (enable && (m_bound.offsetFactor != factor || m_bound.offsetUnits != units))
    {
        glPolygonOffset(factor, units);
        m_bound.offsetFactor = factor;
        m_bound.offsetUnits = units;
    }
}

void Device::SetViewport(float x, float y, float width, float height)
{
    const float box[4] = {x, y, width, height};
    if (std::memcmp(box, m_bound.viewport, sizeof(box)))
    {
        glViewportIndexedf(0, x, y, width, height);
        std::memcpy(m_bound.viewport, box, sizeof(box));
    }
}

void Device::SetDepthRange(float minDepth, float maxDepth)
{
    if (m_bound.depthRange[0] != minDepth || m_bound.depthRange[1] != maxDepth)
    {
        glDepthRangef(minDepth, maxDepth);
        m_bound.depthRange[0] = minDepth;
        m_bound.depthRange[1] = maxDepth;
    }
}

// ---- objects

void Device::Attach(GLenum target, GLenum attachment, const Texture *texture, uint8_t level, uint8_t layer)
{
    const TextureDesc &d = texture->desc;
    switch (d.type)
    {
    case TextureType::Tex2D:
        glFramebufferTexture2D(target, attachment, TextureTarget(d), texture->name, level);
        break;
    case TextureType::Cube:
        glFramebufferTexture2D(target, attachment, GL_TEXTURE_CUBE_MAP_POSITIVE_X + layer, texture->name, level);
        break;
    case TextureType::Tex3D:
        glFramebufferTextureLayer(target, attachment, texture->name, level, layer);
        break;
    }
}

void Device::Detach(GLenum target, GLenum attachment)
{
    glFramebufferTexture(target, attachment, 0, 0);
}

Fbo &Device::FboFor(const PassDesc &desc)
{
    Fbo key = {};
    key.colorCount = desc.colorCount;
    for (int i = 0; i < desc.colorCount; ++i)
        key.color[i] = {G(desc.color[i].texture), desc.color[i].level, desc.color[i].layer};
    key.depth = G(desc.depth);
    for (Fbo &fbo : m_fbos)
    {
        if (fbo.colorCount != key.colorCount || fbo.depth != key.depth)
            continue;
        bool same = true;
        for (int i = 0; i < key.colorCount && same; ++i)
            same = fbo.color[i] == key.color[i];
        if (same)
            return fbo;
    }
    glGenFramebuffers(1, &key.name);
    BindDrawFbo(key.name);
    GLenum buffers[MAX_COLOR_TARGETS];
    for (int i = 0; i < key.colorCount; ++i)
    {
        Attach(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, key.color[i].texture, key.color[i].level,
               key.color[i].layer);
        buffers[i] = GL_COLOR_ATTACHMENT0 + i;
    }
    if (key.depth)
        Attach(GL_DRAW_FRAMEBUFFER, DepthAttachment(key.depth->desc.format), key.depth, 0, 0);
    if (key.colorCount)
        glDrawBuffers(key.colorCount, buffers);
    else
        glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    const GLenum status = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
        Log("gl: a pass's framebuffer is incomplete (0x%x): %u colour attachments, %s depth", status,
            key.colorCount, key.depth ? "a" : "no");
    m_fbos.push_back(key);
    return m_fbos.back();
}

void Device::ForgetTexture(const Texture *texture)
{
    for (size_t i = 0; i < m_fbos.size();)
    {
        const Fbo &fbo = m_fbos[i];
        bool uses = fbo.depth == texture;
        for (int c = 0; c < fbo.colorCount; ++c)
            uses = uses || fbo.color[c].texture == texture;
        if (!uses)
        {
            ++i;
            continue;
        }
        glDeleteFramebuffers(1, &fbo.name);
        if (m_bound.drawFbo == fbo.name)
            m_bound.drawFbo = 0;
        if (m_bound.readFbo == fbo.name)
            m_bound.readFbo = 0;
        m_fbos[i] = m_fbos.back();
        m_fbos.pop_back();
    }
    // Deleting a texture unbinds it from every unit of the context.
    for (GLuint &bound : m_bound.unitTexture)
        if (bound == texture->name)
            bound = 0;
}

Vao *Device::VaoFor(const PipelineDesc &desc)
{
    std::string key;
    for (int i = 0; i < desc.attribCount; ++i)
    {
        const VertexAttrib &a = desc.attribs[i];
        const uint8_t bytes[5] = {a.location, a.stream, uint8_t(a.offset), uint8_t(a.offset >> 8), uint8_t(a.type)};
        key.append(reinterpret_cast<const char *>(bytes), sizeof(bytes));
    }
    std::unique_ptr<Vao> &slot = m_vaos[key];
    if (!slot)
    {
        slot = std::make_unique<Vao>();
        glGenVertexArrays(1, &slot->name);
        BindVao(slot->name);
        for (int i = 0; i < desc.attribCount; ++i)
        {
            const VertexAttrib &a = desc.attribs[i];
            glEnableVertexAttribArray(a.location);
            AttribFormat(a);
            glVertexAttribBinding(a.location, a.stream);
            slot->streams |= uint8_t(1u << a.stream);
        }
    }
    return slot.get();
}

PipelineGl &Device::Translate(const Pipeline *pipeline)
{
    PipelineGl &g = pipeline->gl;
    if (g.ready)
        return g;
    const PipelineDesc &d = pipeline->desc;
    g.vao = VaoFor(d);
    g.program = pipeline->program->name;
    const BlendState &b = d.blend;
    g.blend = b.enable;
    g.srcColor = ToBlend(b.srcColor);
    g.dstColor = ToBlend(b.dstColor);
    g.srcAlpha = ToBlend(b.srcAlpha);
    g.dstAlpha = ToBlend(b.dstAlpha);
    g.opColor = ToBlendOp(b.opColor);
    g.opAlpha = ToBlendOp(b.opAlpha);
    g.colorMask = b.writeMask & 0xF;
    const DepthStencilState &ds = d.depthStencil;
    g.depthTest = ds.depthTest;
    g.depthWrite = ds.depthWrite;
    g.depthFunc = ToCompare(ds.depthFunc);
    g.stencil = ds.stencil;
    const StencilFace *faces[2] = {&ds.front, &ds.back};
    for (int f = 0; f < 2; ++f)
    {
        g.stencilFunc[f] = ToCompare(faces[f]->func);
        g.stencilFail[f] = ToStencilOp(faces[f]->fail);
        g.stencilDepthFail[f] = ToStencilOp(faces[f]->depthFail);
        g.stencilPass[f] = ToStencilOp(faces[f]->pass);
    }
    g.stencilReadMask = ds.readMask;
    g.stencilWriteMask = ds.writeMask;
    const RasterState &r = d.raster;
    g.cull = r.cull != Cull::None;
    g.cullFace = r.cull == Cull::Front ? GL_FRONT : GL_BACK;
    g.polygonMode = r.fill == Fill::Wireframe ? GL_LINE : GL_FILL;
    g.scissor = r.scissor;
    g.offset = r.depthBias != 0.0f || r.slopeScaledDepthBias != 0.0f;
    g.offsetFactor = r.slopeScaledDepthBias;
    g.offsetUnits = r.depthBias * (d.depthFormat == Format::D16 ? 65536.0f : 16777216.0f);
    g.ready = true;
    return g;
}

UniformBlock *Device::TakeBlock()
{
    while (!m_inFlight.empty())
    {
        const GLenum status = glClientWaitSync(m_inFlight.front().sync, 0, 0);
        if (status != GL_ALREADY_SIGNALED && status != GL_CONDITION_SATISFIED)
            break;
        glDeleteSync(m_inFlight.front().sync);
        for (UniformBlock *block : m_inFlight.front().blocks)
            m_freeBlocks.push_back(block);
        m_inFlight.pop_front();
    }
    if (!m_freeBlocks.empty())
    {
        UniformBlock *block = m_freeBlocks.back();
        m_freeBlocks.pop_back();
        return block;
    }
    auto block = std::make_unique<UniformBlock>();
    glGenBuffers(1, &block->name);
    glBindBuffer(GL_COPY_WRITE_BUFFER, block->name);
    if (m_bufferStorage)
    {
        const GLbitfield flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
        glBufferStorage(GL_COPY_WRITE_BUFFER, UNIFORM_BLOCK_BYTES, nullptr, flags);
        block->mapped = static_cast<uint8_t *>(glMapBufferRange(GL_COPY_WRITE_BUFFER, 0, UNIFORM_BLOCK_BYTES, flags));
    }
    else
        glBufferData(GL_COPY_WRITE_BUFFER, UNIFORM_BLOCK_BYTES, nullptr, GL_STREAM_DRAW);
    m_blocks.push_back(std::move(block));
    return m_blocks.back().get();
}

UniformBlock *Device::Block(uint32_t index)
{
    while (m_frameBlocks.size() <= index)
        m_frameBlocks.push_back(TakeBlock());
    return m_frameBlocks[index];
}

void Device::EndFrame()
{
    if (!m_frameBlocks.empty())
    {
        m_inFlight.push_back({glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0), std::move(m_frameBlocks)});
        m_frameBlocks.clear();
    }
}

GLuint Device::ResolveScratch(Format format, uint32_t width, uint32_t height)
{
    for (const Scratch &s : m_resolves)
        if (s.format == format && s.width == width && s.height == height)
            return s.name;
    Scratch s = {0, format, width, height};
    glGenTextures(1, &s.name);
    EditTexture(GL_TEXTURE_2D, s.name);
    glTexStorage2D(GL_TEXTURE_2D, 1, Info(format).internal, GLsizei(width), GLsizei(height));
    m_resolves.push_back(s);
    return s.name;
}

// ---- the lanes

void Device::Compile(Shader *shader, const std::string &source)
{
    shader->name = glCreateShader(shader->stage == Stage::Vertex ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER);
    const char *text = source.c_str();
    const GLint length = GLint(source.size());
    glShaderSource(shader->name, 1, &text, &length);
    glCompileShader(shader->name);
}

void Device::ReportShader(const Shader *shader)
{
    if (shader->logged)
        return;
    shader->logged = true;
    GLint ok = GL_FALSE;
    glGetShaderiv(shader->name, GL_COMPILE_STATUS, &ok);
    if (ok)
        return;
    char text[768] = {};
    glGetShaderInfoLog(shader->name, sizeof(text), nullptr, text);
    for (char &c : text)
        if (c == '\n')
            c = ' ';
    Log("gl: %s shader %016llx does not compile: %s", shader->stage == Stage::Vertex ? "vertex" : "pixel",
        static_cast<unsigned long long>(shader->hash), text);
}

void Device::Link(Program *program)
{
    const GLuint name = glCreateProgram();
    glAttachShader(name, program->vertex->name);
    glAttachShader(name, program->pixel->name);
    glLinkProgram(name);
    GLint ok = GL_FALSE;
    glGetProgramiv(name, GL_LINK_STATUS, &ok);
    glDetachShader(name, program->vertex->name);
    glDetachShader(name, program->pixel->name);
    if (!ok)
    {
        ReportShader(program->vertex);
        ReportShader(program->pixel);
        char text[768] = {};
        glGetProgramInfoLog(name, sizeof(text), nullptr, text);
        for (char &c : text)
            if (c == '\n')
                c = ' ';
        Log("gl: shaders %016llx and %016llx do not link: %s",
            static_cast<unsigned long long>(program->vertex->hash),
            static_cast<unsigned long long>(program->pixel->hash), text);
        glDeleteProgram(name);
        program->status.store(-1, std::memory_order_release);
        return;
    }
    program->name = name;
    program->status.store(1, std::memory_order_release);
}

void Device::ServeBuilds()
{
    const auto start = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(q.lock);
        m_builds.swap(q.builds);
        q.buildsPending.store(false, std::memory_order_relaxed);
    }
    bool linked = false;
    for (Build &build : m_builds)
    {
        if (build.shader)
            Compile(build.shader, build.source);
        else
        {
            Link(build.program);
            linked = true;
        }
    }
    m_builds.clear();
    q.buildNs += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start)
                              .count());
    if (linked)
    {
        {
            std::lock_guard<std::mutex> lock(q.lock);
        }
        q.progress.notify_all();
    }
}

void Device::Execute(const Chunk &chunk)
{
    for (const Command &command : chunk.commands)
    {
        if (q.buildsPending.load(std::memory_order_acquire))
            ServeBuilds();
        std::visit([&](const auto &c) { Do(c, chunk); }, command);
    }
    m_flushed = false;
}

bool Device::Polling() const
{
    return !m_queries.empty() || !m_fences.empty();
}

void Device::Poll()
{
    for (size_t i = 0; i < m_queries.size();)
    {
        Query *query = m_queries[i].first;
        GLuint available = GL_FALSE;
        glGetQueryObjectuiv(query->name, GL_QUERY_RESULT_AVAILABLE, &available);
        if (!available)
        {
            ++i;
            continue;
        }
        GLuint64 value = 0;
        glGetQueryObjectui64v(query->name, GL_QUERY_RESULT, &value);
        query->value.store(value, std::memory_order_relaxed);
        query->available.store(m_queries[i].second, std::memory_order_release);
        m_queries[i] = m_queries.back();
        m_queries.pop_back();
    }
    for (size_t i = 0; i < m_fences.size();)
    {
        Fence *fence = m_fences[i];
        const GLenum status = glClientWaitSync(static_cast<GLsync>(fence->sync), 0, 0);
        if (status != GL_ALREADY_SIGNALED && status != GL_CONDITION_SATISFIED)
        {
            ++i;
            continue;
        }
        glDeleteSync(static_cast<GLsync>(fence->sync));
        fence->sync = nullptr;
        fence->reached.store(true, std::memory_order_release);
        m_fences[i] = m_fences.back();
        m_fences.pop_back();
    }
}

void Device::Run()
{
    for (;;)
    {
        if (q.buildsPending.load(std::memory_order_acquire))
            ServeBuilds();
        Chunk chunk;
        bool got = false;
        {
            std::unique_lock<std::mutex> lock(q.lock);
            auto ready = [&] { return !q.chunks.empty() || q.quit || q.buildsPending.load(); };
            if (!ready())
            {
                if (Polling())
                {
                    // Results only come once the GPU has the work: hand it over before waiting for them.
                    if (!m_flushed)
                    {
                        lock.unlock();
                        glFlush();
                        m_flushed = true;
                        lock.lock();
                    }
                    q.wake.wait_for(lock, std::chrono::milliseconds(1), ready);
                }
                else
                    q.wake.wait(lock, ready);
            }
            if (!q.chunks.empty())
            {
                chunk = std::move(q.chunks.front());
                q.chunks.pop_front();
                got = true;
            }
            else if (q.quit && !q.buildsPending.load())
                return;
        }
        if (got)
        {
            const auto start = std::chrono::steady_clock::now();
            Execute(chunk);
            q.commandNs += uint64_t(
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count());
            const uint64_t serial = chunk.serial;
            chunk.commands.clear();
            chunk.payload.clear();
            {
                std::lock_guard<std::mutex> lock(q.lock);
                q.executedChunks = serial;
                // Keep a few chunks for their capacity, but not the ones a large upload grew.
                if (q.spare.size() < 4 && chunk.payload.capacity() <= (4u << 20))
                    q.spare.push_back(std::move(chunk));
            }
            q.progress.notify_all();
        }
        Poll();
    }
}

// ---- commands

void Device::Do(const cmd::CreateBuffer &c, const Chunk &)
{
    Buffer *buffer = c.buffer;
    glGenBuffers(1, &buffer->name);
    glBindBuffer(GL_COPY_WRITE_BUFFER, buffer->name);
    const GLenum usage = buffer->desc.usage == Usage::Static      ? GL_STATIC_DRAW
                         : buffer->desc.memory == Memory::Shared ? GL_STREAM_DRAW
                                                                  : GL_DYNAMIC_DRAW;
    glBufferData(GL_COPY_WRITE_BUFFER, buffer->desc.size, nullptr, usage);
}

void Device::Do(const cmd::DestroyBuffer &c, const Chunk &)
{
    // A deleted buffer stays bound to the vertex arrays that are not current, and its address may come back: every
    // vertex array's bindings are redone.
    ++m_bufferGeneration;
    glDeleteBuffers(1, &c.buffer->name);
    delete c.buffer;
}

void Device::Do(const cmd::UploadBuffer &c, const Chunk &chunk)
{
    Buffer *buffer = c.buffer;
    const uint8_t *data = chunk.payload.data() + c.data.offset;
    glBindBuffer(GL_COPY_WRITE_BUFFER, buffer->name);
    switch (c.mode)
    {
    case Upload::Discard:
    {
        const GLenum usage = buffer->desc.memory == Memory::Shared ? GL_STREAM_DRAW : GL_DYNAMIC_DRAW;
        glBufferData(GL_COPY_WRITE_BUFFER, buffer->desc.size, nullptr, usage);
        glBufferSubData(GL_COPY_WRITE_BUFFER, c.offset, c.data.size, data);
        break;
    }
    case Upload::NoOverwrite:
    {
        void *out = glMapBufferRange(GL_COPY_WRITE_BUFFER, c.offset, c.data.size,
                                     GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT | GL_MAP_INVALIDATE_RANGE_BIT);
        if (out)
        {
            std::memcpy(out, data, c.data.size);
            glUnmapBuffer(GL_COPY_WRITE_BUFFER);
        }
        else
            glBufferSubData(GL_COPY_WRITE_BUFFER, c.offset, c.data.size, data);
        break;
    }
    case Upload::Ordered:
        glBufferSubData(GL_COPY_WRITE_BUFFER, c.offset, c.data.size, data);
        break;
    }
}

void Device::Do(const cmd::CreateTexture &c, const Chunk &)
{
    Texture *texture = c.texture;
    const TextureDesc &d = texture->desc;
    const GLenum target = TextureTarget(d);
    const GLenum internal = Info(d.format).internal;
    glGenTextures(1, &texture->name);
    EditTexture(target, texture->name);
    if (target == GL_TEXTURE_2D_MULTISAMPLE)
        glTexStorage2DMultisample(target, d.samples, internal, d.width, d.height, GL_TRUE);
    else if (target == GL_TEXTURE_3D)
        glTexStorage3D(target, d.levels, internal, d.width, d.height, d.depth);
    else
        glTexStorage2D(target, d.levels, internal, d.width, d.height);
    if (d.sampled)
    {
        const GLint swizzle[4] = {GLint(ToSource(d.swizzle.r)), GLint(ToSource(d.swizzle.g)),
                                  GLint(ToSource(d.swizzle.b)), GLint(ToSource(d.swizzle.a))};
        glTexParameteriv(target, GL_TEXTURE_SWIZZLE_RGBA, swizzle);
    }
}

void Device::Do(const cmd::DestroyTexture &c, const Chunk &)
{
    ForgetTexture(c.texture);
    glDeleteTextures(1, &c.texture->name);
    delete c.texture;
}

void Device::Do(const cmd::UploadTexture &c, const Chunk &chunk)
{
    const Texture *texture = c.texture;
    const TextureDesc &d = texture->desc;
    const GLenum target = TextureTarget(d);
    const GLenum image = d.type == TextureType::Cube ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + c.layer : target;
    const FormatInfo &f = Info(d.format);
    const uint8_t *data = chunk.payload.data() + c.data.offset;
    const Box &b = c.box;
    EditTexture(target, texture->name);
    if (IsBlockFormat(d.format))
    {
        if (target == GL_TEXTURE_3D)
            glCompressedTexSubImage3D(target, c.level, b.x, b.y, b.z, b.width, b.height, b.depth, f.internal,
                                      GLsizei(c.data.size), data);
        else
            glCompressedTexSubImage2D(image, c.level, b.x, b.y, b.width, b.height, f.internal, GLsizei(c.data.size),
                                      data);
    }
    else if (target == GL_TEXTURE_3D)
        glTexSubImage3D(target, c.level, b.x, b.y, b.z, b.width, b.height, b.depth, f.format, f.type, data);
    else
        glTexSubImage2D(image, c.level, b.x, b.y, b.width, b.height, f.format, f.type, data);
}

void Device::Do(const cmd::CreateSampler &c, const Chunk &)
{
    Sampler *sampler = c.sampler;
    const SamplerDesc &d = sampler->desc;
    glGenSamplers(1, &sampler->name);
    const GLuint s = sampler->name;
    const bool linear = d.minFilter == Filter::Linear;
    GLint min = linear ? GL_LINEAR : GL_NEAREST;
    if (d.mipFilter == MipFilter::Nearest)
        min = linear ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST;
    else if (d.mipFilter == MipFilter::Linear)
        min = linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR;
    glSamplerParameteri(s, GL_TEXTURE_MIN_FILTER, min);
    glSamplerParameteri(s, GL_TEXTURE_MAG_FILTER, d.magFilter == Filter::Linear ? GL_LINEAR : GL_NEAREST);
    auto wrap = [&](Address a) -> GLint {
        switch (a)
        {
        case Address::Wrap:
            return GL_REPEAT;
        case Address::Mirror:
            return GL_MIRRORED_REPEAT;
        case Address::Clamp:
            return GL_CLAMP_TO_EDGE;
        case Address::Border:
            return GL_CLAMP_TO_BORDER;
        case Address::MirrorOnce:
            return m_mirrorOnce ? GL_MIRROR_CLAMP_TO_EDGE : GL_MIRRORED_REPEAT;
        }
        return GL_REPEAT;
    };
    glSamplerParameteri(s, GL_TEXTURE_WRAP_S, wrap(d.addressU));
    glSamplerParameteri(s, GL_TEXTURE_WRAP_T, wrap(d.addressV));
    glSamplerParameteri(s, GL_TEXTURE_WRAP_R, wrap(d.addressW));
    const float border[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    glSamplerParameterfv(s, GL_TEXTURE_BORDER_COLOR, border);
    if (m_anisotropy && d.maxAnisotropy > 1)
        glSamplerParameterf(s, GL_TEXTURE_MAX_ANISOTROPY,
                            float(d.maxAnisotropy) < m_maxAnisotropy ? float(d.maxAnisotropy) : m_maxAnisotropy);
    if (d.depthCompare)
    {
        glSamplerParameteri(s, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glSamplerParameteri(s, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    }
}

void Device::Do(const cmd::DestroyShader &c, const Chunk &)
{
    glDeleteShader(c.shader->name);
    delete c.shader;
}

void Device::Do(const cmd::DestroyProgram &c, const Chunk &)
{
    if (c.program->name && m_bound.program == c.program->name)
        UseProgram(0);
    glDeleteProgram(c.program->name);
    delete c.program;
}

void Device::Do(const cmd::DestroyPipeline &c, const Chunk &)
{
    delete c.pipeline;
}

void Device::Do(const cmd::BeginPass &c, const Chunk &)
{
    const PassDesc &d = c.desc;
    const Fbo &fbo = FboFor(d);
    BindDrawFbo(fbo.name);
    m_passColors = d.colorCount;
    m_passDepth = G(d.depth);
    GLenum dontCare[MAX_COLOR_TARGETS + 2];
    GLsizei dontCares = 0;
    for (int i = 0; i < d.colorCount; ++i)
        if (d.color[i].load == LoadOp::DontCare)
            dontCare[dontCares++] = GL_COLOR_ATTACHMENT0 + i;
    const bool stencil = m_passDepth && m_passDepth->desc.format == Format::D24S8;
    if (m_passDepth && d.depthLoad == LoadOp::DontCare)
        dontCare[dontCares++] = GL_DEPTH_ATTACHMENT;
    if (stencil && d.stencilLoad == LoadOp::DontCare)
        dontCare[dontCares++] = GL_STENCIL_ATTACHMENT;
    if (dontCares)
        glInvalidateFramebuffer(GL_DRAW_FRAMEBUFFER, dontCares, dontCare);
    bool anyClear = (m_passDepth && d.depthLoad == LoadOp::Clear) || (stencil && d.stencilLoad == LoadOp::Clear);
    for (int i = 0; i < d.colorCount; ++i)
        anyClear = anyClear || d.color[i].load == LoadOp::Clear;
    if (!anyClear)
        return;
    SetScissorTest(false);
    SetColorMask(0xF);
    for (int i = 0; i < d.colorCount; ++i)
        if (d.color[i].load == LoadOp::Clear)
            glClearBufferfv(GL_COLOR, i, d.color[i].clear);
    const bool clearDepth = m_passDepth && d.depthLoad == LoadOp::Clear;
    const bool clearStencil = stencil && d.stencilLoad == LoadOp::Clear;
    if (clearDepth)
        SetDepthMask(true);
    if (clearStencil)
        SetStencilWriteMask(0xFF);
    const GLint stencilValue = d.clearStencil;
    if (clearDepth && clearStencil)
        glClearBufferfi(GL_DEPTH_STENCIL, 0, d.clearDepth, stencilValue);
    else if (clearDepth)
        glClearBufferfv(GL_DEPTH, 0, &d.clearDepth);
    else if (clearStencil)
        glClearBufferiv(GL_STENCIL, 0, &stencilValue);
}

void Device::Do(const cmd::EndPass &, const Chunk &)
{
}

void Device::ClearAttachments(bool color, bool depth, bool stencil, const float rgba[4], float depthValue,
                              uint8_t stencilValue)
{
    if (color)
    {
        SetColorMask(0xF);
        for (int i = 0; i < m_passColors; ++i)
            glClearBufferfv(GL_COLOR, i, rgba);
    }
    depth = depth && m_passDepth;
    stencil = stencil && m_passDepth && m_passDepth->desc.format == Format::D24S8;
    if (depth)
        SetDepthMask(true);
    if (stencil)
        SetStencilWriteMask(0xFF);
    const GLint s = stencilValue;
    if (depth && stencil)
        glClearBufferfi(GL_DEPTH_STENCIL, 0, depthValue, s);
    else if (depth)
        glClearBufferfv(GL_DEPTH, 0, &depthValue);
    else if (stencil)
        glClearBufferiv(GL_STENCIL, 0, &s);
}

void Device::Do(const cmd::Clear &c, const Chunk &chunk)
{
    const Rect *rects = reinterpret_cast<const Rect *>(chunk.payload.data() + c.rects.offset);
    SetScissorTest(true);
    for (uint32_t i = 0; i < c.count; ++i)
    {
        Rect r;
        std::memcpy(&r, rects + i, sizeof(r));
        SetScissor(r);
        ClearAttachments(c.color, c.depth, c.stencil, c.rgba, c.depthValue, c.stencilValue);
    }
}

void Device::Do(const cmd::Uniforms &c, const Chunk &chunk)
{
    UniformBlock *block = Block(c.offset / UNIFORM_BLOCK_BYTES);
    const uint32_t offset = c.offset % UNIFORM_BLOCK_BYTES;
    const uint8_t *data = chunk.payload.data() + c.data.offset;
    if (block->mapped)
        std::memcpy(block->mapped + offset, data, c.data.size);
    else
    {
        glBindBuffer(GL_COPY_WRITE_BUFFER, block->name);
        glBufferSubData(GL_COPY_WRITE_BUFFER, offset, c.data.size, data);
    }
}

void Device::Do(const cmd::Draw &c, const Chunk &)
{
    const DrawState &s = c.state;
    const Pipeline *pipeline = G(s.pipeline);
    const PipelineGl &p = Translate(pipeline);
    UseProgram(p.program);

    SetBlend(p);
    SetColorMask(p.colorMask);
    SetBlendColor(s.blendConstant);
    SetDepth(p.depthTest, p.depthWrite, p.depthFunc);
    SetStencil(p, s.stencilRef);
    SetCull(p.cull, p.cullFace);
    SetPolygonMode(p.polygonMode);
    SetOffset(p.offset, p.offsetFactor, p.offsetUnits);
    SetScissorTest(p.scissor);
    if (p.scissor)
        SetScissor(s.scissor);
    SetViewport(s.viewport.x, s.viewport.y, s.viewport.width, s.viewport.height);
    SetDepthRange(s.viewport.minDepth, s.viewport.maxDepth);

    Vao *vao = p.vao;
    BindVao(vao->name);
    if (vao->generation != m_bufferGeneration)
    {
        std::memset(vao->buffers, 0, sizeof(vao->buffers));
        vao->indices = nullptr;
        vao->generation = m_bufferGeneration;
    }
    for (int i = 0; i < MAX_STREAMS; ++i)
    {
        if (!(vao->streams & (1u << i)))
            continue;
        const VertexBinding &v = s.vertices[i];
        const Buffer *buffer = G(v.buffer);
        if (vao->buffers[i] != buffer || vao->offsets[i] != v.offset || vao->strides[i] != v.stride)
        {
            glBindVertexBuffer(GLuint(i), buffer ? buffer->name : 0, v.offset, GLsizei(v.stride));
            vao->buffers[i] = buffer;
            vao->offsets[i] = v.offset;
            vao->strides[i] = v.stride;
        }
    }
    const Buffer *indices = G(s.indices);
    if (c.indexed && vao->indices != indices)
    {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indices->name);
        vao->indices = indices;
    }

    for (int i = 0; i < MAX_TEXTURES; ++i)
    {
        const TextureBinding &t = s.textures[i];
        if (!t.texture)
            continue;
        const Texture *texture = G(t.texture);
        BindTexture(GLuint(i), TextureTarget(texture->desc), texture->name);
        BindSampler(GLuint(i), G(t.sampler)->name);
    }

    const UniformSlice *slices[2] = {&s.vertexConstants, &s.pixelConstants};
    for (GLuint i = 0; i < 2; ++i)
    {
        const uint32_t index = slices[i]->offset / UNIFORM_BLOCK_BYTES;
        if (index >= m_frameBlocks.size())
        {
            Log("gl: a draw reads uniforms this frame has not written: skipped");
            return;
        }
        BindUbo(i, m_frameBlocks[index]->name, slices[i]->offset % UNIFORM_BLOCK_BYTES, slices[i]->size);
    }

    const GLenum mode = ToMode(c.primitive);
    if (c.indexed)
    {
        const bool shortIndices = indices->desc.type == BufferType::Index16;
        const uintptr_t offset = uintptr_t(c.first) * (shortIndices ? 2u : 4u);
        glDrawElementsBaseVertex(mode, GLsizei(c.count), shortIndices ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
                                 reinterpret_cast<const void *>(offset), c.baseVertex);
    }
    else
        glDrawArrays(mode, GLint(c.first), GLsizei(c.count));
}

void Device::Do(const cmd::Blit &c, const Chunk &)
{
    const Texture *src = c.src, *dst = c.dst;
    const bool depth = IsDepthFormat(src->desc.format);
    const GLenum attachment = depth ? DepthAttachment(src->desc.format) : GL_COLOR_ATTACHMENT0;
    const GLbitfield mask = !depth                            ? GL_COLOR_BUFFER_BIT
                            : src->desc.format == Format::D24S8 ? GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT
                                                                : GL_DEPTH_BUFFER_BIT;
    const GLenum filter = c.filter == Filter::Linear ? GL_LINEAR : GL_NEAREST;
    SetScissorTest(false);
    BindReadFbo(m_readFbo);
    Attach(GL_READ_FRAMEBUFFER, attachment, src, c.srcLevel, c.srcLayer);
    glReadBuffer(depth ? GL_NONE : GL_COLOR_ATTACHMENT0);
    BindDrawFbo(m_drawFbo);
    Rect from = c.srcRect;
    // A resolve copies equal rects only: resolve into a scratch texture first when the blit also scales.
    if (src->desc.samples > 1 && (from.width != c.dstRect.width || from.height != c.dstRect.height))
    {
        const GLuint scratch = ResolveScratch(src->desc.format, from.width, from.height);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, attachment, GL_TEXTURE_2D, scratch, 0);
        glDrawBuffer(depth ? GL_NONE : GL_COLOR_ATTACHMENT0);
        glBlitFramebuffer(from.x, from.y, from.x + GLint(from.width), from.y + GLint(from.height), 0, 0,
                          GLint(from.width), GLint(from.height), mask, GL_NEAREST);
        Detach(GL_READ_FRAMEBUFFER, attachment);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, attachment, GL_TEXTURE_2D, scratch, 0);
        Detach(GL_DRAW_FRAMEBUFFER, attachment);
        from.x = 0;
        from.y = 0;
    }
    Attach(GL_DRAW_FRAMEBUFFER, attachment, dst, c.dstLevel, c.dstLayer);
    glDrawBuffer(depth ? GL_NONE : GL_COLOR_ATTACHMENT0);
    glBlitFramebuffer(from.x, from.y, from.x + GLint(from.width), from.y + GLint(from.height), c.dstRect.x,
                      c.dstRect.y, c.dstRect.x + GLint(c.dstRect.width), c.dstRect.y + GLint(c.dstRect.height), mask,
                      filter);
    Detach(GL_READ_FRAMEBUFFER, attachment);
    Detach(GL_DRAW_FRAMEBUFFER, attachment);
    // An empty framebuffer left bound is validated, and reported incomplete, at every later draw.
    BindReadFbo(0);
    BindDrawFbo(0);
}

void Device::Do(const cmd::ReadTexture &c, const Chunk &)
{
    const Texture *texture = c.texture;
    const TextureDesc &d = texture->desc;
    const FormatInfo &f = Info(d.format);
    const bool depth = IsDepthFormat(d.format);
    const GLenum attachment = depth ? DepthAttachment(d.format) : GL_COLOR_ATTACHMENT0;
    BindReadFbo(m_readFbo);
    Attach(GL_READ_FRAMEBUFFER, attachment, texture, c.level, d.type == TextureType::Tex3D ? uint8_t(c.box.z) : c.layer);
    glReadBuffer(depth ? GL_NONE : GL_COLOR_ATTACHMENT0);
    const uint32_t bytes = FormatBytes(d.format);
    const uint32_t rowBytes = c.box.width * bytes;
    if (c.rowPitch % bytes == 0)
    {
        glPixelStorei(GL_PACK_ROW_LENGTH, GLint(c.rowPitch / bytes));
        glReadPixels(c.box.x, c.box.y, c.box.width, c.box.height, f.format, f.type, c.data);
        glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    }
    else
    {
        m_readScratch.resize(size_t(rowBytes) * c.box.height);
        glReadPixels(c.box.x, c.box.y, c.box.width, c.box.height, f.format, f.type, m_readScratch.data());
        for (uint32_t y = 0; y < c.box.height; ++y)
            std::memcpy(static_cast<uint8_t *>(c.data) + size_t(y) * c.rowPitch,
                        m_readScratch.data() + size_t(y) * rowBytes, rowBytes);
    }
    Detach(GL_READ_FRAMEBUFFER, attachment);
    BindReadFbo(0);
}

void Device::Do(const cmd::CreateQuery &c, const Chunk &)
{
    glGenQueries(1, &c.query->name);
}

void Device::Do(const cmd::DestroyQuery &c, const Chunk &)
{
    for (size_t i = 0; i < m_queries.size(); ++i)
    {
        if (m_queries[i].first == c.query)
        {
            m_queries[i] = m_queries.back();
            m_queries.pop_back();
            break;
        }
    }
    glDeleteQueries(1, &c.query->name);
    delete c.query;
}

void Device::Do(const cmd::BeginQuery &c, const Chunk &)
{
    glBeginQuery(GL_SAMPLES_PASSED, c.query->name);
}

void Device::Do(const cmd::EndQuery &c, const Chunk &)
{
    Query *query = c.query;
    if (query->type == QueryType::Occlusion)
        glEndQuery(GL_SAMPLES_PASSED);
    else
        glQueryCounter(query->name, GL_TIMESTAMP);
    for (auto &pending : m_queries)
    {
        if (pending.first == query)
        {
            pending.second = c.serial;
            return;
        }
    }
    m_queries.emplace_back(query, c.serial);
}

void Device::Do(const cmd::InsertFence &c, const Chunk &)
{
    c.fence->sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    m_fences.push_back(c.fence);
}

void Device::Do(const cmd::DestroyFence &c, const Chunk &)
{
    for (size_t i = 0; i < m_fences.size(); ++i)
    {
        if (m_fences[i] == c.fence)
        {
            m_fences[i] = m_fences.back();
            m_fences.pop_back();
            break;
        }
    }
    if (c.fence->sync)
        glDeleteSync(static_cast<GLsync>(c.fence->sync));
    delete c.fence;
}

void Device::Do(const cmd::CreateSwapchain &c, const Chunk &)
{
    Swapchain *swapchain = c.swapchain;
    uint16_t identity[256 * 3];
    for (int i = 0; i < 256; ++i)
        identity[i * 3] = identity[i * 3 + 1] = identity[i * 3 + 2] = uint16_t(i * 257);
    glGenTextures(1, &swapchain->gamma);
    EditTexture(GL_TEXTURE_2D, swapchain->gamma);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGB16, 256, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGB, GL_UNSIGNED_SHORT, identity);
}

void Device::Do(const cmd::DestroySwapchain &c, const Chunk &)
{
    Swapchain *swapchain = c.swapchain;
    if (m_window == swapchain->window)
    {
        host::MakeCurrent(nullptr);
        m_window = nullptr;
    }
    for (GLuint &bound : m_bound.unitTexture)
        if (bound == swapchain->gamma)
            bound = 0;
    glDeleteTextures(1, &swapchain->gamma);
    delete swapchain;
}

void Device::Do(const cmd::SetGamma &c, const Chunk &chunk)
{
    uint16_t ramps[3][256];
    std::memcpy(ramps, chunk.payload.data() + c.ramps.offset, sizeof(ramps));
    uint16_t texels[256 * 3];
    for (int i = 0; i < 256; ++i)
        for (int channel = 0; channel < 3; ++channel)
            texels[i * 3 + channel] = ramps[channel][i];
    EditTexture(GL_TEXTURE_2D, c.swapchain->gamma);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGB, GL_UNSIGNED_SHORT, texels);
}

void Device::Do(const cmd::Present &c, const Chunk &)
{
    Swapchain *swapchain = c.swapchain;
    bool drawn = false;
    if (m_window != swapchain->window)
    {
        if (host::MakeCurrent(swapchain->window))
            m_window = swapchain->window;
        else
        {
            m_window = nullptr;
            host::MakeCurrent(nullptr);
            if (!m_reportedWindow)
                Log("gl: the game window cannot be drawn into: its frames are not shown");
            m_reportedWindow = true;
        }
    }
    if (m_window == swapchain->window)
    {
        const int interval = swapchain->vsync ? 1 : 0;
        if (swapchain->interval != interval)
        {
            host::SetSwapInterval(interval);
            swapchain->interval = interval;
        }
        int width = 0, height = 0;
        host::DrawableSize(swapchain->window, &width, &height);
        if (width > 0 && height > 0)
        {
            BindDrawFbo(0);
            SetBlendOff();
            SetColorMask(0xF);
            SetDepth(false, false, GL_ALWAYS);
            SetStencilOff();
            SetCull(false, GL_BACK);
            SetPolygonMode(GL_FILL);
            SetOffset(false, 0.0f, 0.0f);
            SetScissorTest(false);
            SetViewport(0.0f, 0.0f, float(width), float(height));
            UseProgram(m_present);
            BindVao(m_emptyVao);
            BindTexture(0, GL_TEXTURE_2D, c.image->name);
            BindSampler(0, m_linearClamp);
            BindTexture(1, GL_TEXTURE_2D, swapchain->gamma);
            BindSampler(1, m_nearestClamp);
            glUniform4f(0, 1.0f / float(width), 1.0f / float(height), float(height), 0.0f);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            const auto start = std::chrono::steady_clock::now();
            host::Swap(swapchain->window);
            q.swapNs += uint64_t(
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count());
            drawn = true;
        }
    }
    if (!drawn)
        glFlush();
    EndFrame();
    {
        std::lock_guard<std::mutex> lock(q.lock);
        ++q.presentedFrames;
    }
    q.progress.notify_all();
}

void Device::Do(const cmd::Finish &, const Chunk &)
{
    glFinish();
}

void DeviceMain(Queue *queue, DeviceInfo *info, std::promise<bool> *started)
{
    Device device(*queue);
    const bool ok = device.Start(*info);
    started->set_value(ok);
    if (ok)
    {
        device.Run();
        device.Stop();
    }
    host::ReleaseCurrent();
}
} // namespace

bool StartDevice(Queue &queue, DeviceInfo &info, std::thread &thread)
{
    std::promise<bool> started;
    std::future<bool> result = started.get_future();
    thread = std::thread(DeviceMain, &queue, &info, &started);
    if (result.get())
        return true;
    thread.join();
    return false;
}
} // namespace gb::gl
