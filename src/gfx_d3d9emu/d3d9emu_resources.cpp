// Textures, surfaces and buffers. A lockable resource keeps a CPU copy of what was locked (D3D9's managed copy): a
// lock returns that copy, and the unlock uploads the locked region to the backend.

#include "d3d9emu.h"

#include <cstdio>

namespace d3d9emu
{
namespace
{
uint32_t Blocks(const FormatInfo &format, uint32_t texels)
{
    return format.block ? (texels + 3) / 4 : texels;
}

uint32_t MaxLevels(uint32_t width, uint32_t height, uint32_t depth)
{
    uint32_t largest = width > height ? width : height;
    largest = largest > depth ? largest : depth;
    uint32_t levels = 1;
    while (largest > 1)
    {
        largest >>= 1;
        ++levels;
    }
    return levels;
}

void FillSurfaceDesc(const Storage &s, uint32_t level, D3DSURFACE_DESC *desc)
{
    desc->Format = s.format->d3d;
    desc->Type = D3DRTYPE_SURFACE;
    desc->Usage = s.usage;
    desc->Pool = s.pool;
    desc->MultiSampleType = s.multiSample;
    desc->MultiSampleQuality = 0;
    desc->Width = s.Width(level);
    desc->Height = s.Height(level);
}

// A lock rect as a box of the level, or false if it is outside it or splits a block.
bool RectToBox(const Storage &s, uint32_t level, const RECT *rect, D3DBOX *box)
{
    const uint32_t width = s.Width(level), height = s.Height(level);
    if (!rect)
    {
        *box = {0, 0, width, height, 0, 1};
        return true;
    }
    if (rect->left < 0 || rect->top < 0 || rect->left >= rect->right || rect->top >= rect->bottom ||
        UINT(rect->right) > width || UINT(rect->bottom) > height)
        return false;
    *box = {UINT(rect->left), UINT(rect->top), UINT(rect->right), UINT(rect->bottom), 0, 1};
    return true;
}
} // namespace

// ---- storage

Storage::~Storage()
{
    gb::DestroyTexture(gpu);
}

uint32_t Storage::RowPitch(uint32_t level) const
{
    return Blocks(*format, Width(level)) * format->bytes;
}

uint32_t Storage::Rows(uint32_t level) const
{
    return Blocks(*format, Height(level));
}

uint8_t *Storage::Cpu(uint32_t layer, uint32_t level)
{
    std::vector<uint8_t> &cpu = subresources[layer * levels + level].cpu;
    if (cpu.empty())
        cpu.assign(size_t(SlicePitch(level)) * Depth(level), 0);
    return cpu.data();
}

HRESULT Storage::Lock(uint32_t layer, uint32_t level, const D3DBOX *box, DWORD flags, void **bits, INT *rowPitch,
                      INT *slicePitch)
{
    if (!lockable || layer >= layers || level >= levels || !bits)
        return D3DERR_INVALIDCALL;
    Subresource &sub = subresources[layer * levels + level];
    if (sub.locked)
        return D3DERR_INVALIDCALL;
    D3DBOX full = {0, 0, Width(level), Height(level), 0, Depth(level)};
    const D3DBOX &b = box ? *box : full;
    if (b.Left >= b.Right || b.Top >= b.Bottom || b.Front >= b.Back || b.Right > full.Right ||
        b.Bottom > full.Bottom || b.Back > full.Back)
        return D3DERR_INVALIDCALL;
    if (format->block && (b.Left % 4 || b.Top % 4))
        return D3DERR_INVALIDCALL;
    if (gpuNewer)
        device->ReadBack(*this);
    const uint32_t pitch = RowPitch(level);
    const uint32_t slice = SlicePitch(level);
    uint8_t *data = Cpu(layer, level);
    *bits = data + size_t(b.Front) * slice + size_t(Blocks(*format, b.Top)) * pitch +
            size_t(Blocks(*format, b.Left)) * format->bytes;
    if (rowPitch)
        *rowPitch = INT(pitch);
    if (slicePitch)
        *slicePitch = INT(slice);
    sub.locked = true;
    sub.lockFlags = flags;
    sub.lockBox = b;
    return D3D_OK;
}

HRESULT Storage::Unlock(uint32_t layer, uint32_t level)
{
    if (layer >= layers || level >= levels)
        return D3DERR_INVALIDCALL;
    Subresource &sub = subresources[layer * levels + level];
    if (!sub.locked)
        return D3DERR_INVALIDCALL;
    sub.locked = false;
    if (gpu && !(sub.lockFlags & D3DLOCK_READONLY))
        Upload(layer, level, sub.lockBox);
    return D3D_OK;
}

void Storage::Upload(uint32_t layer, uint32_t level, const D3DBOX &box)
{
    device->BeforeTextureWrite(gpu);
    const uint32_t pitch = RowPitch(level);
    const uint32_t slice = SlicePitch(level);
    const uint8_t *data = Cpu(layer, level) + size_t(box.Front) * slice + size_t(Blocks(*format, box.Top)) * pitch +
                          size_t(Blocks(*format, box.Left)) * format->bytes;
    const gb::Box region = {uint16_t(box.Left),
                            uint16_t(box.Top),
                            uint16_t(box.Front),
                            uint16_t(box.Right - box.Left),
                            uint16_t(box.Bottom - box.Top),
                            uint16_t(box.Back - box.Front)};
    gb::UploadTexture(gpu, uint8_t(level), uint8_t(layer), region, data, pitch, slice);
}

HRESULT CreateStorage(Device *device, D3DRESOURCETYPE type, uint32_t width, uint32_t height, uint32_t depth,
                      uint32_t levels, DWORD usage, D3DFORMAT format, D3DPOOL pool, D3DMULTISAMPLE_TYPE multiSample,
                      std::shared_ptr<Storage> &out)
{
    const FormatInfo *info = FindFormat(format);
    if (!info)
    {
        char key[32];
        std::snprintf(key, sizeof(key), "format %08x", unsigned(format));
        WarnOnce(key, "d3d9emu: format 0x%08x is not supported", unsigned(format));
        return D3DERR_INVALIDCALL;
    }
    const gb::Caps &caps = gb::GetCaps();
    const bool renderTarget = usage & D3DUSAGE_RENDERTARGET;
    const bool depthStencil = usage & D3DUSAGE_DEPTHSTENCIL;
    const bool cpuPool = pool == D3DPOOL_SYSTEMMEM || pool == D3DPOOL_SCRATCH;
    const bool layered = type == D3DRTYPE_CUBETEXTURE;
    const uint32_t maxLevels = MaxLevels(width, height, type == D3DRTYPE_VOLUMETEXTURE ? depth : 1);
    if (!levels)
        levels = maxLevels;
    // Same checks as before, kept in order (each is independent, so an else-if chain gives the first failure); a
    // render-target/depth-stencil rejection is logged once with its inputs and the backend caps, because the engine
    // chose those from GetCaps/CheckDevice* and a caps mismatch here is otherwise invisible.
    const char *reject = nullptr;
    if (!width || !height || !depth || width > caps.maxTextureSize || height > caps.maxTextureSize ||
        (type == D3DRTYPE_VOLUMETEXTURE && (width > 2048 || height > 2048 || depth > 2048)))
        reject = "dimensions";
    else if (pool > D3DPOOL_SCRATCH || (pool == D3DPOOL_MANAGED && device->IsEx()))
        reject = "pool";
    else if ((renderTarget || depthStencil) && (pool != D3DPOOL_DEFAULT || (renderTarget && depthStencil)))
        reject = "rt/ds pool";
    else if (info->depth != depthStencil || (renderTarget && !info->renderTarget) ||
             (!info->texture && type != D3DRTYPE_SURFACE))
        reject = "format role";
    else if (info->depth && type != D3DRTYPE_TEXTURE && type != D3DRTYPE_SURFACE)
        reject = "depth type";
    else if (info->format == gb::Format::Unknown && !(type == D3DRTYPE_SURFACE && renderTarget))
        reject = "unknown format";
    else if (SampleCount(multiSample) > 1 &&
             (type != D3DRTYPE_SURFACE || cpuPool || SampleCount(multiSample) > caps.maxSamples))
        reject = "multisample";
    else if (levels > maxLevels)
        reject = "levels";
    if (reject)
    {
        if (renderTarget || depthStencil)
            WarnOnce(reject,
                     "d3d9emu: CreateStorage reject '%s' (rt=%d ds=%d): %ux%u fmt=0x%08x usage=0x%lx pool=%d ms=%d / "
                     "caps maxTex=%u maxSamp=%u infoDepth=%d infoRT=%d infoTex=%d infoFmt=%d",
                     reject, int(renderTarget), int(depthStencil), width, height, unsigned(format),
                     (unsigned long)usage, int(pool), int(multiSample), caps.maxTextureSize, caps.maxSamples,
                     int(info->depth), int(info->renderTarget), int(info->texture), int(info->format));
        return D3DERR_INVALIDCALL;
    }

    auto storage = std::make_shared<Storage>();
    Storage &s = *storage;
    s.device = device;
    s.format = info;
    s.type = type;
    s.pool = pool;
    s.usage = usage;
    s.multiSample = SampleCount(multiSample) > 1 ? multiSample : D3DMULTISAMPLE_NONE;
    s.width = width;
    s.height = height;
    s.depth = type == D3DRTYPE_VOLUMETEXTURE ? depth : 1;
    s.levels = levels;
    s.layers = layered ? 6 : 1;
    const bool plainSurface = type == D3DRTYPE_SURFACE && !renderTarget && !depthStencil;
    s.lockable = pool != D3DPOOL_DEFAULT || (usage & D3DUSAGE_DYNAMIC) || plainSurface;
    s.shadowTest = info->depth && type == D3DRTYPE_TEXTURE && format != FOURCC_INTZ;
    s.subresources.resize(s.layers * s.levels);

    if (!cpuPool && info->format != gb::Format::Unknown)
    {
        gb::TextureDesc desc = {};
        desc.type = type == D3DRTYPE_CUBETEXTURE     ? gb::TextureType::Cube
                    : type == D3DRTYPE_VOLUMETEXTURE ? gb::TextureType::Tex3D
                                                     : gb::TextureType::Tex2D;
        desc.format = info->format;
        desc.usage = renderTarget                                ? gb::Usage::RenderTarget
                     : depthStencil                              ? gb::Usage::DepthStencil
                     : (usage & D3DUSAGE_DYNAMIC) || plainSurface ? gb::Usage::Dynamic
                                                                  : gb::Usage::Static;
        desc.sampled = type != D3DRTYPE_SURFACE || plainSurface;
        desc.width = uint16_t(width);
        desc.height = uint16_t(height);
        desc.depth = uint16_t(s.depth);
        desc.levels = uint8_t(levels);
        desc.samples = SampleCount(multiSample);
        desc.swizzle = info->swizzle;
        s.gpu = gb::CreateTexture(desc);
        if (!s.gpu)
            return D3DERR_OUTOFVIDEOMEMORY;
    }
    out = std::move(storage);
    return D3D_OK;
}

// ---- textures

TextureImpl::TextureImpl(std::shared_ptr<Storage> s) : storage(std::move(s))
{
    surfaces.resize(storage->layers * storage->levels);
    storage->device->AddRef();
}

TextureImpl::~TextureImpl() = default;

IUnknown *TextureImpl::Parent()
{
    return storage->device;
}

Surface *TextureImpl::SurfaceAt(uint32_t layer, uint32_t level)
{
    std::unique_ptr<Surface> &slot = surfaces[layer * storage->levels + level];
    if (!slot)
        slot.reset(new Surface(storage, layer, level, this));
    return slot.get();
}

TextureImpl *ImplOf(IDirect3DBaseTexture9 *texture)
{
    if (!texture)
        return nullptr;
    switch (texture->GetType())
    {
    case D3DRTYPE_TEXTURE:
        return static_cast<Texture *>(static_cast<IDirect3DTexture9 *>(texture));
    case D3DRTYPE_CUBETEXTURE:
        return static_cast<CubeTexture *>(static_cast<IDirect3DCubeTexture9 *>(texture));
    case D3DRTYPE_VOLUMETEXTURE:
        return static_cast<VolumeTexture *>(static_cast<IDirect3DVolumeTexture9 *>(texture));
    default:
        return nullptr;
    }
}

template <class I>
HRESULT TextureBase<I>::GetDevice(IDirect3DDevice9 **device)
{
    if (!device)
        return D3DERR_INVALIDCALL;
    storage->device->AddRef();
    *device = storage->device;
    return D3D_OK;
}

template class TextureBase<IDirect3DTexture9>;
template class TextureBase<IDirect3DCubeTexture9>;
template class TextureBase<IDirect3DVolumeTexture9>;

HRESULT Texture::GetLevelDesc(UINT level, D3DSURFACE_DESC *desc)
{
    if (level >= storage->levels || !desc)
        return D3DERR_INVALIDCALL;
    FillSurfaceDesc(*storage, level, desc);
    return D3D_OK;
}

HRESULT Texture::GetSurfaceLevel(UINT level, IDirect3DSurface9 **surface)
{
    if (!surface)
        return D3DERR_INVALIDCALL;
    *surface = nullptr;
    if (level >= storage->levels)
        return D3DERR_INVALIDCALL;
    Guard guard(DeviceLock());
    Surface *s = SurfaceAt(0, level);
    s->AddRef();
    *surface = s;
    return D3D_OK;
}

HRESULT Texture::LockRect(UINT level, D3DLOCKED_RECT *locked, CONST RECT *rect, DWORD flags)
{
    Guard guard(DeviceLock());
    D3DBOX box;
    if (!locked || level >= storage->levels || !RectToBox(*storage, level, rect, &box))
        return D3DERR_INVALIDCALL;
    return storage->Lock(0, level, &box, flags, &locked->pBits, &locked->Pitch, nullptr);
}

HRESULT Texture::UnlockRect(UINT level)
{
    Guard guard(DeviceLock());
    return storage->Unlock(0, level);
}

HRESULT CubeTexture::GetLevelDesc(UINT level, D3DSURFACE_DESC *desc)
{
    if (level >= storage->levels || !desc)
        return D3DERR_INVALIDCALL;
    FillSurfaceDesc(*storage, level, desc);
    return D3D_OK;
}

HRESULT CubeTexture::GetCubeMapSurface(D3DCUBEMAP_FACES face, UINT level, IDirect3DSurface9 **surface)
{
    if (!surface)
        return D3DERR_INVALIDCALL;
    *surface = nullptr;
    if (UINT(face) >= 6 || level >= storage->levels)
        return D3DERR_INVALIDCALL;
    Guard guard(DeviceLock());
    Surface *s = SurfaceAt(UINT(face), level);
    s->AddRef();
    *surface = s;
    return D3D_OK;
}

HRESULT CubeTexture::LockRect(D3DCUBEMAP_FACES face, UINT level, D3DLOCKED_RECT *locked, CONST RECT *rect,
                              DWORD flags)
{
    Guard guard(DeviceLock());
    D3DBOX box;
    if (!locked || UINT(face) >= 6 || level >= storage->levels || !RectToBox(*storage, level, rect, &box))
        return D3DERR_INVALIDCALL;
    return storage->Lock(UINT(face), level, &box, flags, &locked->pBits, &locked->Pitch, nullptr);
}

HRESULT CubeTexture::UnlockRect(D3DCUBEMAP_FACES face, UINT level)
{
    Guard guard(DeviceLock());
    return storage->Unlock(UINT(face), level);
}

HRESULT VolumeTexture::GetLevelDesc(UINT level, D3DVOLUME_DESC *desc)
{
    if (level >= storage->levels || !desc)
        return D3DERR_INVALIDCALL;
    desc->Format = storage->format->d3d;
    desc->Type = D3DRTYPE_VOLUME;
    desc->Usage = storage->usage;
    desc->Pool = storage->pool;
    desc->Width = storage->Width(level);
    desc->Height = storage->Height(level);
    desc->Depth = storage->Depth(level);
    return D3D_OK;
}

HRESULT VolumeTexture::LockBox(UINT level, D3DLOCKED_BOX *locked, CONST D3DBOX *box, DWORD flags)
{
    Guard guard(DeviceLock());
    if (!locked)
        return D3DERR_INVALIDCALL;
    return storage->Lock(0, level, box, flags, &locked->pBits, &locked->RowPitch, &locked->SlicePitch);
}

HRESULT VolumeTexture::UnlockBox(UINT level)
{
    Guard guard(DeviceLock());
    return storage->Unlock(0, level);
}

// ---- surfaces

Surface::Surface(std::shared_ptr<Storage> storage, uint32_t layer, uint32_t level, Counted *container)
    : m_storage(std::move(storage)), m_layer(layer), m_level(level), m_container(container)
{
    if (!m_container)
        m_storage->device->AddRef();
}

Surface::~Surface() = default;

ULONG Surface::AddRefCount()
{
    return m_container ? m_container->AddRefCount() : Counted::AddRefCount();
}

ULONG Surface::ReleaseCount()
{
    return m_container ? m_container->ReleaseCount() : Counted::ReleaseCount();
}

void Surface::Bind()
{
    if (m_container)
        m_container->Bind();
    else
        Counted::Bind();
}

void Surface::Unbind()
{
    if (m_container)
        m_container->Unbind();
    else
        Counted::Unbind();
}

IUnknown *Surface::Parent()
{
    return m_container ? nullptr : m_storage->device;
}

HRESULT Surface::GetDevice(IDirect3DDevice9 **device)
{
    if (!device)
        return D3DERR_INVALIDCALL;
    m_storage->device->AddRef();
    *device = m_storage->device;
    return D3D_OK;
}

HRESULT Surface::GetDesc(D3DSURFACE_DESC *desc)
{
    if (!desc)
        return D3DERR_INVALIDCALL;
    FillSurfaceDesc(*m_storage, m_level, desc);
    return D3D_OK;
}

HRESULT Surface::LockRect(D3DLOCKED_RECT *locked, CONST RECT *rect, DWORD flags)
{
    Guard guard(DeviceLock());
    D3DBOX box;
    if (!locked || !RectToBox(*m_storage, m_level, rect, &box))
        return D3DERR_INVALIDCALL;
    return m_storage->Lock(m_layer, m_level, &box, flags, &locked->pBits, &locked->Pitch, nullptr);
}

HRESULT Surface::UnlockRect()
{
    Guard guard(DeviceLock());
    return m_storage->Unlock(m_layer, m_level);
}

// ---- buffers

BufferStorage::~BufferStorage()
{
    gb::DestroyBuffer(gpu);
}

HRESULT BufferStorage::Lock(UINT offset, UINT length, void **data, DWORD flags)
{
    if (!data)
        return D3DERR_INVALIDCALL;
    if (!length && offset < size)
        length = size - offset;
    if (!length || uint64_t(offset) + length > size)
        return D3DERR_INVALIDCALL;
    // DISCARD and NOOVERWRITE only mean something for dynamic buffers; the runtime ignores them otherwise.
    gb::Upload mode = gb::Upload::Ordered;
    if (usage & D3DUSAGE_DYNAMIC)
        mode = (flags & D3DLOCK_DISCARD)       ? gb::Upload::Discard
               : (flags & D3DLOCK_NOOVERWRITE) ? gb::Upload::NoOverwrite
                                               : gb::Upload::Ordered;
    if (!lockDepth)
    {
        lockBegin = offset;
        lockEnd = offset + length;
        lockMode = mode;
        lockWrites = false;
    }
    else
    {
        lockBegin = offset < lockBegin ? offset : lockBegin;
        lockEnd = offset + length > lockEnd ? offset + length : lockEnd;
        if (mode == gb::Upload::Ordered)
            lockMode = gb::Upload::Ordered;
    }
    if (!(flags & D3DLOCK_READONLY))
        lockWrites = true;
    ++lockDepth;
    *data = cpu.data() + offset;
    return D3D_OK;
}

HRESULT BufferStorage::Unlock()
{
    if (!lockDepth)
        return D3DERR_INVALIDCALL;
    if (--lockDepth)
        return D3D_OK;
    if (gpu && lockWrites)
        gb::UploadBuffer(gpu, lockBegin, cpu.data() + lockBegin, lockEnd - lockBegin, lockMode);
    return D3D_OK;
}

HRESULT CreateBufferStorage(Device *device, gb::BufferType type, UINT size, DWORD usage, D3DPOOL pool,
                            BufferStorage &out)
{
    if (!size || pool > D3DPOOL_SYSTEMMEM || (pool == D3DPOOL_MANAGED && device->IsEx()))
        return D3DERR_INVALIDCALL;
    out.device = device;
    out.size = size;
    out.usage = usage;
    out.pool = pool;
    out.cpu.assign(size, 0);
    if (pool != D3DPOOL_SYSTEMMEM)
    {
        const bool dynamic = usage & D3DUSAGE_DYNAMIC;
        const gb::BufferDesc desc = {type, dynamic ? gb::Usage::Dynamic : gb::Usage::Static,
                                     dynamic ? gb::Memory::Shared : gb::Memory::Device, size};
        out.gpu = gb::CreateBuffer(desc);
        if (!out.gpu)
            return D3DERR_OUTOFVIDEOMEMORY;
    }
    return D3D_OK;
}

VertexBuffer::VertexBuffer(Device *device, DWORD fvf) : m_fvf(fvf)
{
    storage.device = device;
    device->AddRef();
}

VertexBuffer::~VertexBuffer() = default;

IUnknown *VertexBuffer::Parent()
{
    return storage.device;
}

HRESULT VertexBuffer::GetDevice(IDirect3DDevice9 **device)
{
    if (!device)
        return D3DERR_INVALIDCALL;
    storage.device->AddRef();
    *device = storage.device;
    return D3D_OK;
}

HRESULT VertexBuffer::Lock(UINT offset, UINT size, void **data, DWORD flags)
{
    Guard guard(DeviceLock());
    return storage.Lock(offset, size, data, flags);
}

HRESULT VertexBuffer::Unlock()
{
    Guard guard(DeviceLock());
    return storage.Unlock();
}

HRESULT VertexBuffer::GetDesc(D3DVERTEXBUFFER_DESC *desc)
{
    if (!desc)
        return D3DERR_INVALIDCALL;
    desc->Format = D3DFMT_VERTEXDATA;
    desc->Type = D3DRTYPE_VERTEXBUFFER;
    desc->Usage = storage.usage;
    desc->Pool = storage.pool;
    desc->Size = storage.size;
    desc->FVF = m_fvf;
    return D3D_OK;
}

IndexBuffer::IndexBuffer(Device *device, D3DFORMAT indexFormat) : format(indexFormat)
{
    storage.device = device;
    device->AddRef();
}

IndexBuffer::~IndexBuffer() = default;

IUnknown *IndexBuffer::Parent()
{
    return storage.device;
}

HRESULT IndexBuffer::GetDevice(IDirect3DDevice9 **device)
{
    if (!device)
        return D3DERR_INVALIDCALL;
    storage.device->AddRef();
    *device = storage.device;
    return D3D_OK;
}

HRESULT IndexBuffer::Lock(UINT offset, UINT size, void **data, DWORD flags)
{
    Guard guard(DeviceLock());
    return storage.Lock(offset, size, data, flags);
}

HRESULT IndexBuffer::Unlock()
{
    Guard guard(DeviceLock());
    return storage.Unlock();
}

HRESULT IndexBuffer::GetDesc(D3DINDEXBUFFER_DESC *desc)
{
    if (!desc)
        return D3DERR_INVALIDCALL;
    desc->Format = format;
    desc->Type = D3DRTYPE_INDEXBUFFER;
    desc->Usage = storage.usage;
    desc->Pool = storage.pool;
    desc->Size = storage.size;
    return D3D_OK;
}
} // namespace d3d9emu
