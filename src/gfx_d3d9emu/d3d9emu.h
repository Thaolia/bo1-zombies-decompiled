#pragma once

// The D3D9 frontend's objects: the part of Direct3D 9 the engine calls (inventoried in docs/RENDERER.md), recorded
// into the backend contract (src/gfx_backend/gfx_backend.h). No GL or deko3d here.

#include <d3d9.h>

#include <gfx_backend/gfx_backend.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace d3d9emu
{
// Every call into the frontend takes this lock, as the runtime does for a D3DCREATE_MULTITHREADED device; the
// backend so sees one call at a time.
std::recursive_mutex &DeviceLock();
using Guard = std::lock_guard<std::recursive_mutex>;

void Logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
// Logs a line the first time `key` is seen.
void WarnOnce(const char *key, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
// A method the engine never calls: ends the process naming it, so that a new caller is noticed at once.
[[noreturn]] void Unemulated(const char *method);

// ---- formats (d3d9emu_format.cpp)

inline constexpr D3DFORMAT FOURCC_NULL = D3DFORMAT(MAKEFOURCC('N', 'U', 'L', 'L'));
inline constexpr D3DFORMAT FOURCC_INTZ = D3DFORMAT(MAKEFOURCC('I', 'N', 'T', 'Z'));
inline constexpr D3DFORMAT FOURCC_RESZ = D3DFORMAT(MAKEFOURCC('R', 'E', 'S', 'Z'));
// D3DRS_POINTSIZE value that resolves the depth buffer into the INTZ texture bound at sampler 0 (AMD's RESZ).
inline constexpr DWORD RESZ_CODE = 0x7FA05000;

struct FormatInfo
{
    D3DFORMAT d3d;
    gb::Format format; // Unknown for FOURCC_NULL, which has no memory
    gb::Swizzle swizzle;
    uint8_t bytes;     // per texel, or per 4x4 block
    bool block;
    bool texture;      // usable as a texture
    bool renderTarget; // usable as a colour target
    bool depth;        // a depth-stencil format
    bool stencil;
};

// Null for a format the adapter does not support.
const FormatInfo *FindFormat(D3DFORMAT format);

inline uint32_t LevelExtent(uint32_t extent, uint32_t level)
{
    return extent >> level ? extent >> level : 1;
}

// Samples per pixel; D3DMULTISAMPLE_NONMASKABLE's quality levels are not emulated, so it has one.
inline uint8_t SampleCount(D3DMULTISAMPLE_TYPE type)
{
    return type >= D3DMULTISAMPLE_2_SAMPLES ? uint8_t(type) : 1;
}

// ---- reference counting

// The two reference counts of every object: the application's (AddRef/Release) and the device's while the object is
// bound, as the runtime keeps a bound texture alive after its last Release. A resource holds a reference on its
// device for as long as the application holds one on the resource.
class Counted
{
public:
    Counted() = default;
    Counted(const Counted &) = delete;
    Counted &operator=(const Counted &) = delete;

    virtual ULONG AddRefCount();
    virtual ULONG ReleaseCount();
    virtual void Bind();
    virtual void Unbind();

protected:
    virtual ~Counted() = default;
    // The reference this object holds while the application holds it (its device), or null.
    virtual IUnknown *Parent()
    {
        return nullptr;
    }

private:
    ULONG m_refs = 1;
    ULONG m_binds = 0;
};

// Binds `next` in place of `slot` (null allowed for both).
template <class T>
void Rebind(T *&slot, T *next)
{
    if (slot == next)
        return;
    if (next)
        next->Bind();
    T *previous = slot;
    slot = next;
    if (previous)
        previous->Unbind();
}

#define D3D9EMU_IUNKNOWN                                                                                              \
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **object) override                                         \
    {                                                                                                                  \
        if (object)                                                                                                    \
            *object = nullptr;                                                                                         \
        return E_NOINTERFACE;                                                                                          \
    }                                                                                                                  \
    ULONG STDMETHODCALLTYPE AddRef() override                                                                         \
    {                                                                                                                  \
        return AddRefCount();                                                                                          \
    }                                                                                                                  \
    ULONG STDMETHODCALLTYPE Release() override                                                                        \
    {                                                                                                                  \
        return ReleaseCount();                                                                                         \
    }

#define D3D9EMU_UNEMULATED(type, name, params)                                                                        \
    type STDMETHODCALLTYPE name params override                                                                        \
    {                                                                                                                  \
        Unemulated(__PRETTY_FUNCTION__);                                                                              \
    }

#define D3D9EMU_PRIVATE_DATA                                                                                          \
    D3D9EMU_UNEMULATED(HRESULT, SetPrivateData, (REFGUID, CONST void *, DWORD, DWORD))                              \
    D3D9EMU_UNEMULATED(HRESULT, GetPrivateData, (REFGUID, void *, DWORD *))                                         \
    D3D9EMU_UNEMULATED(HRESULT, FreePrivateData, (REFGUID))

class Device;
class Surface;

// ---- texture storage (d3d9emu_resources.cpp)

// What a texture, cube or volume texture or standalone surface stores: the backend texture, and for lockable
// resources a CPU copy of each locked level (the runtime's managed copy), so that a lock sees what was written.
struct Storage
{
    Device *device;
    const FormatInfo *format;
    D3DRESOURCETYPE type; // D3DRTYPE_TEXTURE, D3DRTYPE_CUBETEXTURE, D3DRTYPE_VOLUMETEXTURE or D3DRTYPE_SURFACE
    D3DPOOL pool;
    DWORD usage;
    D3DMULTISAMPLE_TYPE multiSample;
    uint32_t width, height, depth, levels, layers;
    bool lockable;
    bool shadowTest; // a depth texture sampled as a hardware shadow map (not INTZ)
    gb::Texture *gpu = nullptr; // null in the CPU pools and for FOURCC_NULL surfaces
    bool gpuNewer = false;      // the backend wrote it (StretchRect) since the CPU copies were made

    struct Subresource
    {
        std::vector<uint8_t> cpu; // sized at the first lock, or at creation in the CPU pools
        bool locked = false;
        DWORD lockFlags = 0;
        D3DBOX lockBox = {};
    };
    std::vector<Subresource> subresources; // layer * levels + level

    ~Storage();
    uint32_t Width(uint32_t level) const
    {
        return LevelExtent(width, level);
    }
    uint32_t Height(uint32_t level) const
    {
        return LevelExtent(height, level);
    }
    uint32_t Depth(uint32_t level) const
    {
        return type == D3DRTYPE_VOLUMETEXTURE ? LevelExtent(depth, level) : 1;
    }
    uint32_t RowPitch(uint32_t level) const;
    uint32_t Rows(uint32_t level) const; // rows of texels, or of blocks
    uint32_t SlicePitch(uint32_t level) const
    {
        return RowPitch(level) * Rows(level);
    }
    bool CpuPool() const
    {
        return pool == D3DPOOL_SYSTEMMEM || pool == D3DPOOL_SCRATCH;
    }
    // The CPU copy of a subresource, allocated (zeroed) if needed.
    uint8_t *Cpu(uint32_t layer, uint32_t level);
    HRESULT Lock(uint32_t layer, uint32_t level, const D3DBOX *box, DWORD flags, void **bits, INT *rowPitch,
                 INT *slicePitch);
    HRESULT Unlock(uint32_t layer, uint32_t level);
    // Copies a region of this subresource's CPU copy to the backend texture.
    void Upload(uint32_t layer, uint32_t level, const D3DBOX &box);
};

// Creates the storage for a resource the application asked for; D3DERR_INVALIDCALL or D3DERR_NOTAVAILABLE as the
// runtime would answer otherwise.
HRESULT CreateStorage(Device *device, D3DRESOURCETYPE type, uint32_t width, uint32_t height, uint32_t depth,
                      uint32_t levels, DWORD usage, D3DFORMAT format, D3DPOOL pool, D3DMULTISAMPLE_TYPE multiSample,
                      std::shared_ptr<Storage> &out);

// Our side of the three texture interfaces.
class TextureImpl : public Counted
{
public:
    std::shared_ptr<Storage> storage;
    // Child surfaces by subresource, made on demand; they share this object's reference counts.
    std::vector<std::unique_ptr<Surface>> surfaces;

    Surface *SurfaceAt(uint32_t layer, uint32_t level);

protected:
    explicit TextureImpl(std::shared_ptr<Storage> storage);
    ~TextureImpl() override;
    IUnknown *Parent() override;
};

TextureImpl *ImplOf(IDirect3DBaseTexture9 *texture);

template <class I>
class TextureBase : public I, public TextureImpl
{
public:
    D3D9EMU_IUNKNOWN
    D3D9EMU_PRIVATE_DATA

    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **device) override;
    DWORD STDMETHODCALLTYPE SetPriority(DWORD priority) override
    {
        const DWORD previous = m_priority;
        m_priority = priority;
        return previous;
    }
    DWORD STDMETHODCALLTYPE GetPriority() override
    {
        return m_priority;
    }
    void STDMETHODCALLTYPE PreLoad() override
    {
    }
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override
    {
        return storage->type;
    }
    DWORD STDMETHODCALLTYPE SetLOD(DWORD) override
    {
        return 0;
    }
    DWORD STDMETHODCALLTYPE GetLOD() override
    {
        return 0;
    }
    DWORD STDMETHODCALLTYPE GetLevelCount() override
    {
        return storage->levels;
    }
    D3D9EMU_UNEMULATED(HRESULT, SetAutoGenFilterType, (D3DTEXTUREFILTERTYPE))
    D3D9EMU_UNEMULATED(D3DTEXTUREFILTERTYPE, GetAutoGenFilterType, ())
    D3D9EMU_UNEMULATED(void, GenerateMipSubLevels, ())

    explicit TextureBase(std::shared_ptr<Storage> storage) : TextureImpl(std::move(storage))
    {
    }

private:
    DWORD m_priority = 0;
};

