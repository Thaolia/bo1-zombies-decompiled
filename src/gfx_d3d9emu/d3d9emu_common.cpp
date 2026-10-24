// The frontend's lock, log, reference counts and format table.

#include "d3d9emu.h"

#include "d3d9emu_host.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>

namespace d3d9emu
{
std::recursive_mutex &DeviceLock()
{
    static std::recursive_mutex *lock = new std::recursive_mutex();
    return *lock;
}

namespace
{
void LogV(const char *fmt, va_list args)
{
    char line[1024];
    std::vsnprintf(line, sizeof(line), fmt, args);
    host::Log(line);
}
} // namespace

void Logf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    LogV(fmt, args);
    va_end(args);
}

void WarnOnce(const char *key, const char *fmt, ...)
{
    static std::set<std::string> *seen = new std::set<std::string>();
    {
        Guard guard(DeviceLock());
        if (!seen->insert(key).second)
            return;
    }
    va_list args;
    va_start(args, fmt);
    LogV(fmt, args);
    va_end(args);
}

void Unemulated(const char *method)
{
    char line[512];
    std::snprintf(line, sizeof(line), "d3d9emu: %s is not emulated", method);
    host::Log(line);
    std::fprintf(stderr, "%s\n", line);
    std::fflush(nullptr);
    std::abort();
}

ULONG Counted::AddRefCount()
{
    Guard guard(DeviceLock());
    return ++m_refs;
}

ULONG Counted::ReleaseCount()
{
    Guard guard(DeviceLock());
    if (--m_refs)
        return m_refs;
    IUnknown *parent = Parent();
    if (!m_binds)
        delete this;
    // The device may go with this reference, unbinding (and so deleting) this object if it is still bound.
    if (parent)
        parent->Release();
    return 0;
}

void Counted::Bind()
{
    ++m_binds;
}

void Counted::Unbind()
{
    if (!--m_binds && !m_refs)
        delete this;
}

const FormatInfo *FindFormat(D3DFORMAT format)
{
    using gb::Format;
    using S = gb::Source;
    constexpr gb::Swizzle rgba = {S::R, S::G, S::B, S::A};
    constexpr gb::Swizzle rgb1 = {S::R, S::G, S::B, S::One};
    // D3D9 reads the components a format lacks as 1 (alpha included), except A8's colour, which reads 0.
    constexpr gb::Swizzle r111 = {S::R, S::One, S::One, S::One};
    constexpr gb::Swizzle rg11 = {S::R, S::G, S::One, S::One};
    constexpr gb::Swizzle luminance = {S::R, S::R, S::R, S::One};
    constexpr gb::Swizzle alpha = {S::Zero, S::Zero, S::Zero, S::R};
    constexpr gb::Swizzle luminanceAlpha = {S::R, S::R, S::R, S::G};
    constexpr gb::Swizzle depth = {S::R, S::R, S::R, S::R};
    //                                d3d                   format             swizzle    bytes block  tex    rt     depth  stencil
    static const FormatInfo table[] = {
        {D3DFMT_A8R8G8B8, Format::BGRA8, rgba, 4, false, true, true, false, false},
        {D3DFMT_X8R8G8B8, Format::BGRX8, rgb1, 4, false, true, true, false, false},
        {D3DFMT_A8B8G8R8, Format::RGBA8, rgba, 4, false, true, true, false, false},
        {D3DFMT_R5G6B5, Format::B5G6R5, rgb1, 2, false, true, true, false, false},
        {D3DFMT_A1R5G5B5, Format::B5G5R5A1, rgba, 2, false, true, true, false, false},
        {D3DFMT_L8, Format::R8, luminance, 1, false, true, false, false, false},
        {D3DFMT_A8, Format::R8, alpha, 1, false, true, false, false, false},
        {D3DFMT_A8L8, Format::RG8, luminanceAlpha, 2, false, true, false, false, false},
        {D3DFMT_G16R16, Format::RG16, rg11, 4, false, true, true, false, false},
        {D3DFMT_A16B16G16R16, Format::RGBA16, rgba, 8, false, true, true, false, false},
        {D3DFMT_R16F, Format::R16F, r111, 2, false, true, true, false, false},
        {D3DFMT_G16R16F, Format::RG16F, rg11, 4, false, true, true, false, false},
        {D3DFMT_A16B16G16R16F, Format::RGBA16F, rgba, 8, false, true, true, false, false},
        {D3DFMT_R32F, Format::R32F, r111, 4, false, true, true, false, false},
        {D3DFMT_DXT1, Format::BC1, rgba, 8, true, true, false, false, false},
        {D3DFMT_DXT3, Format::BC2, rgba, 16, true, true, false, false, false},
        {D3DFMT_DXT5, Format::BC3, rgba, 16, true, true, false, false, false},
        {D3DFMT_D16, Format::D16, depth, 2, false, true, false, true, false},
        {D3DFMT_D24S8, Format::D24S8, depth, 4, false, true, false, true, true},
        {D3DFMT_D24X8, Format::D24S8, depth, 4, false, true, false, true, false},
        {FOURCC_INTZ, Format::D24S8, depth, 4, false, true, false, true, true},
        {FOURCC_NULL, Format::Unknown, rgba, 0, false, false, true, false, false},
    };
    for (const FormatInfo &info : table)
        if (info.d3d == format)
            return &info;
    return nullptr;
}
} // namespace d3d9emu
