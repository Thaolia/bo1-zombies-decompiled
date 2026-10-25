#pragma once

// The software mixer's pure math, for the SDL2 sound backend (and the Switch's later). It mirrors the XAudio2
// driver's per-voice routing (SDXA2_UpdateVoiceSends, snd_driver_xaudio2.cpp:1079) without naming an XAudio2 or SDL
// type, so tests/platform can check it against a hand-computed reference. The engine fills a voice's speaker map
// (snd_speaker_map: input/output channel counts and volumes[16], indexed volumes[out + outputChannels*in], see
// Snd_SpeakerMapGetVolume / Snd_SpeakerMapGetIndex in snd_utils.cpp:652,690); from it and the voice's dry/wet send
// levels this builds the two output matrices the driver hands to SetOutputMatrix, and applies them to sample data.
//
// Matrices are destination-major, as XAudio2's SetOutputMatrix takes them and as the driver writes them:
// matrix[destChannel * sourceChannels + sourceChannel]. Read them back the same way, or channels silently swap.

namespace bo1snd
{
// SDXA2_ChannelFixup (snd_driver_xaudio2.cpp:89): the identity except on 8-channel output, where the four surround
// channels are reordered. Ported verbatim; the dry send runs the destination speaker through it, the wet send does
// not.
unsigned int ChannelFixup(unsigned int outputChannels, unsigned int channel);

// The two output matrices for one voice. dry routes to the master or novoice bus (outputChannels wide); wet routes
// to the 4-channel reverb (radverb) bus. inputChannels is the asset's channel count (1 or 2), outputChannels the
// device's (1, 2, 6 or 8). Both matrices are destination-major and sized inputChannels * (outputChannels | 4).
struct VoiceSends
{
    float dry[2 * 8]; // inputChannels * outputChannels
    float wet[2 * 4]; // inputChannels * 4
    int inputChannels;
    int outputChannels;
    bool toMasterBus; // (aliasFlags & 0x3000) >> 12 == 2: the master bus, else the novoice (unducked) bus
};

// Builds a voice's sends, mirroring SDXA2_UpdateVoiceSends. panVolumes is the voice's speaker map volumes[16]
// (indexed out + outputChannels*in); aliasFlags is snd_alias_t::flags. The futz/LPF DSP parameters that function
// also sets are a separate concern (the per-voice effect), not part of the routing matrices.
void BuildVoiceSends(const float *panVolumes, int inputChannels, int outputChannels, float dryLevel, float wetLevel,
                     unsigned int aliasFlags, VoiceSends *out);

// Accumulates `frames` frames of inputChannels-interleaved float source into outputChannels-interleaved float dst
// through a destination-major gain matrix (matrix[dest * inputChannels + src]). dst is summed into, never cleared,
// so several voices mix by calling this in turn over one cleared bus buffer.
void MixAccumulate(const float *src, int frames, int inputChannels, const float *matrix, int outputChannels,
                   float *dst);
} // namespace bo1snd
