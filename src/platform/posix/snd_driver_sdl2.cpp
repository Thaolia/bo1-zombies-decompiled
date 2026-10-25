// The sound driver on posix: a software mixer on an SDL2 audio device, in place of the XAudio2 driver
// (snd_driver_xaudio2.cpp). It implements the engine-facing contract of sound/snd_driver.h and keeps the engine's
// voice model: the engine owns the voice table (g_snd.voice[], free-ness in g_snd.voiceAliasHash), picks aliases,
// computes each voice's speaker map and send levels, and drives the driver through SD_StartAlias / SD_UpdateVoice /
// SD_StopVoice. This driver turns that per-voice state into samples (snd_mix.h, ported from SDXA2_UpdateVoiceSends)
// and mixes the RAM voices to the device.
//
// Threading. SDL owns the audio thread and calls AudioCallback when it needs samples; a game-loop refill would
// starve on long frames (the libnx note in .claude/rules/switch-homebrew.md), so the pull model is deliberate. The
// game thread mutates the voice table in the SD_* entry points; the callback reads it. One mutex guards the table,
// held only briefly and never across an allocation or I/O: the callback allocates nothing (its scratch is
// pre-allocated in SD_Init) and reads only RAM, and every decode/alloc happens on the game thread at voice start.
//
// Scope (docs/PORTING.md, phase 4b). RAM voices (indices 10..73) only. Streamed voices (0..9: music, long ambience)
// are deferred: SND_StartAliasStream reports not-played, as with no device. The reverb (wet) send and the master
// EQ/limiter DSP are deferred too, so the mix is dry; SD_PreUpdate therefore does nothing. The decoders (snd_adpcm)
// and the send matrices (snd_mix) are unit-tested offline; audible correctness, 3D and the non-unity resampler are
// verified on hardware, not here.

#include "snd_adpcm.h"
#include "snd_mix.h"

#include <sound/snd_driver.h>

#include <qcommon/common.h>

#include <SDL.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <vector>

