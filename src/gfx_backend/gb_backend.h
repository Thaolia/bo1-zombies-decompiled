#pragma once

// How the backends plug in (not part of the contract the frontend sees): each one fills a table of the contract's
// functions, and gb_backend.cpp forwards gfx_backend.h's free functions to the table gb::Init chose.

#include "gfx_backend.h"

namespace gb
{
// The contract's handles: each backend's objects derive from these and are static_cast back by that backend only.
struct Buffer
{
};
struct Texture
{
};
struct Sampler
{
};
struct Shader
{
};
struct Pipeline
{
};
struct Query
{
};
struct Fence
{
};
struct Swapchain
{
};

// Every contract function but Init and Name, as X(return type, name, parameters, arguments).
#define GB_BACKEND_CALLS(X)                                                                                           \
    X(void, Shutdown, (), ())                                                                                         \
    X(const Caps &, GetCaps, (), ())                                                                                  \
    X(Buffer *, CreateBuffer, (const BufferDesc &desc), (desc))                                                       \
    X(Texture *, CreateTexture, (const TextureDesc &desc), (desc))                                                    \
    X(void, DestroyBuffer, (Buffer * buffer), (buffer))                                                               \
    X(void, DestroyTexture, (Texture * texture), (texture))                                                           \
    X(void, UploadBuffer, (Buffer * buffer, uint32_t offset, const void *data, uint32_t size, Upload mode),          \
      (buffer, offset, data, size, mode))                                                                             \
    X(void, UploadTexture,                                                                                            \
      (Texture * texture, uint8_t level, uint8_t layer, const Box &box, const void *data, uint32_t rowPitch,          \
       uint32_t slicePitch),                                                                                          \
      (texture, level, layer, box, data, rowPitch, slicePitch))                                                       \
    X(const Sampler *, GetSampler, (const SamplerDesc &desc), (desc))                                                 \
    X(Shader *, CreateShader, (Stage stage, uint64_t hash, const char *code, uint32_t size), (stage, hash, code, size)) \
    X(void, DestroyShader, (Shader * shader), (shader))                                                               \
    X(const Pipeline *, GetPipeline, (const PipelineDesc &desc), (desc))                                              \
    X(void, BeginPass, (const PassDesc &desc), (desc))                                                                \
    X(void, EndPass, (), ())                                                                                          \
    X(void, Clear,                                                                                                    \
      (const Rect *rects, uint32_t count, bool color, bool depth, bool stencil, const float rgba[4], float depthValue, \
       uint8_t stencilValue),                                                                                         \
      (rects, count, color, depth, stencil, rgba, depthValue, stencilValue))                                          \
    X(UniformSlice, AllocUniforms, (const void *data, uint32_t size), (data, size))                                   \
    X(void, Draw, (const DrawState &state, Primitive primitive, uint32_t vertexCount, uint32_t firstVertex),         \
      (state, primitive, vertexCount, firstVertex))                                                                   \
    X(void, DrawIndexed,                                                                                              \
      (const DrawState &state, Primitive primitive, uint32_t indexCount, uint32_t firstIndex, int32_t baseVertex),   \
      (state, primitive, indexCount, firstIndex, baseVertex))                                                         \
    X(void, Blit,                                                                                                     \
      (Texture * src, uint8_t srcLevel, uint8_t srcLayer, const Rect &srcRect, Texture *dst, uint8_t dstLevel,        \
       uint8_t dstLayer, const Rect &dstRect, Filter filter),                                                         \
      (src, srcLevel, srcLayer, srcRect, dst, dstLevel, dstLayer, dstRect, filter))                                   \
    X(void, ReadTexture,                                                                                              \
      (Texture * texture, uint8_t level, uint8_t layer, const Box &box, void *data, uint32_t rowPitch),               \
      (texture, level, layer, box, data, rowPitch))                                                                   \
    X(Query *, CreateQuery, (QueryType type), (type))                                                                 \
    X(void, DestroyQuery, (Query * query), (query))                                                                   \
    X(void, BeginQuery, (Query * query), (query))                                                                     \
    X(void, EndQuery, (Query * query), (query))                                                                       \
    X(bool, QueryResult, (Query * query, uint64_t * value), (query, value))                                           \
    X(Fence *, InsertFence, (), ())                                                                                   \
    X(bool, FenceReached, (Fence * fence), (fence))                                                                   \
    X(void, DestroyFence, (Fence * fence), (fence))                                                                   \
    X(Swapchain *, CreateSwapchain, (const SwapchainDesc &desc), (desc))                                              \
    X(void, ResizeSwapchain, (Swapchain * swapchain, uint16_t width, uint16_t height), (swapchain, width, height))    \
    X(void, DestroySwapchain, (Swapchain * swapchain), (swapchain))                                                   \
    X(void, SetGamma,                                                                                                 \
      (Swapchain * swapchain, const uint16_t red[256], const uint16_t green[256], const uint16_t blue[256]),          \
      (swapchain, red, green, blue))                                                                                  \
    X(void, Present, (Swapchain * swapchain, Texture *image), (swapchain, image))                                     \
    X(void, WaitIdle, (), ())

struct Backend
{
    bool (*Init)();
    const char *(*Name)();
#define GB_BACKEND_MEMBER(ret, name, params, args) ret(*name) params;
    GB_BACKEND_CALLS(GB_BACKEND_MEMBER)
#undef GB_BACKEND_MEMBER
};

// The backends' tables. Each defines its functions in its own namespace and builds the table from qualified names,
// so a function it forgot fails the build instead of resolving to the dispatcher's.
namespace null
{
const Backend &Table();
}
namespace gl
{
const Backend &Table();
}
namespace deko3d
{
const Backend &Table();
}
} // namespace gb
