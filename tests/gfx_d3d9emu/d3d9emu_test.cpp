// Unit test of the D3D9 frontend (src/gfx_d3d9emu) on the null backend (src/gfx_backend/null): it drives Direct3D 9
// the way the engine does and checks what reaches the backend, from the null backend's log. The null backend also
// ends the process on any breach of the contract (gfx_backend.h), so a complete run proves the frontend keeps it.
//
// Build and run: docs/PORTING.md, phase 4 (4b step 1).

#include <d3d9.h>
#include <d3dx9.h>

#include <gfx_backend/null/gb_null.h>
#include <gfx_d3d9emu/d3d9emu_host.h>
#include <gfx_d3d9emu/shader_pack.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
int g_failures = 0;
std::vector<std::string> g_hostLog;
std::string g_packPath = "d3d9emu_test.pack";
bool g_visible = true;
std::string g_savedPath;
std::vector<uint8_t> g_saved;
const HWND kWindow = reinterpret_cast<HWND>(0x1234);

void Check(bool condition, const char *what, int line)
{
    if (!condition)
    {
        std::printf("FAILED line %d: %s\n", line, what);
        ++g_failures;
    }
}

#define CHECK(condition) Check((condition), #condition, __LINE__)

std::vector<std::string> g_log;

void Collect()
{
    for (std::string &line : gb::null::TakeLog())
        g_log.push_back(std::move(line));
}

// The lines logged since the last Mark, after collecting the backend's.
size_t g_mark = 0;
void Mark()
{
    Collect();
    g_mark = g_log.size();
}

int Count(const std::string &needle)
{
    Collect();
    int n = 0;
    for (size_t i = g_mark; i < g_log.size(); ++i)
        n += g_log[i].find(needle) != std::string::npos;
    return n;
}

bool Has(const std::string &needle)
{
    return Count(needle) > 0;
}

// The index of the first line from the mark holding `needle`, or -1.
long Find(const std::string &needle)
{
    Collect();
    for (size_t i = g_mark; i < g_log.size(); ++i)
        if (g_log[i].find(needle) != std::string::npos)
            return long(i);
    return -1;
}

bool HostLogged(const char *needle)
{
    for (const std::string &line : g_hostLog)
        if (line.find(needle) != std::string::npos)
            return true;
    return false;
}

// The backend name of the last resource whose creation line holds `needle` ("T7").
std::string LastId(const char *creation)
{
    Collect();
    for (size_t i = g_log.size(); i-- > 0;)
        if (g_log[i].rfind(creation, 0) == 0)
        {
            const size_t start = std::strlen(creation) + 1;
            return g_log[i].substr(start, g_log[i].find(' ', start) - start);
        }
    return "?";
}