namespace
{
constexpr int kDeviceRate = 48000;
constexpr int kDeviceChannels = 2; // the device is stereo; snd_speakerConfiguration defaults to "Stereo" (index 0),
                                   // so the engine fills each voice's pan with output_channel_count == 2 to match.

struct MixerVoice
{
    bool active = false;
    bool started = false;  // the engine starts a voice on its first SD_UpdateVoice, as the XAudio2 driver does
    bool paused = false;
    bool looping = false;
    bool drained = false;  // set by the callback when a non-looping voice runs out; SD_UpdateVoice then stops it
    int inputChannels = 1;
    int outputChannels = kDeviceChannels;
    int frameCount = 0;          // decoded source frames held in `samples`
    int assetRate = kDeviceRate; // the asset's native frame rate, for the resample step
    double readPos = 0.0;        // source read cursor in frames (audio thread)
    double playedSrc = 0.0;      // cumulative source frames consumed, monotonic across loops (SamplesPlayed)
    double step = 1.0;           // source frames per output frame = pitch * assetRate / deviceRate
    float dry[2 * 8] = {};       // the dry send matrix, destination-major (dest * inputChannels + src)
    std::vector<float> samples;  // interleaved float PCM, frameCount * inputChannels; owned by the game thread
};

std::mutex s_lock;
MixerVoice s_voices[SND_MAX_VOICES];
SDL_AudioDeviceID s_device = 0;
int s_callbackFrames = 0;
std::vector<float> s_scratch; // resampled source for one voice, reused each callback; sized in SD_Init
bool s_subsystemReady = false;

// Decodes a RAM asset (PCM16 or MS-ADPCM) into interleaved float in [-1, 1]. Called on the game thread at voice
// start, into a local buffer, before the voice is made active, so the callback never races the decode.
void DecodeAsset(const snd_asset *snd, int frames, int channels, std::vector<float> &outF)
{
    outF.assign(size_t(frames) * channels, 0.0f);
    if (snd->format == SND_ASSET_FORMAT_MSADPCM)
    {
        // The block align is 262 bytes a channel, as iSND_CreateVoice hard-codes it (snd_driver_xaudio2.cpp:410);
        // the asset's block_size field is not used by the engine, so it is not trusted here.
        const int blockAlign = 262 * channels;
        std::vector<int16_t> pcm(size_t(frames) * channels);
        const int got = Bo1_DecodeMsAdpcm(reinterpret_cast<const uint8_t *>(snd->data), snd->data_size, channels,
                                           blockAlign, pcm.data(), frames);
        for (int i = 0; i < got * channels; ++i)
            outF[i] = pcm[i] * (1.0f / 32768.0f);
    }
    else // PCM16 (format 0): the data is interleaved signed 16-bit already
    {
        const int16_t *pcm = reinterpret_cast<const int16_t *>(snd->data);
        const int avail = int(snd->data_size / sizeof(int16_t));
        const int n = frames * channels < avail ? frames * channels : avail;
        for (int i = 0; i < n; ++i)
            outF[i] = pcm[i] * (1.0f / 32768.0f);
    }
}

void SDLCALL AudioCallback(void *, Uint8 *stream, int len)
{
    std::memset(stream, 0, size_t(len));
    float *out = reinterpret_cast<float *>(stream);
    const int frames = len / (kDeviceChannels * int(sizeof(float)));

    std::lock_guard<std::mutex> lock(s_lock);
    float *src = s_scratch.data();
    for (int vi = 0; vi < SND_MAX_VOICES; ++vi)
    {
        MixerVoice &v = s_voices[vi];
        if (!v.active || v.paused || !v.started || v.drained || v.frameCount == 0)
            continue;

        // Resample the voice into the scratch at the device rate, linearly. At most `frames`, fewer if a
        // non-looping voice ends inside this block.
        int produced = 0;
        for (int f = 0; f < frames; ++f)
        {
            if (v.readPos >= v.frameCount)
            {
                if (!v.looping)
                {
                    v.drained = true;
                    break;
                }
                v.readPos -= v.frameCount;
            }
            const int i0 = int(v.readPos);
            const double frac = v.readPos - i0;
            int i1 = i0 + 1;
            if (i1 >= v.frameCount)
                i1 = v.looping ? 0 : i0;
            for (int c = 0; c < v.inputChannels; ++c)
            {
                const float a = v.samples[size_t(i0) * v.inputChannels + c];
                const float b = v.samples[size_t(i1) * v.inputChannels + c];
                src[f * v.inputChannels + c] = float(a + (b - a) * frac);
            }
            v.readPos += v.step;
            v.playedSrc += v.step;
            ++produced;
        }
        bo1snd::MixAccumulate(src, produced, v.inputChannels, v.dry, kDeviceChannels, out);
    }

    // The buses sum without headroom; clamp before the device, as a hard limiter would. The master limiter DSP is
    // deferred, so this only guards against clipping noise, not for tone.
    const int total = frames * kDeviceChannels;
    for (int i = 0; i < total; ++i)
        out[i] = out[i] < -1.0f ? -1.0f : out[i] > 1.0f ? 1.0f : out[i];
}

bool EnsureSubsystem()
{
    if (s_subsystemReady)
        return true;
    // Whether sound runs is decided by whether a device actually opens (SD_Xaudio2CanInit), not by headlessness: a
    // headless run with no audio hardware fails the open and falls back to nosnd on its own.
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
        return false;
    s_subsystemReady = true;
    return true;
}
} // namespace

bool __cdecl SD_Xaudio2CanInit()
{
    // Reports whether sound can start at all; false makes the engine run as with 'nosnd' (device missing), a state it
    // already handles. Enumerating a device is not enough (WSLg lists an ALSA device it cannot open), so this opens
    // and closes one for real: only a device that actually opens turns sound on.
    if (!EnsureSubsystem())
        return false;
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = kDeviceRate;
    want.format = AUDIO_F32SYS;
    want.channels = kDeviceChannels;
    want.samples = 1024;
    SDL_AudioDeviceID probe = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!probe)
        return false;
    SDL_CloseAudioDevice(probe);
    return true;
}

