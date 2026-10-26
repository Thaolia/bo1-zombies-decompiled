#pragma once

// The GL backend's objects and its command stream, shared by the side the frontend calls (gb_gl.cpp) and the
// backend's thread (gb_gl_device.cpp), which owns the context and makes every GL call. The frontend side records
// commands into chunks and hands them over in order; the backend's thread executes them. Shader compiles and links
// go on a second lane, served between commands: they make objects that nothing recorded uses yet, so a GetPipeline
// waits for its link, not for the frames queued before it.
//
// Fields marked "backend thread" are only touched there; the rest is set before the object is first recorded and
// read-only afterwards, unless it is atomic.

#include "../gb_backend.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace gb::gl
{
struct Buffer : gb::Buffer
{
    explicit Buffer(const BufferDesc &d) : desc(d) {}
    BufferDesc desc;
    uint32_t name = 0; // backend thread
};

struct Texture : gb::Texture
{
    explicit Texture(const TextureDesc &d) : desc(d) {}
    TextureDesc desc;
    uint32_t name = 0; // backend thread
};

struct Sampler : gb::Sampler
{
    explicit Sampler(const SamplerDesc &d) : desc(d) {}
    SamplerDesc desc;
    uint32_t name = 0; // backend thread
};

struct Shader : gb::Shader
{
    Shader(Stage s, uint64_t h) : stage(s), hash(h) {}
    Stage stage;
    uint64_t hash;
    // The GLSL, held from CreateShader until the backend thread compiles it the first time a program that uses it is
    // linked (lazy: a map references ~13k shaders but draws a fraction; compiling them all at load OOMs Mesa's GLSL
    // compiler, PORTING.md 2026-10-25). Freed once compiled. backend thread owns name/source/logged.
    mutable std::string source;
    mutable uint32_t name = 0;   // 0 until compiled
    mutable bool logged = false; // its compile status was reported
};

// A linked vertex and pixel shader pair, shared by the pipelines made with it.
struct Program
{
    Program(const Shader *v, const Shader *p) : vertex(v), pixel(p) {}
    const Shader *vertex, *pixel;
    std::atomic<int> status{0}; // 0 while linking, 1 linked, -1 failed
    uint32_t name = 0;          // backend thread
};

struct Vao;

// A pipeline in GL's terms (enums as uint32_t: no GL header here), translated by the backend's thread at the first
// draw with it.
struct PipelineGl
{
    bool ready = false;
    Vao *vao = nullptr;
    uint32_t program = 0;
    bool blend = false;
    uint32_t srcColor = 0, dstColor = 0, srcAlpha = 0, dstAlpha = 0, opColor = 0, opAlpha = 0;
    uint8_t colorMask = 0xF;
    bool depthTest = false, depthWrite = false;
    uint32_t depthFunc = 0;
    bool stencil = false;
    uint32_t stencilFunc[2] = {}, stencilFail[2] = {}, stencilDepthFail[2] = {}, stencilPass[2] = {};
    uint32_t stencilReadMask = 0xFF, stencilWriteMask = 0xFF;
    bool cull = false;
    uint32_t cullFace = 0, polygonMode = 0;
    bool scissor = false;
    bool offset = false;
    float offsetFactor = 0.0f, offsetUnits = 0.0f;
};

struct Pipeline : gb::Pipeline
{
    Pipeline(const PipelineDesc &d, Program *p) : desc(d), program(p) {}
    PipelineDesc desc;
    Program *program;
    mutable PipelineGl gl; // backend thread
};

struct Query : gb::Query
{
    explicit Query(QueryType t) : type(t) {}
    QueryType type;
    uint64_t ends = 0;                  // frontend side: EndQuery calls so far
    uint64_t endChunk = 0;              // frontend side: the chunk that records the last EndQuery
    std::atomic<uint64_t> available{0}; // the EndQuery whose result `value` holds
    std::atomic<uint64_t> value{0};
    uint32_t name = 0;                  // backend thread
};

struct Fence : gb::Fence
{
    uint64_t chunk = 0; // frontend side: the chunk that records it
    std::atomic<bool> reached{false};
    void *sync = nullptr; // backend thread
};

struct Swapchain : gb::Swapchain
{
    explicit Swapchain(const SwapchainDesc &d) : desc(d), window(d.window), vsync(d.vsync) {}
    SwapchainDesc desc; // frontend side: ResizeSwapchain changes its size
    void *window;
    bool vsync;
    int interval = -1;      // backend thread: the swap interval it set
    uint32_t gamma = 0;     // backend thread: the ramp, a 256x1 texture
};

// Frame-local uniform memory comes in blocks of this size; the frontend side places each AllocUniforms (never across
// a block boundary) and the command carries the offset, so a slice needs no reply.
inline constexpr uint32_t UNIFORM_BLOCK_BYTES = 1u << 20;

// A range of the chunk's payload.
struct Bytes
{
    uint32_t offset;
    uint32_t size;
};

namespace cmd
{
struct CreateBuffer
{
    Buffer *buffer;
};
struct DestroyBuffer
{
    Buffer *buffer;
};
struct UploadBuffer
{
    Buffer *buffer;
    uint32_t offset;
    Upload mode;
    Bytes data;
};
struct CreateTexture
{
    Texture *texture;
};
struct DestroyTexture
{
    Texture *texture;
};
// The box's rows (block rows) packed, then its slices.
struct UploadTexture
{
    Texture *texture;
    uint8_t level, layer;
    Box box;
    Bytes data;
};
struct CreateSampler
{
    Sampler *sampler;
};
struct DestroyShader
{
    Shader *shader;
};
struct DestroyProgram
{
    Program *program;
};
struct DestroyPipeline
{
    Pipeline *pipeline;
};
struct BeginPass
{
    PassDesc desc;
};
struct EndPass
{
};
struct Clear
{
    Bytes rects;
    uint32_t count;
    bool color, depth, stencil;
    float rgba[4];
    float depthValue;
    uint8_t stencilValue;
};
struct Uniforms
{
    uint32_t offset;
    Bytes data;
};
struct Draw
{
    DrawState state;
    Primitive primitive;
    bool indexed;
    uint32_t count, first;
    int32_t baseVertex;
};
struct Blit
{
    Texture *src;
    uint8_t srcLevel, srcLayer;
    Rect srcRect;
    Texture *dst;
    uint8_t dstLevel, dstLayer;
    Rect dstRect;
    Filter filter;
};
// Synchronous: the frontend side waits while the backend's thread writes `data`.
struct ReadTexture
{
    Texture *texture;
    uint8_t level, layer;
    Box box;
    void *data;
    uint32_t rowPitch;
};
struct CreateQuery
{
    Query *query;
};
struct DestroyQuery
{
    Query *query;
};
struct BeginQuery
{
    Query *query;
};
struct EndQuery
{
    Query *query;
    uint64_t serial;
};
struct InsertFence
{
    Fence *fence;
};
struct DestroyFence
{
    Fence *fence;
};
struct CreateSwapchain
{
    Swapchain *swapchain;
};
// Synchronous: once it returns, the platform may destroy the window.
struct DestroySwapchain
{
    Swapchain *swapchain;
};
// Red, green and blue ramps of 256 uint16_t each.
struct SetGamma
{
    Swapchain *swapchain;
    Bytes ramps;
};
struct Present
{
    Swapchain *swapchain;
    Texture *image;
};
// WaitIdle's glFinish.
struct Finish
{
};
} // namespace cmd

using Command =
    std::variant<cmd::CreateBuffer, cmd::DestroyBuffer, cmd::UploadBuffer, cmd::CreateTexture, cmd::DestroyTexture,
                 cmd::UploadTexture, cmd::CreateSampler, cmd::DestroyShader, cmd::DestroyProgram, cmd::DestroyPipeline,
                 cmd::BeginPass, cmd::EndPass, cmd::Clear, cmd::Uniforms, cmd::Draw, cmd::Blit, cmd::ReadTexture,
                 cmd::CreateQuery, cmd::DestroyQuery, cmd::BeginQuery, cmd::EndQuery, cmd::InsertFence,
                 cmd::DestroyFence, cmd::CreateSwapchain, cmd::DestroySwapchain, cmd::SetGamma, cmd::Present,
                 cmd::Finish>;

struct Chunk
{
    uint64_t serial = 0;
    std::vector<Command> commands;
    std::vector<uint8_t> payload;
};

// The second lane: a program to link. Its two shaders are compiled on demand inside the link (they carry their own
// source), so a shader that is never drawn is never compiled.
struct Build
{
    Program *program;
};

struct Queue
{
    std::mutex lock;
    std::condition_variable wake;     // for the backend's thread: work, or quit
    std::condition_variable progress; // for the frontend side: a chunk, a frame or a link is done
    std::deque<Chunk> chunks;         // submitted, oldest first
    std::vector<Chunk> spare;         // executed, kept for their capacity
    std::vector<Build> builds;
    std::atomic<bool> buildsPending{false};
    uint64_t executedChunks = 0;
    uint64_t presentedFrames = 0;
    bool quit = false;
    std::vector<std::string> log; // the backend thread's lines, printed by the frontend side
    // Nanoseconds the backend's thread spent on commands, on compiles and links, and in the platform's swap, for the
    // frontend side's perf line.
    std::atomic<uint64_t> commandNs{0}, buildNs{0}, swapNs{0};
};

struct DeviceInfo
{
    Caps caps = {};
    std::string device;
    uint32_t uniformAlign = 256;
};

// The backend's thread (gb_gl_device.cpp). StartDevice starts it on `queue` and waits until it has made the
// platform's context current and checked it: false, the thread ended, when the context cannot run the backend.
bool StartDevice(Queue &queue, DeviceInfo &info, std::thread &thread);
} // namespace gb::gl