class Texture final : public TextureBase<IDirect3DTexture9>
{
public:
    using TextureBase::TextureBase;
    HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT level, D3DSURFACE_DESC *desc) override;
    HRESULT STDMETHODCALLTYPE GetSurfaceLevel(UINT level, IDirect3DSurface9 **surface) override;
    HRESULT STDMETHODCALLTYPE LockRect(UINT level, D3DLOCKED_RECT *locked, CONST RECT *rect, DWORD flags) override;
    HRESULT STDMETHODCALLTYPE UnlockRect(UINT level) override;
    D3D9EMU_UNEMULATED(HRESULT, AddDirtyRect, (CONST RECT *))
};

class CubeTexture final : public TextureBase<IDirect3DCubeTexture9>
{
public:
    using TextureBase::TextureBase;
    HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT level, D3DSURFACE_DESC *desc) override;
    HRESULT STDMETHODCALLTYPE GetCubeMapSurface(D3DCUBEMAP_FACES face, UINT level, IDirect3DSurface9 **surface) override;
    HRESULT STDMETHODCALLTYPE LockRect(D3DCUBEMAP_FACES face, UINT level, D3DLOCKED_RECT *locked, CONST RECT *rect,
                                       DWORD flags) override;
    HRESULT STDMETHODCALLTYPE UnlockRect(D3DCUBEMAP_FACES face, UINT level) override;
    D3D9EMU_UNEMULATED(HRESULT, AddDirtyRect, (D3DCUBEMAP_FACES, CONST RECT *))
};

class VolumeTexture final : public TextureBase<IDirect3DVolumeTexture9>
{
public:
    using TextureBase::TextureBase;
    HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT level, D3DVOLUME_DESC *desc) override;
    D3D9EMU_UNEMULATED(HRESULT, GetVolumeLevel, (UINT, IDirect3DVolume9 **))
    HRESULT STDMETHODCALLTYPE LockBox(UINT level, D3DLOCKED_BOX *locked, CONST D3DBOX *box, DWORD flags) override;
    HRESULT STDMETHODCALLTYPE UnlockBox(UINT level) override;
    D3D9EMU_UNEMULATED(HRESULT, AddDirtyBox, (CONST D3DBOX *))
};

// A level of a texture (its counts are the container's), a back buffer (the device's), or a standalone surface.
class Surface final : public IDirect3DSurface9, public Counted
{
public:
    // A standalone surface (no container) holds a reference on its device; the others share their container's
    // counts.
    Surface(std::shared_ptr<Storage> storage, uint32_t layer, uint32_t level, Counted *container);
    ~Surface() override;