void __cdecl SD_TruncateAudioDeviceNames(Font_s *, float, int)
{
    // The device-name list is a Windows settings-menu affordance; the SDL backend exposes no such list.
}

char __cdecl SD_Init()
{
    if (!EnsureSubsystem())
        return 0;

    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = kDeviceRate;
    want.format = AUDIO_F32SYS;
    want.channels = kDeviceChannels;
    want.samples = 1024; // frames a callback; a buffer the engine's frame comfortably refills
    want.callback = AudioCallback;

    s_device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!s_device)
    {
        Com_Printf(16, "snd: SDL2 device would not open (%s); running without sound\n", SDL_GetError());
        return 0;
    }

    s_callbackFrames = have.samples;
    s_scratch.assign(size_t(s_callbackFrames) * 2, 0.0f); // *2: stereo asset worst case, pre-allocated once
    SDL_PauseAudioDevice(s_device, 0);
    Com_Printf(16, "snd: SDL2 software mixer on %s, %d Hz, %d ch, %d-frame buffer (RAM voices; streaming/reverb deferred)\n",
               SDL_GetCurrentAudioDriver(), have.freq, have.channels, have.samples);
    return 1;
}

void SD_Shutdown()
{
    if (s_device)
    {
        SDL_CloseAudioDevice(s_device);
        s_device = 0;
    }
    std::lock_guard<std::mutex> lock(s_lock);
    for (auto &v : s_voices)
    {
        v = MixerVoice();
        std::vector<float>().swap(v.samples);
    }
}

void __cdecl SD_PreUpdate()
{
    // Sets the reverb/master/novoice bus DSP parameters in the XAudio2 driver; those effects are deferred, so there
    // is nothing to set yet.
}

void __cdecl SD_StopVoice(int voiceIndex)
{
    std::lock_guard<std::mutex> lock(s_lock);
    MixerVoice &v = s_voices[voiceIndex];
    v.active = false;
    v.started = false;
    v.drained = false;
    std::vector<float>().swap(v.samples);
    v.frameCount = 0;
}

void __cdecl SD_PauseVoice(int voiceIndex)
{
    g_snd.voice[voiceIndex].paused = 1;
    std::lock_guard<std::mutex> lock(s_lock);
    s_voices[voiceIndex].paused = true;
}

void __cdecl SD_UnpauseVoice(int voiceIndex)
{
    std::lock_guard<std::mutex> lock(s_lock);
    s_voices[voiceIndex].paused = false;
}

__int64 SD_BO1SamplesPlayed(int voiceIndex)
{
    if (g_snd.voice[voiceIndex].soundFileInfo.loadingState == SFLS_LOADING)
        return -1;
    std::lock_guard<std::mutex> lock(s_lock);
    const MixerVoice &v = s_voices[voiceIndex];
    if (!v.active)
        return -1;
    return (__int64)(v.playedSrc);
}