uint64_t Fnv(const void *data, size_t size)
{
    uint64_t hash = 1469598103934665603ull;
    for (size_t i = 0; i < size; ++i)
    {
        hash ^= static_cast<const uint8_t *>(data)[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string Hex(uint64_t value)
{
    char text[24];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
    return text;
}

// vs_3_0: dcl_position v0; dcl_position o0; mov o0, v0
const DWORD kVertexShader[] = {0xFFFE0300, 0x0200001F, 0x80000000, 0x900F0000, 0x0200001F, 0x80000000,
                               0xE00F0000, 0x02000001, 0xE00F0000, 0x90E40000, 0x0000FFFF};
// ps_3_0: dcl_texcoord0 v0.xy; dcl_2d s0; dcl_2d s3; texld r0, v0, s0; mov oC0, r0
const DWORD kPixelShader[] = {0xFFFF0300, 0x0200001F, 0x80000005, 0x90030000, 0x0200001F, 0x90000000,
                              0xA00F0800, 0x0200001F, 0x90000000, 0xA00F0803, 0x03000042, 0x800F0000,
                              0x90E40000, 0xA0E40800, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF};
// The same pixel shader with a comment token, so a different hash that the pack does not hold.
const DWORD kMissingPixelShader[] = {0xFFFF0300, 0x0001FFFE, 0x12345678, 0x02000001, 0x800F0800, 0xA0E40000,
                                     0x0000FFFF};

uint64_t g_vertexHash, g_pixelHash;

bool WritePack()
{
    g_vertexHash = ShaderPack_Hash(kVertexShader, sizeof(kVertexShader));
    g_pixelHash = ShaderPack_Hash(kPixelShader, sizeof(kPixelShader));
    const std::string vs = "// test vertex shader", ps = "// test pixel shader";
    struct Item
    {
        uint64_t hash;
        uint32_t stage;
        const std::string *text;
    } items[2] = {{g_vertexHash, SHADER_PACK_VERTEX, &vs}, {g_pixelHash, SHADER_PACK_PIXEL, &ps}};
    if (items[0].hash > items[1].hash)
        std::swap(items[0], items[1]);
    ShaderPackHeader header = {};
    std::memcpy(header.magic, SHADER_PACK_MAGIC, sizeof(header.magic));
    header.version = SHADER_PACK_VERSION;
    header.count = 2;
    uint32_t offset = sizeof(header) + 2 * sizeof(ShaderPackEntry);
    ShaderPackEntry entries[2];
    for (int i = 0; i < 2; ++i)
    {
        entries[i] = {items[i].hash, items[i].stage, offset, uint32_t(items[i].text->size()), 0};
        offset += uint32_t(items[i].text->size()) + 1;
    }
    FILE *file = std::fopen(g_packPath.c_str(), "wb");
    if (!file)
        return false;
    bool ok = std::fwrite(&header, sizeof(header), 1, file) == 1 && std::fwrite(entries, sizeof(entries), 1, file) == 1;
    for (const Item &item : items)
        ok = ok && std::fwrite(item.text->c_str(), item.text->size() + 1, 1, file) == 1;
    return std::fclose(file) == 0 && ok;
}

struct Vertex
{
    float position[3];
    float uv[2];
};
} // namespace

namespace d3d9emu::host
{
uint32_t DisplayModes(DisplayMode *modes, uint32_t capacity)
{
    const DisplayMode all[] = {{640, 480, 60}, {1280, 720, 60}};
    for (uint32_t i = 0; i < 2 && i < capacity; ++i)
        modes[i] = all[i];
    return 2;
}

bool DesktopMode(DisplayMode *mode)
{
    *mode = {1280, 720, 60};
    return true;
}

HMONITOR AdapterMonitor()
{
    return reinterpret_cast<HMONITOR>(1);
}

void *NativeWindow(HWND hwnd)
{
    return hwnd;
}

bool ClientSize(HWND, uint32_t *width, uint32_t *height)
{
    *width = 640;
    *height = 480;
    return true;
}

bool ClientOrigin(HWND, int32_t *x, int32_t *y)
{
    *x = 10;
    *y = 20;
    return true;
}

bool WindowVisible(HWND)
{
    return g_visible;
}

const char *ShaderPackPath()
{
    return g_packPath.c_str();
}

bool SaveFile(const char *path, const void *data, size_t size)
{
    g_savedPath = path;
    g_saved.assign(static_cast<const uint8_t *>(data), static_cast<const uint8_t *>(data) + size);
    return true;
}

void Log(const char *line)
{
    g_hostLog.emplace_back(line);
}
} // namespace d3d9emu::host

// d3d9emu_test [--log] [shaders.pack to write]: --log prints the backend's calls.
int main(int argc, char **argv)
{
    bool printLog = false;
    for (int i = 1; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "--log"))
            printLog = true;
        else
            g_packPath = argv[i];
    }
    if (!WritePack())
    {
        std::printf("cannot write %s\n", g_packPath.c_str());
        return 2;
    }
    gb::null::SetRecording(true);

    // ---- the adapter
    IDirect3D9Ex *d3d = nullptr;
    CHECK(Direct3DCreate9Ex(D3D_SDK_VERSION, &d3d) == D3D_OK && d3d);
    CHECK(Has("Init"));
    D3DCAPS9 caps;
    CHECK(d3d->GetDeviceCaps(0, D3DDEVTYPE_HAL, &caps) == D3D_OK);
    CHECK(caps.VertexShaderVersion == D3DVS_VERSION(3, 0) && caps.PixelShaderVersion == D3DPS_VERSION(3, 0));
    CHECK(caps.MaxTextureWidth == 16384 && caps.MaxVolumeExtent == 2048 && caps.MaxAnisotropy == 16);
    const D3DFORMAT kIntz = D3DFORMAT(MAKEFOURCC('I', 'N', 'T', 'Z'));
    const D3DFORMAT kResz = D3DFORMAT(MAKEFOURCC('R', 'E', 'S', 'Z'));
    const D3DFORMAT kNull = D3DFORMAT(MAKEFOURCC('N', 'U', 'L', 'L'));
    const D3DFORMAT kSsaa = D3DFORMAT(MAKEFOURCC('S', 'S', 'A', 'A'));
    auto format = [&](DWORD usage, D3DRESOURCETYPE type, D3DFORMAT f) {
        return d3d->CheckDeviceFormat(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, usage, type, f);
    };
    CHECK(format(D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE, kIntz) == D3D_OK);
    CHECK(format(D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE, kResz) == D3D_OK);
    CHECK(format(D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE, kNull) == D3D_OK);
    CHECK(format(D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE, D3DFMT_D24S8) == D3D_OK);
    CHECK(format(D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, D3DFMT_D24FS8) == D3DERR_NOTAVAILABLE);
    CHECK(format(0, D3DRTYPE_TEXTURE, D3DFMT_DXT1) == D3D_OK);
    CHECK(format(D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, D3DFMT_DXT1) == D3DERR_NOTAVAILABLE);
    CHECK(format(D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, D3DFMT_L8) == D3DERR_NOTAVAILABLE);
    CHECK(format(0, D3DRTYPE_SURFACE, kSsaa) == D3DERR_NOTAVAILABLE);
    CHECK(format(D3DUSAGE_QUERY_VERTEXTEXTURE, D3DRTYPE_TEXTURE, D3DFMT_R32F) == D3DERR_NOTAVAILABLE);
    CHECK(d3d->CheckDepthStencilMatch(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, kNull, D3DFMT_D24S8) == D3D_OK);
    CHECK(d3d->CheckDepthStencilMatch(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DFMT_DXT1, D3DFMT_D24S8) ==
          D3DERR_NOTAVAILABLE);
    DWORD quality = 0;
    CHECK(d3d->CheckDeviceMultiSampleType(0, D3DDEVTYPE_HAL, D3DFMT_A8R8G8B8, TRUE, D3DMULTISAMPLE_4_SAMPLES,
                                          &quality) == D3D_OK &&
          quality == 1);
    CHECK(d3d->CheckDeviceMultiSampleType(0, D3DDEVTYPE_HAL, D3DFMT_A8R8G8B8, TRUE, D3DMULTISAMPLE_3_SAMPLES,
                                          &quality) == D3DERR_NOTAVAILABLE);
    CHECK(d3d->GetAdapterModeCount(0, D3DFMT_X8R8G8B8) == 2);
    D3DDISPLAYMODE mode;
    CHECK(d3d->EnumAdapterModes(0, D3DFMT_X8R8G8B8, 1, &mode) == D3D_OK && mode.Width == 1280 && mode.Height == 720);
    CHECK(d3d->EnumAdapterModes(0, D3DFMT_X8R8G8B8, 2, &mode) == D3DERR_INVALIDCALL);
    D3DADAPTER_IDENTIFIER9 id;
    CHECK(d3d->GetAdapterIdentifier(0, 0, &id) == D3D_OK && std::strstr(id.Description, "on null") && !id.VendorId);

    // ---- the device: a 9Ex one, as the headless client makes
    D3DPRESENT_PARAMETERS pp = {};
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_COPY;
    pp.hDeviceWindow = kWindow;
    pp.Windowed = TRUE;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9Ex *device = nullptr;
    Mark();
    CHECK(d3d->CreateDeviceEx(0, D3DDEVTYPE_HAL, kWindow, D3DCREATE_MULTITHREADED, &pp, nullptr, &device) == D3D_OK);
    CHECK(pp.BackBufferWidth == 640 && pp.BackBufferHeight == 480);
    CHECK(Has("CreateSwapchain W") && Has("640x480 vsync=0"));
    CHECK(Has("BGRA8 RenderTarget 640x480x1"));
    const std::string backBuffer = LastId("CreateTexture");
    const UINT memory = device->GetAvailableTextureMem();
    CHECK(memory > 0 && memory % (1u << 20) == 0);

    // ---- textures: pools, locks and uploads
    IDirect3DTexture9 *managed = nullptr;
    CHECK(device->CreateTexture(4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &managed, nullptr) ==
          D3DERR_INVALIDCALL); // a 9Ex device has no managed pool
    IDirect3DTexture9 *dynamic = nullptr;
    Mark();
    CHECK(device->CreateTexture(4, 4, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &dynamic, nullptr) ==
          D3D_OK);
    CHECK(Has("2D BGRA8 Dynamic sampled 4x4x1 levels=1 samples=1 swizzle=RGBA"));
    const std::string dynamicId = LastId("CreateTexture");
    D3DLOCKED_RECT lr;
    uint8_t pixels[64];
    for (int i = 0; i < 64; ++i)
        pixels[i] = uint8_t(i * 7 + 1);
    CHECK(dynamic->LockRect(0, &lr, nullptr, 0) == D3D_OK && lr.Pitch == 16);
    std::memcpy(lr.pBits, pixels, sizeof(pixels));
    CHECK(dynamic->LockRect(0, &lr, nullptr, 0) == D3DERR_INVALIDCALL); // locked already
    CHECK(dynamic->UnlockRect(0) == D3D_OK);
    CHECK(Has("UploadTexture " + dynamicId + " level=0 layer=0 box=0,0,0 4x4x1 fnv=" + Hex(Fnv(pixels, 64))));
    RECT part = {1, 2, 3, 4};
    CHECK(dynamic->LockRect(0, &lr, &part, D3DLOCK_READONLY) == D3D_OK);
    CHECK(static_cast<uint8_t *>(lr.pBits)[0] == pixels[2 * 16 + 1 * 4]); // a lock sees what was written
    Mark();
    CHECK(dynamic->UnlockRect(0) == D3D_OK && !Has("UploadTexture"));
    CHECK(dynamic->LockRect(0, &lr, &part, 0) == D3D_OK && dynamic->UnlockRect(0) == D3D_OK);
    CHECK(Has("UploadTexture " + dynamicId + " level=0 layer=0 box=1,2,0 2x2x1"));

    IDirect3DTexture9 *dxt = nullptr;
    Mark();
    CHECK(device->CreateTexture(8, 8, 0, D3DUSAGE_DYNAMIC, D3DFMT_DXT1, D3DPOOL_DEFAULT, &dxt, nullptr) == D3D_OK);
    CHECK(dxt->GetLevelCount() == 4 && Has("BC1 Dynamic sampled 8x8x1 levels=4"));
    CHECK(dxt->LockRect(2, &lr, nullptr, 0) == D3D_OK && lr.Pitch == 8);
    CHECK(dxt->UnlockRect(2) == D3D_OK && Has("level=2 layer=0 box=0,0,0 2x2x1"));
    RECT unaligned = {1, 1, 4, 4};
    CHECK(dxt->LockRect(0, &lr, &unaligned, 0) == D3DERR_INVALIDCALL);

    IDirect3DCubeTexture9 *cube = nullptr;
    CHECK(device->CreateCubeTexture(4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &cube, nullptr) == D3D_OK);
    CHECK(cube->LockRect(D3DCUBEMAP_FACE_NEGATIVE_Y, 0, &lr, nullptr, 0) == D3DERR_INVALIDCALL); // not lockable
    cube->Release();
    CHECK(device->CreateCubeTexture(4, 1, D3DUSAGE_DYNAMIC, D3DFMT_L8, D3DPOOL_DEFAULT, &cube, nullptr) == D3D_OK);
    CHECK(Has("Cube R8 Dynamic sampled 4x4x1 levels=1 samples=1 swizzle=RRR1"));
    Mark();
    CHECK(cube->LockRect(D3DCUBEMAP_FACE_NEGATIVE_Y, 0, &lr, nullptr, 0) == D3D_OK && lr.Pitch == 4);
    CHECK(cube->UnlockRect(D3DCUBEMAP_FACE_NEGATIVE_Y, 0) == D3D_OK && Has("level=0 layer=3 box=0,0,0 4x4x1"));

    IDirect3DVolumeTexture9 *volume = nullptr;
    CHECK(device->CreateVolumeTexture(4, 4, 2, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &volume,
                                      nullptr) == D3D_OK);
    D3DLOCKED_BOX lb;
    D3DBOX box = {1, 0, 3, 2, 1, 2};
    Mark();
    CHECK(volume->LockBox(0, &lb, &box, 0) == D3D_OK && lb.RowPitch == 16 && lb.SlicePitch == 64);
    CHECK(volume->UnlockBox(0) == D3D_OK && Has("level=0 layer=0 box=1,0,1 2x2x1"));

    // ---- buffers
    IDirect3DVertexBuffer9 *vb = nullptr;
    Mark();
    CHECK(device->CreateVertexBuffer(1024, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, &vb, nullptr) ==
          D3D_OK);
    CHECK(Has("Vertex Dynamic Shared 1024"));
    const std::string vbId = LastId("CreateBuffer");
    void *data = nullptr;
    const Vertex quad[4] = {{{-1, -1, 0}, {0, 1}}, {{-1, 1, 0}, {0, 0}}, {{1, 1, 0}, {1, 0}}, {{1, -1, 0}, {1, 1}}};
    CHECK(vb->Lock(0, 0, &data, D3DLOCK_DISCARD) == D3D_OK);
    std::memcpy(data, quad, sizeof(quad));
    CHECK(vb->Unlock() == D3D_OK && Has("UploadBuffer " + vbId + " 0+1024 Discard"));
    CHECK(vb->Lock(256, 64, &data, D3DLOCK_NOOVERWRITE) == D3D_OK && vb->Unlock() == D3D_OK);
    CHECK(Has("UploadBuffer " + vbId + " 256+64 NoOverwrite"));
    CHECK(vb->Lock(1000, 100, &data, 0) == D3DERR_INVALIDCALL);
    IDirect3DIndexBuffer9 *ib = nullptr;
    CHECK(device->CreateIndexBuffer(12, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &ib, nullptr) == D3D_OK);
    const std::string ibId = LastId("CreateBuffer");
    const uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
    CHECK(ib->Lock(0, 0, &data, 0) == D3D_OK);
    std::memcpy(data, indices, sizeof(indices));
    CHECK(ib->Unlock() == D3D_OK && Has("UploadBuffer " + ibId + " 0+12 Ordered fnv=" + Hex(Fnv(indices, 12))));

    // ---- shaders and declarations
    IDirect3DVertexShader9 *vs = nullptr;
    IDirect3DPixelShader9 *ps = nullptr, *missing = nullptr;
    Mark();
    CHECK(device->CreateVertexShader(kVertexShader, &vs) == D3D_OK && Has("vertex " + Hex(g_vertexHash)));
    CHECK(device->CreatePixelShader(kPixelShader, &ps) == D3D_OK && Has("pixel " + Hex(g_pixelHash)));
    CHECK(device->CreatePixelShader(kMissingPixelShader, &missing) == D3D_OK && HostLogged("is not in shaders.pack"));
    IDirect3DPixelShader9 *invalid = nullptr;
    CHECK(device->CreatePixelShader(kVertexShader, &invalid) == D3DERR_INVALIDCALL && !invalid);
    const D3DVERTEXELEMENT9 elements[] = {
        {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        {0, 12, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
        {0, 16, D3DDECLTYPE_FLOAT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_PSIZE, 0}, // no shader input: left out
        D3DDECL_END()};
    IDirect3DVertexDeclaration9 *decl = nullptr;
    CHECK(device->CreateVertexDeclaration(elements, &decl) == D3D_OK);
    const D3DVERTEXELEMENT9 udec3[] = {{0, 0, D3DDECLTYPE_UDEC3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0},
                                       D3DDECL_END()};
    IDirect3DVertexDeclaration9 *refused = nullptr;
    CHECK(device->CreateVertexDeclaration(udec3, &refused) == D3DERR_INVALIDCALL);

    // ---- a first pass: whole-target clears become load operations
    IDirect3DSurface9 *depth = nullptr;
    Mark();
    CHECK(device->CreateDepthStencilSurface(640, 480, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, &depth, nullptr) ==
          D3D_OK);
    CHECK(Has("D24S8 DepthStencil 640x480x1"));
    const std::string depthId = LastId("CreateTexture");
    CHECK(device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 4, 0, 2) == D3DERR_INVALIDCALL); // outside a scene
    CHECK(device->BeginScene() == D3D_OK);
    CHECK(device->SetDepthStencilSurface(depth) == D3D_OK);
    Mark();
    CHECK(device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0xFF336699, 1.0f, 0) ==
          D3D_OK);
    CHECK(!Has("BeginPass") && !Has("Clear"));
    device->SetVertexShader(vs);
    device->SetPixelShader(ps);
    device->SetVertexDeclaration(decl);
    device->SetStreamSource(0, vb, 0, sizeof(Vertex));
    device->SetIndices(ib);
    device->SetTexture(0, dynamic);
    device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_ANISOTROPIC);
    device->SetSamplerState(0, D3DSAMP_MAXANISOTROPY, 8);
    device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    CHECK(device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 4, 0, 2) == D3D_OK);
    CHECK(Has("BeginPass c0=" + backBuffer + "/0/0:clear(0.2,0.4,0.6,1) d=" + depthId + ":clear(1)/clear(0) 640x480"));
    CHECK(Has("GetPipeline P") && Has("attribs=2 colour=1 depth=D24S8"));
    CHECK(Has("mag=0 min=1 mip=0 address=0,0,0 aniso=8 compare=0"));
    // Both blocks: 256 registers and one state vector (bo1_pos_fixup, bo1_alpha_test).
    CHECK(Count("AllocUniforms 4112") == 2);
    CHECK(Has("TriangleList count=6 first=0 base=0 i=" + ibId + " v0=" + vbId + "+0/20 s0=" + dynamicId + "/S"));
    Mark();
    CHECK(device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 4, 0, 2) == D3D_OK);
    CHECK(Count("DrawIndexed") == 1 && !Has("GetPipeline") && !Has("AllocUniforms") && !Has("BeginPass"));
    const float constant[4] = {1, 2, 3, 4};
    device->SetPixelShaderConstantF(5, constant, 1);
    device->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
    device->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
    device->SetRenderState(D3DRS_ALPHAREF, 128);
    Mark();
    CHECK(device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 4, 0, 2) == D3D_OK);
    CHECK(Count("AllocUniforms 4112") == 1 && !Has("GetPipeline"));
    device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    CHECK(device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 4, 0, 2) == D3D_OK);
    // A new viewport size moves bo1_pos_fixup: only the vertex block goes again.
    D3DVIEWPORT9 half = {0, 0, 320, 240, 0.0f, 1.0f};
    device->SetViewport(&half);
    Mark();
    CHECK(device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 4, 0, 2) == D3D_OK);
    CHECK(Count("AllocUniforms 4112") == 1 && Has("viewport=0,0,320x240"));
    Mark();
    CHECK(device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 4, 0, 2) == D3D_OK);
    CHECK(!Has("AllocUniforms"));
    D3DVIEWPORT9 full = {0, 0, 640, 480, 0.0f, 1.0f};
    device->SetViewport(&full);
    // a partial clear inside the open pass
    D3DRECT clearRect = {10, 10, 20, 30};
    Mark();
    CHECK(device->Clear(1, &clearRect, D3DCLEAR_ZBUFFER, 0, 0.5f, 0) == D3D_OK);
    CHECK(Has("Clear 10,10,10x20 depth (0,0,0,0) 0.5 0") && !Has("EndPass"));

    // ---- a render target change ends the pass; the depth buffer may be larger than the target
    IDirect3DTexture9 *rtTexture = nullptr;
    CHECK(device->CreateTexture(256, 256, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &rtTexture,
                                nullptr) == D3D_OK);
    const std::string rtId = LastId("CreateTexture");
    IDirect3DSurface9 *rtSurface = nullptr;
    CHECK(rtTexture->GetSurfaceLevel(0, &rtSurface) == D3D_OK);
    IDirect3DQuery9 *occlusion = nullptr;
    CHECK(device->CreateQuery(D3DQUERYTYPE_OCCLUSION, &occlusion) == D3D_OK);
    CHECK(occlusion->Issue(D3DISSUE_BEGIN) == D3D_OK && Has("BeginQuery"));
    Mark();
    CHECK(device->SetRenderTarget(0, rtSurface) == D3D_OK && !Has("EndPass"));
    device->SetTexture(0, rtTexture); // the target itself: the draw leaves the sampler unbound
    CHECK(device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 4, 0, 2) == D3D_OK);
    CHECK(Find("EndQuery") < Find("EndPass") && Find("EndPass") < Find("BeginPass c0=" + rtId));
    CHECK(Has("BeginPass c0=" + rtId + "/0/0 d=" + depthId + ":load(1)/load(0) 256x256"));
    CHECK(Find("BeginQuery") > Find("BeginPass") && HostLogged("left unbound") && !Has(" s0="));
    CHECK(!Has("GetPipeline P")); // same formats, depth and samples: the pipeline carries over
    device->SetTexture(0, dynamic);
    IDirect3DSurface9 *back = nullptr;
    CHECK(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back) == D3D_OK);
    CHECK(device->SetRenderTarget(0, back) == D3D_OK);
    Mark();
    device->SetTexture(0, rtTexture);
    CHECK(device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 4, 0, 2) == D3D_OK);
    CHECK(Has(" s0=" + rtId + "/S"));
    CHECK(occlusion->Issue(D3DISSUE_END) == D3D_OK);
    DWORD samples = 99;
    CHECK(occlusion->GetData(&samples, sizeof(samples), D3DGETDATA_FLUSH) == D3D_OK && samples == 0);
    // a query opened inside another counts alone until it closes
    IDirect3DQuery9 *inner = nullptr;
    CHECK(device->CreateQuery(D3DQUERYTYPE_OCCLUSION, &inner) == D3D_OK);
    CHECK(occlusion->Issue(D3DISSUE_BEGIN) == D3D_OK && inner->Issue(D3DISSUE_BEGIN) == D3D_OK);
    CHECK(inner->Issue(D3DISSUE_END) == D3D_OK && occlusion->Issue(D3DISSUE_END) == D3D_OK);
    inner->Release();

    // ---- copies
    Mark();
    CHECK(device->StretchRect(back, nullptr, rtSurface, nullptr, D3DTEXF_LINEAR) == D3D_OK);
    CHECK(Find("EndPass") >= 0 &&
          Find("EndPass") < Find("Blit " + backBuffer + "/0/0 0,0,640x480 -> " + rtId + "/0/0 0,0,256x256 linear"));
    IDirect3DSurface9 *readback = nullptr;
    CHECK(device->CreateOffscreenPlainSurface(640, 480, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &readback, nullptr) ==
          D3D_OK);
    CHECK(device->GetRenderTargetData(back, readback) == D3D_OK &&
          Has("ReadTexture " + backBuffer + " level=0 layer=0 box=0,0 640x480"));
    CHECK(device->GetRenderTargetData(back, rtSurface) == D3DERR_INVALIDCALL);
    IDirect3DSurface9 *upload = nullptr, *dynamicLevel = nullptr;
    CHECK(device->CreateOffscreenPlainSurface(4, 4, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &upload, nullptr) == D3D_OK);
    CHECK(dynamic->GetSurfaceLevel(0, &dynamicLevel) == D3D_OK);
    RECT from = {0, 0, 2, 2};
    POINT to = {1, 1};
    Mark();
    CHECK(device->UpdateSurface(upload, &from, dynamicLevel, &to) == D3D_OK);
    CHECK(Has("UploadTexture " + dynamicId + " level=0 layer=0 box=1,1,0 2x2x1"));
    upload->Release();

    // ---- RESZ: the depth buffer into the INTZ texture at sampler 0, and a hardware shadow map
    IDirect3DTexture9 *intz = nullptr, *shadow = nullptr;
    CHECK(device->CreateTexture(640, 480, 1, D3DUSAGE_DEPTHSTENCIL, kIntz, D3DPOOL_DEFAULT, &intz, nullptr) == D3D_OK);
    CHECK(Has("D24S8 DepthStencil sampled 640x480x1 levels=1 samples=1 swizzle=RRRR"));
    const std::string intzId = LastId("CreateTexture");
    CHECK(device->CreateTexture(64, 64, 1, D3DUSAGE_DEPTHSTENCIL, D3DFMT_D24S8, D3DPOOL_DEFAULT, &shadow, nullptr) ==
          D3D_OK);
    device->SetTexture(0, intz);
    const float dummy[3] = {};
    device->SetRenderState(D3DRS_POINTSIZE, 0);
    Mark();
    CHECK(device->DrawPrimitiveUP(D3DPT_POINTLIST, 1, dummy, 12) == D3D_OK);
    CHECK(Has(" 0+268 NoOverwrite") && Has("PointList count=1"));
    device->SetRenderState(D3DRS_POINTSIZE, 0x7FA05000);
    CHECK(Has("Blit " + depthId + "/0/0 0,0,640x480 -> " + intzId + "/0/0 0,0,640x480 nearest"));
    Mark();
    CHECK(device->DrawPrimitive(D3DPT_POINTLIST, 0, 1) == D3D_OK); // stream 0 was unset by the UP draw
    CHECK(!Has("Draw ") && HostLogged("stream without a vertex buffer"));
    device->SetStreamSource(0, vb, 0, sizeof(Vertex));
    device->SetTexture(3, shadow);
    Mark();
    CHECK(device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2) == D3D_OK);
    CHECK(Has("compare=1") && Has("TriangleStrip count=4"));

    // ---- other queries
    IDirect3DQuery9 *event = nullptr, *timestamp = nullptr, *frequency = nullptr;
    CHECK(device->CreateQuery(D3DQUERYTYPE_EVENT, nullptr) == D3D_OK);
    CHECK(device->CreateQuery(D3DQUERYTYPE_VCACHE, nullptr) == D3DERR_NOTAVAILABLE);
    CHECK(device->CreateQuery(D3DQUERYTYPE_EVENT, &event) == D3D_OK);
    CHECK(event->Issue(D3DISSUE_END) == D3D_OK && Has("InsertFence"));
    BOOL done = FALSE;
    CHECK(event->GetData(&done, sizeof(done), D3DGETDATA_FLUSH) == D3D_OK && done);
    CHECK(device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &timestamp) == D3D_OK);
    UINT64 ticks = 0;
    CHECK(timestamp->GetData(&ticks, sizeof(ticks), 0) == S_FALSE);
    CHECK(timestamp->Issue(D3DISSUE_END) == D3D_OK && timestamp->GetData(&ticks, sizeof(ticks), 0) == D3D_OK);
    CHECK(timestamp->GetData(&ticks, 4, 0) == D3DERR_INVALIDCALL);
    CHECK(device->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ, &frequency) == D3D_OK);
    CHECK(frequency->Issue(D3DISSUE_END) == D3D_OK &&
          frequency->GetData(&ticks, sizeof(ticks), 0) == D3D_OK && ticks == 1000000000ull);

    // ---- presentation
    CHECK(device->EndScene() == D3D_OK);
    g_visible = false;
    Mark();
    CHECK(device->PresentEx(nullptr, nullptr, nullptr, nullptr, 0) == S_PRESENT_OCCLUDED);
    CHECK(Has("Present W") && Has(" " + backBuffer) && Find("EndPass") < Find("Present W"));
    g_visible = true;
    IDirect3DSwapChain9 *swapChain = nullptr;
    CHECK(device->GetSwapChain(0, &swapChain) == D3D_OK && swapChain->Present(nullptr, nullptr, nullptr, nullptr, 0) == D3D_OK);
    IDirect3DSurface9 *front = nullptr;
    CHECK(device->CreateOffscreenPlainSurface(1280, 720, D3DFMT_A8R8G8B8, D3DPOOL_SCRATCH, &front, nullptr) == D3D_OK);
    CHECK(swapChain->GetFrontBufferData(front) == D3D_OK);
    swapChain->Release();
    front->Release();

    // ---- screenshots: D3DXSaveSurfaceToFileA on a 2x2 surface whose bytes are B, G, R, A
    IDirect3DSurface9 *shot = nullptr;
    CHECK(device->CreateOffscreenPlainSurface(2, 2, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &shot, nullptr) == D3D_OK);
    D3DLOCKED_RECT shotRect;
    CHECK(shot->LockRect(&shotRect, nullptr, 0) == D3D_OK);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x)
        {
            uint8_t *texel = static_cast<uint8_t *>(shotRect.pBits) + y * shotRect.Pitch + x * 4;
            texel[0] = uint8_t(10 + x); // blue
            texel[1] = uint8_t(20 + y); // green
            texel[2] = 30;              // red
            texel[3] = 40;              // alpha
        }
    shot->UnlockRect();
    CHECK(D3DXSaveSurfaceToFileA("shots/a.tga", D3DXIFF_TGA, shot, nullptr, nullptr) == D3D_OK);
    // An uncompressed 32-bit TGA, rows from the bottom: its first pixel is (0, 1), as B, G, R, A.
    CHECK(g_savedPath == "shots/a.tga" && g_saved.size() >= 18 + 16 && g_saved[2] == 2 && g_saved[12] == 2 &&
          g_saved[14] == 2 && g_saved[16] == 32);
    CHECK(g_saved.size() >= 22 && g_saved[18] == 10 && g_saved[19] == 21 && g_saved[20] == 30 && g_saved[21] == 40);
    const RECT one = {1, 0, 2, 1};
    CHECK(D3DXSaveSurfaceToFileA("b.tga", D3DXIFF_TGA, shot, nullptr, &one) == D3D_OK);
    CHECK(g_saved.size() >= 22 && g_saved[12] == 1 && g_saved[14] == 1 && g_saved[18] == 11 && g_saved[19] == 20);
    CHECK(D3DXSaveSurfaceToFileA("c.jpg", D3DXIFF_JPG, shot, nullptr, nullptr) == D3D_OK);
    CHECK(g_saved.size() > 4 && g_saved[0] == 0xFF && g_saved[1] == 0xD8 && g_saved[g_saved.size() - 2] == 0xFF &&
          g_saved.back() == 0xD9);
    CHECK(D3DXSaveSurfaceToFileA("d.dds", D3DXIFF_DDS, shot, nullptr, nullptr) == E_NOTIMPL);
    shot->Release();

    // ---- reference counts: a released texture lives while bound; a level surface shares its texture's counts
    CHECK(device->BeginScene() == D3D_OK);
    device->SetTexture(5, cube);
    Mark();
    CHECK(cube->Release() == 0 && !Has("DestroyTexture"));
    device->SetTexture(5, nullptr);
    CHECK(Has("DestroyTexture"));
    CHECK(rtSurface->Release() == 1 && rtTexture->Release() == 0);
    CHECK(device->EndScene() == D3D_OK);

    // ---- reset to another size
    pp.BackBufferWidth = 320;
    pp.BackBufferHeight = 240;
    back->Release();
    Mark();
    CHECK(device->Reset(&pp) == D3D_OK);
    CHECK(Has("ResizeSwapchain W") && Has("BGRA8 RenderTarget 320x240x1") && Has("DestroyTexture " + backBuffer));

    // ---- teardown: the device goes with its last reference, the backend with the last IDirect3D9
    for (IUnknown *object : std::initializer_list<IUnknown *>{dynamic, dxt, volume, vb, ib, vs, ps, missing, decl, depth,
                                                               occlusion, readback, dynamicLevel, intz, shadow, event,
                                                               timestamp, frequency})
        object->Release();
    CHECK(device->Release() == 0);
    Mark();
    CHECK(d3d->Release() == 0 && Has("Shutdown"));

    std::remove(g_packPath.c_str());
    for (const std::string &line : g_hostLog)
        std::printf("host: %s\n", line.c_str());
    if (g_failures || printLog)
    {
        std::printf("backend log:\n");
        for (const std::string &line : g_log)
            std::printf("  %s\n", line.c_str());
    }
    if (g_failures)
    {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("d3d9emu_test: all checks passed (%zu backend calls)\n", g_log.size());
    return 0;
}
