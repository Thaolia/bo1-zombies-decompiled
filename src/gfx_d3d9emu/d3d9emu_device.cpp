// IDirect3DDevice9Ex: the D3D9 state the engine sets, turned into backend passes and draws. Passes follow the render
// targets: a change of targets ends the pass, the next draw or clear begins one. A clear of whole targets before the
// pass's first draw becomes its load operation.

#include "d3d9emu.h"

#include "d3d9emu_host.h"
#include "shader_pack.h"

#include <algorithm>
#include <cstring>

namespace d3d9emu
{
namespace
{
constexpr uint32_t kUpBufferSize = 256 * 1024;
// Zeros after DrawPrimitiveUP's vertices, so that a declaration reading past the stride stays inside the buffer.
constexpr uint32_t kUpPadding = 256;

float AsFloat(DWORD value)
{
    float f;
    std::memcpy(&f, &value, sizeof(f));
    return f;
}

DWORD AsDword(float value)
{
    DWORD d;
    std::memcpy(&d, &value, sizeof(d));
    return d;
}

void Unpack(D3DCOLOR color, float rgba[4])
{
    rgba[0] = float((color >> 16) & 0xFF) / 255.0f;
    rgba[1] = float((color >> 8) & 0xFF) / 255.0f;
    rgba[2] = float(color & 0xFF) / 255.0f;
    rgba[3] = float(color >> 24) / 255.0f;
}

gb::Blend ToBlend(DWORD blend)
{
    switch (blend)
    {
    case D3DBLEND_ZERO:
        return gb::Blend::Zero;
    case D3DBLEND_SRCCOLOR:
        return gb::Blend::SrcColor;
    case D3DBLEND_INVSRCCOLOR:
        return gb::Blend::InvSrcColor;
    case D3DBLEND_SRCALPHA:
        return gb::Blend::SrcAlpha;
    case D3DBLEND_INVSRCALPHA:
        return gb::Blend::InvSrcAlpha;
    case D3DBLEND_DESTALPHA:
        return gb::Blend::DstAlpha;
    case D3DBLEND_INVDESTALPHA:
        return gb::Blend::InvDstAlpha;
    case D3DBLEND_DESTCOLOR:
        return gb::Blend::DstColor;
    case D3DBLEND_INVDESTCOLOR:
        return gb::Blend::InvDstColor;
    case D3DBLEND_SRCALPHASAT:
        return gb::Blend::SrcAlphaSat;
    case D3DBLEND_BLENDFACTOR:
        return gb::Blend::Constant;
    case D3DBLEND_INVBLENDFACTOR:
        return gb::Blend::InvConstant;
    default:
        return gb::Blend::One;
    }
}

gb::BlendOp ToBlendOp(DWORD op)
{
    return op >= D3DBLENDOP_ADD && op <= D3DBLENDOP_MAX ? gb::BlendOp(op - D3DBLENDOP_ADD) : gb::BlendOp::Add;
}

gb::Compare ToCompare(DWORD func)
{
    return func >= D3DCMP_NEVER && func <= D3DCMP_ALWAYS ? gb::Compare(func - D3DCMP_NEVER) : gb::Compare::Always;
}

gb::StencilOp ToStencilOp(DWORD op)
{
    return op >= D3DSTENCILOP_KEEP && op <= D3DSTENCILOP_DECR ? gb::StencilOp(op - D3DSTENCILOP_KEEP)
                                                              : gb::StencilOp::Keep;
}

gb::Address ToAddress(DWORD address)
{
    switch (address)
    {
    case D3DTADDRESS_MIRROR:
        return gb::Address::Mirror;
    case D3DTADDRESS_CLAMP:
        return gb::Address::Clamp;
    case D3DTADDRESS_BORDER:
        return gb::Address::Border;
    case D3DTADDRESS_MIRRORONCE:
        return gb::Address::MirrorOnce;
    default:
        return gb::Address::Wrap;
    }
}

bool PrimitiveInfo(D3DPRIMITIVETYPE type, UINT primitives, gb::Primitive *primitive, gb::Topology *topology,
                   uint32_t *vertices)
{
    switch (type)
    {
    case D3DPT_POINTLIST:
        *primitive = gb::Primitive::PointList;
        *topology = gb::Topology::Points;
        *vertices = primitives;
        return true;
    case D3DPT_LINELIST:
        *primitive = gb::Primitive::LineList;
        *topology = gb::Topology::Lines;
        *vertices = primitives * 2;
        return true;
    case D3DPT_LINESTRIP:
        *primitive = gb::Primitive::LineStrip;
        *topology = gb::Topology::Lines;
        *vertices = primitives + 1;
        return true;
    case D3DPT_TRIANGLELIST:
        *primitive = gb::Primitive::TriangleList;
        *topology = gb::Topology::Triangles;
        *vertices = primitives * 3;
        return true;
    case D3DPT_TRIANGLESTRIP:
        *primitive = gb::Primitive::TriangleStrip;
        *topology = gb::Topology::Triangles;
        *vertices = primitives + 2;
        return true;
    case D3DPT_TRIANGLEFAN:
        *primitive = gb::Primitive::TriangleFan;
        *topology = gb::Topology::Triangles;
        *vertices = primitives + 2;
        return true;
    default:
        return false;
    }
}

// The intersection of two rects; false when it is empty.
bool Intersect(const gb::Rect &a, const gb::Rect &b, gb::Rect *out)
{
    const int64_t left = std::max<int64_t>(a.x, b.x);
    const int64_t top = std::max<int64_t>(a.y, b.y);
    const int64_t right = std::min<int64_t>(int64_t(a.x) + a.width, int64_t(b.x) + b.width);
    const int64_t bottom = std::min<int64_t>(int64_t(a.y) + a.height, int64_t(b.y) + b.height);
    if (left >= right || top >= bottom)
        return false;
    *out = {int32_t(left), int32_t(top), uint32_t(right - left), uint32_t(bottom - top)};
    return true;
}

gb::Rect FromRect(LONG left, LONG top, LONG right, LONG bottom)
{
    if (right <= left || bottom <= top)
        return {0, 0, 0, 0};
    return {int32_t(left), int32_t(top), uint32_t(right - left), uint32_t(bottom - top)};
}

Surface *SurfaceOf(IDirect3DSurface9 *surface)
{
    return static_cast<Surface *>(surface);
}
} // namespace

// ---- creation, reset and destruction

Device::Device(Direct3D9 *d3d, bool ex) : m_d3d(d3d), m_ex(ex), m_swapChainObject(this)
{
    m_d3d->AddRef();
    m_upScratch.resize(kUpBufferSize);
}

Device::~Device()
{
    EndPass();
    UnbindAll();
    gb::DestroyBuffer(m_upBuffer);
    m_backBuffer.reset();
    m_autoDepthStencil.reset();
    m_presentImage.reset();
    gb::DestroySwapchain(m_swapchain);
}

IUnknown *Device::Parent()
{
    return m_d3d;
}

HRESULT Device::CheckParameters(D3DPRESENT_PARAMETERS &p, HWND window)
{
    if (!window || p.BackBufferCount > 1 || p.SwapEffect < D3DSWAPEFFECT_DISCARD || p.SwapEffect > D3DSWAPEFFECT_COPY)
        return D3DERR_INVALIDCALL;
    if (!p.BackBufferWidth || !p.BackBufferHeight)
    {
        uint32_t width, height;
        if (!p.Windowed || !host::ClientSize(window, &width, &height) || !width || !height)
            return D3DERR_INVALIDCALL;
        p.BackBufferWidth = p.BackBufferWidth ? p.BackBufferWidth : width;
        p.BackBufferHeight = p.BackBufferHeight ? p.BackBufferHeight : height;
    }
    if (p.BackBufferFormat == D3DFMT_UNKNOWN && p.Windowed)
        p.BackBufferFormat = D3DFMT_X8R8G8B8;
    if (p.BackBufferFormat != D3DFMT_A8R8G8B8 && p.BackBufferFormat != D3DFMT_X8R8G8B8)
        return D3DERR_INVALIDCALL;
    if (p.MultiSampleType != D3DMULTISAMPLE_NONE && p.SwapEffect != D3DSWAPEFFECT_DISCARD)
        return D3DERR_INVALIDCALL;
    p.BackBufferCount = 1;
    p.hDeviceWindow = window;
    return D3D_OK;
}

HRESULT Device::Create(Direct3D9 *d3d, bool ex, HWND focusWindow, D3DPRESENT_PARAMETERS *parameters, Device **out)
{
    D3DPRESENT_PARAMETERS p = *parameters;
    const HWND window = p.hDeviceWindow ? p.hDeviceWindow : focusWindow;
    HRESULT hr = CheckParameters(p, window);
    if (FAILED(hr))
        return hr;
    Device *device = new Device(d3d, ex);
    device->m_window = window;
    const gb::SwapchainDesc swapchain = {host::NativeWindow(window), uint16_t(p.BackBufferWidth),
                                         uint16_t(p.BackBufferHeight),
                                         p.PresentationInterval != D3DPRESENT_INTERVAL_IMMEDIATE};
    device->m_swapchain = gb::CreateSwapchain(swapchain);
    const gb::BufferDesc up = {gb::BufferType::Vertex, gb::Usage::Dynamic, gb::Memory::Shared, kUpBufferSize};
    device->m_upBuffer = gb::CreateBuffer(up);
    hr = device->m_swapchain && device->m_upBuffer ? device->CreateBackBuffer(p) : D3DERR_NOTAVAILABLE;
    if (FAILED(hr))
    {
        device->Release();
        return hr;
    }
    device->m_parameters = p;
    *parameters = p;
    device->ResetState();
    *out = device;
    return D3D_OK;
}

HRESULT Device::CreateBackBuffer(const D3DPRESENT_PARAMETERS &p)
{
    std::shared_ptr<Storage> storage;
    HRESULT hr = CreateStorage(this, D3DRTYPE_SURFACE, p.BackBufferWidth, p.BackBufferHeight, 1, 1,
                               D3DUSAGE_RENDERTARGET, p.BackBufferFormat, D3DPOOL_DEFAULT, p.MultiSampleType, storage);
    if (FAILED(hr))
        return hr;
    m_backBuffer.reset(new Surface(storage, 0, 0, this));
    if (p.MultiSampleType != D3DMULTISAMPLE_NONE)
    {
        hr = CreateStorage(this, D3DRTYPE_SURFACE, p.BackBufferWidth, p.BackBufferHeight, 1, 1, D3DUSAGE_RENDERTARGET,
                           p.BackBufferFormat, D3DPOOL_DEFAULT, D3DMULTISAMPLE_NONE, m_presentImage);
        if (FAILED(hr))
            return hr;
    }
    if (p.EnableAutoDepthStencil)
    {
        hr = CreateStorage(this, D3DRTYPE_SURFACE, p.BackBufferWidth, p.BackBufferHeight, 1, 1,
                           D3DUSAGE_DEPTHSTENCIL, p.AutoDepthStencilFormat, D3DPOOL_DEFAULT, p.MultiSampleType,
                           storage);
        if (FAILED(hr))
            return hr;
        m_autoDepthStencil.reset(new Surface(storage, 0, 0, this));
    }
    return D3D_OK;
}

void Device::UnbindAll()
{
    for (TextureImpl *&texture : m_textures)
        Rebind<TextureImpl>(texture, nullptr);
    for (Stream &stream : m_streams)
        Rebind<VertexBuffer>(stream.buffer, nullptr);
    Rebind<IndexBuffer>(m_indices, nullptr);
    Rebind<VertexDeclaration>(m_declaration, nullptr);
    Rebind<VertexShader>(m_vertexShader, nullptr);
    Rebind<PixelShader>(m_pixelShader, nullptr);
    for (Surface *&target : m_renderTargets)
        Rebind<Surface>(target, nullptr);
    Rebind<Surface>(m_depthStencil, nullptr);
}

// D3D9's default state (the D3DRENDERSTATETYPE and D3DSAMPLERSTATETYPE documentation), with the back buffer as the
// target.
void Device::ResetState()
{
    UnbindAll();
    std::memset(m_renderStates, 0, sizeof(m_renderStates));
    DWORD *rs = m_renderStates;
    rs[D3DRS_ZENABLE] = m_autoDepthStencil ? D3DZB_TRUE : D3DZB_FALSE;
    rs[D3DRS_FILLMODE] = D3DFILL_SOLID;
    rs[D3DRS_ZWRITEENABLE] = TRUE;
    rs[D3DRS_SRCBLEND] = D3DBLEND_ONE;
    rs[D3DRS_DESTBLEND] = D3DBLEND_ZERO;
    rs[D3DRS_CULLMODE] = D3DCULL_CCW;
    rs[D3DRS_ZFUNC] = D3DCMP_LESSEQUAL;
    rs[D3DRS_ALPHAFUNC] = D3DCMP_ALWAYS;
    rs[D3DRS_STENCILFAIL] = D3DSTENCILOP_KEEP;
    rs[D3DRS_STENCILZFAIL] = D3DSTENCILOP_KEEP;
    rs[D3DRS_STENCILPASS] = D3DSTENCILOP_KEEP;
    rs[D3DRS_STENCILFUNC] = D3DCMP_ALWAYS;
    rs[D3DRS_STENCILMASK] = 0xFFFFFFFF;
    rs[D3DRS_STENCILWRITEMASK] = 0xFFFFFFFF;
    rs[D3DRS_COLORWRITEENABLE] = 0xF;
    rs[D3DRS_BLENDOP] = D3DBLENDOP_ADD;
    rs[D3DRS_POINTSIZE] = AsDword(1.0f);
    rs[D3DRS_CCW_STENCILFAIL] = D3DSTENCILOP_KEEP;
    rs[D3DRS_CCW_STENCILZFAIL] = D3DSTENCILOP_KEEP;
    rs[D3DRS_CCW_STENCILPASS] = D3DSTENCILOP_KEEP;
    rs[D3DRS_CCW_STENCILFUNC] = D3DCMP_ALWAYS;
    rs[D3DRS_COLORWRITEENABLE1] = 0xF;
    rs[D3DRS_COLORWRITEENABLE2] = 0xF;
    rs[D3DRS_COLORWRITEENABLE3] = 0xF;
    rs[D3DRS_BLENDFACTOR] = 0xFFFFFFFF;
    rs[D3DRS_SRCBLENDALPHA] = D3DBLEND_ONE;
    rs[D3DRS_DESTBLENDALPHA] = D3DBLEND_ZERO;
    rs[D3DRS_BLENDOPALPHA] = D3DBLENDOP_ADD;
    for (auto &sampler : m_samplerStates)
    {
        std::memset(sampler, 0, sizeof(sampler));
        sampler[D3DSAMP_ADDRESSU] = D3DTADDRESS_WRAP;
        sampler[D3DSAMP_ADDRESSV] = D3DTADDRESS_WRAP;
        sampler[D3DSAMP_ADDRESSW] = D3DTADDRESS_WRAP;
        sampler[D3DSAMP_MAGFILTER] = D3DTEXF_POINT;
        sampler[D3DSAMP_MINFILTER] = D3DTEXF_POINT;
        sampler[D3DSAMP_MIPFILTER] = D3DTEXF_NONE;
        sampler[D3DSAMP_MAXANISOTROPY] = 1;
    }
    for (Stream &stream : m_streams)
        stream = Stream();
    std::memset(m_vertexConstants, 0, sizeof(m_vertexConstants));
    std::memset(m_pixelConstants, 0, sizeof(m_pixelConstants));
    Rebind(m_renderTargets[0], m_backBuffer.get());
    Rebind(m_depthStencil, m_autoDepthStencil.get());
    m_viewport = {0, 0, m_backBuffer->Width(), m_backBuffer->Height(), 0.0f, 1.0f};
    m_scissor = {0, 0, LONG(m_backBuffer->Width()), LONG(m_backBuffer->Height())};
    m_inScene = false;
    m_pipelineDirty = true;
    m_pipeline = nullptr;
    m_vertexConstantsDirty = true;
    m_pixelConstantsDirty = true;
    m_samplerDirty = 0xFFFFFFFF;
}

HRESULT Device::Reset(D3DPRESENT_PARAMETERS *parameters)
{
    Guard guard(DeviceLock());
    if (!parameters)
        return D3DERR_INVALIDCALL;
    D3DPRESENT_PARAMETERS p = *parameters;
    const HWND window = p.hDeviceWindow ? p.hDeviceWindow : m_window;
    HRESULT hr = CheckParameters(p, window);
    if (FAILED(hr))
        return hr;
    EndPass();
    UnbindAll();
    m_backBuffer.reset();
    m_autoDepthStencil.reset();
    m_presentImage.reset();
    m_window = window;
    gb::ResizeSwapchain(m_swapchain, uint16_t(p.BackBufferWidth), uint16_t(p.BackBufferHeight));
    hr = CreateBackBuffer(p);
    if (FAILED(hr))
        return hr;
    m_parameters = p;
    *parameters = p;
    ResetState();
    return D3D_OK;
}

HRESULT Device::TestCooperativeLevel()
{
    return D3D_OK;
}

UINT Device::GetAvailableTextureMem()
{
    // A UINT, as on a 32-bit runtime: the most it reports is just under 4 GB.
    const uint64_t bytes = gb::GetCaps().memoryBytes;
    return UINT(std::min<uint64_t>(bytes, 0xFFF00000ull) & ~0xFFFFFull);
}

HRESULT Device::GetDeviceCaps(D3DCAPS9 *caps)
{
    if (!caps)
        return D3DERR_INVALIDCALL;
    FillCaps(caps);
    return D3D_OK;
}

HRESULT Device::GetSwapChain(UINT index, IDirect3DSwapChain9 **swapChain)
{
    if (!swapChain)
        return D3DERR_INVALIDCALL;
    *swapChain = nullptr;
    if (index != 0)
        return D3DERR_INVALIDCALL;
    m_swapChainObject.AddRef();
    *swapChain = &m_swapChainObject;
    return D3D_OK;
}

HRESULT Device::GetBackBuffer(UINT swapChain, UINT index, D3DBACKBUFFER_TYPE type, IDirect3DSurface9 **surface)
{
    if (!surface)
        return D3DERR_INVALIDCALL;
    *surface = nullptr;
    if (swapChain != 0 || index != 0 || type != D3DBACKBUFFER_TYPE_MONO)
        return D3DERR_INVALIDCALL;
    m_backBuffer->AddRef();
    *surface = m_backBuffer.get();
    return D3D_OK;
}

void Device::SetGammaRamp(UINT swapChain, DWORD, CONST D3DGAMMARAMP *ramp)
{
    Guard guard(DeviceLock());
    if (swapChain == 0 && ramp)
        gb::SetGamma(m_swapchain, ramp->red, ramp->green, ramp->blue);
}

// ---- resources

HRESULT Device::CreateTexture(UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool,
                              IDirect3DTexture9 **texture, HANDLE *sharedHandle)
{
    if (!texture || sharedHandle)
        return D3DERR_INVALIDCALL;
    *texture = nullptr;
    Guard guard(DeviceLock());
    std::shared_ptr<Storage> storage;
    const HRESULT hr = CreateStorage(this, D3DRTYPE_TEXTURE, width, height, 1, levels, usage, format, pool,
                                     D3DMULTISAMPLE_NONE, storage);
    if (SUCCEEDED(hr))
        *texture = new Texture(std::move(storage));
    return hr;
}

HRESULT Device::CreateVolumeTexture(UINT width, UINT height, UINT depth, UINT levels, DWORD usage, D3DFORMAT format,
                                    D3DPOOL pool, IDirect3DVolumeTexture9 **texture, HANDLE *sharedHandle)
{
    if (!texture || sharedHandle)
        return D3DERR_INVALIDCALL;
    *texture = nullptr;
    if (usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL))
        return D3DERR_INVALIDCALL;
    Guard guard(DeviceLock());
    std::shared_ptr<Storage> storage;
    const HRESULT hr = CreateStorage(this, D3DRTYPE_VOLUMETEXTURE, width, height, depth, levels, usage, format, pool,
                                     D3DMULTISAMPLE_NONE, storage);
    if (SUCCEEDED(hr))
        *texture = new VolumeTexture(std::move(storage));
    return hr;
}