    D3D9EMU_IUNKNOWN
    D3D9EMU_PRIVATE_DATA
    ULONG AddRefCount() override;
    ULONG ReleaseCount() override;
    void Bind() override;
    void Unbind() override;

    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **device) override;
    DWORD STDMETHODCALLTYPE SetPriority(DWORD) override
    {
        return 0;
    }
    DWORD STDMETHODCALLTYPE GetPriority() override
    {
        return 0;
    }
    void STDMETHODCALLTYPE PreLoad() override
    {
    }
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override
    {
        return D3DRTYPE_SURFACE;
    }
    D3D9EMU_UNEMULATED(HRESULT, GetContainer, (REFIID, void **))
    HRESULT STDMETHODCALLTYPE GetDesc(D3DSURFACE_DESC *desc) override;
    HRESULT STDMETHODCALLTYPE LockRect(D3DLOCKED_RECT *locked, CONST RECT *rect, DWORD flags) override;
    HRESULT STDMETHODCALLTYPE UnlockRect() override;
    D3D9EMU_UNEMULATED(HRESULT, GetDC, (HDC *))
    D3D9EMU_UNEMULATED(HRESULT, ReleaseDC, (HDC))

    Storage &GetStorage() const
    {
        return *m_storage;
    }
    uint32_t Layer() const
    {
        return m_layer;
    }
    uint32_t Level() const
    {
        return m_level;
    }
    uint32_t Width() const
    {
        return m_storage->Width(m_level);
    }
    uint32_t Height() const
    {
        return m_storage->Height(m_level);
    }

protected:
    IUnknown *Parent() override;

private:
    std::shared_ptr<Storage> m_storage;
    uint32_t m_layer;
    uint32_t m_level;
    Counted *m_container; // the texture or device whose counts this surface shares, or null
};

// ---- buffers (d3d9emu_resources.cpp)

struct BufferStorage
{
    Device *device;
    gb::Buffer *gpu = nullptr;
    std::vector<uint8_t> cpu;
    UINT size;
    DWORD usage;
    D3DPOOL pool;
    unsigned lockDepth = 0;
    UINT lockBegin = 0, lockEnd = 0;
    gb::Upload lockMode = gb::Upload::Ordered;
    bool lockWrites = false;

    ~BufferStorage();
    HRESULT Lock(UINT offset, UINT size, void **data, DWORD flags);
    HRESULT Unlock();
};

HRESULT CreateBufferStorage(Device *device, gb::BufferType type, UINT size, DWORD usage, D3DPOOL pool,
                            BufferStorage &out);

class VertexBuffer final : public IDirect3DVertexBuffer9, public Counted
{
public:
    VertexBuffer(Device *device, DWORD fvf);
    ~VertexBuffer() override;
    D3D9EMU_IUNKNOWN
    D3D9EMU_PRIVATE_DATA
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **device) override;
    DWORD STDMETHODCALLTYPE SetPriority(DWORD) override
    {
        return 0;
    }
    DWORD STDMETHODCALLTYPE GetPriority() override
    {
        return 0;
    }
    void STDMETHODCALLTYPE PreLoad() override
    {
    }
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override
    {
        return D3DRTYPE_VERTEXBUFFER;
    }
    HRESULT STDMETHODCALLTYPE Lock(UINT offset, UINT size, void **data, DWORD flags) override;
    HRESULT STDMETHODCALLTYPE Unlock() override;
    HRESULT STDMETHODCALLTYPE GetDesc(D3DVERTEXBUFFER_DESC *desc) override;

    BufferStorage storage;

protected:
    IUnknown *Parent() override;

private:
    DWORD m_fvf;
};

class IndexBuffer final : public IDirect3DIndexBuffer9, public Counted
{
public:
    IndexBuffer(Device *device, D3DFORMAT format);
    ~IndexBuffer() override;
    D3D9EMU_IUNKNOWN
    D3D9EMU_PRIVATE_DATA
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **device) override;
    DWORD STDMETHODCALLTYPE SetPriority(DWORD) override
    {
        return 0;
    }
    DWORD STDMETHODCALLTYPE GetPriority() override
    {
        return 0;
    }
    void STDMETHODCALLTYPE PreLoad() override
    {
    }
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override
    {
        return D3DRTYPE_INDEXBUFFER;
    }
    HRESULT STDMETHODCALLTYPE Lock(UINT offset, UINT size, void **data, DWORD flags) override;
    HRESULT STDMETHODCALLTYPE Unlock() override;
    HRESULT STDMETHODCALLTYPE GetDesc(D3DINDEXBUFFER_DESC *desc) override;

    BufferStorage storage;
    D3DFORMAT format;

protected:
    IUnknown *Parent() override;
};

// ---- shaders and vertex declarations (d3d9emu_shaders.cpp)

// The length in bytes of a D3D9 shader, up to and including its end token; 0 if it has none within `maxBytes`.
size_t ShaderBytecodeSize(const DWORD *function, size_t maxBytes);

class VertexShader final : public IDirect3DVertexShader9, public Counted
{
public:
    VertexShader(Device *device, gb::Shader *shader, uint64_t hash);
    ~VertexShader() override;
    D3D9EMU_IUNKNOWN
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **device) override;
    D3D9EMU_UNEMULATED(HRESULT, GetFunction, (void *, UINT *))

    gb::Shader *const shader; // null when shaders.pack lacks it
    const uint64_t hash;

protected:
    IUnknown *Parent() override;

private:
    Device *m_device;
};

class PixelShader final : public IDirect3DPixelShader9, public Counted
{
public:
    PixelShader(Device *device, gb::Shader *shader, uint64_t hash, uint32_t samplerMask);
    ~PixelShader() override;
    D3D9EMU_IUNKNOWN
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **device) override;
    D3D9EMU_UNEMULATED(HRESULT, GetFunction, (void *, UINT *))

    gb::Shader *const shader; // null when shaders.pack lacks it
    const uint64_t hash;
    const uint32_t samplerMask; // the samplers it declares

protected:
    IUnknown *Parent() override;

