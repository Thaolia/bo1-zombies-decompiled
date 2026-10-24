#pragma once

// Helpers the backends share (not part of the contract the frontend sees): format sizes, and canonical keys for the
// cached state objects, so that equal descriptions give equal keys whatever their padding bytes hold.

#include "gfx_backend.h"

#include <string>

namespace gb
{
inline bool IsDepthFormat(Format format)
{
    return format == Format::D16 || format == Format::D24S8;
}

inline bool IsBlockFormat(Format format)
{
    return format == Format::BC1 || format == Format::BC2 || format == Format::BC3;
}

// Bytes per texel, or per 4x4 block for the BC formats.
inline uint32_t FormatBytes(Format format)
{
    switch (format)
    {
    case Format::R8:
        return 1;
    case Format::B5G6R5:
    case Format::B5G5R5A1:
    case Format::RG8:
    case Format::R16F:
    case Format::D16:
        return 2;
    case Format::BGRA8:
    case Format::BGRX8:
    case Format::RGBA8:
    case Format::RG16:
    case Format::RG16F:
    case Format::R32F:
    case Format::D24S8:
        return 4;
    case Format::RGBA16:
    case Format::RGBA16F:
    case Format::BC1:
        return 8;
    case Format::BC2:
    case Format::BC3:
        return 16;
    case Format::Unknown:
    case Format::Count:
        break;
    }
    return 0;
}

inline uint32_t LevelExtent(uint32_t extent, uint32_t level)
{
    return extent >> level ? extent >> level : 1;
}

namespace detail
{
inline void Put(std::string &key, uint32_t value)
{
    key.append(reinterpret_cast<const char *>(&value), sizeof(value));
}

inline void Put(std::string &key, float value)
{
    key.append(reinterpret_cast<const char *>(&value), sizeof(value));
}

inline void Put(std::string &key, const void *pointer)
{
    key.append(reinterpret_cast<const char *>(&pointer), sizeof(pointer));
}
} // namespace detail

inline std::string SamplerKey(const SamplerDesc &d)
{
    std::string key;
    detail::Put(key, uint32_t(d.magFilter) | uint32_t(d.minFilter) << 4 | uint32_t(d.mipFilter) << 8 |
                         uint32_t(d.addressU) << 12 | uint32_t(d.addressV) << 16 | uint32_t(d.addressW) << 20 |
                         uint32_t(d.depthCompare) << 24);
    detail::Put(key, uint32_t(d.maxAnisotropy));
    return key;
}

inline std::string PipelineKey(const PipelineDesc &d)
{
    using detail::Put;
    std::string key;
    Put(key, d.vertex);
    Put(key, d.pixel);
    Put(key, uint32_t(d.topology) | uint32_t(d.attribCount) << 8 | uint32_t(d.colorCount) << 16 |
                 uint32_t(d.depthFormat) << 24);
    Put(key, uint32_t(d.samples));
    for (int i = 0; i < d.attribCount && i < MAX_ATTRIBS; ++i)
    {
        const VertexAttrib &a = d.attribs[i];
        Put(key, uint32_t(a.location) | uint32_t(a.stream) << 8 | uint32_t(a.type) << 16);
        Put(key, uint32_t(a.offset));
    }
    for (int i = 0; i < d.colorCount && i < MAX_COLOR_TARGETS; ++i)
        Put(key, uint32_t(d.colorFormats[i]));
    const BlendState &b = d.blend;
    Put(key, uint32_t(b.enable) | uint32_t(b.srcColor) << 4 | uint32_t(b.dstColor) << 8 | uint32_t(b.opColor) << 12 |
                 uint32_t(b.srcAlpha) << 16 | uint32_t(b.dstAlpha) << 20 | uint32_t(b.opAlpha) << 24 |
                 uint32_t(b.writeMask) << 28);
    const DepthStencilState &ds = d.depthStencil;
    Put(key, uint32_t(ds.depthTest) | uint32_t(ds.depthWrite) << 1 | uint32_t(ds.depthFunc) << 4 |
                 uint32_t(ds.stencil) << 8 | uint32_t(ds.readMask) << 16 | uint32_t(ds.writeMask) << 24);
    for (const StencilFace &f : {ds.front, ds.back})
        Put(key, uint32_t(f.func) | uint32_t(f.fail) << 4 | uint32_t(f.depthFail) << 8 | uint32_t(f.pass) << 12);
    const RasterState &r = d.raster;
    Put(key, uint32_t(r.cull) | uint32_t(r.fill) << 4 | uint32_t(r.scissor) << 8);
    Put(key, r.depthBias);
    Put(key, r.slopeScaledDepthBias);
    return key;
}
} // namespace gb