HRESULT Device::CreateCubeTexture(UINT edgeLength, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool,
                                  IDirect3DCubeTexture9 **texture, HANDLE *sharedHandle)
{
    if (!texture || sharedHandle)
        return D3DERR_INVALIDCALL;
    *texture = nullptr;
    if (usage & D3DUSAGE_DEPTHSTENCIL)
        return D3DERR_INVALIDCALL;
    Guard guard(DeviceLock());
    std::shared_ptr<Storage> storage;
    const HRESULT hr = CreateStorage(this, D3DRTYPE_CUBETEXTURE, edgeLength, edgeLength, 1, levels, usage, format,
                                     pool, D3DMULTISAMPLE_NONE, storage);
    if (SUCCEEDED(hr))
        *texture = new CubeTexture(std::move(storage));
    return hr;
}

HRESULT Device::CreateVertexBuffer(UINT length, DWORD usage, DWORD fvf, D3DPOOL pool, IDirect3DVertexBuffer9 **buffer,
                                   HANDLE *sharedHandle)
{
    if (!buffer || sharedHandle)
        return D3DERR_INVALIDCALL;
    *buffer = nullptr;
    Guard guard(DeviceLock());
    VertexBuffer *created = new VertexBuffer(this, fvf);
    const HRESULT hr = CreateBufferStorage(this, gb::BufferType::Vertex, length, usage, pool, created->storage);
    if (FAILED(hr))
    {
        created->Release();
        return hr;
    }
    *buffer = created;
    return D3D_OK;
}

