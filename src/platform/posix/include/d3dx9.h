#pragma once

// The part of D3DX the engine uses (screenshots, and the load_obj material path), declared as in the June 2010 SDK.
// The SDK's own d3dx9 headers pull in GDI fonts, IStream and d3dx9math, none of which the engine uses. gfx_d3d9emu
// implements these functions.

#include <d3d9.h>

typedef struct _D3DXSEMANTIC
{
    UINT Usage;
    UINT UsageIndex;
} D3DXSEMANTIC, *LPD3DXSEMANTIC;

typedef struct _D3DXSHADER_CONSTANTTABLE
{
    DWORD Size;
    DWORD Creator;
    DWORD Version;
    DWORD Constants;
    DWORD ConstantInfo;
    DWORD Flags;
    DWORD Target;
} D3DXSHADER_CONSTANTTABLE, *LPD3DXSHADER_CONSTANTTABLE;

typedef struct _D3DXSHADER_CONSTANTINFO
{
    DWORD Name;
    WORD RegisterSet;
    WORD RegisterIndex;
    WORD RegisterCount;
    WORD Reserved;
    DWORD TypeInfo;
    DWORD DefaultValue;
} D3DXSHADER_CONSTANTINFO, *LPD3DXSHADER_CONSTANTINFO;

typedef struct _D3DXMACRO
{
    LPCSTR Name;
    LPCSTR Definition;
} D3DXMACRO;

typedef enum _D3DXIMAGE_FILEFORMAT
{
    D3DXIFF_BMP = 0,
    D3DXIFF_JPG = 1,
    D3DXIFF_TGA = 2,
    D3DXIFF_PNG = 3,
    D3DXIFF_DDS = 4,
    D3DXIFF_PPM = 5,
    D3DXIFF_DIB = 6,
    D3DXIFF_HDR = 7,
    D3DXIFF_PFM = 8,
    D3DXIFF_FORCE_DWORD = 0x7fffffff
} D3DXIMAGE_FILEFORMAT;

DECLARE_INTERFACE_(ID3DXBuffer, IUnknown)
{
    STDMETHOD_(LPVOID, GetBufferPointer)(THIS) PURE;
    STDMETHOD_(DWORD, GetBufferSize)(THIS) PURE;
};
typedef ID3DXBuffer *LPD3DXBUFFER;

// The engine only reads the constant table's raw CTAB bytes.
DECLARE_INTERFACE_(ID3DXConstantTable, IUnknown)
{
    STDMETHOD_(LPVOID, GetBufferPointer)(THIS) PURE;
    STDMETHOD_(DWORD, GetBufferSize)(THIS) PURE;
};
typedef ID3DXConstantTable *LPD3DXCONSTANTTABLE;

interface ID3DXInclude;
typedef ID3DXInclude *LPD3DXINCLUDE;

extern "C" {
HRESULT WINAPI D3DXCreateBuffer(DWORD NumBytes, LPD3DXBUFFER *ppBuffer);
HRESULT WINAPI D3DXCompileShader(LPCSTR pSrcData, UINT SrcDataLen, const D3DXMACRO *pDefines, LPD3DXINCLUDE pInclude,
                                 LPCSTR pFunctionName, LPCSTR pProfile, DWORD Flags, LPD3DXBUFFER *ppShader,
                                 LPD3DXBUFFER *ppErrorMsgs, LPD3DXCONSTANTTABLE *ppConstantTable);
HRESULT WINAPI D3DXGetShaderConstantTable(const DWORD *pFunction, LPD3DXCONSTANTTABLE *ppConstantTable);
HRESULT WINAPI D3DXGetShaderInputSemantics(const DWORD *pFunction, D3DXSEMANTIC *pSemantics, UINT *pCount);
HRESULT WINAPI D3DXGetShaderOutputSemantics(const DWORD *pFunction, D3DXSEMANTIC *pSemantics, UINT *pCount);
HRESULT WINAPI D3DXSaveSurfaceToFileA(LPCSTR pDestFile, D3DXIMAGE_FILEFORMAT DestFormat,
                                      LPDIRECT3DSURFACE9 pSrcSurface, const PALETTEENTRY *pSrcPalette,
                                      const RECT *pSrcRect);
}
