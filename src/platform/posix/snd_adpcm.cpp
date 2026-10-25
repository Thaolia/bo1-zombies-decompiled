// MS-ADPCM decoder (snd_adpcm.h). The standard algorithm: a block holds, per channel, a coefficient index, a delta
// and the two most recent samples, then 4-bit nibbles. The engine uses the seven standard coefficient pairs
// (iSND_CreateVoice hard-codes them), the same ffmpeg's adpcm_ms uses, so they are not read from the stream.

#include "snd_adpcm.h"

namespace
{
// The seven standard MS-ADPCM coefficient pairs (iCoef1, iCoef2), scaled by 256.
const int kCoef1[7] = {256, 512, 0, 192, 240, 460, 392};
const int kCoef2[7] = {0, -256, 0, 64, 0, -208, -232};

// The delta adaptation table, indexed by the nibble.
const int kAdapt[16] = {230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230};

int16_t ReadS16(const uint8_t *p)
{
    return int16_t(uint16_t(p[0]) | uint16_t(p[1]) << 8);
}

int Clamp16(int v)
{
    return v < -32768 ? -32768 : v > 32767 ? 32767 : v;
}

// One sample from a nibble, advancing the channel's predictor state.
int DecodeNibble(int nibble, int coef1, int coef2, int &sample1, int &sample2, int &delta)
{
    const int signed4 = nibble & 0x8 ? nibble - 16 : nibble;
    int predicted = (sample1 * coef1 + sample2 * coef2) >> 8;
    predicted += signed4 * delta;
    predicted = Clamp16(predicted);
    sample2 = sample1;
    sample1 = predicted;
    delta = (kAdapt[nibble] * delta) >> 8;
    if (delta < 16)
        delta = 16;
    return predicted;
}
} // namespace

int Bo1_MsAdpcmFramesPerBlock(int channels, int blockAlign)
{
    // 7 header bytes a channel, then one nibble (one sample) a 4-bit code; a byte holds two.
    const int headerBytes = 7 * channels;
    if (blockAlign <= headerBytes || channels < 1)
        return 0;
    return 2 + 2 * (blockAlign - headerBytes) / channels;
}

int Bo1_DecodeMsAdpcm(const uint8_t *data, size_t dataSize, int channels, int blockAlign, int16_t *out, int outFrames)
{
    if (channels < 1 || channels > 2 || blockAlign <= 7 * channels)
        return 0;
    int frames = 0;
    for (size_t base = 0; base + size_t(7 * channels) <= dataSize && frames < outFrames; base += blockAlign)
    {
        const size_t blockBytes = dataSize - base < size_t(blockAlign) ? dataSize - base : size_t(blockAlign);
        const uint8_t *p = data + base;
        int coef1[2], coef2[2], delta[2], sample1[2], sample2[2];
        size_t cursor = 0;
        bool bad = false;
        for (int c = 0; c < channels; ++c)
        {
            const int index = p[cursor++];
            if (index > 6)
                bad = true;
            coef1[c] = kCoef1[index > 6 ? 0 : index];
            coef2[c] = kCoef2[index > 6 ? 0 : index];
        }
        for (int c = 0; c < channels; ++c)
        {
            delta[c] = ReadS16(p + cursor);
            cursor += 2;
        }
        for (int c = 0; c < channels; ++c)
        {
            sample1[c] = ReadS16(p + cursor);
            cursor += 2;
        }
        for (int c = 0; c < channels; ++c)
        {
            sample2[c] = ReadS16(p + cursor);
            cursor += 2;
        }
        if (bad)
            continue;
        // The block's first two frames are the header samples, older (sample2) first.
        for (int c = 0; c < channels; ++c)
            out[frames * channels + c] = int16_t(sample2[c]);
        ++frames;
        if (frames < outFrames)
        {
            for (int c = 0; c < channels; ++c)
                out[frames * channels + c] = int16_t(sample1[c]);
            ++frames;
        }
        // Then the nibbles: high nibble first, channels alternating within a byte for stereo.
        while (cursor < blockBytes && frames < outFrames)
        {
            const uint8_t byte = p[cursor++];
            const int high = byte >> 4, low = byte & 0xF;
            if (channels == 1)
            {
                out[frames * channels] = int16_t(DecodeNibble(high, coef1[0], coef2[0], sample1[0], sample2[0], delta[0]));
                ++frames;
                if (frames < outFrames)
                {
                    out[frames * channels] =
                        int16_t(DecodeNibble(low, coef1[0], coef2[0], sample1[0], sample2[0], delta[0]));
                    ++frames;
                }
            }
            else
            {
                out[frames * channels] = int16_t(DecodeNibble(high, coef1[0], coef2[0], sample1[0], sample2[0], delta[0]));
                out[frames * channels + 1] =
                    int16_t(DecodeNibble(low, coef1[1], coef2[1], sample1[1], sample2[1], delta[1]));
                ++frames;
            }
        }
    }
    return frames;
}