HRESULT Device::CreateIndexBuffer(UINT length, DWORD usage, D3DFORMAT format, D3DPOOL pool,
                                  IDirect3DIndexBuffer9 **buffer, HANDLE *sharedHandle)
{
    if (!buffer || sharedHandle)
        return D3DERR_INVALIDCALL;
    *buffer = nullptr;
    if (format != D3DFMT_INDEX16 && format != D3DFMT_INDEX32)
        return D3DERR_INVALIDCALL;
    Guard guard(DeviceLock());
    IndexBuffer *created = new IndexBuffer(this, format);
    const HRESULT hr = CreateBufferStorage(
        this, format == D3DFMT_INDEX16 ? gb::BufferType::Index16 : gb::BufferType::Index32, length, usage, pool,
        created->storage);
    if (FAILED(hr))
    {
        created->Release();
        return hr;
    }
    *buffer = created;
    return D3D_OK;
}

HRESULT Device::CreateSurface(UINT width, UINT height, D3DFORMAT format, D3DPOOL pool, DWORD usage,
                              D3DMULTISAMPLE_TYPE multiSample, IDirect3DSurface9 **surface)
{
    Guard guard(DeviceLock());
    std::shared_ptr<Storage> storage;
    const HRESULT hr =
        CreateStorage(this, D3DRTYPE_SURFACE, width, height, 1, 1, usage, format, pool, multiSample, storage);
    if (SUCCEEDED(hr))
        *surface = new Surface(std::move(storage), 0, 0, nullptr);
    return hr;
}

HRESULT Device::CreateRenderTarget(UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE multiSample, DWORD,
                                   BOOL lockable, IDirect3DSurface9 **surface, HANDLE *sharedHandle)
{
    if (!surface || sharedHandle)
        return D3DERR_INVALIDCALL;
    *surface = nullptr;
    if (lockable)
    {
        WarnOnce("lockable render target", "d3d9emu: lockable render targets are not emulated");
        return D3DERR_INVALIDCALL;
    }
    return CreateSurface(width, height, format, D3DPOOL_DEFAULT, D3DUSAGE_RENDERTARGET, multiSample, surface);
}

HRESULT Device::CreateDepthStencilSurface(UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE multiSample,
                                          DWORD, BOOL, IDirect3DSurface9 **surface, HANDLE *sharedHandle)
{
    if (!surface || sharedHandle)
        return D3DERR_INVALIDCALL;
    *surface = nullptr;
    return CreateSurface(width, height, format, D3DPOOL_DEFAULT, D3DUSAGE_DEPTHSTENCIL, multiSample, surface);
}

HRESULT Device::CreateOffscreenPlainSurface(UINT width, UINT height, D3DFORMAT format, D3DPOOL pool,
                                            IDirect3DSurface9 **surface, HANDLE *sharedHandle)
{
    if (!surface || sharedHandle)
        return D3DERR_INVALIDCALL;
    *surface = nullptr;
    if (pool == D3DPOOL_MANAGED)
        return D3DERR_INVALIDCALL;
    return CreateSurface(width, height, format, pool, 0, D3DMULTISAMPLE_NONE, surface);
}

HRESULT Device::CreateVertexDeclaration(CONST D3DVERTEXELEMENT9 *elements, IDirect3DVertexDeclaration9 **declaration)
{
    if (!elements || !declaration)
        return D3DERR_INVALIDCALL;
    *declaration = nullptr;
    constexpr size_t kMaxElements = 64;
    size_t count = 0;
    while (count < kMaxElements && elements[count].Stream != 0xFF)
        ++count;
    if (count == kMaxElements)
        return D3DERR_INVALIDCALL;
    gb::VertexAttrib attribs[gb::MAX_ATTRIBS];
    uint8_t attribCount = 0;
    if (!TranslateDeclaration(elements, count, attribs, &attribCount))
        return D3DERR_INVALIDCALL;
    Guard guard(DeviceLock());
    *declaration = new VertexDeclaration(this, std::vector<D3DVERTEXELEMENT9>(elements, elements + count + 1), attribs,
                                         attribCount);
    return D3D_OK;
}

HRESULT Device::CreateVertexShader(CONST DWORD *function, IDirect3DVertexShader9 **shader)
{
    if (!shader)
        return D3DERR_INVALIDCALL;
    *shader = nullptr;
    const size_t bytes = ShaderBytecodeSize(function, 1u << 20);
    if (!bytes || (function[0] >> 16) != 0xFFFE)
        return D3DERR_INVALIDCALL;
    const uint64_t hash = ShaderPack_Hash(function, bytes);
    Guard guard(DeviceLock());
    *shader = new VertexShader(this, CreatePackedShader(gb::Stage::Vertex, hash), hash);
    return D3D_OK;
}

