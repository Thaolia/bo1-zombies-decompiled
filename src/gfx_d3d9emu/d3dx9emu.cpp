// The D3D9 runtime's free functions besides Direct3DCreate9 (d3d9emu_direct3d.cpp): the D3DPERF markers, and the
// part of D3DX the engine calls: D3DXSaveSurfaceToFileA writes its screenshots (stb_image_write, third_party/stb);
// the load_obj material path reports E_NOTIMPL.

#include "d3d9emu_host.h"

#include <d3d9.h>
#include <d3dx9.h>

#include <cstdint>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#define STBI_WRITE_NO_STDIO
#include <stb_image_write.h>

namespace
{
thread_local int t_perfEventLevel = 0;

void Append(void *context, void *data, int size)
{
    auto *file = static_cast<std::vector<uint8_t> *>(context);
    file->insert(file->end(), static_cast<const uint8_t *>(data), static_cast<const uint8_t *>(data) + size);
}
} // namespace

extern "C" {

// The D3D9 runtime keeps these markers' nesting without any device; Begin returns the level the event starts in.
int WINAPI D3DPERF_BeginEvent(D3DCOLOR, LPCWSTR)
{
    return t_perfEventLevel++;
}

int WINAPI D3DPERF_EndEvent(void)
{
    return t_perfEventLevel > 0 ? --t_perfEventLevel : -1;
}

HRESULT WINAPI D3DXCreateBuffer(DWORD, LPD3DXBUFFER *ppBuffer)
{
    if (ppBuffer)
        *ppBuffer = nullptr;
    return E_NOTIMPL;
}

HRESULT WINAPI D3DXCompileShader(LPCSTR, UINT, const D3DXMACRO *, LPD3DXINCLUDE, LPCSTR, LPCSTR, DWORD,
                                 LPD3DXBUFFER *ppShader, LPD3DXBUFFER *ppErrorMsgs,
                                 LPD3DXCONSTANTTABLE *ppConstantTable)
{
    if (ppShader)
        *ppShader = nullptr;
    if (ppErrorMsgs)
        *ppErrorMsgs = nullptr;
    if (ppConstantTable)
        *ppConstantTable = nullptr;
    return E_NOTIMPL;
}

HRESULT WINAPI D3DXGetShaderConstantTable(const DWORD *, LPD3DXCONSTANTTABLE *ppConstantTable)
{
    if (ppConstantTable)
        *ppConstantTable = nullptr;
    return E_NOTIMPL;
}

HRESULT WINAPI D3DXGetShaderInputSemantics(const DWORD *, D3DXSEMANTIC *, UINT *pCount)
{
    if (pCount)
        *pCount = 0;
    return E_NOTIMPL;
}

HRESULT WINAPI D3DXGetShaderOutputSemantics(const DWORD *, D3DXSEMANTIC *, UINT *pCount)
{
    if (pCount)
        *pCount = 0;
    return E_NOTIMPL;
}

// The engine saves 32-bit surfaces (its back buffer copies) as JPG or TGA; BMP and PNG come with stb_image_write. JPG
// and BMP drop the alpha, as D3DX writes them, and so does every format for an X8R8G8B8 surface.
HRESULT WINAPI D3DXSaveSurfaceToFileA(LPCSTR pDestFile, D3DXIMAGE_FILEFORMAT DestFormat,
                                      LPDIRECT3DSURFACE9 pSrcSurface, const PALETTEENTRY *, const RECT *pSrcRect)
{
    if (!pDestFile || !pSrcSurface)
        return D3DERR_INVALIDCALL;
    if (DestFormat != D3DXIFF_JPG && DestFormat != D3DXIFF_TGA && DestFormat != D3DXIFF_BMP &&
        DestFormat != D3DXIFF_PNG)
        return E_NOTIMPL;
    D3DSURFACE_DESC desc;
    if (FAILED(pSrcSurface->GetDesc(&desc)))
        return D3DERR_INVALIDCALL;
    if (desc.Format != D3DFMT_A8R8G8B8 && desc.Format != D3DFMT_X8R8G8B8)
        return E_NOTIMPL;
    const RECT all = {0, 0, LONG(desc.Width), LONG(desc.Height)};
    const RECT r = pSrcRect ? *pSrcRect : all;
    if (r.left < 0 || r.top < 0 || r.right > all.right || r.bottom > all.bottom || r.left >= r.right ||
        r.top >= r.bottom)
        return D3DERR_INVALIDCALL;
    D3DLOCKED_RECT locked;
    const HRESULT hr = pSrcSurface->LockRect(&locked, &r, D3DLOCK_READONLY);
    if (FAILED(hr))
        return hr;
    const int width = int(r.right - r.left), height = int(r.bottom - r.top);
    const bool alpha = desc.Format == D3DFMT_A8R8G8B8 && (DestFormat == D3DXIFF_TGA || DestFormat == D3DXIFF_PNG);
    const int components = alpha ? 4 : 3;
    std::vector<uint8_t> pixels(size_t(width) * size_t(height) * size_t(components));
    for (int y = 0; y < height; ++y)
    {
        const uint8_t *in = static_cast<const uint8_t *>(locked.pBits) + size_t(y) * size_t(locked.Pitch);
        uint8_t *out = pixels.data() + size_t(y) * size_t(width) * size_t(components);
        for (int x = 0; x < width; ++x, in += 4, out += components)
        {
            out[0] = in[2];
            out[1] = in[1];
            out[2] = in[0];
            if (alpha)
                out[3] = in[3];
        }
    }
    pSrcSurface->UnlockRect();
    // D3DX writes uncompressed TGA files.
    stbi_write_tga_with_rle = 0;
    std::vector<uint8_t> file;
    int written = 0;
    switch (DestFormat)
    {
    case D3DXIFF_JPG:
        written = stbi_write_jpg_to_func(Append, &file, width, height, components, pixels.data(), 90);
        break;
    case D3DXIFF_TGA:
        written = stbi_write_tga_to_func(Append, &file, width, height, components, pixels.data());
        break;
    case D3DXIFF_BMP:
        written = stbi_write_bmp_to_func(Append, &file, width, height, components, pixels.data());
        break;
    default:
        written = stbi_write_png_to_func(Append, &file, width, height, components, pixels.data(), width * components);
        break;
    }
    if (!written || !d3d9emu::host::SaveFile(pDestFile, file.data(), file.size()))
        return E_FAIL;
    return D3D_OK;
}

} // extern "C"