private:
    Device *m_device;
};

// Creates the backend shader for D3D9 bytecode from shaders.pack; null (logged once per shader) if it is not there.
gb::Shader *CreatePackedShader(gb::Stage stage, uint64_t hash);
// The samplers (s0-s15) a pixel shader declares.
uint32_t PixelShaderSamplers(const DWORD *function, size_t bytes);

class VertexDeclaration final : public IDirect3DVertexDeclaration9, public Counted
{
public:
    VertexDeclaration(Device *device, std::vector<D3DVERTEXELEMENT9> elements, const gb::VertexAttrib *attribs,
                      uint8_t attribCount);
    ~VertexDeclaration() override;
    D3D9EMU_IUNKNOWN
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **device) override;
    D3D9EMU_UNEMULATED(HRESULT, GetDeclaration, (D3DVERTEXELEMENT9 *, UINT *))

    const std::vector<D3DVERTEXELEMENT9> elements;
    gb::VertexAttrib attribs[gb::MAX_ATTRIBS];
    const uint8_t attribCount;

protected:
    IUnknown *Parent() override;

private:
    Device *m_device;
};

// The backend attributes of a declaration (elements whose usage no shader input has are left out); false if one has
// a type the adapter does not report.
bool TranslateDeclaration(const D3DVERTEXELEMENT9 *elements, size_t count, gb::VertexAttrib *attribs,
                          uint8_t *attribCount);

// ---- queries (d3d9emu_queries.cpp)

class Query final : public IDirect3DQuery9, public Counted
{
public:
    Query(Device *device, D3DQUERYTYPE type);
    ~Query() override;
    D3D9EMU_IUNKNOWN
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **device) override;
    D3DQUERYTYPE STDMETHODCALLTYPE GetType() override
    {
        return m_type;
    }
    DWORD STDMETHODCALLTYPE GetDataSize() override;
    HRESULT STDMETHODCALLTYPE Issue(DWORD flags) override;
    HRESULT STDMETHODCALLTYPE GetData(void *data, DWORD size, DWORD flags) override;

    // An occlusion query open across a pass boundary counts in parts, one backend query per pass.
    void SplitAtPassEnd();
    void ResumeInPass();

protected:
    IUnknown *Parent() override;

private:
    void Reset();

    Device *m_device;
    D3DQUERYTYPE m_type;
    bool m_issued = false;   // ended at least once
    bool m_open = false;     // occlusion: between Issue(BEGIN) and Issue(END)
    bool m_partOpen = false; // occlusion: the last part is counting in the current pass
    std::vector<gb::Query *> m_parts;
    gb::Fence *m_fence = nullptr;
};

// ---- the implicit swap chain (d3d9emu_device.cpp)

class SwapChain final : public IDirect3DSwapChain9
{
public:
    explicit SwapChain(Device *device);
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **object) override
    {
        if (object)
            *object = nullptr;
        return E_NOINTERFACE;
    }
    // The swap chain lives as long as its device and shares its counts.
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE Present(CONST RECT *, CONST RECT *, HWND, CONST RGNDATA *, DWORD flags) override;
    HRESULT STDMETHODCALLTYPE GetFrontBufferData(IDirect3DSurface9 *destination) override;
    HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT index, D3DBACKBUFFER_TYPE type, IDirect3DSurface9 **surface) override;
    D3D9EMU_UNEMULATED(HRESULT, GetRasterStatus, (D3DRASTER_STATUS *))
    D3D9EMU_UNEMULATED(HRESULT, GetDisplayMode, (D3DDISPLAYMODE *))
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **device) override;
    D3D9EMU_UNEMULATED(HRESULT, GetPresentParameters, (D3DPRESENT_PARAMETERS *))

private:
    Device *m_device;
};

// ---- the adapter (d3d9emu_direct3d.cpp)

class Direct3D9 final : public IDirect3D9Ex, public Counted
{
public:
    Direct3D9();
    ~Direct3D9() override;
    D3D9EMU_IUNKNOWN

    D3D9EMU_UNEMULATED(HRESULT, RegisterSoftwareDevice, (void *))
    UINT STDMETHODCALLTYPE GetAdapterCount() override;
    HRESULT STDMETHODCALLTYPE GetAdapterIdentifier(UINT adapter, DWORD flags,
                                                   D3DADAPTER_IDENTIFIER9 *identifier) override;
    UINT STDMETHODCALLTYPE GetAdapterModeCount(UINT adapter, D3DFORMAT format) override;
    HRESULT STDMETHODCALLTYPE EnumAdapterModes(UINT adapter, D3DFORMAT format, UINT mode,
                                               D3DDISPLAYMODE *displayMode) override;
    HRESULT STDMETHODCALLTYPE GetAdapterDisplayMode(UINT adapter, D3DDISPLAYMODE *mode) override;
    HRESULT STDMETHODCALLTYPE CheckDeviceType(UINT adapter, D3DDEVTYPE type, D3DFORMAT adapterFormat,
                                              D3DFORMAT backBufferFormat, BOOL windowed) override;
    HRESULT STDMETHODCALLTYPE CheckDeviceFormat(UINT adapter, D3DDEVTYPE type, D3DFORMAT adapterFormat, DWORD usage,
                                                D3DRESOURCETYPE resourceType, D3DFORMAT checkFormat) override;
    HRESULT STDMETHODCALLTYPE CheckDeviceMultiSampleType(UINT adapter, D3DDEVTYPE type, D3DFORMAT surfaceFormat,
                                                         BOOL windowed, D3DMULTISAMPLE_TYPE multiSample,
                                                         DWORD *qualityLevels) override;
    HRESULT STDMETHODCALLTYPE CheckDepthStencilMatch(UINT adapter, D3DDEVTYPE type, D3DFORMAT adapterFormat,
                                                     D3DFORMAT renderTargetFormat,
                                                     D3DFORMAT depthStencilFormat) override;
    D3D9EMU_UNEMULATED(HRESULT, CheckDeviceFormatConversion, (UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT))
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(UINT adapter, D3DDEVTYPE type, D3DCAPS9 *caps) override;
    HMONITOR STDMETHODCALLTYPE GetAdapterMonitor(UINT adapter) override;
    HRESULT STDMETHODCALLTYPE CreateDevice(UINT adapter, D3DDEVTYPE type, HWND focusWindow, DWORD behaviorFlags,
                                           D3DPRESENT_PARAMETERS *parameters, IDirect3DDevice9 **device) override;
    D3D9EMU_UNEMULATED(UINT, GetAdapterModeCountEx, (UINT, CONST D3DDISPLAYMODEFILTER *))
    D3D9EMU_UNEMULATED(HRESULT, EnumAdapterModesEx, (UINT, CONST D3DDISPLAYMODEFILTER *, UINT, D3DDISPLAYMODEEX *))
    D3D9EMU_UNEMULATED(HRESULT, GetAdapterDisplayModeEx, (UINT, D3DDISPLAYMODEEX *, D3DDISPLAYROTATION *))
    HRESULT STDMETHODCALLTYPE CreateDeviceEx(UINT adapter, D3DDEVTYPE type, HWND focusWindow, DWORD behaviorFlags,
                                             D3DPRESENT_PARAMETERS *parameters, D3DDISPLAYMODEEX *fullscreenMode,
                                             IDirect3DDevice9Ex **device) override;
    D3D9EMU_UNEMULATED(HRESULT, GetAdapterLUID, (UINT, LUID *))

