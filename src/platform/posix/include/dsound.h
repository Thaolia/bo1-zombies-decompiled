#pragma once

// The part of DirectSound the engine uses: r_cinematic.cpp opens a device for Bink's audio. On posix and switch no
// DirectSound device exists; DirectSoundCreate8 fails as it does on a PC without a sound device, a case the engine
// already handles ("Unable to initialize sound").

#include <windows.h>

typedef const GUID *LPCGUID;

DECLARE_INTERFACE_(IDirectSound8, IUnknown)
{
    STDMETHOD(SetCooperativeLevel)(THIS_ HWND hwnd, DWORD dwLevel) PURE;
};
typedef IDirectSound8 *LPDIRECTSOUND8;

// Voice chat's sample buffers (groupvoice/directsound.h) only hold pointers to these.
interface IDirectSoundBuffer;
interface IDirectSoundCaptureBuffer;

#define DSERR_NODRIVER MAKE_HRESULT(1, 0x878, 120)

extern "C" HRESULT WINAPI DirectSoundCreate8(LPCGUID pcGuidDevice, LPDIRECTSOUND8 *ppDS8, LPUNKNOWN pUnkOuter);
