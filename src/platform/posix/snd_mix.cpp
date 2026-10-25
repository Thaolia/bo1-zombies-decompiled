// The software mixer's pure math (snd_mix.h), ported from the XAudio2 driver's SDXA2_UpdateVoiceSends
// (snd_driver_xaudio2.cpp:1079) and SDXA2_ChannelFixup (89). The per-voice send levels, the speaker map and the
// bus layout are the engine's; this turns them into the two output matrices and applies them. Kept free of XAudio2
// and SDL so it is unit-testable against a hand-computed reference.

#include "snd_mix.h"

namespace
{
// Snd_SpeakerMapGetVolume / Snd_SpeakerMapGetIndex (snd_utils.cpp:652,690): the map stores one row an input channel,
// volumes[out + outputChannels * in]. The engine asserts 0 <= volume <= 1; here the value is taken as given.
double SpeakerVol(const float *volumes, int outputChannels, int in, int out)
{
    return volumes[out + outputChannels * in];
}

const double kSqrtHalf = 0.70710677; // 1/sqrt(2), the driver's exact literal for the surround down-mix
} // namespace

namespace bo1snd
{
unsigned int ChannelFixup(unsigned int outputChannels, unsigned int channel)
{
    if (outputChannels != 8)
        return channel;
    switch (channel)
    {
    case 4u:
        return 6;
    case 5u:
        return 7;
    case 6u:
        return 4;
    case 7u:
        return 5;
    default:
        return channel;
    }
}

void BuildVoiceSends(const float *panVolumes, int inputChannels, int outputChannels, float dryLevel, float wetLevel,
                     unsigned int aliasFlags, VoiceSends *out)
{
    out->inputChannels = inputChannels;
    out->outputChannels = outputChannels;
    out->toMasterBus = ((aliasFlags & 0x3000) >> 12) == 2;

    // The reverb (radverb) bus: 4 destination channels, destination-major, down-mixed from the voice's speaker map
    // the same way the driver does for each possible device layout. Zeroed first, as the driver zeroes its 64-float
    // scratch before the switch.
    for (int i = 0; i < 2 * 4; ++i)
        out->wet[i] = 0.0f;
    const int in = inputChannels;
    switch (outputChannels)
    {
    case 1:
        for (int k = 0; k < in; ++k)
        {
            const double v = SpeakerVol(panVolumes, outputChannels, k, 0) * 0.5 * wetLevel;
            out->wet[k] = float(v);
            out->wet[in + k] = float(v);
            out->wet[2 * in + k] = float(v);
            out->wet[3 * in + k] = float(v);
        }
        break;
    case 2:
        for (int m = 0; m < in; ++m)
        {
            out->wet[m] = float(SpeakerVol(panVolumes, outputChannels, m, 0) * kSqrtHalf * wetLevel);
            out->wet[in + m] = float(SpeakerVol(panVolumes, outputChannels, m, 1) * kSqrtHalf * wetLevel);
            out->wet[2 * in + m] = float(SpeakerVol(panVolumes, outputChannels, m, 1) * kSqrtHalf * wetLevel);
            out->wet[3 * in + m] = float(SpeakerVol(panVolumes, outputChannels, m, 0) * kSqrtHalf * wetLevel);
        }
        break;
    case 6:
        for (int n = 0; n < in; ++n)
        {
            out->wet[n] = float(SpeakerVol(panVolumes, outputChannels, n, 4) * wetLevel);
            out->wet[in + n] = float(SpeakerVol(panVolumes, outputChannels, n, 1) * wetLevel);
            out->wet[2 * in + n] = float(SpeakerVol(panVolumes, outputChannels, n, 5) * wetLevel);
            out->wet[3 * in + n] = float(SpeakerVol(panVolumes, outputChannels, n, 0) * wetLevel);
            out->wet[3 * in + n] =
                float(SpeakerVol(panVolumes, outputChannels, n, 2) * kSqrtHalf * wetLevel + out->wet[3 * in + n]);
            out->wet[in + n] =
                float(SpeakerVol(panVolumes, outputChannels, n, 2) * kSqrtHalf * wetLevel + out->wet[in + n]);
        }
        break;
    case 8:
        for (int ii = 0; ii < in; ++ii)
        {
            out->wet[in + ii] = float(SpeakerVol(panVolumes, outputChannels, ii, 1) * wetLevel);
            out->wet[3 * in + ii] = float(SpeakerVol(panVolumes, outputChannels, ii, 0) * wetLevel);
            out->wet[3 * in + ii] =
                float(SpeakerVol(panVolumes, outputChannels, ii, 2) * kSqrtHalf * wetLevel + out->wet[3 * in + ii]);
            out->wet[in + ii] =
                float(SpeakerVol(panVolumes, outputChannels, ii, 2) * kSqrtHalf * wetLevel + out->wet[in + ii]);
            out->wet[2 * in + ii] = float(SpeakerVol(panVolumes, outputChannels, ii, 5) * kSqrtHalf * wetLevel);
            out->wet[2 * in + ii] =
                float(SpeakerVol(panVolumes, outputChannels, ii, 7) * kSqrtHalf * wetLevel + out->wet[2 * in + ii]);
            out->wet[ii] = float(SpeakerVol(panVolumes, outputChannels, ii, 6) * kSqrtHalf * wetLevel);
            out->wet[ii] = float(SpeakerVol(panVolumes, outputChannels, ii, 4) * kSqrtHalf * wetLevel + out->wet[ii]);
        }
        break;
    default:
        break;
    }

    // The dry bus: outputChannels destination channels, destination-major, the speaker map straight through with the
    // 8-channel reorder (ChannelFixup) and the dry level.
    for (int channel = 0; channel < outputChannels; ++channel)
        for (int j = 0; j < in; ++j)
        {
            const unsigned int dest = ChannelFixup(unsigned(outputChannels), unsigned(channel));
            out->dry[channel * in + j] = float(SpeakerVol(panVolumes, outputChannels, j, int(dest)) * dryLevel);
        }
}

void MixAccumulate(const float *src, int frames, int inputChannels, const float *matrix, int outputChannels,
                   float *dst)
{
    for (int f = 0; f < frames; ++f)
        for (int c = 0; c < outputChannels; ++c)
        {
            float acc = dst[f * outputChannels + c];
            for (int s = 0; s < inputChannels; ++s)
                acc += src[f * inputChannels + s] * matrix[c * inputChannels + s];
            dst[f * outputChannels + c] = acc;
        }
}
} // namespace bo1snd