    // Whether this object was made by Direct3DCreate9Ex: only those make 9Ex devices.
    bool ex = false;
};

// The adapter's capabilities: those the engine requires of a shader model 3 card (gfx_d3d/r_caps.cpp), limited by
// the backend's.
void FillCaps(D3DCAPS9 *caps);

// ---- the device (d3d9emu_device.cpp)

class Device final : public IDirect3DDevice9Ex, public Counted
{
public:
    static HRESULT Create(Direct3D9 *d3d, bool ex, HWND focusWindow, D3DPRESENT_PARAMETERS *parameters,
                          Device **device);
    ~Device() override;
    D3D9EMU_IUNKNOWN
    // What the device owns (back buffer, swap chain) shares its counts; binding them must not keep it alive.
    void Bind() override
    {
    }
    void Unbind() override
    {
    }

    HRESULT STDMETHODCALLTYPE TestCooperativeLevel() override;
    UINT STDMETHODCALLTYPE GetAvailableTextureMem() override;
    D3D9EMU_UNEMULATED(HRESULT, EvictManagedResources, ())
    D3D9EMU_UNEMULATED(HRESULT, GetDirect3D, (IDirect3D9 **))
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS9 *caps) override;
    D3D9EMU_UNEMULATED(HRESULT, GetDisplayMode, (UINT, D3DDISPLAYMODE *))
    D3D9EMU_UNEMULATED(HRESULT, GetCreationParameters, (D3DDEVICE_CREATION_PARAMETERS *))
    D3D9EMU_UNEMULATED(HRESULT, SetCursorProperties, (UINT, UINT, IDirect3DSurface9 *))
    D3D9EMU_UNEMULATED(void, SetCursorPosition, (int, int, DWORD))
    D3D9EMU_UNEMULATED(BOOL, ShowCursor, (BOOL))
    D3D9EMU_UNEMULATED(HRESULT, CreateAdditionalSwapChain, (D3DPRESENT_PARAMETERS *, IDirect3DSwapChain9 **))
    HRESULT STDMETHODCALLTYPE GetSwapChain(UINT index, IDirect3DSwapChain9 **swapChain) override;
    D3D9EMU_UNEMULATED(UINT, GetNumberOfSwapChains, ())
    HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS *parameters) override;
    HRESULT STDMETHODCALLTYPE Present(CONST RECT *sourceRect, CONST RECT *destRect, HWND destWindowOverride,
                                      CONST RGNDATA *dirtyRegion) override;
    HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT swapChain, UINT index, D3DBACKBUFFER_TYPE type,
                                            IDirect3DSurface9 **surface) override;
    D3D9EMU_UNEMULATED(HRESULT, GetRasterStatus, (UINT, D3DRASTER_STATUS *))
    D3D9EMU_UNEMULATED(HRESULT, SetDialogBoxMode, (BOOL))
    void STDMETHODCALLTYPE SetGammaRamp(UINT swapChain, DWORD flags, CONST D3DGAMMARAMP *ramp) override;
    D3D9EMU_UNEMULATED(void, GetGammaRamp, (UINT, D3DGAMMARAMP *))
    HRESULT STDMETHODCALLTYPE CreateTexture(UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format,
                                            D3DPOOL pool, IDirect3DTexture9 **texture, HANDLE *sharedHandle) override;
    HRESULT STDMETHODCALLTYPE CreateVolumeTexture(UINT width, UINT height, UINT depth, UINT levels, DWORD usage,
                                                  D3DFORMAT format, D3DPOOL pool, IDirect3DVolumeTexture9 **texture,
                                                  HANDLE *sharedHandle) override;
    HRESULT STDMETHODCALLTYPE CreateCubeTexture(UINT edgeLength, UINT levels, DWORD usage, D3DFORMAT format,
                                                D3DPOOL pool, IDirect3DCubeTexture9 **texture,
                                                HANDLE *sharedHandle) override;
    HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT length, DWORD usage, DWORD fvf, D3DPOOL pool,
                                                 IDirect3DVertexBuffer9 **buffer, HANDLE *sharedHandle) override;
    HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT length, DWORD usage, D3DFORMAT format, D3DPOOL pool,
                                                IDirect3DIndexBuffer9 **buffer, HANDLE *sharedHandle) override;
    HRESULT STDMETHODCALLTYPE CreateRenderTarget(UINT width, UINT height, D3DFORMAT format,
                                                 D3DMULTISAMPLE_TYPE multiSample, DWORD multisampleQuality,
                                                 BOOL lockable, IDirect3DSurface9 **surface,
                                                 HANDLE *sharedHandle) override;
    HRESULT STDMETHODCALLTYPE CreateDepthStencilSurface(UINT width, UINT height, D3DFORMAT format,
                                                        D3DMULTISAMPLE_TYPE multiSample, DWORD multisampleQuality,
                                                        BOOL discard, IDirect3DSurface9 **surface,
                                                        HANDLE *sharedHandle) override;
    HRESULT STDMETHODCALLTYPE UpdateSurface(IDirect3DSurface9 *source, CONST RECT *sourceRect,
                                            IDirect3DSurface9 *destination, CONST POINT *destPoint) override;
    D3D9EMU_UNEMULATED(HRESULT, UpdateTexture, (IDirect3DBaseTexture9 *, IDirect3DBaseTexture9 *))
    HRESULT STDMETHODCALLTYPE GetRenderTargetData(IDirect3DSurface9 *renderTarget,
                                                  IDirect3DSurface9 *destination) override;
    HRESULT STDMETHODCALLTYPE GetFrontBufferData(UINT swapChain, IDirect3DSurface9 *destination) override;
    HRESULT STDMETHODCALLTYPE StretchRect(IDirect3DSurface9 *source, CONST RECT *sourceRect,
                                          IDirect3DSurface9 *destination, CONST RECT *destRect,
                                          D3DTEXTUREFILTERTYPE filter) override;
    D3D9EMU_UNEMULATED(HRESULT, ColorFill, (IDirect3DSurface9 *, CONST RECT *, D3DCOLOR))
    HRESULT STDMETHODCALLTYPE CreateOffscreenPlainSurface(UINT width, UINT height, D3DFORMAT format, D3DPOOL pool,
                                                          IDirect3DSurface9 **surface,
                                                          HANDLE *sharedHandle) override;
    HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD index, IDirect3DSurface9 *surface) override;
    D3D9EMU_UNEMULATED(HRESULT, GetRenderTarget, (DWORD, IDirect3DSurface9 **))
    HRESULT STDMETHODCALLTYPE SetDepthStencilSurface(IDirect3DSurface9 *surface) override;
    D3D9EMU_UNEMULATED(HRESULT, GetDepthStencilSurface, (IDirect3DSurface9 **))
    HRESULT STDMETHODCALLTYPE BeginScene() override;
    HRESULT STDMETHODCALLTYPE EndScene() override;
    HRESULT STDMETHODCALLTYPE Clear(DWORD count, CONST D3DRECT *rects, DWORD flags, D3DCOLOR color, float z,
                                    DWORD stencil) override;
    D3D9EMU_UNEMULATED(HRESULT, SetTransform, (D3DTRANSFORMSTATETYPE, CONST D3DMATRIX *))
    D3D9EMU_UNEMULATED(HRESULT, GetTransform, (D3DTRANSFORMSTATETYPE, D3DMATRIX *))
    D3D9EMU_UNEMULATED(HRESULT, MultiplyTransform, (D3DTRANSFORMSTATETYPE, CONST D3DMATRIX *))
    HRESULT STDMETHODCALLTYPE SetViewport(CONST D3DVIEWPORT9 *viewport) override;
    D3D9EMU_UNEMULATED(HRESULT, GetViewport, (D3DVIEWPORT9 *))
    D3D9EMU_UNEMULATED(HRESULT, SetMaterial, (CONST D3DMATERIAL9 *))
    D3D9EMU_UNEMULATED(HRESULT, GetMaterial, (D3DMATERIAL9 *))
    D3D9EMU_UNEMULATED(HRESULT, SetLight, (DWORD, CONST D3DLIGHT9 *))
    D3D9EMU_UNEMULATED(HRESULT, GetLight, (DWORD, D3DLIGHT9 *))
    D3D9EMU_UNEMULATED(HRESULT, LightEnable, (DWORD, BOOL))
    D3D9EMU_UNEMULATED(HRESULT, GetLightEnable, (DWORD, BOOL *))
    D3D9EMU_UNEMULATED(HRESULT, SetClipPlane, (DWORD, CONST float *))
    D3D9EMU_UNEMULATED(HRESULT, GetClipPlane, (DWORD, float *))
    HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE state, DWORD value) override;
    D3D9EMU_UNEMULATED(HRESULT, GetRenderState, (D3DRENDERSTATETYPE, DWORD *))
    D3D9EMU_UNEMULATED(HRESULT, CreateStateBlock, (D3DSTATEBLOCKTYPE, IDirect3DStateBlock9 **))
    D3D9EMU_UNEMULATED(HRESULT, BeginStateBlock, ())
    D3D9EMU_UNEMULATED(HRESULT, EndStateBlock, (IDirect3DStateBlock9 **))
    D3D9EMU_UNEMULATED(HRESULT, SetClipStatus, (CONST D3DCLIPSTATUS9 *))
    D3D9EMU_UNEMULATED(HRESULT, GetClipStatus, (D3DCLIPSTATUS9 *))
    D3D9EMU_UNEMULATED(HRESULT, GetTexture, (DWORD, IDirect3DBaseTexture9 **))
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD stage, IDirect3DBaseTexture9 *texture) override;
    D3D9EMU_UNEMULATED(HRESULT, GetTextureStageState, (DWORD, D3DTEXTURESTAGESTATETYPE, DWORD *))
    D3D9EMU_UNEMULATED(HRESULT, SetTextureStageState, (DWORD, D3DTEXTURESTAGESTATETYPE, DWORD))
    D3D9EMU_UNEMULATED(HRESULT, GetSamplerState, (DWORD, D3DSAMPLERSTATETYPE, DWORD *))
    HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD sampler, D3DSAMPLERSTATETYPE type, DWORD value) override;
    D3D9EMU_UNEMULATED(HRESULT, ValidateDevice, (DWORD *))
    D3D9EMU_UNEMULATED(HRESULT, SetPaletteEntries, (UINT, CONST PALETTEENTRY *))
    D3D9EMU_UNEMULATED(HRESULT, GetPaletteEntries, (UINT, PALETTEENTRY *))
    D3D9EMU_UNEMULATED(HRESULT, SetCurrentTexturePalette, (UINT))
    D3D9EMU_UNEMULATED(HRESULT, GetCurrentTexturePalette, (UINT *))
    HRESULT STDMETHODCALLTYPE SetScissorRect(CONST RECT *rect) override;
    D3D9EMU_UNEMULATED(HRESULT, GetScissorRect, (RECT *))
    D3D9EMU_UNEMULATED(HRESULT, SetSoftwareVertexProcessing, (BOOL))
    D3D9EMU_UNEMULATED(BOOL, GetSoftwareVertexProcessing, ())
    D3D9EMU_UNEMULATED(HRESULT, SetNPatchMode, (float))
    D3D9EMU_UNEMULATED(float, GetNPatchMode, ())
    HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE type, UINT startVertex, UINT primitiveCount) override;
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE type, INT baseVertex, UINT minVertex,
                                                   UINT vertexCount, UINT startIndex, UINT primitiveCount) override;
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE type, UINT primitiveCount, CONST void *vertices,
                                              UINT stride) override;
    D3D9EMU_UNEMULATED(HRESULT, DrawIndexedPrimitiveUP,
                       (D3DPRIMITIVETYPE, UINT, UINT, UINT, CONST void *, D3DFORMAT, CONST void *, UINT))
    D3D9EMU_UNEMULATED(HRESULT, ProcessVertices,
                       (UINT, UINT, UINT, IDirect3DVertexBuffer9 *, IDirect3DVertexDeclaration9 *, DWORD))
    HRESULT STDMETHODCALLTYPE CreateVertexDeclaration(CONST D3DVERTEXELEMENT9 *elements,
                                                      IDirect3DVertexDeclaration9 **declaration) override;
    HRESULT STDMETHODCALLTYPE SetVertexDeclaration(IDirect3DVertexDeclaration9 *declaration) override;
    D3D9EMU_UNEMULATED(HRESULT, GetVertexDeclaration, (IDirect3DVertexDeclaration9 **))
    D3D9EMU_UNEMULATED(HRESULT, SetFVF, (DWORD))
    D3D9EMU_UNEMULATED(HRESULT, GetFVF, (DWORD *))
    HRESULT STDMETHODCALLTYPE CreateVertexShader(CONST DWORD *function, IDirect3DVertexShader9 **shader) override;
    HRESULT STDMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9 *shader) override;
    D3D9EMU_UNEMULATED(HRESULT, GetVertexShader, (IDirect3DVertexShader9 **))
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantF(UINT start, CONST float *data, UINT count) override;
    D3D9EMU_UNEMULATED(HRESULT, GetVertexShaderConstantF, (UINT, float *, UINT))
    D3D9EMU_UNEMULATED(HRESULT, SetVertexShaderConstantI, (UINT, CONST int *, UINT))
    D3D9EMU_UNEMULATED(HRESULT, GetVertexShaderConstantI, (UINT, int *, UINT))
    D3D9EMU_UNEMULATED(HRESULT, SetVertexShaderConstantB, (UINT, CONST BOOL *, UINT))
    D3D9EMU_UNEMULATED(HRESULT, GetVertexShaderConstantB, (UINT, BOOL *, UINT))
    HRESULT STDMETHODCALLTYPE SetStreamSource(UINT stream, IDirect3DVertexBuffer9 *buffer, UINT offset,
                                              UINT stride) override;
    D3D9EMU_UNEMULATED(HRESULT, GetStreamSource, (UINT, IDirect3DVertexBuffer9 **, UINT *, UINT *))
    D3D9EMU_UNEMULATED(HRESULT, SetStreamSourceFreq, (UINT, UINT))
    D3D9EMU_UNEMULATED(HRESULT, GetStreamSourceFreq, (UINT, UINT *))
    HRESULT STDMETHODCALLTYPE SetIndices(IDirect3DIndexBuffer9 *buffer) override;
    D3D9EMU_UNEMULATED(HRESULT, GetIndices, (IDirect3DIndexBuffer9 **))
    HRESULT STDMETHODCALLTYPE CreatePixelShader(CONST DWORD *function, IDirect3DPixelShader9 **shader) override;
    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9 *shader) override;
    D3D9EMU_UNEMULATED(HRESULT, GetPixelShader, (IDirect3DPixelShader9 **))
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantF(UINT start, CONST float *data, UINT count) override;
    D3D9EMU_UNEMULATED(HRESULT, GetPixelShaderConstantF, (UINT, float *, UINT))
    D3D9EMU_UNEMULATED(HRESULT, SetPixelShaderConstantI, (UINT, CONST int *, UINT))
    D3D9EMU_UNEMULATED(HRESULT, GetPixelShaderConstantI, (UINT, int *, UINT))
    D3D9EMU_UNEMULATED(HRESULT, SetPixelShaderConstantB, (UINT, CONST BOOL *, UINT))
    D3D9EMU_UNEMULATED(HRESULT, GetPixelShaderConstantB, (UINT, BOOL *, UINT))
    D3D9EMU_UNEMULATED(HRESULT, DrawRectPatch, (UINT, CONST float *, CONST D3DRECTPATCH_INFO *))
    D3D9EMU_UNEMULATED(HRESULT, DrawTriPatch, (UINT, CONST float *, CONST D3DTRIPATCH_INFO *))
    D3D9EMU_UNEMULATED(HRESULT, DeletePatch, (UINT))
    HRESULT STDMETHODCALLTYPE CreateQuery(D3DQUERYTYPE type, IDirect3DQuery9 **query) override;
    D3D9EMU_UNEMULATED(HRESULT, SetConvolutionMonoKernel, (UINT, UINT, float *, float *))
    D3D9EMU_UNEMULATED(HRESULT, ComposeRects,
                       (IDirect3DSurface9 *, IDirect3DSurface9 *, IDirect3DVertexBuffer9 *, UINT,
                        IDirect3DVertexBuffer9 *, D3DCOMPOSERECTSOP, int, int))
    HRESULT STDMETHODCALLTYPE PresentEx(CONST RECT *sourceRect, CONST RECT *destRect, HWND destWindowOverride,
                                        CONST RGNDATA *dirtyRegion, DWORD flags) override;
    D3D9EMU_UNEMULATED(HRESULT, GetGPUThreadPriority, (INT *))
    D3D9EMU_UNEMULATED(HRESULT, SetGPUThreadPriority, (INT))
    D3D9EMU_UNEMULATED(HRESULT, WaitForVBlank, (UINT))
    D3D9EMU_UNEMULATED(HRESULT, CheckResourceResidency, (IDirect3DResource9 **, UINT32))
    D3D9EMU_UNEMULATED(HRESULT, SetMaximumFrameLatency, (UINT))
    D3D9EMU_UNEMULATED(HRESULT, GetMaximumFrameLatency, (UINT *))
    D3D9EMU_UNEMULATED(HRESULT, CheckDeviceState, (HWND))
    D3D9EMU_UNEMULATED(HRESULT, CreateRenderTargetEx,
                       (UINT, UINT, D3DFORMAT, D3DMULTISAMPLE_TYPE, DWORD, BOOL, IDirect3DSurface9 **, HANDLE *, DWORD))
    D3D9EMU_UNEMULATED(HRESULT, CreateOffscreenPlainSurfaceEx,
                       (UINT, UINT, D3DFORMAT, D3DPOOL, IDirect3DSurface9 **, HANDLE *, DWORD))
    D3D9EMU_UNEMULATED(HRESULT, CreateDepthStencilSurfaceEx,
                       (UINT, UINT, D3DFORMAT, D3DMULTISAMPLE_TYPE, DWORD, BOOL, IDirect3DSurface9 **, HANDLE *, DWORD))
    D3D9EMU_UNEMULATED(HRESULT, ResetEx, (D3DPRESENT_PARAMETERS *, D3DDISPLAYMODEEX *))
    D3D9EMU_UNEMULATED(HRESULT, GetDisplayModeEx, (UINT, D3DDISPLAYMODEEX *, D3DDISPLAYROTATION *))

    // ---- for the resources and the swap chain

    // Ends the current pass if it renders into `texture`, before the texture is written outside it.
    void BeforeTextureWrite(const gb::Texture *texture);
    // Brings the CPU copies of a lockable texture up to date with what the backend wrote into it.
    void ReadBack(Storage &storage);
    HRESULT PresentFrame(DWORD flags);
    HRESULT ReadFrontBuffer(IDirect3DSurface9 *destination);
    Surface *BackBuffer() const
    {
        return m_backBuffer.get();
    }
    bool IsEx() const
    {
        return m_ex;
    }
    // Open occlusion queries count in every pass begun until they close.
    void QueryOpened(Query *query);
    void QueryClosed(Query *query);

