// The deko3d backend. Shaders are DKSH that uam compiled offline (no runtime GLSL compiler -- the GL path's OOM,
// docs/PORTING.md). This stage draws geometry: DKSH shaders bound as a pipeline (vertex attribute + blend + depth +
// raster state translated from the contract), vertex/index buffers, the two std140 constant blocks (vertex UBO at
// binding 0, pixel UBO at binding 1, docs/RENDERER.md), and a draw. Textures are bound to a single placeholder image
// for now (geometry and lighting render; per-material textures via descriptor sets are the next step), so the result
// shows shapes, not final colours. Passes clear the engine's real targets and Present blits the final image.

#include <switch.h>

#include <deko3d.h>

#include "../gb_backend.h"
#include "../gb_common.h"
#include "gb_deko3d.h"

#include <malloc.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

extern "C" void Switch_BootLogWrite(const char *text); // switch_main.cpp

namespace gb::deko3d
{
namespace
{
constexpr unsigned kFbCount = 2;
constexpr uint32_t kCmdMemSize = 16 << 20;  // a map frame records many draws
constexpr uint32_t kCodeChunk = 4 << 20;    // shader code pool grows in 4 MiB blocks
constexpr uint32_t kUboRingSize = 16 << 20; // per-frame uniform memory, bump-allocated

DkDevice s_device;
DkQueue s_queue;
Caps s_caps;

std::vector<DkMemBlock> s_codeBlocks;
uint32_t s_codeOffset = 0;

void Log(const char *fmt, ...)
{
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    std::printf("[gb-deko3d] %s\n", line);
    std::fflush(stdout);
    // svcOutputDebugString reaches the emulator's own log immediately (it survives an abort that loses buffered SD
    // writes), which is how a message logged right before a deko3d assert can still be read.
    {
        char dbg[560];
        const int n = std::snprintf(dbg, sizeof(dbg), "[gb-deko3d] %s", line);
        svcOutputDebugString(dbg, n > 0 ? size_t(n) : 0);
    }
    // Also to the SD boot log, the only log a user's console run leaves: the emulator does not capture the guest's
    // stdout. Through the process's one boot-log writer, as a second fopen of the file fails on the console.
    {
        char sd[560];
        std::snprintf(sd, sizeof(sd), "[gb-deko3d] %s\n", line);
        Switch_BootLogWrite(sd);
    }
}

// The heap in use: deko3d allocates every memory block from it (no storage given), so this is the backend's footprint
// plus the engine's, the figure that runs out first on the console (switch-homebrew.md, mallinfo).
double HeapMB()
{
    const struct mallinfo mi = mallinfo();
    return double(static_cast<unsigned>(mi.uordblks)) / double(1 << 20);
}

// deko3d reports API misuse / resource problems here (only the debug build, libdeko3dd, validates); it is how a bad
// pipeline/vertex/descriptor state shows up instead of silently faulting the GPU.
void Log(const char *fmt, ...);
void DebugCallback(void *, const char *context, DkResult result, const char *message)
{
    Log("DEBUG %s (result %d): %s", context ? context : "?", int(result), message ? message : "");
}

uint32_t AlignUp(uint64_t value, uint64_t alignment)
{
    return uint32_t((value + alignment - 1) & ~(alignment - 1));
}

// Live memory blocks: each one is an nvmap handle and a GPU mapping that nvservices keeps in the small transfer memory
// libnx gives it, a limit the console has and Ryujinx does not emulate.
uint32_t s_blocksLive = 0;
uint64_t s_blockBytes = 0;

DkMemBlock MakeBlock(uint32_t size, uint32_t flags)
{
    DkMemBlockMaker maker;
    dkMemBlockMakerDefaults(&maker, s_device, AlignUp(size, DK_MEMBLOCK_ALIGNMENT));
    maker.flags = flags;
    const DkMemBlock block = dkMemBlockCreate(&maker);
    if (block)
    {
        ++s_blocksLive;
        s_blockBytes += maker.size;
    }
    else
        Log("MakeBlock: dkMemBlockCreate failed (%u bytes, flags 0x%x) with %u blocks live (%.1f MB), heap %.1f MB",
            maker.size, flags, s_blocksLive, double(s_blockBytes) / double(1 << 20), HeapMB());
    return block;
}

void FreeBlock(DkMemBlock block)
{
    --s_blocksLive;
    s_blockBytes -= dkMemBlockGetSize(block);
    dkMemBlockDestroy(block);
}

// dkQueueWaitIdle that says why it waits. After a GPU fault the queue's work never completes, so an unbounded wait
// holds the calling thread for good, silently: an upload from the loader thread then keeps the frontend lock and the
// whole game hangs. Every 2 s without completion is logged with the queue's error state; a faulted queue is given up
// on, a healthy busy one (Ryujinx translating shaders) is still waited for, as the caller then reuses the memory.
bool WaitQueue(const char *what)
{
    DkFence fence;
    dkQueueSignalFence(s_queue, &fence, true); // `true` flushes the GPU caches; dkQueueFlush submits
    dkQueueFlush(s_queue);
    static int logged = 0;
    for (int waited = 2;; waited += 2)
    {
        const DkResult r = dkFenceWait(&fence, 2000000000);
        if (r == DkResult_Success)
            return true;
        const bool faulted = dkQueueIsInErrorState(s_queue);
        if (logged < 32)
        {
            ++logged;
            Log("%s: GPU still busy after %d s (result %d), queue %s", what, waited, int(r),
                faulted ? "in error state (GPU fault): giving up" : "not in error state");
        }
        if (faulted || r != DkResult_Timeout)
            return false;
    }
}

DkImageFormat MapFormat(Format f)
{
    switch (f)
    {
    case Format::BGRA8:
    case Format::BGRX8:
    case Format::RGBA8:
        return DkImageFormat_RGBA8_Unorm;
    case Format::B5G6R5:
        return DkImageFormat_RGB565_Unorm;
    case Format::B5G5R5A1:
        return DkImageFormat_RGB5A1_Unorm;
    case Format::R8:
        return DkImageFormat_R8_Unorm;
    case Format::RG8:
        return DkImageFormat_RG8_Unorm;
    case Format::RG16:
        return DkImageFormat_RG16_Unorm;
    case Format::RGBA16:
        return DkImageFormat_RGBA16_Unorm;
    case Format::R16F:
        return DkImageFormat_R16_Float;
    case Format::RG16F:
        return DkImageFormat_RG16_Float;
    case Format::RGBA16F:
        return DkImageFormat_RGBA16_Float;
    case Format::R32F:
        return DkImageFormat_R32_Float;
    case Format::BC1:
        return DkImageFormat_RGBA_BC1;
    case Format::BC2:
        return DkImageFormat_RGBA_BC2;
    case Format::BC3:
        return DkImageFormat_RGBA_BC3;
    case Format::D16:
        return DkImageFormat_Z16;
    case Format::D24S8:
        return DkImageFormat_Z24S8;
    default:
        return DkImageFormat_RGBA8_Unorm;
    }
}

DkBlendFactor MapFactor(Blend b)
{
    switch (b)
    {
    case Blend::Zero: return DkBlendFactor_Zero;
    case Blend::One: return DkBlendFactor_One;
    case Blend::SrcColor: return DkBlendFactor_SrcColor;
    case Blend::InvSrcColor: return DkBlendFactor_InvSrcColor;
    case Blend::SrcAlpha: return DkBlendFactor_SrcAlpha;
    case Blend::InvSrcAlpha: return DkBlendFactor_InvSrcAlpha;
    case Blend::DstAlpha: return DkBlendFactor_DstAlpha;
    case Blend::InvDstAlpha: return DkBlendFactor_InvDstAlpha;
    case Blend::DstColor: return DkBlendFactor_DstColor;
    case Blend::InvDstColor: return DkBlendFactor_InvDstColor;
    case Blend::SrcAlphaSat: return DkBlendFactor_SrcAlphaSaturate;
    case Blend::Constant: return DkBlendFactor_ConstColor;
    case Blend::InvConstant: return DkBlendFactor_InvConstColor;
    }
    return DkBlendFactor_One;
}

DkBlendOp MapBlendOp(BlendOp o)
{
    switch (o)
    {
    case BlendOp::Add: return DkBlendOp_Add;
    case BlendOp::Subtract: return DkBlendOp_Sub;
    case BlendOp::RevSubtract: return DkBlendOp_RevSub;
    case BlendOp::Min: return DkBlendOp_Min;
    case BlendOp::Max: return DkBlendOp_Max;
    }
    return DkBlendOp_Add;
}

DkCompareOp MapCompare(Compare c)
{
    switch (c)
    {
    case Compare::Never: return DkCompareOp_Never;
    case Compare::Less: return DkCompareOp_Less;
    case Compare::Equal: return DkCompareOp_Equal;
    case Compare::LessEqual: return DkCompareOp_Lequal;
    case Compare::Greater: return DkCompareOp_Greater;
    case Compare::NotEqual: return DkCompareOp_NotEqual;
    case Compare::GreaterEqual: return DkCompareOp_Gequal;
    case Compare::Always: return DkCompareOp_Always;
    }
    return DkCompareOp_Always;
}

DkStencilOp MapStencil(StencilOp o)
{
    switch (o)
    {
    case StencilOp::Keep: return DkStencilOp_Keep;
    case StencilOp::Zero: return DkStencilOp_Zero;
    case StencilOp::Replace: return DkStencilOp_Replace;
    case StencilOp::IncrSat: return DkStencilOp_Incr;
    case StencilOp::DecrSat: return DkStencilOp_Decr;
    case StencilOp::Invert: return DkStencilOp_Invert;
    case StencilOp::Incr: return DkStencilOp_IncrWrap;
    case StencilOp::Decr: return DkStencilOp_DecrWrap;
    }
    return DkStencilOp_Keep;
}

DkPrimitive MapPrimitive(Primitive p)
{
    switch (p)
    {
    case Primitive::PointList: return DkPrimitive_Points;
    case Primitive::LineList: return DkPrimitive_Lines;
    case Primitive::LineStrip: return DkPrimitive_LineStrip;
    case Primitive::TriangleList: return DkPrimitive_Triangles;
    case Primitive::TriangleStrip: return DkPrimitive_TriangleStrip;
    case Primitive::TriangleFan: return DkPrimitive_TriangleFan;
    }
    return DkPrimitive_Triangles;
}

void MapAttrib(AttribType t, DkVtxAttribSize *size, DkVtxAttribType *type, bool *bgra)
{
    *bgra = false;
    switch (t)
    {
    case AttribType::Float1: *size = DkVtxAttribSize_1x32; *type = DkVtxAttribType_Float; return;
    case AttribType::Float2: *size = DkVtxAttribSize_2x32; *type = DkVtxAttribType_Float; return;
    case AttribType::Float3: *size = DkVtxAttribSize_3x32; *type = DkVtxAttribType_Float; return;
    case AttribType::Float4: *size = DkVtxAttribSize_4x32; *type = DkVtxAttribType_Float; return;
    case AttribType::UByte4: *size = DkVtxAttribSize_4x8; *type = DkVtxAttribType_Uscaled; return;
    case AttribType::UByte4N: *size = DkVtxAttribSize_4x8; *type = DkVtxAttribType_Unorm; return;
    case AttribType::UByte4NBGRA: *size = DkVtxAttribSize_4x8; *type = DkVtxAttribType_Unorm; *bgra = true; return;
    case AttribType::Short2: *size = DkVtxAttribSize_2x16; *type = DkVtxAttribType_Sscaled; return;
    case AttribType::Short4: *size = DkVtxAttribSize_4x16; *type = DkVtxAttribType_Sscaled; return;
    case AttribType::Short2N: *size = DkVtxAttribSize_2x16; *type = DkVtxAttribType_Snorm; return;
    case AttribType::Short4N: *size = DkVtxAttribSize_4x16; *type = DkVtxAttribType_Snorm; return;
    case AttribType::UShort2N: *size = DkVtxAttribSize_2x16; *type = DkVtxAttribType_Unorm; return;
    case AttribType::UShort4N: *size = DkVtxAttribSize_4x16; *type = DkVtxAttribType_Unorm; return;
    case AttribType::Half2: *size = DkVtxAttribSize_2x16; *type = DkVtxAttribType_Float; return;
    case AttribType::Half4: *size = DkVtxAttribSize_4x16; *type = DkVtxAttribType_Float; return;
    }
    *size = DkVtxAttribSize_4x32;
    *type = DkVtxAttribType_Float;
}

// One backing store of a buffer. A buffer renames to another store when an upload must not reach memory that
// recorded or running work still reads (Upload::Discard / Upload::Ordered), as a D3D9 driver does.
struct BufferStore
{
    DkMemBlock mem = nullptr;
    DkGpuAddr gpu = 0;
    uint8_t *cpu = nullptr;
    uint32_t busySerial = 0; // the last frame that recorded a read of this store
};

struct Buffer : gb::Buffer
{
    DkMemBlock mem = nullptr; // the current store, which draws bind
    DkGpuAddr gpu = 0;
    uint32_t size = 0;
    BufferType type = BufferType::Vertex;
    mutable std::vector<BufferStore> stores; // empty for the uniform ring, which manages its own memory
    uint32_t cur = 0;
};

struct Texture : gb::Texture
{
    DkMemBlock mem = nullptr;
    DkImage image = {};
    DkImageLayout layout = {};
    TextureDesc desc = {};
    int descId = 0;        // index in the image-descriptor pool (0 = white placeholder)
    bool uploaded = false; // true once real pixels landed, or for a render target (sampled as-is)
    DkImageFormat dkFormat = DkImageFormat_RGBA8_Unorm; // the storage format actually used
};

struct Shader : gb::Shader
{
    DkShader shader = {};
    bool valid = false;
};

struct Sampler : gb::Sampler
{
    DkSampler sampler = {};
    int descId = 0; // index in the sampler-descriptor pool (0 = default sampler)
};

struct Query : gb::Query
{
    QueryType type = QueryType::Occlusion;
};

struct Pipeline : gb::Pipeline
{
    const Shader *vertex = nullptr;
    const Shader *pixel = nullptr;
    DkVtxAttribState attribs[MAX_ATTRIBS] = {};
    uint32_t attribCount = 0;
    uint32_t streamCount = 0;
    DkBlendState blend = {};
    DkColorState colorState = {};
    DkColorWriteState colorWrite = {};
    DkDepthStencilState depthStencil = {};
    DkRasterizerState raster = {};
    float biasConstant = 0.0f, biasSlope = 0.0f; // dkCmdBufSetDepthBias, when raster.depthBiasEnableMask is set
    uint8_t stencilReadMask = 0xFF, stencilWriteMask = 0xFF;
    Compare depthFunc = Compare::Always; // as the contract gave it (the frame trace logs it)
    bool scissorTest = false;            // RasterState::scissor: whether DrawState::scissor clips at all
    bool valid = false;
};

struct Swapchain : gb::Swapchain
{
    DkMemBlock fbMem = nullptr;
    DkImage fb[kFbCount] = {};
    DkSwapchain sc = nullptr;
    DkMemBlock cmdMem = nullptr;
    DkCmdBuf cmd = nullptr;
    DkFence fence = {};
    bool submitted = false;
    bool recording = false;
    uint16_t width = 0, height = 0;
};

Swapchain *s_swapchain = nullptr;

// Frame serials: the frame being recorded (or last recorded), and the last one the GPU is known to have finished
// (BeginFrame waits for the previous frame). Memory the engine frees waits in s_pendingFree until that point.
uint32_t s_frameSerial = 0, s_completedSerial = 0;
std::vector<DkMemBlock> s_pendingFree;
uint32_t s_renames = 0; // buffer renames so far (logged with Present)

void MarkRead(const Buffer *b)
{
    if (!b->stores.empty())
        b->stores[b->cur].busySerial = s_frameSerial;
}

// Per-frame uniform memory (bump, reset each frame), wrapped in one persistent Buffer the frontend's UniformSlice
// points into.
DkMemBlock s_uboMem = nullptr;
uint8_t *s_uboCpu = nullptr;
DkGpuAddr s_uboGpu = 0;
uint32_t s_uboNext = 0;
Buffer s_uboBuffer;

// Texture descriptor pools: each Texture and Sampler gets a persistent id (assigned at creation) that a draw turns
// into a handle with dkMakeTextureHandle. Id 0 is a white 1x1 placeholder image / default sampler, used for a null
// binding and for a sampled texture whose pixels have not been uploaded yet (so geometry is still visible).
constexpr uint32_t kImgPoolCap = 16384; // sampled textures + render targets a map creates
constexpr uint32_t kSmpPoolCap = 1024;
DkMemBlock s_dummyMem = nullptr;
DkImage s_dummyImage = {};
DkMemBlock s_imgPoolMem = nullptr, s_smpPoolMem = nullptr;
uint8_t *s_imgPoolCpu = nullptr, *s_smpPoolCpu = nullptr;
DkGpuAddr s_imgDescGpu = 0, s_smpDescGpu = 0;
uint32_t s_imgNext = 1, s_smpNext = 1; // id 0 reserved for the placeholder

DkImageSwizzle MapSwizzle(Source s)
{
    switch (s)
    {
    case Source::R: return DkImageSwizzle_Red;
    case Source::G: return DkImageSwizzle_Green;
    case Source::B: return DkImageSwizzle_Blue;
    case Source::A: return DkImageSwizzle_Alpha;
    case Source::Zero: return DkImageSwizzle_Zero;
    case Source::One: return DkImageSwizzle_One;
    }
    return DkImageSwizzle_Zero;
}

// `swizzle` (sampled textures only): how the shader's channels read the stored ones -- the contract's way of serving
// D3DFMT_L8 / A8 / A8L8 from R8 / RG8 (gfx_backend.h).
void WriteImageDesc(uint32_t id, const DkImage *img, const Swizzle *swizzle = nullptr)
{
    DkImageView view;
    dkImageViewDefaults(&view, img);
    if (swizzle)
    {
        view.swizzle[0] = MapSwizzle(swizzle->r);
        view.swizzle[1] = MapSwizzle(swizzle->g);
        view.swizzle[2] = MapSwizzle(swizzle->b);
        view.swizzle[3] = MapSwizzle(swizzle->a);
    }
    dkImageDescriptorInitialize(reinterpret_cast<DkImageDescriptor *>(s_imgPoolCpu) + id, &view, false, false);
}

// Texture upload: a staging buffer the pixels are repacked into (tight, no row padding), copied into the image on a
// dedicated command buffer. Synchronous for now (submit + wait per upload); batch if load time bites.
constexpr uint32_t kStageCap = 32 << 20;
constexpr uint32_t kUpCmdSize = 64 << 10;
DkMemBlock s_stageMem = nullptr, s_upCmdMem = nullptr;
uint8_t *s_stageCpu = nullptr;
DkGpuAddr s_stageGpu = 0;
DkCmdBuf s_upCmd = nullptr;

std::unordered_map<std::string, Pipeline *> s_pipelines;
std::unordered_map<std::string, Sampler *> s_samplers;

void FillCaps()
{
    s_caps = {};
    s_caps.device = "deko3d (Tegra X1, Maxwell)";
    s_caps.maxTextureSize = 16384;
    s_caps.maxSamples = 1;
    s_caps.memoryBytes = 0;
    for (int f = 1; f < static_cast<int>(Format::Count); ++f)
    {
        const Format fmt = static_cast<Format>(f);
        uint8_t flags = FORMAT_SAMPLED | FORMAT_FILTERABLE;
        flags |= (fmt == Format::D16 || fmt == Format::D24S8) ? FORMAT_DEPTH_TARGET : FORMAT_COLOR_TARGET;
        s_caps.formatFlags[f] = flags;
    }
}

int s_dbgPass = 0, s_dbgDraw = 0, s_dbgPresent = 0; // log the first few of each to find where a frame hangs
int s_drawTotal = 0; // cumulative DrawIndexed calls, logged at each Present to read draws-per-frame

// One-shot framebuffer dump: copy the presented image to a buffer and write it as a PPM on the SD, to see offline
// what actually reaches the screen (Ryujinx's GL/Vulkan surface is not capturable by a host screenshot). Debug only.
DkMemBlock s_dumpMem = nullptr;
bool s_dumpPending = false, s_dumpBgra = false;
uint32_t s_dumpW = 0, s_dumpH = 0, s_dumpLabel = 0, s_dumpIdx = 0;
// Seconds since the first Present at which the presented image is dumped, and the one-frame trace runs. Both only
// run when sdmc:/switch/bo1zm/deko3d_debug exists: each stalls a frame, and only an emulator's log receives what
// they print.
const uint32_t kDumpAt[] = {15, 30, 150, 210, 270};
constexpr uint32_t kTraceAt = 20;
bool s_debugCaptures = false;

// The current pass's attachment size and colour-target count (Clear covers them), and a one-frame trace of a world
// frame's passes, draws and clears (debug).
uint32_t s_passW = 0, s_passH = 0, s_passColors = 0;
int s_passIdx = 0, s_passDraws = 0;
bool s_frameTrace = false, s_frameTraceDone = false;

// The colour target of a few passes of the traced frame, copied out and written as PPMs after its Present (debug).
struct PassCap
{
    uint32_t offset, w, h, bpp;
    int pass;
    bool bgra;
};
constexpr uint32_t kPassCapSize = 4 * 1280 * 720 * 4;
DkMemBlock s_passCapMem = nullptr;
std::vector<PassCap> s_passCaps;
uint32_t s_passCapNext = 0;
bool s_passCapPending = false;
const Texture *s_passColor0 = nullptr;

// Dumps go out through svcOutputDebugString, as hex lines between "IMG WxH label" and "IMGEND label", downscaled 4x:
// on Ryujinx every SD write fails with EIO once a map is loaded (the engine's own boot.log stops there too), while
// the debug string always reaches the emulator's log. Reassemble on the PC.
void LogImage(const char *label, const uint8_t *px, uint32_t w, uint32_t h, bool bgra, uint32_t bpp = 4,
              uint32_t kStep = 4, uint32_t x0 = 0, uint32_t y0 = 0, uint32_t cw = 0, uint32_t chgt = 0)
{
    if (!cw || !chgt)
        cw = w, chgt = h;
    const uint32_t ow = cw / kStep, oh = chgt / kStep;
    const uint32_t hi = bpp == 8 ? 1 : 0, ch = bpp == 8 ? 2 : 1; // RGBA16: keep each channel's high byte
    Log("IMG %ux%u %s", ow, oh, label);
    static const char hex[] = "0123456789abcdef";
    char line[512];
    const int prefix = snprintf(line, sizeof(line), "[gb-deko3d] IMGD ");
    int n = prefix;
    for (uint32_t y = 0; y < oh; ++y)
        for (uint32_t x = 0; x < ow; ++x)
        {
            const uint8_t *p = px + ((y0 + size_t(y) * kStep) * w + x0 + size_t(x) * kStep) * bpp;
            const uint8_t rgb[3] = {p[(bgra ? 2 : 0) * ch + hi], p[1 * ch + hi], p[(bgra ? 0 : 2) * ch + hi]};
            for (uint8_t c : rgb)
            {
                line[n++] = hex[c >> 4];
                line[n++] = hex[c & 15];
            }
            if (n > 480)
            {
                svcOutputDebugString(line, size_t(n));
                n = prefix;
            }
        }
    if (n > prefix)
        svcOutputDebugString(line, size_t(n));
    Log("IMGEND %s", label);
}

void BeginFrame(Swapchain *s)
{
    if (s->recording)
        return;
    bool idle = true;
    if (s->submitted)
    {
        // A finite wait so a GPU fault degrades (the frame proceeds, logging continues) instead of freezing the app
        // on an infinite wait, which is how a bad draw used to look like a hang.
        const DkResult r = dkFenceWait(&s->fence, 2000000000); // 2 s
        if (r != DkResult_Success)
            Log("fence wait result %d (GPU may have faulted on the previous frame), queue %s", int(r),
                dkQueueIsInErrorState(s_queue) ? "in error state" : "not in error state");
        idle = r == DkResult_Success;
    }
    // Only once the previous frame is known finished: its stores become free and the memory freed meanwhile goes.
    if (idle)
    {
        s_completedSerial = s_frameSerial;
        for (DkMemBlock m : s_pendingFree)
            FreeBlock(m);
        s_pendingFree.clear();
    }
    ++s_frameSerial;
    dkCmdBufClear(s->cmd);
    dkCmdBufAddMemory(s->cmd, s->cmdMem, 0, kCmdMemSize);
    s_uboNext = 0;
    // Since the last frame the CPU rewrote descriptors (an upload repoints a texture from the placeholder to its
    // image), texel data (uploads) and the uniform ring / dynamic buffers: drop the GPU caches that could hold the old
    // contents before this frame reads them.
    dkCmdBufBarrier(s->cmd, DkBarrier_None,
                    DkInvalidateFlags_Image | DkInvalidateFlags_Descriptors | DkInvalidateFlags_Shader |
                        DkInvalidateFlags_L2Cache);
    // Bind the descriptor pools for the whole frame; every draw's textures index into them by id.
    dkCmdBufBindImageDescriptorSet(s->cmd, s_imgDescGpu, kImgPoolCap);
    dkCmdBufBindSamplerDescriptorSet(s->cmd, s_smpDescGpu, kSmpPoolCap);
    s->recording = true;
}
} // namespace

bool Init()
{
    DkDeviceMaker deviceMaker;
    dkDeviceMakerDefaults(&deviceMaker);
    deviceMaker.cbDebug = &DebugCallback; // no-op with the release libdeko3d; validates with libdeko3dd
    s_device = dkDeviceCreate(&deviceMaker);
    if (!s_device)
    {
        Log("dkDeviceCreate failed");
        return false;
    }
    DkQueueMaker queueMaker; // the defaults (graphics + compute + zcull + scratch) suit a full render path
    dkQueueMakerDefaults(&queueMaker, s_device);
    s_queue = dkQueueCreate(&queueMaker);
    if (!s_queue)
    {
        Log("dkQueueCreate failed");
        dkDeviceDestroy(s_device);
        s_device = nullptr;
        return false;
    }
    FillCaps();
    if (FILE *f = std::fopen("sdmc:/switch/bo1zm/deko3d_debug", "rb"))
    {
        std::fclose(f);
        s_debugCaptures = true;
    }
    Log("device up (%s), heap %.1f MB%s", s_caps.device, HeapMB(),
        s_debugCaptures ? "; timed frame dumps and trace on (deko3d_debug)" : "");
    return true;
}

void Shutdown()
{
    if (s_queue)
    {
        dkQueueWaitIdle(s_queue);
        dkQueueDestroy(s_queue);
        s_queue = nullptr;
    }
    for (DkMemBlock b : s_codeBlocks)
        FreeBlock(b);
    s_codeBlocks.clear();
    s_codeOffset = 0;
    if (s_device)
    {
        dkDeviceDestroy(s_device);
        s_device = nullptr;
    }
}

const Caps &GetCaps()
{
    return s_caps;
}

const char *Name()
{
    return "deko3d";
}

// ---- resources

namespace
{
bool MakeStore(uint32_t size, BufferStore *store)
{
    store->mem = MakeBlock(size, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    if (!store->mem)
        return false;
    store->gpu = dkMemBlockGetGpuAddr(store->mem);
    store->cpu = static_cast<uint8_t *>(dkMemBlockGetCpuAddr(store->mem));
    return true;
}
} // namespace

gb::Buffer *CreateBuffer(const BufferDesc &desc)
{
    Buffer *b = new Buffer();
    b->size = desc.size;
    b->type = desc.type;
    BufferStore store;
    if (!MakeStore(desc.size, &store))
    {
        delete b;
        return nullptr;
    }
    b->stores.push_back(store);
    b->mem = store.mem;
    b->gpu = store.gpu;
    return b;
}

void DestroyBuffer(gb::Buffer *buffer)
{
    Buffer *b = static_cast<Buffer *>(buffer);
    if (!b)
        return;
    // Recorded or running work may still read it: free the memory once the GPU is past this frame.
    for (const BufferStore &store : b->stores)
        s_pendingFree.push_back(store.mem);
    delete b;
}

void UploadBuffer(gb::Buffer *buffer, uint32_t offset, const void *data, uint32_t size, Upload mode)
{
    Buffer *b = static_cast<Buffer *>(buffer);
    if (!b || b->stores.empty() || offset + size > b->size)
        return;
    // The whole frame is recorded before it is submitted, so a draw recorded earlier in this frame (or a frame still
    // running) reads the current store when the GPU gets to it, after this write. Discard (old contents dead to the
    // caller) and Ordered (earlier work keeps the old contents) therefore go to a store nothing reads, which later
    // draws bind; NoOverwrite promises the range is not read, so it writes in place.
    if (mode != Upload::NoOverwrite && b->stores[b->cur].busySerial > s_completedSerial)
    {
        uint32_t next = uint32_t(b->stores.size());
        for (uint32_t i = 0; i < b->stores.size(); ++i)
            if (i != b->cur && b->stores[i].busySerial <= s_completedSerial)
            {
                next = i;
                break;
            }
        BufferStore fresh;
        if (next == b->stores.size() && MakeStore(b->size, &fresh))
            b->stores.push_back(fresh);
        if (next < b->stores.size())
        {
            if (mode == Upload::Ordered)
                std::memcpy(b->stores[next].cpu, b->stores[b->cur].cpu, b->size);
            b->cur = next;
            b->mem = b->stores[next].mem;
            b->gpu = b->stores[next].gpu;
            ++s_renames;
        }
        else
            Log("UploadBuffer: no memory to rename a %u-byte buffer; writing in place", b->size);
    }
    if (s_frameTrace)
        Log("trace up: buf %p size %u mode %d off %u len %u -> store %u/%u busy %u done %u", (void *)b, b->size,
            int(mode), offset, size, b->cur, uint32_t(b->stores.size()), b->stores[b->cur].busySerial,
            s_completedSerial);
    std::memcpy(b->stores[b->cur].cpu + offset, data, size);
}

gb::Texture *CreateTexture(const TextureDesc &desc)
{
    Texture *t = new Texture();
    t->desc = desc;
    DkImageLayoutMaker lm;
    dkImageLayoutMakerDefaults(&lm, s_device);
    lm.type = desc.type == TextureType::Tex3D ? DkImageType_3D
              : desc.type == TextureType::Cube ? DkImageType_Cubemap
                                               : DkImageType_2D;
    lm.flags = (desc.usage == Usage::RenderTarget || desc.usage == Usage::DepthStencil)
                   ? (DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine)
                   : 0;
    lm.format = MapFormat(desc.format);
    // Maxwell cannot use the packed 16-bit colour formats as a render target; render into RGBA8 instead (same
    // colours, 32-bit). A texture is sampled through this same image, so the substitution stays consistent.
    if ((lm.flags & DkImageFlags_UsageRender) &&
        (lm.format == DkImageFormat_RGB565_Unorm || lm.format == DkImageFormat_RGB5A1_Unorm))
        lm.format = DkImageFormat_RGBA8_Unorm;
    // A sampled BGRA8/BGRX8 texture is uploaded in the D3D byte order (B, G, R, A): store it in the matching format so
    // sampling returns the right channels (the GL backend converts with GL_BGRA). Render targets keep RGBA8: their
    // storage order is invisible to sampling, and RGBA8 is the target format proven to work here.
    if (!(lm.flags & DkImageFlags_UsageRender))
    {
        if (desc.format == Format::BGRA8)
            lm.format = DkImageFormat_BGRA8_Unorm;
        else if (desc.format == Format::BGRX8)
            lm.format = DkImageFormat_BGRX8_Unorm;
    }
    t->dkFormat = lm.format;
    lm.dimensions[0] = desc.width;
    lm.dimensions[1] = desc.height;
    lm.dimensions[2] = desc.type == TextureType::Tex3D ? desc.depth : 1;
    lm.mipLevels = desc.levels ? desc.levels : 1;
    if (lm.flags & DkImageFlags_UsageRender)
        Log("CreateTexture RT/DS: Format %d -> DkFmt %d, usage %d, %ux%u", int(desc.format), int(lm.format),
            int(desc.usage), desc.width, desc.height);
    dkImageLayoutInitialize(&t->layout, &lm);
    // Slack after the image (test): Ryujinx's DMA copy read-modify-writes a GOB-rounded destination range, which for
    // a small mip in the mip tail can run past the end of a tightly sized block into an unmapped page.
    constexpr uint32_t kTexSlack = 64 << 10;
    t->mem = MakeBlock(uint32_t(dkImageLayoutGetSize(&t->layout)) + kTexSlack,
                       DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);
    if (!t->mem)
    {
        delete t;
        return nullptr;
    }
    dkImageInitialize(&t->image, &t->layout, t->mem, 0);

    // Give the texture a descriptor id. A render target is sampled as rendered (the final composite reads the scene
    // this way), so its descriptor points at its own image. A sampled texture points at the white placeholder until
    // UploadTexture fills it, so an un-uploaded texture reads white (visible) rather than its empty (black) memory.
    t->uploaded = (lm.flags & DkImageFlags_UsageRender) != 0;
    t->descId = s_imgNext < kImgPoolCap ? s_imgNext++ : 0;
    if (t->uploaded)
        WriteImageDesc(t->descId, &t->image, &t->desc.swizzle);
    else
        WriteImageDesc(t->descId, &s_dummyImage);
    return t;
}

void DestroyTexture(gb::Texture *texture)
{
    Texture *t = static_cast<Texture *>(texture);
    if (!t)
        return;
    if (t->mem) // recorded or running work may still sample it: free once the GPU is past this frame
        s_pendingFree.push_back(t->mem);
    delete t;
}

void UploadTexture(gb::Texture *handle, uint8_t level, uint8_t layer, const Box &box, const void *data,
                   uint32_t rowPitch, uint32_t slicePitch)
{
    Texture *t = static_cast<Texture *>(handle);
    if (!t || !data || !s_stageCpu)
        return;
    const bool block = IsBlockFormat(t->desc.format);
    const uint32_t depth = box.depth ? box.depth : 1;
    const uint32_t rows = block ? (box.height + 3u) / 4u : box.height;
    const uint32_t rowBytes = (block ? (box.width + 3u) / 4u : box.width) * FormatBytes(t->desc.format);
    const uint32_t total = rowBytes * rows * depth;
    if (total == 0 || total > kStageCap)
        return;
    // Repack tightly into the staging buffer (drop the source's row/slice padding), as the GL backend does.
    for (uint32_t z = 0; z < depth; ++z)
        for (uint32_t y = 0; y < rows; ++y)
            std::memcpy(s_stageCpu + (size_t(z) * rows + y) * rowBytes,
                        static_cast<const uint8_t *>(data) + size_t(z) * slicePitch + size_t(y) * rowPitch, rowBytes);

    static int updbg = 0;
    if (updbg < 5 || updbg % 500 == 0)
        Log("upload #%d: fmt %d %ux%u L%u total=%u", updbg, int(t->desc.format), t->desc.width, t->desc.height, level,
            total);
    ++updbg;

    DkImageView view;
    dkImageViewDefaults(&view, &t->image);
    view.mipLevelOffset = level;
    view.layerOffset = layer;
    const DkImageRect rect = {box.x, box.y, box.z, box.width, box.height, depth};
    const DkCopyBuf srcBuf = {s_stageGpu, 0, 0}; // tightly packed -> row length / image height = the region's
    // The staging block is reused at the same address and the CPU just rewrote it: drop the GPU's L2 copy of the
    // previous upload's data.
    dkCmdBufBarrier(s_upCmd, DkBarrier_None, DkInvalidateFlags_L2Cache);
    dkCmdBufCopyBufferToImage(s_upCmd, &srcBuf, &view, &rect, 0);
    dkQueueSubmitCommands(s_queue, dkCmdBufFinishList(s_upCmd));
    WaitQueue("upload");
    dkCmdBufClear(s_upCmd); // reset recording for the next upload (keeps the memory added once at init)

    if (!t->uploaded) // now it has real pixels: point its descriptor at its own image instead of the placeholder
    {
        WriteImageDesc(t->descId, &t->image, &t->desc.swizzle);
        t->uploaded = true;
    }
}

const gb::Sampler *GetSampler(const SamplerDesc &desc)
{
    std::string key(reinterpret_cast<const char *>(&desc), sizeof(desc));
    Sampler *&slot = s_samplers[key];
    if (slot)
        return slot;
    slot = new Sampler();
    dkSamplerDefaults(&slot->sampler);
    slot->sampler.minFilter = desc.minFilter == Filter::Linear ? DkFilter_Linear : DkFilter_Nearest;
    slot->sampler.magFilter = desc.magFilter == Filter::Linear ? DkFilter_Linear : DkFilter_Nearest;
    slot->sampler.mipFilter = desc.mipFilter == MipFilter::None      ? DkMipFilter_None
                              : desc.mipFilter == MipFilter::Nearest ? DkMipFilter_Nearest
                                                                     : DkMipFilter_Linear;
    const Address addr[3] = {desc.addressU, desc.addressV, desc.addressW};
    for (int i = 0; i < 3; ++i)
        slot->sampler.wrapMode[i] = addr[i] == Address::Wrap         ? DkWrapMode_Repeat
                                    : addr[i] == Address::Mirror     ? DkWrapMode_MirroredRepeat
                                    : addr[i] == Address::Clamp      ? DkWrapMode_ClampToEdge
                                    : addr[i] == Address::Border     ? DkWrapMode_ClampToBorder
                                                                     : DkWrapMode_MirrorClampToEdge;
    slot->sampler.maxAnisotropy = float(desc.maxAnisotropy ? desc.maxAnisotropy : 1);
    if (desc.depthCompare)
    {
        slot->sampler.compareEnable = true;
        slot->sampler.compareOp = DkCompareOp_Lequal;
    }
    slot->descId = s_smpNext < kSmpPoolCap ? s_smpNext++ : 0;
    dkSamplerDescriptorInitialize(reinterpret_cast<DkSamplerDescriptor *>(s_smpPoolCpu) + slot->descId,
                                  &slot->sampler);
    return slot;
}

gb::Shader *CreateShader(Stage, uint64_t, const char *code, uint32_t size)
{
    Shader *sh = new Shader();
    const uint32_t need = AlignUp(size, DK_SHADER_CODE_ALIGNMENT);
    if (s_codeBlocks.empty() || s_codeOffset + need > kCodeChunk)
    {
        DkMemBlock block = MakeBlock(need > kCodeChunk ? need : kCodeChunk,
                                     DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code);
        if (!block)
        {
            delete sh;
            return nullptr;
        }
        s_codeBlocks.push_back(block);
        s_codeOffset = 0;
    }
    DkMemBlock block = s_codeBlocks.back();
    const uint32_t offset = s_codeOffset;
    std::memcpy(static_cast<uint8_t *>(dkMemBlockGetCpuAddr(block)) + offset, code, size);
    s_codeOffset += need;
    DkShaderMaker maker;
    dkShaderMakerDefaults(&maker, block, offset);
    dkShaderInitialize(&sh->shader, &maker);
    sh->valid = dkShaderIsValid(&sh->shader);
    return sh;
}

void DestroyShader(gb::Shader *shader)
{
    delete static_cast<Shader *>(shader);
}

const gb::Pipeline *GetPipeline(const PipelineDesc &desc)
{
    static int dbg = 0;
    if (dbg < 8)
        Log("GetPipeline #%d (vs %p ps %p)", dbg++, (const void *)desc.vertex, (const void *)desc.pixel);
    std::string key(reinterpret_cast<const char *>(&desc), sizeof(desc));
    Pipeline *&slot = s_pipelines[key];
    if (slot)
        return slot->valid ? slot : nullptr;
    const Shader *vs = static_cast<const Shader *>(desc.vertex);
    const Shader *ps = static_cast<const Shader *>(desc.pixel);
    slot = new Pipeline();
    slot->vertex = vs;
    slot->pixel = ps;
    slot->valid = vs && ps && vs->valid && ps->valid;

    // deko3d binds a dense array indexed by attribute location, and every slot in [0, attribCount) must be valid --
    // the shader reads the inputs at its own locations, which are sparse. Default the gaps to a valid fixed attribute
    // (not read from any vertex buffer) and overwrite the ones the layout actually uses.
    uint32_t streams = 0, maxLoc = 0;
    for (int i = 0; i < MAX_ATTRIBS; ++i)
    {
        slot->attribs[i].bufferId = 0;
        slot->attribs[i].isFixed = 1;
        slot->attribs[i].offset = 0;
        slot->attribs[i].size = DkVtxAttribSize_1x32;
        slot->attribs[i].type = DkVtxAttribType_Float;
        slot->attribs[i].isBgra = 0;
    }
    for (uint8_t i = 0; i < desc.attribCount && i < MAX_ATTRIBS; ++i)
    {
        const VertexAttrib &a = desc.attribs[i];
        if (a.location >= MAX_ATTRIBS)
            continue;
        DkVtxAttribSize size;
        DkVtxAttribType type;
        bool bgra;
        MapAttrib(a.type, &size, &type, &bgra);
        DkVtxAttribState &s = slot->attribs[a.location];
        s.bufferId = a.stream;
        s.isFixed = 0;
        s.offset = a.offset;
        s.size = size;
        s.type = type;
        s.isBgra = bgra ? 1 : 0;
        if (a.location + 1u > maxLoc)
            maxLoc = a.location + 1u;
        if (a.stream + 1u > streams)
            streams = a.stream + 1u;
    }
    slot->attribCount = maxLoc;
    slot->streamCount = streams;

    dkRasterizerStateDefaults(&slot->raster);
    slot->raster.cullMode = desc.raster.cull == Cull::Front ? DkFace_Front
                            : desc.raster.cull == Cull::Back ? DkFace_Back
                                                             : DkFace_None;
    slot->raster.polygonModeFront = slot->raster.polygonModeBack =
        desc.raster.fill == Fill::Wireframe ? DkPolygonMode_Line : DkPolygonMode_Fill;
    slot->raster.frontFace = DkFrontFace_CW; // D3D9: clockwise faces front (gfx_backend.h)
    if (desc.raster.depthBias != 0.0f || desc.raster.slopeScaledDepthBias != 0.0f)
    {
        // D3DRS_DEPTHBIAS is a depth offset; deko3d's constant factor, like glPolygonOffset's units, counts steps of
        // the depth format.
        slot->raster.depthBiasEnableMask = DkPolygonFlag_All;
        slot->biasConstant = desc.raster.depthBias * (desc.depthFormat == Format::D16 ? 65536.0f : 16777216.0f);
        slot->biasSlope = desc.raster.slopeScaledDepthBias;
    }

    dkColorStateDefaults(&slot->colorState);
    dkColorWriteStateDefaults(&slot->colorWrite);
    dkColorStateSetBlendEnable(&slot->colorState, 0, desc.blend.enable);
    dkColorWriteStateSetMask(&slot->colorWrite, 0, desc.blend.writeMask);
    dkBlendStateDefaults(&slot->blend);
    dkBlendStateSetOps(&slot->blend, MapBlendOp(desc.blend.opColor), MapBlendOp(desc.blend.opAlpha));
    dkBlendStateSetFactors(&slot->blend, MapFactor(desc.blend.srcColor), MapFactor(desc.blend.dstColor),
                           MapFactor(desc.blend.srcAlpha), MapFactor(desc.blend.dstAlpha));

    dkDepthStencilStateDefaults(&slot->depthStencil);
    slot->depthStencil.depthTestEnable = desc.depthStencil.depthTest;
    slot->depthStencil.depthWriteEnable = desc.depthStencil.depthWrite;
    slot->depthStencil.depthCompareOp = MapCompare(desc.depthStencil.depthFunc);
    slot->depthFunc = desc.depthStencil.depthFunc;
    slot->scissorTest = desc.raster.scissor;
    slot->depthStencil.stencilTestEnable = desc.depthStencil.stencil;
    slot->depthStencil.stencilFrontFailOp = MapStencil(desc.depthStencil.front.fail);
    slot->depthStencil.stencilFrontDepthFailOp = MapStencil(desc.depthStencil.front.depthFail);
    slot->depthStencil.stencilFrontPassOp = MapStencil(desc.depthStencil.front.pass);
    slot->depthStencil.stencilFrontCompareOp = MapCompare(desc.depthStencil.front.func);
    slot->depthStencil.stencilBackFailOp = MapStencil(desc.depthStencil.back.fail);
    slot->depthStencil.stencilBackDepthFailOp = MapStencil(desc.depthStencil.back.depthFail);
    slot->depthStencil.stencilBackPassOp = MapStencil(desc.depthStencil.back.pass);
    slot->depthStencil.stencilBackCompareOp = MapCompare(desc.depthStencil.back.func);
    slot->stencilReadMask = desc.depthStencil.readMask;
    slot->stencilWriteMask = desc.depthStencil.writeMask;
    return slot->valid ? slot : nullptr;
}

// ---- swapchain + frame

gb::Swapchain *CreateSwapchain(const SwapchainDesc &desc)
{
    Swapchain *s = new Swapchain();
    s->width = desc.width ? desc.width : 1280;
    s->height = desc.height ? desc.height : 720;

    DkImageLayoutMaker lm;
    dkImageLayoutMakerDefaults(&lm, s_device);
    lm.flags = DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_Usage2DEngine;
    lm.format = DkImageFormat_RGBA8_Unorm;
    lm.dimensions[0] = s->width;
    lm.dimensions[1] = s->height;
    DkImageLayout fbLayout;
    dkImageLayoutInitialize(&fbLayout, &lm);
    const uint32_t fbSize = AlignUp(dkImageLayoutGetSize(&fbLayout), dkImageLayoutGetAlignment(&fbLayout));

    s->fbMem = MakeBlock(kFbCount * fbSize, DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);
    const DkImage *images[kFbCount];
    for (unsigned i = 0; i < kFbCount; ++i)
    {
        dkImageInitialize(&s->fb[i], &fbLayout, s->fbMem, i * fbSize);
        images[i] = &s->fb[i];
    }

    DkSwapchainMaker scMaker;
    dkSwapchainMakerDefaults(&scMaker, s_device, nwindowGetDefault(), images, kFbCount);
    s->sc = dkSwapchainCreate(&scMaker);
    if (!s->sc)
    {
        Log("dkSwapchainCreate failed (nwindowGetDefault already owned?)");
        FreeBlock(s->fbMem);
        delete s;
        return nullptr;
    }

    s->cmdMem = MakeBlock(kCmdMemSize, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    DkCmdBufMaker cbMaker;
    dkCmdBufMakerDefaults(&cbMaker, s_device);
    s->cmd = dkCmdBufCreate(&cbMaker);

    // Uniform ring (bump per frame).
    s_uboMem = MakeBlock(kUboRingSize, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    s_uboCpu = static_cast<uint8_t *>(dkMemBlockGetCpuAddr(s_uboMem));
    s_uboGpu = dkMemBlockGetGpuAddr(s_uboMem);
    s_uboBuffer.mem = s_uboMem;
    s_uboBuffer.gpu = s_uboGpu;
    s_uboBuffer.size = kUboRingSize;

    // Placeholder 1x1 texture (contents left as-is) + its image/sampler descriptors, bound to every slot for now.
    DkImageLayoutMaker dlm;
    dkImageLayoutMakerDefaults(&dlm, s_device);
    dlm.format = DkImageFormat_RGBA8_Unorm;
    dlm.dimensions[0] = 1;
    dlm.dimensions[1] = 1;
    DkImageLayout dLayout;
    dkImageLayoutInitialize(&dLayout, &dlm);
    s_dummyMem = MakeBlock(uint32_t(dkImageLayoutGetSize(&dLayout)),
                           DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);
    dkImageInitialize(&s_dummyImage, &dLayout, s_dummyMem, 0);
    // Fill the placeholder white+opaque (test): a black placeholder makes every textured sample black. White lets
    // lighting/vertex colour through so geometry is at least visible until per-material textures land.
    std::memset(dkMemBlockGetCpuAddr(s_dummyMem), 0xFF, uint32_t(dkImageLayoutGetSize(&dLayout)));

    // Descriptor pools. Id 0 = the white placeholder image + a default sampler; CreateTexture/GetSampler fill the
    // rest. Per draw, BindDraw turns each texture's id into a handle; BeginFrame binds the whole pools.
    s_imgPoolMem = MakeBlock(kImgPoolCap * sizeof(DkImageDescriptor),
                             DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    s_smpPoolMem = MakeBlock(kSmpPoolCap * sizeof(DkSamplerDescriptor),
                             DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    s_imgPoolCpu = static_cast<uint8_t *>(dkMemBlockGetCpuAddr(s_imgPoolMem));
    s_smpPoolCpu = static_cast<uint8_t *>(dkMemBlockGetCpuAddr(s_smpPoolMem));
    s_imgDescGpu = dkMemBlockGetGpuAddr(s_imgPoolMem);
    s_smpDescGpu = dkMemBlockGetGpuAddr(s_smpPoolMem);
    WriteImageDesc(0, &s_dummyImage);
    DkSampler dummySampler;
    dkSamplerDefaults(&dummySampler);
    dkSamplerDescriptorInitialize(reinterpret_cast<DkSamplerDescriptor *>(s_smpPoolCpu), &dummySampler);

    // Texture upload staging + its command buffer.
    s_stageMem = MakeBlock(kStageCap, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    s_stageCpu = static_cast<uint8_t *>(dkMemBlockGetCpuAddr(s_stageMem));
    s_stageGpu = dkMemBlockGetGpuAddr(s_stageMem);
    s_upCmdMem = MakeBlock(kUpCmdSize, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    DkCmdBufMaker upMaker;
    dkCmdBufMakerDefaults(&upMaker, s_device);
    s_upCmd = dkCmdBufCreate(&upMaker);
    dkCmdBufAddMemory(s_upCmd, s_upCmdMem, 0, kUpCmdSize); // once: dkCmdBufClear keeps the memory, only resets recording

    s_swapchain = s;
    Log("swapchain %ux%u, %u framebuffers, heap %.1f MB", s->width, s->height, kFbCount, HeapMB());
    return s;
}

void DestroySwapchain(gb::Swapchain *swapchain)
{
    Swapchain *s = static_cast<Swapchain *>(swapchain);
    if (!s)
        return;
    dkQueueWaitIdle(s_queue);
    if (s->cmd)
        dkCmdBufDestroy(s->cmd);
    if (s->sc)
        dkSwapchainDestroy(s->sc);
    if (s->cmdMem)
        FreeBlock(s->cmdMem);
    if (s->fbMem)
        FreeBlock(s->fbMem);
    if (s_swapchain == s)
        s_swapchain = nullptr;
    delete s;
}

void BeginPass(const PassDesc &desc)
{
    Swapchain *s = s_swapchain;
    if (!s)
        return;
    BeginFrame(s);
    if (s_dbgPass < 8)
        Log("BeginPass #%d: colors %d depth %d", s_dbgPass++, desc.colorCount, desc.depth ? 1 : 0);

    DkImageView colorViews[MAX_COLOR_TARGETS];
    const DkImageView *colorPtrs[MAX_COLOR_TARGETS];
    uint32_t colorCount = 0;
    s_passW = s_passH = 0;
    for (uint8_t i = 0; i < desc.colorCount; ++i)
    {
        Texture *t = static_cast<Texture *>(desc.color[i].texture);
        if (!t)
            continue;
        dkImageViewDefaults(&colorViews[colorCount], &t->image);
        colorPtrs[colorCount] = &colorViews[colorCount];
        ++colorCount;
        if (!s_passW)
            s_passW = t->desc.width, s_passH = t->desc.height;
    }
    DkImageView depthView;
    const DkImageView *depthPtr = nullptr;
    if (Texture *d = static_cast<Texture *>(desc.depth))
    {
        dkImageViewDefaults(&depthView, &d->image);
        depthPtr = &depthView;
        if (!s_passW)
            s_passW = d->desc.width, s_passH = d->desc.height;
    }
    s_passColors = colorCount;
    s_passDraws = 0;
    s_passColor0 = nullptr;
    for (uint8_t i = 0; i < desc.colorCount && !s_passColor0; ++i)
        s_passColor0 = static_cast<const Texture *>(desc.color[i].texture);
    if (s_frameTrace)
    {
        const Texture *c0 = static_cast<const Texture *>(desc.color[0].texture);
        Log("trace pass %d: %u color (fmt %d %ux%u) depth %d | clear color %d depth %d", s_passIdx, colorCount,
            c0 ? int(c0->desc.format) : -1, c0 ? c0->desc.width : 0, c0 ? c0->desc.height : 0, depthPtr ? 1 : 0,
            desc.colorCount && desc.color[0].load == LoadOp::Clear, depthPtr && desc.depthLoad == LoadOp::Clear);
    }
    const bool dbg = s_dbgPass <= 1;
    if (dbg)
        Log("  BeginPass: binding %u color + %s depth", colorCount, depthPtr ? "a" : "no");
    dkCmdBufBindRenderTargets(s->cmd, colorPtrs, colorCount, depthPtr);
    if (dbg)
        Log("  BeginPass: RT bound; setting viewport/scissor");

    // The pass's own attachment size (a shadow atlas can be larger than the screen): the load clears below cover it
    // whole, and each draw then sets its own viewport and scissor.
    const DkViewport viewport = {0.0f, 0.0f, float(s_passW), float(s_passH), 0.0f, 1.0f};
    const DkScissor scissor = {0, 0, s_passW, s_passH};
    dkCmdBufSetViewports(s->cmd, 0, &viewport, 1);
    dkCmdBufSetScissors(s->cmd, 0, &scissor, 1);
    if (dbg)
        Log("  BeginPass: viewport set; clearing");

    for (uint8_t i = 0, target = 0; i < desc.colorCount; ++i)
    {
        if (!desc.color[i].texture)
            continue;
        if (desc.color[i].load == LoadOp::Clear)
            dkCmdBufClearColorFloat(s->cmd, target, DkColorMask_RGBA, desc.color[i].clear[0], desc.color[i].clear[1],
                                    desc.color[i].clear[2], desc.color[i].clear[3]);
        ++target;
    }
    if (depthPtr && (desc.depthLoad == LoadOp::Clear || desc.stencilLoad == LoadOp::Clear))
        dkCmdBufClearDepthStencil(s->cmd, desc.depthLoad == LoadOp::Clear, desc.clearDepth,
                                  desc.stencilLoad == LoadOp::Clear ? 0xFF : 0x00, desc.clearStencil);
    if (dbg)
        Log("  BeginPass: done");
}

void EndPass()
{
    Swapchain *s = s_swapchain;
    if (s_frameTrace)
        Log("trace pass %d end: %d draws", s_passIdx, s_passDraws);
    const Texture *c = s_passColor0;
    const bool capPass = false; // pass numbers to capture (needs free dump files)
    const bool rgba8 = c && (c->dkFormat == DkImageFormat_RGBA8_Unorm || c->dkFormat == DkImageFormat_BGRA8_Unorm ||
                             c->dkFormat == DkImageFormat_BGRX8_Unorm);
    const bool rgba16 = c && c->dkFormat == DkImageFormat_RGBA16_Unorm;
    if (s_frameTrace && capPass && c && s && s->recording && (rgba8 || rgba16))
    {
        const uint32_t bpp = rgba16 ? 8 : 4;
        const uint32_t bytes = uint32_t(c->desc.width) * c->desc.height * bpp;
        if (!s_passCapMem)
            s_passCapMem = MakeBlock(kPassCapSize, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
        if (s_passCapMem && s_passCapNext + bytes <= kPassCapSize)
        {
            dkCmdBufBarrier(s->cmd, DkBarrier_Fragments, 0);
            DkImageView v;
            dkImageViewDefaults(&v, &c->image);
            const DkImageRect r = {0, 0, 0, c->desc.width, c->desc.height, 1};
            const DkCopyBuf dst = {dkMemBlockGetGpuAddr(s_passCapMem) + s_passCapNext, 0, 0};
            dkCmdBufCopyImageToBuffer(s->cmd, &v, &r, &dst, 0);
            s_passCaps.push_back({s_passCapNext, c->desc.width, c->desc.height, bpp, s_passIdx,
                                  c->dkFormat == DkImageFormat_BGRA8_Unorm || c->dkFormat == DkImageFormat_BGRX8_Unorm});
            s_passCapNext += AlignUp(bytes, DK_MEMBLOCK_ALIGNMENT);
        }
    }
    ++s_passIdx;
    // A later pass may sample what this one rendered (the bloom chain, the composite): order the fragments and drop
    // the texture cache's stale lines, as deko3d's deferred-shading example does before reading its G-buffer.
    if (s && s->recording)
        dkCmdBufBarrier(s->cmd, DkBarrier_Fragments, DkInvalidateFlags_Image);
}

void Clear(const Rect *rects, uint32_t count, bool color, bool depth, bool stencil, const float rgba[4],
           float depthValue, uint8_t stencilValue)
{
    Swapchain *s = s_swapchain;
    if (!s || !s->recording)
        return;
    if (s_frameTrace)
        Log("trace   clear: %u rects%s color %d depth %d stencil %d rgba %.2f %.2f %.2f %.2f", count,
            count ? "" : " (whole)", color, depth, stencil, rgba[0], rgba[1], rgba[2], rgba[3]);
    // Only these rects, whatever the last draw's scissor was: each rect (or the whole attachment, with none) becomes
    // the scissor that clips the clear. The next draw sets its own scissor again.
    const Rect whole = {0, 0, s_passW, s_passH};
    const uint32_t n = count ? count : 1;
    for (uint32_t r = 0; r < n; ++r)
    {
        const Rect &rc = count ? rects[r] : whole;
        const int32_t x0 = rc.x < 0 ? 0 : rc.x, y0 = rc.y < 0 ? 0 : rc.y;
        const int32_t x1 = rc.x + int32_t(rc.width), y1 = rc.y + int32_t(rc.height);
        if (x1 <= x0 || y1 <= y0)
            continue;
        const DkScissor sc = {uint32_t(x0), uint32_t(y0), uint32_t(x1 - x0), uint32_t(y1 - y0)};
        dkCmdBufSetScissors(s->cmd, 0, &sc, 1);
        if (color)
            for (uint32_t t = 0; t < s_passColors; ++t)
                dkCmdBufClearColorFloat(s->cmd, t, DkColorMask_RGBA, rgba[0], rgba[1], rgba[2], rgba[3]);
        if (depth || stencil)
            dkCmdBufClearDepthStencil(s->cmd, depth, depthValue, stencil ? 0xFF : 0x00, stencilValue);
    }
}

UniformSlice AllocUniforms(const void *data, uint32_t size)
{
    static int dbg = 0;
    if (dbg < 8)
        Log("AllocUniforms #%d size %u", dbg++, size);
    if (!s_uboMem || s_uboNext + AlignUp(size, DK_UNIFORM_BUF_ALIGNMENT) > kUboRingSize)
        return {};
    const uint32_t offset = s_uboNext;
    std::memcpy(s_uboCpu + offset, data, size);
    s_uboNext += AlignUp(size, DK_UNIFORM_BUF_ALIGNMENT);
    return {&s_uboBuffer, offset, size};
}

namespace
{
void BindDraw(const DrawState &state)
{
    static int dbgn = 0;
    const bool dbg = dbgn++ < 2;
    Swapchain *s = s_swapchain;
    const Pipeline *p = static_cast<const Pipeline *>(state.pipeline);
    DkCmdBuf cmd = s->cmd;
    if (s_frameTrace && s_passDraws == 1)
        Log("trace   first draw: depth test %d write %d func %d | blend %d mask 0x%x | cull %d | stencil %d | "
            "vp %.0f,%.0f %.0fx%.0f z %.2f-%.2f",
            int(p->depthStencil.depthTestEnable), int(p->depthStencil.depthWriteEnable), int(p->depthFunc),
            int(p->colorState.blendEnableMask & 1), unsigned(p->colorWrite.masks & 0xF), int(p->raster.cullMode),
            int(p->depthStencil.stencilTestEnable), state.viewport.x, state.viewport.y, state.viewport.width,
            state.viewport.height, state.viewport.minDepth, state.viewport.maxDepth);
    if (s_frameTrace && s_passDraws <= 12)
        for (int i = 0; i < MAX_TEXTURES; ++i)
            if (const Texture *t = static_cast<const Texture *>(state.textures[i].texture))
            {
                const Sampler *sm = static_cast<const Sampler *>(state.textures[i].sampler);
                const bool rt = t->desc.usage == Usage::RenderTarget || t->desc.usage == Usage::DepthStencil;
                Log("trace     d%d tex %d: id %d type %d %ux%u L%u fmt %d %s%s smp %d", s_passDraws, i, t->descId,
                    int(t->desc.type), t->desc.width, t->desc.height, unsigned(t->desc.levels),
                    int(t->desc.format), rt ? "RT" : "tex",
                    t->uploaded ? "" : " NOT-UPLOADED", sm ? sm->descId : -1);
            }

    const DkShader *shaders[2] = {&p->vertex->shader, &p->pixel->shader};
    dkCmdBufBindShaders(cmd, DkStageFlag_GraphicsMask, shaders, 2);
    if (dbg)
        Log("    BindDraw: shaders bound (attribs %u streams %u)", p->attribCount, p->streamCount);
    dkCmdBufBindRasterizerState(cmd, &p->raster);
    if (p->raster.depthBiasEnableMask)
        dkCmdBufSetDepthBias(cmd, p->biasConstant, 0.0f, p->biasSlope);
    dkCmdBufBindColorState(cmd, &p->colorState);
    dkCmdBufBindColorWriteState(cmd, &p->colorWrite);
    dkCmdBufBindDepthStencilState(cmd, &p->depthStencil);
    dkCmdBufBindBlendState(cmd, 0, &p->blend);
    dkCmdBufBindVtxAttribState(cmd, p->attribs, p->attribCount);

    DkVtxBufferState vbs[MAX_STREAMS] = {};
    for (uint32_t i = 0; i < p->streamCount && i < MAX_STREAMS; ++i)
        vbs[i].stride = state.vertices[i].stride;
    dkCmdBufBindVtxBufferState(cmd, vbs, p->streamCount);
    for (uint32_t i = 0; i < p->streamCount && i < MAX_STREAMS; ++i)
    {
        const Buffer *vb = static_cast<const Buffer *>(state.vertices[i].buffer);
        if (vb)
        {
            dkCmdBufBindVtxBuffer(cmd, i, vb->gpu + state.vertices[i].offset, vb->size - state.vertices[i].offset);
            MarkRead(vb);
        }
    }
    if (dbg)
        Log("    BindDraw: vtx bound; binding UBOs");

    if (const Buffer *vc = static_cast<const Buffer *>(state.vertexConstants.buffer))
        dkCmdBufBindUniformBuffer(cmd, DkStage_Vertex, 0, vc->gpu + state.vertexConstants.offset,
                                  AlignUp(state.vertexConstants.size, DK_UNIFORM_BUF_ALIGNMENT));
    if (const Buffer *pc = static_cast<const Buffer *>(state.pixelConstants.buffer))
        dkCmdBufBindUniformBuffer(cmd, DkStage_Fragment, 1, pc->gpu + state.pixelConstants.offset,
                                  AlignUp(state.pixelConstants.size, DK_UNIFORM_BUF_ALIGNMENT));

    if (dbg)
        Log("    BindDraw: UBOs bound; binding textures");
    // Each slot's texture+sampler by their pool ids; a null slot falls back to the white placeholder (id 0, 0).
    DkResHandle handles[MAX_TEXTURES];
    for (int i = 0; i < MAX_TEXTURES; ++i)
    {
        const Texture *tex = static_cast<const Texture *>(state.textures[i].texture);
        const Sampler *smp = static_cast<const Sampler *>(state.textures[i].sampler);
        handles[i] = dkMakeTextureHandle(tex ? tex->descId : 0, smp ? smp->descId : 0);
    }
    dkCmdBufBindTextures(cmd, DkStage_Fragment, 0, handles, MAX_TEXTURES);
    if (dbg)
        Log("    BindDraw: textures bound; viewport/scissor");

    const DkViewport vp = {state.viewport.x,        state.viewport.y,        state.viewport.width,
                           state.viewport.height,   state.viewport.minDepth, state.viewport.maxDepth};
    dkCmdBufSetViewports(cmd, 0, &vp, 1);
    // deko3d always scissors. With the pipeline's scissor test off the rect means nothing (the frontend fills it
    // from D3D9's scissor rect regardless, which an earlier clipped draw may have left small), so cover the pass.
    const DkScissor sc = p->scissorTest ? DkScissor{uint32_t(state.scissor.x < 0 ? 0 : state.scissor.x),
                                                    uint32_t(state.scissor.y < 0 ? 0 : state.scissor.y),
                                                    state.scissor.width, state.scissor.height}
                                        : DkScissor{0, 0, s_passW, s_passH};
    dkCmdBufSetScissors(cmd, 0, &sc, 1);
    if (p->depthStencil.stencilTestEnable)
        dkCmdBufSetStencil(cmd, DkFace_FrontAndBack, p->stencilWriteMask, state.stencilRef, p->stencilReadMask);
    dkCmdBufSetBlendConst(cmd, state.blendConstant[0], state.blendConstant[1], state.blendConstant[2],
                          state.blendConstant[3]);
    if (dbg)
        Log("    BindDraw: done");
}
} // namespace

void Draw(const DrawState &state, Primitive primitive, uint32_t vertexCount, uint32_t firstVertex)
{
    Swapchain *s = s_swapchain;
    if (!s || !state.pipeline)
        return;
    BeginFrame(s);
    ++s_passDraws;
    if (s_dbgDraw < 8)
        Log("Draw #%d: verts %u", s_dbgDraw++, vertexCount);
    BindDraw(state);
    dkCmdBufDraw(s->cmd, MapPrimitive(primitive), vertexCount, 1, firstVertex, 0);
    if (s_frameTrace && (s_passIdx == 4 || s_passIdx >= 23))
    {
        const Buffer *vb = static_cast<const Buffer *>(state.vertices[0].buffer);
        Log("trace draw p%d (non-indexed): vb %p st %u/%u off %u stride %u | n %u first %u", s_passIdx, (void *)vb,
            vb ? vb->cur : 0, vb ? uint32_t(vb->stores.size()) : 0, state.vertices[0].offset,
            state.vertices[0].stride, vertexCount, firstVertex);
    }
}

void DrawIndexed(const DrawState &state, Primitive primitive, uint32_t indexCount, uint32_t firstIndex,
                 int32_t baseVertex)
{
    Swapchain *s = s_swapchain;
    if (!s || !state.pipeline || !state.indices)
        return;
    BeginFrame(s);
    ++s_passDraws;
    if (s_drawTotal < 8 || (s_drawTotal % 20000) == 0)
        Log("DrawIndexed #%d: indices %u firstIndex %u", s_drawTotal, indexCount, firstIndex);
    ++s_drawTotal;
    BindDraw(state);
    const Buffer *ib = static_cast<const Buffer *>(state.indices);
    const DkIdxFormat idx = ib->type == BufferType::Index32 ? DkIdxFormat_Uint32 : DkIdxFormat_Uint16;
    dkCmdBufBindIdxBuffer(s->cmd, idx, ib->gpu);
    MarkRead(ib);
    if (s_frameTrace && (s_passIdx == 4 || s_passIdx >= 23))
    {
        const Buffer *vb = static_cast<const Buffer *>(state.vertices[0].buffer);
        Log("trace draw p%d: vb %p st %u/%u off %u stride %u | ib %p st %u/%u | n %u first %u base %d | sc %d %d,%d "
            "%ux%u",
            s_passIdx, (void *)vb, vb ? vb->cur : 0, vb ? uint32_t(vb->stores.size()) : 0, state.vertices[0].offset,
            state.vertices[0].stride, (void *)ib, ib->cur, uint32_t(ib->stores.size()), indexCount, firstIndex,
            baseVertex, int(static_cast<const Pipeline *>(state.pipeline)->scissorTest), state.scissor.x,
            state.scissor.y, state.scissor.width, state.scissor.height);
        if (s_passIdx >= 23 && vb && !vb->stores.empty() && !ib->stores.empty() && ib->type != BufferType::Index32)
        {
            const Pipeline *pl = static_cast<const Pipeline *>(state.pipeline);
            for (uint32_t a = 0; a < pl->attribCount; ++a)
            {
                const DkVtxAttribState &at = pl->attribs[a];
                Log("trace   attr %u: buf %u fixed %u off %u size %u type %u bgra %u", a, at.bufferId, at.isFixed,
                    at.offset, at.size, at.type, at.isBgra);
            }
            const uint16_t *ix = reinterpret_cast<const uint16_t *>(ib->stores[ib->cur].cpu) + firstIndex;
            for (int k = 0; k < 2 && (size_t(firstIndex) + k + 1) * 2 <= ib->size; ++k)
            {
                const size_t at = state.vertices[0].offset + size_t(int32_t(ix[k]) + baseVertex) * state.vertices[0].stride;
                if (at + 32 > vb->size)
                    break;
                const uint8_t *v = vb->stores[vb->cur].cpu + at;
                const float *f = reinterpret_cast<const float *>(v);
                const uint32_t *u = reinterpret_cast<const uint32_t *>(v);
                Log("trace   idx %u vtx: %.1f %.1f %.3f | %08x %08x %08x %08x %08x", ix[k], f[0], f[1], f[2], u[3],
                    u[4], u[5], u[6], u[7]);
            }
        }
    }
    dkCmdBufDrawIndexed(s->cmd, MapPrimitive(primitive), indexCount, 1, firstIndex, baseVertex, 0);
}

void Present(gb::Swapchain *swapchain, gb::Texture *image)
{
    Swapchain *s = static_cast<Swapchain *>(swapchain);
    if (!s || !s->sc)
        return;
    BeginFrame(s);
    static uint64_t firstNs = 0;
    const uint64_t nowNs = armTicksToNs(armGetSystemTick());
    if (!firstNs)
        firstNs = nowNs;
    const uint32_t secs = uint32_t((nowNs - firstNs) / 1000000000ull);
    if (s_dbgPresent < 8 || (s_dbgPresent % 16) == 0)
        Log("Present #%d at %us: %d draws total so far, %u buffer renames, %u blocks (%.1f MB), heap %.1f MB",
            s_dbgPresent, secs, s_drawTotal, s_renames, s_blocksLive, double(s_blockBytes) / double(1 << 20), HeapMB());
    ++s_dbgPresent;
    // Debug: trace every pass of one world frame (armed here, so it covers the next frame from its first pass).
    if (s_frameTrace)
    {
        Log("trace frame end (%d passes)", s_passIdx);
        s_frameTrace = false;
        s_frameTraceDone = true;
        s_passCapPending = !s_passCaps.empty();
    }
    else if (s_debugCaptures && !s_frameTraceDone && secs >= kTraceAt)
    {
        s_frameTrace = true;
        s_passIdx = 0;
    }
    Texture *src = static_cast<Texture *>(image);
    const int slot = dkQueueAcquireImage(s_queue, s->sc);
    if (src)
    {
        dkCmdBufBarrier(s->cmd, DkBarrier_Fragments, 0);
        DkImageView srcView, dstView;
        dkImageViewDefaults(&srcView, &src->image);
        dkImageViewDefaults(&dstView, &s->fb[slot]);
        const DkImageRect srcRect = {0, 0, 0, src->desc.width, src->desc.height, 1};
        const DkImageRect dstRect = {0, 0, 0, s->width, s->height, 1};
        dkCmdBufBlitImage(s->cmd, &srcView, &srcRect, &dstView, &dstRect, DkBlitFlag_FilterLinear, 0);

        // Debug: dump the presented image at a few cumulative-draw milestones, to see each render stage offline.
        if (s_debugCaptures && s_dumpIdx < sizeof(kDumpAt) / sizeof(kDumpAt[0]) && secs >= kDumpAt[s_dumpIdx] &&
            !s_dumpPending)
        {
            s_dumpLabel = kDumpAt[s_dumpIdx];
            ++s_dumpIdx;
            s_dumpW = src->desc.width;
            s_dumpH = src->desc.height;
            s_dumpBgra = src->dkFormat == DkImageFormat_BGRA8_Unorm || src->dkFormat == DkImageFormat_BGRX8_Unorm;
            Log("dump: at %u s (%d draws), image %ux%u format %d", s_dumpLabel, s_drawTotal, s_dumpW, s_dumpH,
                int(src->desc.format));
            if (!s_dumpMem)
                s_dumpMem = MakeBlock(s_dumpW * s_dumpH * 4, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
            const DkCopyBuf dst = {dkMemBlockGetGpuAddr(s_dumpMem), 0, 0};
            dkCmdBufCopyImageToBuffer(s->cmd, &srcView, &srcRect, &dst, 0);
            s_dumpPending = true;
        }
    }
    const DkCmdList list = dkCmdBufFinishList(s->cmd);
    dkQueueSubmitCommands(s_queue, list);
    dkQueueSignalFence(s_queue, &s->fence, true);
    dkQueuePresentImage(s_queue, s->sc, slot);
    s->submitted = true;
    s->recording = false;

    if (s_dumpPending)
    {
        s_dumpPending = false;
        if (dkFenceWait(&s->fence, 2000000000) == DkResult_Success && s_dumpMem)
        {
            char path[64];
            snprintf(path, sizeof(path), "frame at %u s", s_dumpLabel);
            const uint8_t *px = static_cast<const uint8_t *>(dkMemBlockGetCpuAddr(s_dumpMem));
            LogImage(path, px, s_dumpW, s_dumpH, s_dumpBgra);
            // The HUD's corners at full resolution.
            if (s_dumpW >= 960 && s_dumpH >= 200)
            {
                snprintf(path, sizeof(path), "frame at %u s bottom left", s_dumpLabel);
                LogImage(path, px, s_dumpW, s_dumpH, s_dumpBgra, 4, 1, 0, s_dumpH - 200, 480, 200);
                snprintf(path, sizeof(path), "frame at %u s bottom right", s_dumpLabel);
                LogImage(path, px, s_dumpW, s_dumpH, s_dumpBgra, 4, 1, s_dumpW - 480, s_dumpH - 200, 480, 200);
            }
        }
        else
            Log("dump: fence wait failed, no PPM");
    }

    if (s_passCapPending)
    {
        s_passCapPending = false;
        if (dkFenceWait(&s->fence, 2000000000) == DkResult_Success && s_passCapMem)
        {
            const uint8_t *base = static_cast<const uint8_t *>(dkMemBlockGetCpuAddr(s_passCapMem));
            for (const PassCap &pc : s_passCaps)
            {
                char path[64];
                snprintf(path, sizeof(path), "pass %02d", pc.pass);
                LogImage(path, base + pc.offset, pc.w, pc.h, pc.bgra, pc.bpp);
            }
        }
        else
            Log("trace: fence wait failed, no pass PPMs");
        s_passCaps.clear();
        s_passCapNext = 0;
    }
}

void WaitIdle()
{
    static int n = 0;
    if (n < 8)
        Log("WaitIdle #%d enter", n++);
    if (!s_queue || !WaitQueue("WaitIdle"))
        return;
    // Everything submitted has run. Unless a frame is being recorded (its commands may reference memory freed during
    // it), the memory waiting for the GPU can go now.
    if (!(s_swapchain && s_swapchain->recording))
    {
        s_completedSerial = s_frameSerial;
        for (DkMemBlock m : s_pendingFree)
            FreeBlock(m);
        s_pendingFree.clear();
    }
}

// ---- copies; readback, gamma and resize are not implemented yet.

// D3D9 StretchRect: the engine resolves the rendered scene into the textures its post-processing samples this way,
// so without it every post pass reads an empty texture and the frame comes out black. Recorded in the frame's
// command buffer, in order with the passes around it.
void Blit(gb::Texture *srcHandle, uint8_t srcLevel, uint8_t srcLayer, const Rect &srcRect, gb::Texture *dstHandle,
          uint8_t dstLevel, uint8_t dstLayer, const Rect &dstRect, Filter filter)
{
    Swapchain *s = s_swapchain;
    Texture *src = static_cast<Texture *>(srcHandle), *dst = static_cast<Texture *>(dstHandle);
    if (!s || !src || !dst)
        return;
    BeginFrame(s);
    if (s_frameTrace)
        Log("trace blit: fmt %d %ux%u (%d,%d %ux%u) -> fmt %d %ux%u (%d,%d %ux%u)", int(src->desc.format),
            src->desc.width, src->desc.height, srcRect.x, srcRect.y, srcRect.width, srcRect.height,
            int(dst->desc.format), dst->desc.width, dst->desc.height, dstRect.x, dstRect.y, dstRect.width,
            dstRect.height);
    DkImageView srcView, dstView;
    dkImageViewDefaults(&srcView, &src->image);
    srcView.mipLevelOffset = srcLevel;
    srcView.layerOffset = srcLayer;
    dkImageViewDefaults(&dstView, &dst->image);
    dstView.mipLevelOffset = dstLevel;
    dstView.layerOffset = dstLayer;
    const DkImageRect from = {uint32_t(srcRect.x), uint32_t(srcRect.y), 0, srcRect.width, srcRect.height, 1};
    const DkImageRect to = {uint32_t(dstRect.x), uint32_t(dstRect.y), 0, dstRect.width, dstRect.height, 1};
    // The source was just rendered and the destination is sampled next: finish the rendering before the copy, and
    // the copy before anything reads the destination, dropping the texture cache's stale lines.
    dkCmdBufBarrier(s->cmd, DkBarrier_Full, 0);
    // Depth copies only between equal formats and sizes (contract); a same-size same-format colour copy needs no
    // filtering either. Anything else scales and converts on the 2D engine.
    if (IsDepthFormat(src->desc.format) ||
        (from.width == to.width && from.height == to.height && src->dkFormat == dst->dkFormat))
        dkCmdBufCopyImage(s->cmd, &srcView, &from, &dstView, &to, 0);
    else
        dkCmdBufBlitImage(s->cmd, &srcView, &from, &dstView, &to,
                          filter == Filter::Linear ? DkBlitFlag_FilterLinear : DkBlitFlag_FilterNearest, 0);
    dkCmdBufBarrier(s->cmd, DkBarrier_Full, DkInvalidateFlags_Image);
}
void ReadTexture(gb::Texture *, uint8_t, uint8_t, const Box &, void *, uint32_t) {}

// Queries are not yet backed by GPU counters. They must still report a *ready* result: the engine's GetData spin
// loops (rb_sky, rb_corona, rb_superflare, rb_pixelcost) poll GetData and busy-wait while it returns S_FALSE, so a
// query that never completes hangs the render thread forever (the first-geometry hang, docs/PORTING.md). Occlusion
// reports a nonzero sample count so sun/flare/sky read as visible instead of fully occluded; timestamps report the
// CPU clock, which yields small positive frame-time deltas. TODO: real DkCounter_SamplesPassed / timestamps.
gb::Query *CreateQuery(QueryType type)
{
    Query *q = new Query;
    q->type = type;
    return q;
}
void DestroyQuery(gb::Query *q) { delete static_cast<Query *>(q); }
void BeginQuery(gb::Query *) {}
void EndQuery(gb::Query *) {}
bool QueryResult(gb::Query *handle, uint64_t *value)
{
    if (!value)
        return false;
    const Query *q = static_cast<const Query *>(handle);
    *value = (q && q->type == QueryType::Timestamp) ? armTicksToNs(armGetSystemTick()) : 0x10000ull;
    return true;
}
gb::Fence *InsertFence()
{
    return nullptr;
}
bool FenceReached(gb::Fence *)
{
    return true;
}
void DestroyFence(gb::Fence *) {}
void ResizeSwapchain(gb::Swapchain *, uint16_t, uint16_t) {}
void SetGamma(gb::Swapchain *, const uint16_t[256], const uint16_t[256], const uint16_t[256]) {}

const Backend &Table()
{
#define GB_DEKO3D_ENTRY(ret, name, params, args) &deko3d::name,
    static const Backend table = {&deko3d::Init, &deko3d::Name, GB_BACKEND_CALLS(GB_DEKO3D_ENTRY)};
#undef GB_DEKO3D_ENTRY
    return table;
}
} // namespace gb::deko3d
