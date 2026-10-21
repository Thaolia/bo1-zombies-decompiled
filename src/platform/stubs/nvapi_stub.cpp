// The NVAPI static library as it behaves without an NVIDIA driver: NvAPI_Initialize cannot load the driver library
// and every other call reports NVAPI_API_NOT_INITIALIZED. dx.nvInitialized and dx.nvStereoHandle stay 0, the state
// R_StoreDirect3DCaps and R_CreateDevice already handle for AMD and Intel adapters.

#include <d3d9.h>
#include <nvapi/nvapi.h>

NvAPI_Status __cdecl NvAPI_Initialize()
{
    return NVAPI_LIBRARY_NOT_FOUND;
}

NvAPI_Status __cdecl NvAPI_Stereo_CreateHandleFromIUnknown(IUnknown *, StereoHandle *)
{
    return NVAPI_API_NOT_INITIALIZED;
}

NvAPI_Status __cdecl NvAPI_Stereo_DestroyHandle(StereoHandle)
{
    return NVAPI_API_NOT_INITIALIZED;
}

NvAPI_Status __cdecl NvAPI_Stereo_IsActivated(StereoHandle, NvU8 *)
{
    return NVAPI_API_NOT_INITIALIZED;
}

NvAPI_Status __cdecl NvAPI_Stereo_SetConvergence(StereoHandle, float)
{
    return NVAPI_API_NOT_INITIALIZED;
}