protected:
    IUnknown *Parent() override;

private:
    Device(Direct3D9 *d3d, bool ex);
    static HRESULT CheckParameters(D3DPRESENT_PARAMETERS &parameters, HWND window);
    HRESULT CreateBackBuffer(const D3DPRESENT_PARAMETERS &parameters);
    HRESULT CreateSurface(UINT width, UINT height, D3DFORMAT format, D3DPOOL pool, DWORD usage,
                          D3DMULTISAMPLE_TYPE multiSample, IDirect3DSurface9 **surface);
    void ResetState();
    void UnbindAll();

    // passes
    void EndPass();
    bool TargetsChanged() const;
    void PreparePass();
    void OpenPass();
    bool IsAttachment(const gb::Texture *texture) const;
    bool PrepareDraw(gb::DrawState &state, D3DPRIMITIVETYPE type, const gb::VertexBinding *upStream);
    void BuildPipeline(gb::PipelineDesc &desc, gb::Topology topology) const;
    const gb::Sampler *SamplerFor(uint32_t index, const Storage &texture);
    void Resolve();

    Direct3D9 *m_d3d;
    bool m_ex;
    HWND m_window = nullptr;
    D3DPRESENT_PARAMETERS m_parameters = {};
    gb::Swapchain *m_swapchain = nullptr;
    SwapChain m_swapChainObject;
    std::unique_ptr<Surface> m_backBuffer;
    std::unique_ptr<Surface> m_autoDepthStencil;
    std::shared_ptr<Storage> m_presentImage; // the resolved back buffer when that is multisampled
    std::vector<uint8_t> m_readScratch;      // GetFrontBufferData's copy of the last frame

    // D3D9 state
    DWORD m_renderStates[256] = {};
    DWORD m_samplerStates[gb::MAX_TEXTURES][D3DSAMP_DMAPOFFSET + 1] = {};
    TextureImpl *m_textures[gb::MAX_TEXTURES] = {};
    struct Stream
    {
        VertexBuffer *buffer = nullptr;
        UINT offset = 0;
        UINT stride = 0;
    } m_streams[gb::MAX_STREAMS];
    IndexBuffer *m_indices = nullptr;
    VertexDeclaration *m_declaration = nullptr;
    VertexShader *m_vertexShader = nullptr;
    PixelShader *m_pixelShader = nullptr;
    // c0-c255, then bo1_pos_fixup (vertex) or bo1_alpha_test (pixel): docs/RENDERER.md, "Shader interface"
    float m_vertexConstants[257][4] = {};
    float m_pixelConstants[257][4] = {};
    D3DVIEWPORT9 m_viewport = {};
    RECT m_scissor = {};
    Surface *m_renderTargets[gb::MAX_COLOR_TARGETS] = {};
    Surface *m_depthStencil = nullptr;
    bool m_inScene = false;

    // what the backend has
    bool m_pipelineDirty = true;
    gb::Topology m_pipelineTopology = gb::Topology::Triangles;
    const gb::Pipeline *m_pipeline = nullptr;
    bool m_vertexConstantsDirty = true;
    bool m_pixelConstantsDirty = true;
    gb::UniformSlice m_vertexSlice = {};
    gb::UniformSlice m_pixelSlice = {};
    uint32_t m_samplerDirty = 0xFFFFFFFF;
    const gb::Sampler *m_samplers[gb::MAX_TEXTURES] = {};
    bool m_samplerShadowTest[gb::MAX_TEXTURES] = {};

    // The current pass: begun in the backend (open), or chosen and not begun yet (pending), so that clears of whole
    // targets before any draw become load operations.
    struct Pass
    {
        bool open = false;
        bool pending = false;
        gb::PassDesc desc;
        Surface *color[gb::MAX_COLOR_TARGETS] = {};
        Surface *depth = nullptr;
        uint32_t width = 0, height = 0;
        uint8_t samples = 1;
        bool drawable = false; // it has at least one attachment
    } m_pass;
    std::vector<Query *> m_openQueries;

    // DrawPrimitiveUP's vertices, streamed through one buffer
    gb::Buffer *m_upBuffer = nullptr;
    uint32_t m_upOffset = 0;
    std::vector<uint8_t> m_upScratch;
};
} // namespace d3d9emu