HRESULT Device::CreatePixelShader(CONST DWORD *function, IDirect3DPixelShader9 **shader)
{
    if (!shader)
        return D3DERR_INVALIDCALL;
    *shader = nullptr;
    const size_t bytes = ShaderBytecodeSize(function, 1u << 20);
    if (!bytes || (function[0] >> 16) != 0xFFFF)
        return D3DERR_INVALIDCALL;
    const uint64_t hash = ShaderPack_Hash(function, bytes);
    Guard guard(DeviceLock());
    *shader = new PixelShader(this, CreatePackedShader(gb::Stage::Pixel, hash), hash,
                              PixelShaderSamplers(function, bytes));
    return D3D_OK;
}

HRESULT Device::CreateQuery(D3DQUERYTYPE type, IDirect3DQuery9 **query)
{
    switch (type)
    {
    case D3DQUERYTYPE_EVENT:
    case D3DQUERYTYPE_OCCLUSION:
    case D3DQUERYTYPE_TIMESTAMP:
    case D3DQUERYTYPE_TIMESTAMPDISJOINT:
    case D3DQUERYTYPE_TIMESTAMPFREQ:
        break;
    default:
        return D3DERR_NOTAVAILABLE;
    }
    // A null pointer asks whether the type is supported.
    if (!query)
        return D3D_OK;
    Guard guard(DeviceLock());
    *query = new Query(this, type);
    return D3D_OK;
}

// ---- state

HRESULT Device::SetRenderState(D3DRENDERSTATETYPE state, DWORD value)
{
    if (UINT(state) >= 256)
        return D3DERR_INVALIDCALL;
    Guard guard(DeviceLock());
    if (state == D3DRS_POINTSIZE && value == RESZ_CODE)
        Resolve();
    if (m_renderStates[state] == value)
        return D3D_OK;
    m_renderStates[state] = value;
    switch (state)
    {
    case D3DRS_STENCILREF:
    case D3DRS_BLENDFACTOR:
    case D3DRS_POINTSIZE:
        break;
    case D3DRS_ALPHATESTENABLE:
    case D3DRS_ALPHAFUNC:
    case D3DRS_ALPHAREF:
        m_pixelConstantsDirty = true;
        break;
    default:
        m_pipelineDirty = true;
        break;
    }
    return D3D_OK;
}

HRESULT Device::SetSamplerState(DWORD sampler, D3DSAMPLERSTATETYPE type, DWORD value)
{
    if (UINT(type) > D3DSAMP_DMAPOFFSET)
        return D3DERR_INVALIDCALL;
    Guard guard(DeviceLock());
    if (sampler >= D3DVERTEXTEXTURESAMPLER0 && sampler <= D3DVERTEXTEXTURESAMPLER3)
        return D3D_OK; // vertex texture fetch is not reported (CheckDeviceFormat), so nothing reads these
    if (sampler >= gb::MAX_TEXTURES)
        return D3DERR_INVALIDCALL;
    if (m_samplerStates[sampler][type] != value)
    {
        m_samplerStates[sampler][type] = value;
        m_samplerDirty |= 1u << sampler;
    }
    return D3D_OK;
}

HRESULT Device::SetTexture(DWORD stage, IDirect3DBaseTexture9 *texture)
{
    Guard guard(DeviceLock());
    if (stage >= D3DVERTEXTEXTURESAMPLER0 && stage <= D3DVERTEXTEXTURESAMPLER3)
    {
        if (texture)
            WarnOnce("vertex texture", "d3d9emu: vertex textures are not emulated");
        return D3D_OK;
    }
    if (stage >= gb::MAX_TEXTURES)
        return D3DERR_INVALIDCALL;
    Rebind(m_textures[stage], ImplOf(texture));
    return D3D_OK;
}

HRESULT Device::SetStreamSource(UINT stream, IDirect3DVertexBuffer9 *buffer, UINT offset, UINT stride)
{
    if (stream >= gb::MAX_STREAMS)
        return D3DERR_INVALIDCALL;
    Guard guard(DeviceLock());
    Stream &s = m_streams[stream];
    Rebind(s.buffer, static_cast<VertexBuffer *>(buffer));
    s.offset = offset;
    s.stride = stride;
    return D3D_OK;
}

HRESULT Device::SetIndices(IDirect3DIndexBuffer9 *buffer)
{
    Guard guard(DeviceLock());
    Rebind(m_indices, static_cast<IndexBuffer *>(buffer));
    return D3D_OK;
}

HRESULT Device::SetVertexDeclaration(IDirect3DVertexDeclaration9 *declaration)
{
    Guard guard(DeviceLock());
    VertexDeclaration *next = static_cast<VertexDeclaration *>(declaration);
    if (next != m_declaration)
        m_pipelineDirty = true;
    Rebind(m_declaration, next);
    return D3D_OK;
}

HRESULT Device::SetVertexShader(IDirect3DVertexShader9 *shader)
{
    Guard guard(DeviceLock());
    VertexShader *next = static_cast<VertexShader *>(shader);
    if (next != m_vertexShader)
        m_pipelineDirty = true;
    Rebind(m_vertexShader, next);
    return D3D_OK;
}

HRESULT Device::SetPixelShader(IDirect3DPixelShader9 *shader)
{
    Guard guard(DeviceLock());
    PixelShader *next = static_cast<PixelShader *>(shader);
    if (next != m_pixelShader)
        m_pipelineDirty = true;
    Rebind(m_pixelShader, next);
    return D3D_OK;
}

HRESULT Device::SetVertexShaderConstantF(UINT start, CONST float *data, UINT count)
{
    if (!data || start > 256 || count > 256 - start)
        return D3DERR_INVALIDCALL;
    Guard guard(DeviceLock());
    std::memcpy(m_vertexConstants[start], data, size_t(count) * 4 * sizeof(float));
    m_vertexConstantsDirty = true;
    return D3D_OK;
}

HRESULT Device::SetPixelShaderConstantF(UINT start, CONST float *data, UINT count)
{
    if (!data || start > 256 || count > 256 - start)
        return D3DERR_INVALIDCALL;
    Guard guard(DeviceLock());
    std::memcpy(m_pixelConstants[start], data, size_t(count) * 4 * sizeof(float));
    m_pixelConstantsDirty = true;
    return D3D_OK;
}

HRESULT Device::SetViewport(CONST D3DVIEWPORT9 *viewport)
{
    if (!viewport || !viewport->Width || !viewport->Height)
        return D3DERR_INVALIDCALL;
    Guard guard(DeviceLock());
    m_viewport = *viewport;
    return D3D_OK;
}

HRESULT Device::SetScissorRect(CONST RECT *rect)
{
    if (!rect)
        return D3DERR_INVALIDCALL;
    Guard guard(DeviceLock());
    m_scissor = *rect;
    return D3D_OK;
}

HRESULT Device::SetRenderTarget(DWORD index, IDirect3DSurface9 *surface)
{
    if (index >= gb::MAX_COLOR_TARGETS || (index == 0 && !surface))
        return D3DERR_INVALIDCALL;
    Surface *target = SurfaceOf(surface);
    if (target && !(target->GetStorage().usage & D3DUSAGE_RENDERTARGET))
        return D3DERR_INVALIDCALL;
    Guard guard(DeviceLock());
    Rebind(m_renderTargets[index], target);
    // Setting the first target resets the viewport and the scissor rect to the whole of it.
    if (index == 0)
    {
        m_viewport = {0, 0, target->Width(), target->Height(), 0.0f, 1.0f};
        m_scissor = {0, 0, LONG(target->Width()), LONG(target->Height())};
    }
    return D3D_OK;
}

HRESULT Device::SetDepthStencilSurface(IDirect3DSurface9 *surface)
{
    Surface *depth = SurfaceOf(surface);
    if (depth && !(depth->GetStorage().usage & D3DUSAGE_DEPTHSTENCIL))
        return D3DERR_INVALIDCALL;
    Guard guard(DeviceLock());
    Rebind(m_depthStencil, depth);
    return D3D_OK;
}

HRESULT Device::BeginScene()
{
    Guard guard(DeviceLock());
    if (m_inScene)
        return D3DERR_INVALIDCALL;
    m_inScene = true;
    return D3D_OK;
}

HRESULT Device::EndScene()
{
    Guard guard(DeviceLock());
    if (!m_inScene)
        return D3DERR_INVALIDCALL;
    m_inScene = false;
    EndPass();
    return D3D_OK;
}

// ---- passes

bool Device::TargetsChanged() const
{
    for (int i = 0; i < gb::MAX_COLOR_TARGETS; ++i)
        if (m_pass.color[i] != m_renderTargets[i])
            return true;
    return m_pass.depth != m_depthStencil;
}

bool Device::IsAttachment(const gb::Texture *texture) const
{
    if (!texture || (!m_pass.open && !m_pass.pending))
        return false;
    for (int i = 0; i < m_pass.desc.colorCount; ++i)
        if (m_pass.desc.color[i].texture == texture)
            return true;
    return m_pass.desc.depth == texture;
}