void __cdecl SD_UpdateVoice(unsigned int voiceIndex)
{
    if (SND_IsStream(voiceIndex)) // streamed voices are deferred; nothing was started for them
        return;
    snd_voice_t *voice = &g_snd.voice[voiceIndex];
    if (voice->soundFileInfo.loadingState == SFLS_LOADING)
        return;

    std::lock_guard<std::mutex> lock(s_lock);
    MixerVoice &v = s_voices[voiceIndex];
    if (!v.active)
        return;
    if (v.drained) // ran out since the last update: let the engine tear it down, as the XAudio2 path does
    {
        SND_StopVoice(int(voiceIndex));
        return;
    }

    // Rebuild the dry send matrix from the engine's current speaker map and dry level, and refresh the pitch. The
    // decoded channel count (v.inputChannels, fixed at start) is the stride of the sample buffer and of the matrix,
    // so it is what the matrix is built with, not voice->pan.input_channel_count: were they ever to disagree, reading
    // the buffer with the pan's count would run off its end. The wet (reverb) send is built too but ignored until the
    // reverb bus exists.
    // The device is stereo, so the dry matrix must be exactly kDeviceChannels wide; the speaker map's volumes[] is
    // laid out for output_channel_count outputs, so that count has to be kDeviceChannels too (it is: the engine
    // defaults snd_speakerConfiguration to "Stereo"). A different count would need an N->2 down-mix the mixer does
    // not do; skip the voice rather than read the matrix or the buffer out of range.
    if (voice->pan.output_channel_count != kDeviceChannels || v.inputChannels < 1 || v.inputChannels > 2)
        return;
    bo1snd::VoiceSends sends;
    bo1snd::BuildVoiceSends(voice->pan.volumes, v.inputChannels, kDeviceChannels, voice->dryLevel, voice->wetLevel,
                            voice->alias->flags, &sends);
    v.outputChannels = kDeviceChannels;
    std::memcpy(v.dry, sends.dry, sizeof(v.dry));
    v.step = SND_GetPitch(voice) * (double(v.assetRate) / kDeviceRate);

    if (!v.started && !voice->paused && !voice->startDelay)
        v.started = true;
}

int __cdecl SND_StartAliasStream(SndStartAliasInfo *, unsigned int)
{
    // Streamed playback (seek tables, double-buffered windows, Snd_Stream*) is deferred; reporting not-played keeps
    // streamed music and long ambience silent while the RAM voices play, rather than faking a stream.
    return SND_PLAYBACKID_NOTPLAYED;
}

int __cdecl SD_StartAlias(SndStartAliasInfo *startAliasInfo, unsigned int voice)
{
    if (SND_IsStream(voice))
        return SND_StartAliasStream(startAliasInfo, voice);
    return SND_StartAliasRam(startAliasInfo, int(voice));
}

int __cdecl SND_StartAliasRam(SndStartAliasInfo *startAliasInfo, int voiceIndex)
{
    const bool isLooping = (startAliasInfo->alias->flags & 1) != 0;
    const snd_asset *snd = &startAliasInfo->alias->soundFile->u.loadSnd->sound;

    if (snd->format != SND_ASSET_FORMAT_MSADPCM && snd->format != 0 /*PCM16*/)
        return -1; // WMA has no RAM asset in the zombies zones; nothing to decode

    const int channels = int(snd->channel_count);
    const int frames = int(snd->frame_count);

    // Decode into a local buffer before touching the shared voice, so the callback never sees a half-filled vector.
    std::vector<float> decoded;
    DecodeAsset(snd, frames, channels, decoded);

    {
        std::lock_guard<std::mutex> lock(s_lock);
        MixerVoice &v = s_voices[voiceIndex];
        v.samples.swap(decoded);
        v.frameCount = frames;
        v.inputChannels = channels;
        v.outputChannels = kDeviceChannels;
        v.assetRate = int(snd->frame_rate);
        v.looping = isLooping;
        v.readPos = 0.0;
        v.playedSrc = 0.0;
        v.step = double(snd->frame_rate) / kDeviceRate;
        v.started = false;
        v.paused = false;
        v.drained = false;
        for (float &g : v.dry)
            g = 0.0f;
        v.active = true;
    }

    const unsigned int rate = snd->frame_rate;
    const unsigned int total_msec =
        isLooping ? 0u : unsigned(startAliasInfo->startDelay + (__int64)(double(snd->frame_count) * 1000.0 / rate));
    SND_SetSoundFileVoiceInfo(voiceIndex, channels, int(rate), total_msec, 0, SFLS_LOADED);
    SND_SetVoiceStartInfo(voiceIndex, startAliasInfo);
    return startAliasInfo->playbackId;
}
