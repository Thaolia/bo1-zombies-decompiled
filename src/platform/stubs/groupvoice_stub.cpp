// The groupvoice (DirectSound + Speex) entry points win_voice.cpp uses, behaving as on a machine with no sound
// device: Sound_Init fails, so Voice_Init leaves g_voice_initialized at 0 and every Voice_* call returns early.

#include <groupvoice/decode.h>
#include <groupvoice/encode.h>
#include <groupvoice/play.h>
#include <groupvoice/record.h>

int g_current_bandwidth_setting;
int g_decode_frame_size;
float voice_current_voicelevel;

int __cdecl Record_Init()
{
    return 0;
}

void __cdecl Record_Shutdown()
{
}

void __cdecl Record_Frame()
{
}

dsound_sample_t *__cdecl Record_NewSample()
{
    return nullptr;
}

int __cdecl Record_DestroySample(dsound_sample_t *)
{
    return 0;
}

HRESULT __cdecl Record_Start(dsound_sample_t *)
{
    return E_FAIL;
}

HRESULT __cdecl Record_Stop(dsound_sample_t *)
{
    return E_FAIL;
}

bool __cdecl Encode_Init(int)
{
    return false;
}

char __cdecl Encode_Shutdown()
{
    return 0;
}

char __cdecl Decode_Init(int)
{
    return 0;
}

void __cdecl Decode_Shutdown()
{
}

int __cdecl Decode_Sample(char *, int, __int16 *, int)
{
    return 0;
}

int __cdecl Sound_Init(void *)
{
    return 0;
}

void __cdecl Sound_Shutdown()
{
}

dsound_sample_t *__cdecl Sound_NewSample()
{
    return nullptr;
}

int __cdecl Sound_DestroySample(dsound_sample_t *)
{
    return 0;
}

char __cdecl Sound_StopSample(dsound_sample_t *)
{
    return 0;
}

void __cdecl Sound_SampleFrame(dsound_sample_t *)
{
}

unsigned int __cdecl Sound_UpdateSample(dsound_sample_t *, char *, unsigned int)
{
    return 0;
}