void Device::PreparePass()
{
    if ((m_pass.open || m_pass.pending) && !TargetsChanged())
        return;
    EndPass();
    Pass &p = m_pass;
    p.desc = gb::PassDesc();
    for (int i = 0; i < gb::MAX_COLOR_TARGETS; ++i)
        Rebind(p.color[i], m_renderTargets[i]);
    Rebind(p.depth, m_depthStencil);
    // The colour attachments are the targets from the first, up to the first that has no memory (FOURCC_NULL).
    p.width = UINT32_MAX;
    p.height = UINT32_MAX;
    p.samples = 1;
    for (int i = 0; i < gb::MAX_COLOR_TARGETS && p.color[i] && p.color[i]->GetStorage().gpu; ++i)
    {
        Surface *s = p.color[i];
        p.desc.color[i].texture = s->GetStorage().gpu;
        p.desc.color[i].level = uint8_t(s->Level());
        p.desc.color[i].layer = uint8_t(s->Layer());
        p.desc.colorCount = uint8_t(i + 1);
        p.width = std::min(p.width, s->Width());
        p.height = std::min(p.height, s->Height());
        p.samples = SampleCount(s->GetStorage().multiSample);
    }
    if (p.depth && p.depth->GetStorage().gpu)
    {
        p.desc.depth = p.depth->GetStorage().gpu;
        p.width = std::min(p.width, p.depth->Width());
        p.height = std::min(p.height, p.depth->Height());
        if (!p.desc.colorCount)
            p.samples = SampleCount(p.depth->GetStorage().multiSample);
    }
    p.drawable = p.desc.colorCount || p.desc.depth;
    if (!p.drawable)
        p.width = p.height = 0;
    p.pending = true;
    m_pipelineDirty = true;
}

void Device::OpenPass()
{
    if (!m_pass.pending)
        return;
    gb::BeginPass(m_pass.desc);
    m_pass.pending = false;
    m_pass.open = true;
    if (!m_openQueries.empty())
        m_openQueries.back()->ResumeInPass();
}

void Device::EndPass()
{
    if (m_pass.pending)
    {
        bool clears = m_pass.desc.depthLoad == gb::LoadOp::Clear || m_pass.desc.stencilLoad == gb::LoadOp::Clear;
        for (int i = 0; i < m_pass.desc.colorCount; ++i)
            clears = clears || m_pass.desc.color[i].load == gb::LoadOp::Clear;
        if (clears && m_pass.drawable)
            OpenPass();
        m_pass.pending = false;
    }
    if (m_pass.open)
    {
        if (!m_openQueries.empty())
            m_openQueries.back()->SplitAtPassEnd();
        gb::EndPass();
        m_pass.open = false;
    }
    for (Surface *&s : m_pass.color)
        Rebind<Surface>(s, nullptr);
    Rebind<Surface>(m_pass.depth, nullptr);
    m_pass.desc = gb::PassDesc();
    m_pass.drawable = false;
}

void Device::BeforeTextureWrite(const gb::Texture *texture)
{
    if (IsAttachment(texture))
        EndPass();
}

void Device::QueryOpened(Query *query)
{
    if (m_pass.open)
    {
        if (!m_openQueries.empty())
            m_openQueries.back()->SplitAtPassEnd();
        query->ResumeInPass();
    }
    m_openQueries.push_back(query);
}

void Device::QueryClosed(Query *query)
{
    // One occlusion query counts at a time: closing the newest resumes the one it interrupted.
    const bool newest = !m_openQueries.empty() && m_openQueries.back() == query;
    m_openQueries.erase(std::remove(m_openQueries.begin(), m_openQueries.end(), query), m_openQueries.end());
    if (newest && m_pass.open && !m_openQueries.empty())
        m_openQueries.back()->ResumeInPass();
}

HRESULT Device::Clear(DWORD count, CONST D3DRECT *rects, DWORD flags, D3DCOLOR color, float z, DWORD stencil)
{
    Guard guard(DeviceLock());
    const bool clearColor = flags & D3DCLEAR_TARGET;
    const bool clearDepth = flags & D3DCLEAR_ZBUFFER;
    const bool clearStencil = flags & D3DCLEAR_STENCIL;
    if ((clearDepth || clearStencil) && !m_depthStencil)
        return D3DERR_INVALIDCALL;
    if (clearStencil && !m_depthStencil->GetStorage().format->stencil)
        return D3DERR_INVALIDCALL;
    PreparePass();
    if (!m_pass.drawable)
        return D3D_OK;
    const bool colorOn = clearColor && m_pass.desc.colorCount;
    const bool depthOn = clearDepth && m_pass.desc.depth;
    const bool stencilOn = clearStencil && m_pass.desc.depth;
    if (!colorOn && !depthOn && !stencilOn)
        return D3D_OK;

    // The cleared area: the viewport, the scissor rect when the test is on, and the rects if any.
    const gb::Rect target = {0, 0, m_pass.width, m_pass.height};
    gb::Rect area;
    const gb::Rect viewport = {int32_t(m_viewport.X), int32_t(m_viewport.Y), m_viewport.Width, m_viewport.Height};
    if (!Intersect(target, viewport, &area))
        return D3D_OK;
    if (m_renderStates[D3DRS_SCISSORTESTENABLE] &&
        !Intersect(area, FromRect(m_scissor.left, m_scissor.top, m_scissor.right, m_scissor.bottom), &area))
        return D3D_OK;
    std::vector<gb::Rect> clearRects;
    if (count && rects)
    {
        for (DWORD i = 0; i < count; ++i)
        {
            gb::Rect r;
            if (Intersect(area, FromRect(rects[i].x1, rects[i].y1, rects[i].x2, rects[i].y2), &r))
                clearRects.push_back(r);
        }
    }
    else
        clearRects.push_back(area);
    if (clearRects.empty())
        return D3D_OK;

    float rgba[4];
    Unpack(color, rgba);
    const float depthValue = std::min(std::max(z, 0.0f), 1.0f);
    const bool whole = clearRects.size() == 1 && clearRects[0].x == 0 && clearRects[0].y == 0 &&
                       clearRects[0].width == m_pass.width && clearRects[0].height == m_pass.height;
    if (whole && m_pass.pending)
    {
        for (int i = 0; colorOn && i < m_pass.desc.colorCount; ++i)
        {
            m_pass.desc.color[i].load = gb::LoadOp::Clear;
            std::memcpy(m_pass.desc.color[i].clear, rgba, sizeof(rgba));
        }
        if (depthOn)
        {
            m_pass.desc.depthLoad = gb::LoadOp::Clear;
            m_pass.desc.clearDepth = depthValue;
        }
        if (stencilOn)
        {
            m_pass.desc.stencilLoad = gb::LoadOp::Clear;
            m_pass.desc.clearStencil = uint8_t(stencil);
        }
        return D3D_OK;
    }
    OpenPass();
    gb::Clear(clearRects.data(), uint32_t(clearRects.size()), colorOn, depthOn, stencilOn, rgba, depthValue,
              uint8_t(stencil));
    return D3D_OK;
}

// ---- draws

