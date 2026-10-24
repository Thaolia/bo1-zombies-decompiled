#pragma once

// The graphics backend the D3D9 frontend (src/gfx_d3d9emu) records into: explicit and low level, so that the GL
// backend today and a deko3d one later implement the same header (docs/RENDERER.md: the contract and the checklist
// every change here must pass). Nothing in it names an API object, enum or extension; handles are opaque.
//
// Threads: the frontend makes one call at a time (it holds its device lock). Resource creation, destruction and
// uploads may come from any thread. Recording (passes, draws, copies, readbacks, queries, fences, Present, WaitIdle)
// comes from one thread at a time, the engine's render thread, which may change between frames; a backend whose
// device is bound to a thread (a GL context) follows it.
//
// Conventions are D3D9's: row 0 of a texture is its top row (uploads, attachments, viewports, scissors and fragment
// coordinates count from it), clip-space depth runs from 0 to 1 and clockwise triangles face the front. Rasterisation
// is the backend API's own: pixels are sampled at their middle (a fragment's coordinates are half-integers); the
// shaders carry D3D9's half-pixel offset (docs/RENDERER.md, bo1_pos_fixup).

#include <cstdint>

namespace gb
{
struct Buffer;
struct Texture;
struct Sampler;
struct Shader;
struct Pipeline;
struct Query;
struct Fence;
struct Swapchain;

// ---- formats

enum class Format : uint8_t
{
    Unknown,
    BGRA8,    // bytes B, G, R, A (D3DFMT_A8R8G8B8)
    BGRX8,    // bytes B, G, R, X; alpha reads 1 (D3DFMT_X8R8G8B8)
    RGBA8,    // bytes R, G, B, A (D3DFMT_A8B8G8R8)
    B5G6R5,   // D3DFMT_R5G6B5
    B5G5R5A1, // D3DFMT_A1R5G5B5
    R8,       // one byte; D3DFMT_L8 and D3DFMT_A8 through a swizzle
    RG8,      // D3DFMT_A8L8 (bytes L, A) through a swizzle
    RG16,     // D3DFMT_G16R16, unorm
    RGBA16,   // D3DFMT_A16B16G16R16, unorm
    R16F,
    RG16F,    // D3DFMT_G16R16F
    RGBA16F,  // D3DFMT_A16B16G16R16F
    R32F,
    BC1,      // D3DFMT_DXT1
    BC2,      // D3DFMT_DXT3
    BC3,      // D3DFMT_DXT5
    D16,
    D24S8,    // D3DFMT_D24S8, D3DFMT_D24X8 and the INTZ depth texture
    Count,
};

// Where each component of a sampled texture comes from.
enum class Source : uint8_t
{
    R,
    G,
    B,
    A,
    Zero,
    One,
};

struct Swizzle
{
    Source r = Source::R, g = Source::G, b = Source::B, a = Source::A;
};

// ---- resources

enum class Usage : uint8_t
{
    Static,       // written rarely, by uploads
    Dynamic,      // rewritten often (every frame or more), by uploads
    RenderTarget, // a colour attachment
    DepthStencil, // a depth attachment
};

enum class Memory : uint8_t
{
    Device, // GPU memory
    Shared, // CPU-visible GPU memory, for data the GPU reads once (streamed vertices)
};

enum class BufferType : uint8_t
{
    Vertex,
    Index16,
    Index32,
};

struct BufferDesc
{
    BufferType type;
    Usage usage; // Static or Dynamic
    Memory memory;
    uint32_t size;
};

enum class TextureType : uint8_t
{
    Tex2D,
    Cube, // 6 layers, in D3D9 face order (+X, -X, +Y, -Y, +Z, -Z)
    Tex3D,
};

struct TextureDesc
{
    TextureType type;
    Format format;
    Usage usage;
    bool sampled;    // read by shaders (always true for Static and Dynamic)
    uint16_t width;
    uint16_t height;
    uint16_t depth;  // Tex3D only, else 1
    uint8_t levels;
    uint8_t samples; // above 1: a multisampled attachment, never sampled; resolved with Blit
    Swizzle swizzle; // sampled textures only
};

Buffer *CreateBuffer(const BufferDesc &desc);
Texture *CreateTexture(const TextureDesc &desc);
// Destruction waits for nothing: the backend frees the resource once the GPU has finished with it.
void DestroyBuffer(Buffer *buffer);
void DestroyTexture(Texture *texture);

// ---- uploads: CPU data into a resource, in order with the work recorded around them

enum class Upload : uint8_t
{
    Ordered,     // recorded work before the upload reads the old contents, work after it the new ones
    Discard,     // the whole buffer's old contents are dead (D3DLOCK_DISCARD): the backend may rename it
    NoOverwrite, // the caller guarantees no recorded work reads the range (D3DLOCK_NOOVERWRITE)
};

void UploadBuffer(Buffer *buffer, uint32_t offset, const void *data, uint32_t size, Upload mode);

// Texels; block-compressed formats in whole 4x4 blocks.
struct Box
{
    uint16_t x, y, z;
    uint16_t width, height, depth;
};

// `layer`: the cube face (0 for other types). `rowPitch`/`slicePitch`: bytes between rows (block rows) and slices.
void UploadTexture(Texture *texture, uint8_t level, uint8_t layer, const Box &box, const void *data, uint32_t rowPitch,
                   uint32_t slicePitch);

// ---- samplers and shaders

enum class Filter : uint8_t
{
    Nearest,
    Linear,
};

enum class MipFilter : uint8_t
{
    None, // the top level only
    Nearest,
    Linear,
};

enum class Address : uint8_t
{
    Wrap,
    Mirror,
    Clamp,
    Border, // transparent black
    MirrorOnce,
};

struct SamplerDesc
{
    Filter magFilter = Filter::Linear;
    Filter minFilter = Filter::Linear;
    MipFilter mipFilter = MipFilter::Linear;
    Address addressU = Address::Wrap, addressV = Address::Wrap, addressW = Address::Wrap;
    uint8_t maxAnisotropy = 1; // above 1: anisotropic filtering
    // A depth texture read as a shadow test (D3D9 hardware shadow maps): 1 where the reference depth (the coordinate
    // after the texture coordinates) is less than or equal to the stored one, else 0, filtered.
    bool depthCompare = false;
};

// Immutable and cached by the backend: equal descriptions give the same sampler.
const Sampler *GetSampler(const SamplerDesc &desc);

enum class Stage : uint8_t
{
    Vertex,
    Pixel,
};

// `code`: the shader's text from shaders.pack (GLSL 4.60 today; a deko3d pack would carry DKSH), copied by the
// backend; `hash` the hash of its D3D9 bytecode. The backend may compile lazily; a shader that fails to build fails
// the pipelines using it.
Shader *CreateShader(Stage stage, uint64_t hash, const char *code, uint32_t size);
// Also ends the pipelines made with the shader: a later description naming a new shader at the same address gets a
// new pipeline.
void DestroyShader(Shader *shader);

// ---- pipeline state: everything fixed for a draw except bindings, viewport, scissor, stencil reference and the
// blend constant

enum class Blend : uint8_t
{
    Zero,
    One,
    SrcColor,
    InvSrcColor,
    SrcAlpha,
    InvSrcAlpha,
    DstAlpha,
    InvDstAlpha,
    DstColor,
    InvDstColor,
    SrcAlphaSat,
    Constant,
    InvConstant,
};

enum class BlendOp : uint8_t
{
    Add,
    Subtract,
    RevSubtract,
    Min,
    Max,
};

enum class Compare : uint8_t
{
    Never,
    Less,
    Equal,
    LessEqual,
    Greater,
    NotEqual,
    GreaterEqual,
    Always,
};

enum class StencilOp : uint8_t
{
    Keep,
    Zero,
    Replace,
    IncrSat,
    DecrSat,
    Invert,
    Incr,
    Decr,
};

enum class Cull : uint8_t
{
    None,
    Front,
    Back,
};

enum class Fill : uint8_t
{
    Solid,
    Wireframe,
};

enum class Topology : uint8_t
{
    Points,
    Lines,
    Triangles,
};

enum class AttribType : uint8_t
{
    Float1,
    Float2,
    Float3,
    Float4,
    UByte4,      // D3DDECLTYPE_UBYTE4: integers 0-255 read as floats
    UByte4N,     // D3DDECLTYPE_UBYTE4N
    UByte4NBGRA, // D3DDECLTYPE_D3DCOLOR: bytes B, G, R, A read as (R, G, B, A)
    Short2,      // D3DDECLTYPE_SHORT2: integers read as floats
    Short4,
    Short2N,
    Short4N,
    UShort2N,
    UShort4N,
    Half2,       // D3DDECLTYPE_FLOAT16_2
    Half4,
};

struct VertexAttrib
{
    uint8_t location; // the shader input location (the material stream destination, docs/RENDERER.md)
    uint8_t stream;
    uint16_t offset;
    AttribType type;
};

inline constexpr int MAX_ATTRIBS = 16;
inline constexpr int MAX_STREAMS = 8;
inline constexpr int MAX_COLOR_TARGETS = 4;
inline constexpr int MAX_TEXTURES = 16;

struct BlendState
{
    bool enable = false;
    Blend srcColor = Blend::One, dstColor = Blend::Zero;
    BlendOp opColor = BlendOp::Add;
    Blend srcAlpha = Blend::One, dstAlpha = Blend::Zero;
    BlendOp opAlpha = BlendOp::Add;
    uint8_t writeMask = 0xF; // bit 0 red .. bit 3 alpha
};

struct StencilFace
{
    Compare func = Compare::Always;
    StencilOp fail = StencilOp::Keep, depthFail = StencilOp::Keep, pass = StencilOp::Keep;
};

struct DepthStencilState
{
    bool depthTest = false;
    bool depthWrite = false;
    Compare depthFunc = Compare::LessEqual;
    bool stencil = false;
    StencilFace front, back; // back faces use `back` (D3D9 two-sided stencil); equal to `front` otherwise
    uint8_t readMask = 0xFF, writeMask = 0xFF;
};

struct RasterState
{
    Cull cull = Cull::None;
    Fill fill = Fill::Solid;
    bool scissor = false;
    float depthBias = 0.0f;            // D3DRS_DEPTHBIAS: added to the depth (0 to 1 range)
    float slopeScaledDepthBias = 0.0f; // D3DRS_SLOPESCALEDEPTHBIAS
};

struct PipelineDesc
{
    const Shader *vertex = nullptr;
    const Shader *pixel = nullptr;
    Topology topology = Topology::Triangles;
    uint8_t attribCount = 0;
    VertexAttrib attribs[MAX_ATTRIBS] = {};
    BlendState blend;
    DepthStencilState depthStencil;
    RasterState raster;
    // The attachments it renders into, as the pass declares them.
    uint8_t colorCount = 0;
    Format colorFormats[MAX_COLOR_TARGETS] = {};
    Format depthFormat = Format::Unknown;
    uint8_t samples = 1;
};

// Immutable and cached by the backend (by the description's value: unused attribs and colour formats are zero):
// equal descriptions give the same pipeline. NULL when a shader failed to build.
const Pipeline *GetPipeline(const PipelineDesc &desc);

// ---- passes and draws

enum class LoadOp : uint8_t
{
    Load,
    Clear,
    DontCare,
};

struct ColorAttachment
{
    Texture *texture = nullptr;
    uint8_t level = 0;
    uint8_t layer = 0;
    LoadOp load = LoadOp::Load;
    float clear[4] = {}; // red, green, blue, alpha
};

struct PassDesc
{
    uint8_t colorCount = 0;
    ColorAttachment color[MAX_COLOR_TARGETS];
    Texture *depth = nullptr;
    LoadOp depthLoad = LoadOp::Load;
    LoadOp stencilLoad = LoadOp::Load;
    float clearDepth = 1.0f;
    uint8_t clearStencil = 0;
};

// Rendering only happens between BeginPass and EndPass. Hazards between passes (a target read as a texture by a
// later pass) are resolved at pass boundaries; inside one pass nothing reads an attachment. Attachments share their
// sample count but not always their size (D3D9 lets a depth buffer be larger than the colour target): the pass
// covers the top-left area they all have.
void BeginPass(const PassDesc &desc);
void EndPass();

struct Rect
{
    int32_t x, y;
    uint32_t width, height;
};

// Clears these rects of the current pass's attachments, unaffected by any pipeline state.
void Clear(const Rect *rects, uint32_t count, bool color, bool depth, bool stencil, const float rgba[4],
           float depthValue, uint8_t stencilValue);

// D3D9's viewport, applied as it is: it covers whole pixels.
struct Viewport
{
    float x, y, width, height;
    float minDepth, maxDepth;
};

struct VertexBinding
{
    const Buffer *buffer = nullptr;
    uint32_t offset = 0;
    uint32_t stride = 0;
};

struct TextureBinding
{
    const Texture *texture = nullptr;
    const Sampler *sampler = nullptr;
};

struct UniformSlice
{
    const Buffer *buffer = nullptr;
    uint32_t offset = 0;
    uint32_t size = 0;
};

// Everything a draw reads besides its pipeline: no other state carries over from one draw to the next.
struct DrawState
{
    const Pipeline *pipeline = nullptr;
    VertexBinding vertices[MAX_STREAMS];
    const Buffer *indices = nullptr;
    TextureBinding textures[MAX_TEXTURES];
    UniformSlice vertexConstants; // binding 0
    UniformSlice pixelConstants;  // binding 1
    Viewport viewport = {};
    Rect scissor = {};
    uint8_t stencilRef = 0;
    float blendConstant[4] = {};
};

// Copies data into this frame's uniform memory (std140 blocks) and returns where it went. Valid until the frame's
// Present.
UniformSlice AllocUniforms(const void *data, uint32_t size);

enum class Primitive : uint8_t
{
    PointList,
    LineList,
    LineStrip,
    TriangleList,
    TriangleStrip,
    TriangleFan,
};

void Draw(const DrawState &state, Primitive primitive, uint32_t vertexCount, uint32_t firstVertex);
void DrawIndexed(const DrawState &state, Primitive primitive, uint32_t indexCount, uint32_t firstIndex,
                 int32_t baseVertex);

// ---- copies, outside passes

// Scaled copy between two textures' levels (D3D9 StretchRect); a multisampled source is resolved. Depth textures
// copy only between equal formats and sizes, with Nearest.
void Blit(Texture *src, uint8_t srcLevel, uint8_t srcLayer, const Rect &srcRect, Texture *dst, uint8_t dstLevel,
          uint8_t dstLayer, const Rect &dstRect, Filter filter);

// Reads a texture level back to the CPU. A sync point: it waits for the recorded work that writes it.
void ReadTexture(Texture *texture, uint8_t level, uint8_t layer, const Box &box, void *data, uint32_t rowPitch);

// ---- queries and fences: written by the GPU, polled without waiting

enum class QueryType : uint8_t
{
    Occlusion, // samples that passed the depth and stencil tests between Begin and End
    Timestamp, // nanoseconds, written at End
};

Query *CreateQuery(QueryType type);
void DestroyQuery(Query *query);
void BeginQuery(Query *query); // Occlusion only, inside a pass
void EndQuery(Query *query);
// False while the GPU has not written the result yet; never waits.
bool QueryResult(Query *query, uint64_t *value);

// A point in the recorded work; reached once the GPU has finished everything recorded before it.
Fence *InsertFence();
bool FenceReached(Fence *fence); // never waits
void DestroyFence(Fence *fence);

// ---- presentation and lifetime

struct SwapchainDesc
{
    void *window; // the platform's window (src/platform/<platform>: an SDL_Window on posix)
    uint16_t width;
    uint16_t height;
    bool vsync;
};

Swapchain *CreateSwapchain(const SwapchainDesc &desc);
void ResizeSwapchain(Swapchain *swapchain, uint16_t width, uint16_t height);
void DestroySwapchain(Swapchain *swapchain);
// The gamma ramp applied to what Present shows (D3D9 SetGammaRamp): 256 entries per channel, 0-65535.
void SetGamma(Swapchain *swapchain, const uint16_t red[256], const uint16_t green[256], const uint16_t blue[256]);
// Ends the frame: shows `image` (a colour attachment of the swapchain's size) and starts the next frame's uniform
// memory.
void Present(Swapchain *swapchain, Texture *image);
// Waits until the GPU has finished all recorded work.
void WaitIdle();

struct Caps
{
    const char *device; // the GPU and its driver, for the log
    uint16_t maxTextureSize;
    uint8_t maxSamples;
    uint64_t memoryBytes; // what textures and buffers can use, an estimate
    // Per Format: bit 0 sampled, bit 1 colour attachment, bit 2 depth attachment, bit 3 filterable.
    uint8_t formatFlags[static_cast<int>(Format::Count)];
};

inline constexpr uint8_t FORMAT_SAMPLED = 1, FORMAT_COLOR_TARGET = 2, FORMAT_DEPTH_TARGET = 4, FORMAT_FILTERABLE = 8;

// The first call. False when no device is available.
bool Init();
void Shutdown();
const Caps &GetCaps();
// The backend's name for the log ("null", "gl").
const char *Name();
} // namespace gb
