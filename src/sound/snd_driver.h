#pragma once

// The sound driver interface the rest of the engine calls. It names no XAudio2 type, so it can be included on every
// platform; the XAudio2 implementation and its internals live in snd_driver_xaudio2.h/.cpp.

#include "snd.h"

struct Font_s;

#define SND_MAX_STREAM_VOICES 10
#define SND_MAX_VOICES 74
#define SND_PLAYBACKID_NOTPLAYED -1


enum snd_stream_status : __int32
{                                       // XREF: ?Snd_StreamStatus@@YA?AW4snd_stream_status@@I@Z/r
                                        // ?Snd_StreamAcquireWindow@@YA?AW4snd_stream_status@@IPAI0PAPAD@Z/r ...
    SND_STREAM_UNUSED   = 0x0,
    SND_STREAM_LOADING  = 0x1,
    SND_STREAM_OK       = 0x2,
    SND_STREAM_STARVING = 0x3,
    SND_STREAM_EOF      = 0x4,
    SND_STREAM_ERROR    = 0x5,
};

bool __cdecl SD_Xaudio2CanInit();
void __cdecl SD_TruncateAudioDeviceNames(Font_s *font, float scale, int size);
void __cdecl SD_StopVoice(int voiceIndex);
char __cdecl SD_Init();
void SD_Shutdown();
void __cdecl SD_PreUpdate();
void __cdecl SD_PauseVoice(int voiceIndex);
void __cdecl SD_UnpauseVoice(int voiceIndex);
// zombies (L4): bo1_audio pause trace only - the source voice's SamplesPlayed, or -1 while its file is loading.
__int64 SD_BO1SamplesPlayed(int voiceIndex);
void __cdecl SD_UpdateVoice(unsigned int voiceIndex);
int __cdecl SD_StartAlias(SndStartAliasInfo *startAliasInfo, unsigned int voice);
int __cdecl SND_StartAliasRam(SndStartAliasInfo *startAliasInfo, int voiceIndex);
