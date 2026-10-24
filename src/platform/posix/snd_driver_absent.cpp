// The sound driver on posix until the SDL2 backend (phase 4, audio): there is no audio device. SD_Xaudio2CanInit
// answers false, so the engine starts as with 'nosnd' (PC_StartWithNoSounds) and never initialises the sound system;
// the other entry points then never run and only complete sound/snd_driver.h.

#include <sound/snd_driver.h>

bool __cdecl SD_Xaudio2CanInit()
{
    return false;
}

void __cdecl SD_TruncateAudioDeviceNames(Font_s *, float, int)
{
}

char __cdecl SD_Init()
{
    return 0;
}

void SD_Shutdown()
{
}

void __cdecl SD_PreUpdate()
{
}

void __cdecl SD_StopVoice(int)
{
}

void __cdecl SD_PauseVoice(int voiceIndex)
{
    g_snd.voice[voiceIndex].paused = 1;
}

void __cdecl SD_UnpauseVoice(int)
{
}

__int64 SD_BO1SamplesPlayed(int)
{
    return -1;
}

void __cdecl SD_UpdateVoice(unsigned int)
{
}

int __cdecl SD_StartAlias(SndStartAliasInfo *, unsigned int)
{
    return SND_PLAYBACKID_NOTPLAYED;
}

int __cdecl SND_StartAliasRam(SndStartAliasInfo *, int)
{
    return SND_PLAYBACKID_NOTPLAYED;
}