void Device::BuildPipeline(gb::PipelineDesc &desc, gb::Topology topology) const
{
    const DWORD *rs = m_renderStates;
    desc.vertex = m_vertexShader->shader;
    desc.pixel = m_pixelShader->shader;
    desc.topology = topology;
    desc.attribCount = m_declaration->attribCount;
    std::memcpy(desc.attribs, m_declaration->attribs, sizeof(desc.attribs));

    gb::BlendState &b = desc.blend;
    b.enable = rs[D3DRS_ALPHABLENDENABLE] && m_pass.desc.colorCount;
    b.srcColor = ToBlend(rs[D3DRS_SRCBLEND]);
    b.dstColor = ToBlend(rs[D3DRS_DESTBLEND]);
    if (rs[D3DRS_SRCBLEND] == D3DBLEND_BOTHSRCALPHA)
    {
        b.srcColor = gb::Blend::SrcAlpha;
        b.dstColor = gb::Blend::InvSrcAlpha;
    }
    else if (rs[D3DRS_SRCBLEND] == D3DBLEND_BOTHINVSRCALPHA)
    {
        b.srcColor = gb::Blend::InvSrcAlpha;
        b.dstColor = gb::Blend::SrcAlpha;
    }
    b.opColor = ToBlendOp(rs[D3DRS_BLENDOP]);
    if (rs[D3DRS_SEPARATEALPHABLENDENABLE])
    {
        b.srcAlpha = ToBlend(rs[D3DRS_SRCBLENDALPHA]);
        b.dstAlpha = ToBlend(rs[D3DRS_DESTBLENDALPHA]);
        b.opAlpha = ToBlendOp(rs[D3DRS_BLENDOPALPHA]);
    }
    else
    {
        b.srcAlpha = b.srcColor;
        b.dstAlpha = b.dstColor;
        b.opAlpha = b.opColor;
    }
    b.writeMask = uint8_t(rs[D3DRS_COLORWRITEENABLE] & 0xF);

    gb::DepthStencilState &ds = desc.depthStencil;
    const Storage *depth = m_pass.depth && m_pass.desc.depth ? &m_pass.depth->GetStorage() : nullptr;
    ds.depthTest = depth && rs[D3DRS_ZENABLE] != D3DZB_FALSE;
    ds.depthWrite = ds.depthTest && rs[D3DRS_ZWRITEENABLE];
    ds.depthFunc = ToCompare(rs[D3DRS_ZFUNC]);
    ds.stencil = depth && depth->format->stencil && rs[D3DRS_STENCILENABLE];
    // D3D9's front faces are the clockwise ones; the CCW_ states apply to the others in two-sided mode.
    ds.front = {ToCompare(rs[D3DRS_STENCILFUNC]), ToStencilOp(rs[D3DRS_STENCILFAIL]),
                ToStencilOp(rs[D3DRS_STENCILZFAIL]), ToStencilOp(rs[D3DRS_STENCILPASS])};
    ds.back = rs[D3DRS_TWOSIDEDSTENCILMODE]
                  ? gb::StencilFace{ToCompare(rs[D3DRS_CCW_STENCILFUNC]), ToStencilOp(rs[D3DRS_CCW_STENCILFAIL]),
                                    ToStencilOp(rs[D3DRS_CCW_STENCILZFAIL]), ToStencilOp(rs[D3DRS_CCW_STENCILPASS])}
                  : ds.front;
    ds.readMask = uint8_t(rs[D3DRS_STENCILMASK]);
    ds.writeMask = uint8_t(rs[D3DRS_STENCILWRITEMASK]);
    if (!ds.stencil)
    {
        ds.front = ds.back = gb::StencilFace();
        ds.readMask = ds.writeMask = 0xFF;
    }

    gb::RasterState &r = desc.raster;
    r.cull = rs[D3DRS_CULLMODE] == D3DCULL_CW ? gb::Cull::Front
             : rs[D3DRS_CULLMODE] == D3DCULL_CCW ? gb::Cull::Back
                                                 : gb::Cull::None;
    r.fill = rs[D3DRS_FILLMODE] == D3DFILL_SOLID ? gb::Fill::Solid : gb::Fill::Wireframe;
    r.scissor = rs[D3DRS_SCISSORTESTENABLE] != 0;
    r.depthBias = AsFloat(rs[D3DRS_DEPTHBIAS]);
    r.slopeScaledDepthBias = AsFloat(rs[D3DRS_SLOPESCALEDEPTHBIAS]);

    desc.colorCount = m_pass.desc.colorCount;
    for (int i = 0; i < desc.colorCount; ++i)
        desc.colorFormats[i] = m_pass.color[i]->GetStorage().format->format;
    desc.depthFormat = depth ? depth->format->format : gb::Format::Unknown;
    desc.samples = m_pass.samples;
}

const gb::Sampler *Device::SamplerFor(uint32_t index, const Storage &texture)
{
    if (!(m_samplerDirty & (1u << index)) && m_samplers[index] && m_samplerShadowTest[index] == texture.shadowTest)
        return m_samplers[index];
    const DWORD *ss = m_samplerStates[index];
    auto filter = [](DWORD f) { return f >= D3DTEXF_LINEAR ? gb::Filter::Linear : gb::Filter::Nearest; };
    gb::SamplerDesc desc;
    desc.magFilter = filter(ss[D3DSAMP_MAGFILTER]);
    desc.minFilter = filter(ss[D3DSAMP_MINFILTER]);
    desc.mipFilter = ss[D3DSAMP_MIPFILTER] == D3DTEXF_NONE    ? gb::MipFilter::None
                     : ss[D3DSAMP_MIPFILTER] == D3DTEXF_POINT ? gb::MipFilter::Nearest
                                                              : gb::MipFilter::Linear;
    desc.addressU = ToAddress(ss[D3DSAMP_ADDRESSU]);
    desc.addressV = ToAddress(ss[D3DSAMP_ADDRESSV]);
    desc.addressW = ToAddress(ss[D3DSAMP_ADDRESSW]);
    // D3D9 filters anisotropically only where the filter says so.
    const bool anisotropic =
        ss[D3DSAMP_MINFILTER] == D3DTEXF_ANISOTROPIC || ss[D3DSAMP_MAGFILTER] == D3DTEXF_ANISOTROPIC;
    desc.maxAnisotropy = anisotropic ? uint8_t(std::min<DWORD>(std::max<DWORD>(ss[D3DSAMP_MAXANISOTROPY], 1), 16)) : 1;
    desc.depthCompare = texture.shadowTest;
    m_samplers[index] = gb::GetSampler(desc);
    m_samplerShadowTest[index] = texture.shadowTest;
    m_samplerDirty &= ~(1u << index);
    return m_samplers[index];
}

bool Device::PrepareDraw(gb::DrawState &state, D3DPRIMITIVETYPE type, const gb::VertexBinding *upStream)
{
    gb::Primitive primitive;
    gb::Topology topology;
    uint32_t vertices;
    if (!PrimitiveInfo(type, 1, &primitive, &topology, &vertices))
        return false;
    PreparePass();
    if (!m_pass.drawable)
    {
        WarnOnce("no target", "d3d9emu: a draw with no render target and no depth buffer is skipped");
        return false;
    }
    if (!m_vertexShader || !m_pixelShader)
    {
        WarnOnce("fixed function", "d3d9emu: fixed-function draws are not emulated: skipped");
        return false;
    }
    if (!m_vertexShader->shader || !m_pixelShader->shader)
        return false;
    if (!m_declaration)
    {
        WarnOnce("no declaration", "d3d9emu: a draw without a vertex declaration is skipped");
        return false;
    }
    if (m_pipelineDirty || m_pipelineTopology != topology)
    {
        gb::PipelineDesc desc;
        BuildPipeline(desc, topology);
        m_pipeline = gb::GetPipeline(desc);
        m_pipelineTopology = topology;
        m_pipelineDirty = false;
    }
    if (!m_pipeline)
        return false;
    state.pipeline = m_pipeline;

    for (uint8_t i = 0; i < m_declaration->attribCount; ++i)
    {
        const uint8_t stream = m_declaration->attribs[i].stream;
        if (stream == 0 && upStream)
        {
            state.vertices[0] = *upStream;
            continue;
        }
        const Stream &s = m_streams[stream];
        if (!s.buffer || !s.buffer->storage.gpu)
        {
            WarnOnce("no stream", "d3d9emu: a draw reading a stream without a vertex buffer is skipped");
            return false;
        }
        state.vertices[stream] = {s.buffer->storage.gpu, s.offset, s.stride};
    }

    for (uint32_t mask = m_pixelShader->samplerMask; mask; mask &= mask - 1)
    {
        const uint32_t i = uint32_t(__builtin_ctz(mask));
        TextureImpl *texture = m_textures[i];
        if (!texture || !texture->storage->gpu)
            continue;
        if (IsAttachment(texture->storage->gpu))
        {
            WarnOnce("feedback", "d3d9emu: a sampler reads a target of the pass it draws into: left unbound");
            continue;
        }
        state.textures[i] = {texture->storage->gpu, SamplerFor(i, *texture->storage)};
    }

    // bo1_pos_fixup: D3D9's pixel centres on a backend that samples pixels at their middle (docs/RENDERER.md). The
    // vertices move right and down by just under half a pixel (wined3d's 63/128), so that an edge through a D3D9
    // centre stays on D3D9's side of it whichever way the backend breaks ties.
    const float halfPixel = 63.0f / 128.0f;
    const float fixup[4] = {2.0f * halfPixel / float(m_viewport.Width ? m_viewport.Width : 1),
                            -2.0f * halfPixel / float(m_viewport.Height ? m_viewport.Height : 1), 0.0f, 0.0f};
    if (std::memcmp(m_vertexConstants[256], fixup, sizeof(fixup)))
    {
        std::memcpy(m_vertexConstants[256], fixup, sizeof(fixup));
        m_vertexConstantsDirty = true;
    }
    // bo1_alpha_test: x 0 off, 1 alpha > ref, 2 alpha >= ref; y the ref.
    float alphaTest[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    if (m_renderStates[D3DRS_ALPHATESTENABLE])
    {
        const DWORD func = m_renderStates[D3DRS_ALPHAFUNC];
        if (func == D3DCMP_GREATER || func == D3DCMP_GREATEREQUAL)
        {
            alphaTest[0] = func == D3DCMP_GREATER ? 1.0f : 2.0f;
            alphaTest[1] = float(m_renderStates[D3DRS_ALPHAREF] & 0xFF);
        }
        else if (func != D3DCMP_ALWAYS)
            WarnOnce("alpha func", "d3d9emu: alpha test function %lu is not emulated: no alpha test",
                     static_cast<unsigned long>(func));
    }
    if (std::memcmp(m_pixelConstants[256], alphaTest, sizeof(alphaTest)))
    {
        std::memcpy(m_pixelConstants[256], alphaTest, sizeof(alphaTest));
        m_pixelConstantsDirty = true;
    }
    if (m_vertexConstantsDirty)
    {
        m_vertexSlice = gb::AllocUniforms(m_vertexConstants, sizeof(m_vertexConstants));
        m_vertexConstantsDirty = false;
    }
    if (m_pixelConstantsDirty)
    {
        m_pixelSlice = gb::AllocUniforms(m_pixelConstants, sizeof(m_pixelConstants));
        m_pixelConstantsDirty = false;
    }
    state.vertexConstants = m_vertexSlice;
    state.pixelConstants = m_pixelSlice;
    state.viewport = {float(m_viewport.X), float(m_viewport.Y), float(m_viewport.Width), float(m_viewport.Height),
                      m_viewport.MinZ, m_viewport.MaxZ};
    gb::Rect scissor = {0, 0, 0, 0};
    Intersect({0, 0, m_pass.width, m_pass.height},
              FromRect(m_scissor.left, m_scissor.top, m_scissor.right, m_scissor.bottom), &scissor);
    state.scissor = scissor;
    state.stencilRef = uint8_t(m_renderStates[D3DRS_STENCILREF]);
    Unpack(m_renderStates[D3DRS_BLENDFACTOR], state.blendConstant);
    OpenPass();
    return true;
}

HRESULT Device::DrawPrimitive(D3DPRIMITIVETYPE type, UINT startVertex, UINT primitiveCount)
{
    Guard guard(DeviceLock());
    gb::Primitive primitive;
    gb::Topology topology;
    uint32_t vertices;
    if (!m_inScene || !PrimitiveInfo(type, primitiveCount, &primitive, &topology, &vertices))
        return D3DERR_INVALIDCALL;
    gb::DrawState state;
    if (primitiveCount && PrepareDraw(state, type, nullptr))
        gb::Draw(state, primitive, vertices, startVertex);
    return D3D_OK;
}

HRESULT Device::DrawIndexedPrimitive(D3DPRIMITIVETYPE type, INT baseVertex, UINT, UINT, UINT startIndex,
                                     UINT primitiveCount)
{
    Guard guard(DeviceLock());
    gb::Primitive primitive;
    gb::Topology topology;
    uint32_t indices;
    if (!m_inScene || !PrimitiveInfo(type, primitiveCount, &primitive, &topology, &indices) || !m_indices)
        return D3DERR_INVALIDCALL;
    if (!m_indices->storage.gpu)
        return D3DERR_INVALIDCALL;
    gb::DrawState state;
    if (primitiveCount && PrepareDraw(state, type, nullptr))
    {
        state.indices = m_indices->storage.gpu;
        gb::DrawIndexed(state, primitive, indices, startIndex, baseVertex);
    }
    return D3D_OK;
}

HRESULT Device::DrawPrimitiveUP(D3DPRIMITIVETYPE type, UINT primitiveCount, CONST void *vertexData, UINT stride)
{
    Guard guard(DeviceLock());
    gb::Primitive primitive;
    gb::Topology topology;
    uint32_t vertices;
    if (!m_inScene || !vertexData || !stride ||
        !PrimitiveInfo(type, primitiveCount, &primitive, &topology, &vertices))
        return D3DERR_INVALIDCALL;
    // The runtime leaves stream 0 unset after a DrawPrimitiveUP.
    Rebind<VertexBuffer>(m_streams[0].buffer, nullptr);
    m_streams[0].offset = m_streams[0].stride = 0;
    if (!primitiveCount)
        return D3D_OK;
    const uint64_t bytes = uint64_t(vertices) * stride;
    if (bytes + kUpPadding > kUpBufferSize)
    {
        WarnOnce("large UP draw", "d3d9emu: a DrawPrimitiveUP of more than %u bytes is skipped",
                 kUpBufferSize - kUpPadding);
        return D3D_OK;
    }
    const uint32_t size = uint32_t(bytes) + kUpPadding;
    gb::Upload mode = gb::Upload::NoOverwrite;
    if (m_upOffset + size > kUpBufferSize)
    {
        m_upOffset = 0;
        mode = gb::Upload::Discard;
    }
    std::memcpy(m_upScratch.data(), vertexData, size_t(bytes));
    std::memset(m_upScratch.data() + bytes, 0, kUpPadding);
    gb::DrawState state;
    const gb::VertexBinding up = {m_upBuffer, m_upOffset, stride};
    if (PrepareDraw(state, type, &up))
    {
        gb::UploadBuffer(m_upBuffer, m_upOffset, m_upScratch.data(), size, mode);
        gb::Draw(state, primitive, vertices, 0);
        m_upOffset = (m_upOffset + size + 15) & ~15u;
    }
    return D3D_OK;
}

// AMD's RESZ, which D3D9 drivers of every vendor answer for: the depth buffer is copied into the INTZ texture at
// sampler 0 (gfx_d3d/rb_backend.cpp R_ResolveIntZ_PC).
void Device::Resolve()
{
    TextureImpl *target = m_textures[0];
    if (!m_depthStencil || !target || target->storage->format->d3d != FOURCC_INTZ)
    {
        WarnOnce("resz", "d3d9emu: a RESZ resolve without a depth buffer and an INTZ texture at sampler 0 is skipped");
        return;
    }
    Storage &source = m_depthStencil->GetStorage();
    Storage &destination = *target->storage;
    if (source.Width(m_depthStencil->Level()) != destination.width ||
        source.Height(m_depthStencil->Level()) != destination.height || source.format->format != destination.format->format)
    {
        WarnOnce("resz size", "d3d9emu: a RESZ resolve between different sizes or formats is skipped");
        return;
    }
    EndPass();
    const gb::Rect all = {0, 0, destination.width, destination.height};
    gb::Blit(source.gpu, uint8_t(m_depthStencil->Level()), 0, all, destination.gpu, 0, 0, all, gb::Filter::Nearest);
}

// ---- copies

HRESULT Device::StretchRect(IDirect3DSurface9 *sourceSurface, CONST RECT *sourceRect,
                            IDirect3DSurface9 *destinationSurface, CONST RECT *destRect, D3DTEXTUREFILTERTYPE filter)
{
    if (!sourceSurface || !destinationSurface)
        return D3DERR_INVALIDCALL;
    Guard guard(DeviceLock());
    Surface *src = SurfaceOf(sourceSurface);
    Surface *dst = SurfaceOf(destinationSurface);
    Storage &s = src->GetStorage();
    Storage &d = dst->GetStorage();
    if (!s.gpu || !d.gpu || d.multiSample != D3DMULTISAMPLE_NONE || d.format->block)
        return D3DERR_INVALIDCALL;
    const gb::Rect srcAll = {0, 0, src->Width(), src->Height()};
    const gb::Rect dstAll = {0, 0, dst->Width(), dst->Height()};
    gb::Rect srcRect = srcAll, dstRect = dstAll;
    if (sourceRect && !Intersect(srcAll, FromRect(sourceRect->left, sourceRect->top, sourceRect->right,
                                                  sourceRect->bottom), &srcRect))
        return D3DERR_INVALIDCALL;
    if (destRect &&
        !Intersect(dstAll, FromRect(destRect->left, destRect->top, destRect->right, destRect->bottom), &dstRect))
        return D3DERR_INVALIDCALL;
    if (s.format->depth || d.format->depth)
    {
        if (sourceRect || destRect || s.format->format != d.format->format || src->Width() != dst->Width() ||
            src->Height() != dst->Height())
            return D3DERR_INVALIDCALL;
        filter = D3DTEXF_POINT;
    }
    EndPass();
    gb::Blit(s.gpu, uint8_t(src->Level()), uint8_t(src->Layer()), srcRect, d.gpu, uint8_t(dst->Level()),
             uint8_t(dst->Layer()), dstRect, filter >= D3DTEXF_LINEAR ? gb::Filter::Linear : gb::Filter::Nearest);
    if (d.lockable)
        d.gpuNewer = true;
    return D3D_OK;
}

HRESULT Device::UpdateSurface(IDirect3DSurface9 *sourceSurface, CONST RECT *sourceRect,
                              IDirect3DSurface9 *destinationSurface, CONST POINT *destPoint)
{
    if (!sourceSurface || !destinationSurface)
        return D3DERR_INVALIDCALL;
    Guard guard(DeviceLock());
    Surface *src = SurfaceOf(sourceSurface);
    Surface *dst = SurfaceOf(destinationSurface);
    Storage &s = src->GetStorage();
    Storage &d = dst->GetStorage();
    if (s.pool != D3DPOOL_SYSTEMMEM || d.pool != D3DPOOL_DEFAULT || !d.gpu || s.format != d.format ||
        d.multiSample != D3DMULTISAMPLE_NONE || s.format->depth)
        return D3DERR_INVALIDCALL;
    gb::Rect from = {0, 0, src->Width(), src->Height()};
    if (sourceRect && !Intersect(from, FromRect(sourceRect->left, sourceRect->top, sourceRect->right,
                                                sourceRect->bottom), &from))
        return D3DERR_INVALIDCALL;
    const int32_t x = destPoint ? destPoint->x : 0, y = destPoint ? destPoint->y : 0;
    if (x < 0 || y < 0 || uint64_t(x) + from.width > dst->Width() || uint64_t(y) + from.height > dst->Height())
        return D3DERR_INVALIDCALL;
    if (s.format->block && (from.x % 4 || from.y % 4 || x % 4 || y % 4))
        return D3DERR_INVALIDCALL;
    const FormatInfo &f = *s.format;
    const uint32_t blockRows = f.block ? (from.height + 3) / 4 : from.height;
    const uint32_t rowBytes = (f.block ? (from.width + 3) / 4 : from.width) * f.bytes;
    const uint32_t srcPitch = s.RowPitch(src->Level());
    const uint8_t *srcData = s.Cpu(src->Layer(), src->Level()) + size_t(f.block ? from.y / 4 : from.y) * srcPitch +
                             size_t(f.block ? from.x / 4 : from.x) * f.bytes;
    if (d.lockable)
    {
        if (d.gpuNewer)
            ReadBack(d);
        const uint32_t dstPitch = d.RowPitch(dst->Level());
        uint8_t *dstData = d.Cpu(dst->Layer(), dst->Level()) + size_t(f.block ? y / 4 : y) * dstPitch +
                           size_t(f.block ? x / 4 : x) * f.bytes;
        for (uint32_t row = 0; row < blockRows; ++row)
            std::memcpy(dstData + size_t(row) * dstPitch, srcData + size_t(row) * srcPitch, rowBytes);
        const D3DBOX box = {UINT(x), UINT(y), UINT(x) + from.width, UINT(y) + from.height, 0, 1};
        d.Upload(dst->Layer(), dst->Level(), box);
        return D3D_OK;
    }
    BeforeTextureWrite(d.gpu);
    const gb::Box box = {uint16_t(x), uint16_t(y), 0, uint16_t(from.width), uint16_t(from.height), 1};
    gb::UploadTexture(d.gpu, uint8_t(dst->Level()), uint8_t(dst->Layer()), box, srcData, srcPitch,
                      srcPitch * blockRows);
    return D3D_OK;
}

void Device::ReadBack(Storage &storage)
{
    storage.gpuNewer = false;
    if (!storage.gpu || storage.format->block || storage.multiSample != D3DMULTISAMPLE_NONE)
        return;
    EndPass();
    for (uint32_t layer = 0; layer < storage.layers; ++layer)
        for (uint32_t level = 0; level < storage.levels; ++level)
        {
            if (storage.subresources[layer * storage.levels + level].cpu.empty())
                continue;
            const gb::Box box = {0, 0, 0, uint16_t(storage.Width(level)), uint16_t(storage.Height(level)), 1};
            gb::ReadTexture(storage.gpu, uint8_t(level), uint8_t(layer), box, storage.Cpu(layer, level),
                            storage.RowPitch(level));
        }
}

HRESULT Device::GetRenderTargetData(IDirect3DSurface9 *renderTarget, IDirect3DSurface9 *destination)
{
    if (!renderTarget || !destination)
        return D3DERR_INVALIDCALL;
    Guard guard(DeviceLock());
    Surface *src = SurfaceOf(renderTarget);
    Surface *dst = SurfaceOf(destination);
    Storage &s = src->GetStorage();
    Storage &d = dst->GetStorage();
    if (!s.gpu || s.multiSample != D3DMULTISAMPLE_NONE || !(s.usage & D3DUSAGE_RENDERTARGET) ||
        d.pool != D3DPOOL_SYSTEMMEM || s.format != d.format || src->Width() != dst->Width() ||
        src->Height() != dst->Height())
        return D3DERR_INVALIDCALL;
    EndPass();
    const gb::Box box = {0, 0, 0, uint16_t(src->Width()), uint16_t(src->Height()), 1};
    gb::ReadTexture(s.gpu, uint8_t(src->Level()), uint8_t(src->Layer()), box, d.Cpu(dst->Layer(), dst->Level()),
                    d.RowPitch(dst->Level()));
    return D3D_OK;
}

HRESULT Device::GetFrontBufferData(UINT swapChain, IDirect3DSurface9 *destination)
{
    if (swapChain != 0)
        return D3DERR_INVALIDCALL;
    return ReadFrontBuffer(destination);
}

// The front buffer is the display: the last frame lands at the window's place on it, the rest reads black.
HRESULT Device::ReadFrontBuffer(IDirect3DSurface9 *destination)
{
    if (!destination)
        return D3DERR_INVALIDCALL;
    Guard guard(DeviceLock());
    Surface *dst = SurfaceOf(destination);
    Storage &d = dst->GetStorage();
    if (d.format->d3d != D3DFMT_A8R8G8B8 || !d.CpuPool())
        return D3DERR_INVALIDCALL;
    Storage &image = m_presentImage ? *m_presentImage : m_backBuffer->GetStorage();
    int32_t originX = 0, originY = 0;
    if (m_parameters.Windowed && !host::ClientOrigin(m_window, &originX, &originY))
        return D3DERR_INVALIDCALL;
    EndPass();
    const uint32_t width = image.width, height = image.height, pitch = width * 4;
    m_readScratch.resize(size_t(pitch) * height);
    gb::ReadTexture(image.gpu, 0, 0, {0, 0, 0, uint16_t(width), uint16_t(height), 1}, m_readScratch.data(), pitch);
    uint8_t *out = d.Cpu(0, 0);
    const uint32_t outPitch = d.RowPitch(0);
    std::memset(out, 0, size_t(outPitch) * d.height);
    const bool opaque = image.format->d3d == D3DFMT_X8R8G8B8;
    for (uint32_t row = 0; row < height; ++row)
    {
        const int64_t y = int64_t(originY) + row;
        if (y < 0 || y >= int64_t(d.height))
            continue;
        for (uint32_t column = 0; column < width; ++column)
        {
            const int64_t x = int64_t(originX) + column;
            if (x < 0 || x >= int64_t(d.width))
                continue;
            uint8_t *pixel = out + size_t(y) * outPitch + size_t(x) * 4;
            std::memcpy(pixel, &m_readScratch[size_t(row) * pitch + size_t(column) * 4], 4);
            if (opaque)
                pixel[3] = 0xFF;
        }
    }
    return D3D_OK;
}

// ---- presentation

HRESULT Device::PresentFrame(DWORD)
{
    Guard guard(DeviceLock());
    EndPass();
    Storage &back = m_backBuffer->GetStorage();
    gb::Texture *image = back.gpu;
    if (m_presentImage)
    {
        const gb::Rect all = {0, 0, back.width, back.height};
        gb::Blit(back.gpu, 0, 0, all, m_presentImage->gpu, 0, 0, all, gb::Filter::Nearest);
        image = m_presentImage->gpu;
    }
    gb::Present(m_swapchain, image);
    // Uniform memory starts over with each frame.
    m_vertexConstantsDirty = true;
    m_pixelConstantsDirty = true;
    // A 9Ex device tells a frame nobody can see (D3D9 on a hidden desktop); plain D3D9 does not.
    if (m_ex && !host::WindowVisible(m_window))
        return S_PRESENT_OCCLUDED;
    return D3D_OK;
}

HRESULT Device::Present(CONST RECT *, CONST RECT *, HWND, CONST RGNDATA *)
{
    return PresentFrame(0);
}

HRESULT Device::PresentEx(CONST RECT *, CONST RECT *, HWND, CONST RGNDATA *, DWORD flags)
{
    return PresentFrame(flags);
}

// ---- the implicit swap chain

SwapChain::SwapChain(Device *device) : m_device(device)
{
}

ULONG SwapChain::AddRef()
{
    return m_device->AddRef();
}

ULONG SwapChain::Release()
{
    return m_device->Release();
}

HRESULT SwapChain::Present(CONST RECT *, CONST RECT *, HWND, CONST RGNDATA *, DWORD flags)
{
    return m_device->PresentFrame(flags);
}

HRESULT SwapChain::GetFrontBufferData(IDirect3DSurface9 *destination)
{
    return m_device->ReadFrontBuffer(destination);
}

HRESULT SwapChain::GetBackBuffer(UINT index, D3DBACKBUFFER_TYPE type, IDirect3DSurface9 **surface)
{
    return m_device->GetBackBuffer(0, index, type, surface);
}

HRESULT SwapChain::GetDevice(IDirect3DDevice9 **device)
{
    if (!device)
        return D3DERR_INVALIDCALL;
    m_device->AddRef();
    *device = m_device;
    return D3D_OK;
}
} // namespace d3d9emu
