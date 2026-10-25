// The software mixer's math (src/platform/posix/snd_mix): the voice send matrices (BuildVoiceSends, ported from
// SDXA2_UpdateVoiceSends) and their application (MixAccumulate). Two parts, both without a device or real time:
//   1. pure checks of the matrix layout, the stereo down-mix and the bus routing;
//   2. an offline oracle: two mono voices panned and summed into a stereo bus, compared sample for sample against a
//      reference computed straight from the gains. The mixed buffer is also written to a WAV (optional argv[1]) so a
//      human can inspect or listen later; the pass/fail is the in-memory comparison, which needs no ears.
// ./snd_mix_test [out.wav] in the build folder.

#include <platform/posix/snd_mix.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
int g_failures = 0;

void Check(bool ok, const char *what)
{
    if (!ok)
    {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

bool Near(float a, float b)
{
    return std::fabs(a - b) <= 1e-6f * (1.0f + std::fabs(b));
}

void WriteWavS16Stereo(const char *path, const float *interleaved, int frames, int rate)
{
    FILE *f = std::fopen(path, "wb");
    if (!f)
        return;
    const int channels = 2, bits = 16;
    const uint32_t dataBytes = uint32_t(frames) * channels * (bits / 8);
    const uint32_t byteRate = uint32_t(rate) * channels * (bits / 8);
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f);
    u32(36 + dataBytes);
    std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f);
    u32(16);
    u16(1); // PCM
    u16(channels);
    u32(uint32_t(rate));
    u32(byteRate);
    u16(channels * (bits / 8));
    u16(bits);
    std::fwrite("data", 1, 4, f);
    u32(dataBytes);
    for (int i = 0; i < frames * channels; ++i)
    {
        float v = interleaved[i];
        v = v < -1.0f ? -1.0f : v > 1.0f ? 1.0f : v;
        int16_t s = int16_t(std::lrint(v * 32767.0f));
        std::fwrite(&s, 2, 1, f);
    }
    std::fclose(f);
}
} // namespace

int main(int argc, char **argv)
{
    using namespace bo1snd;

    // --- ChannelFixup: identity except the 8-channel surround reorder (snd_driver_xaudio2.cpp:89). ---
    for (unsigned oc : {1u, 2u, 6u})
        for (unsigned c = 0; c < oc; ++c)
            Check(ChannelFixup(oc, c) == c, "ChannelFixup is the identity below 8 channels");
    Check(ChannelFixup(8, 0) == 0 && ChannelFixup(8, 1) == 1 && ChannelFixup(8, 2) == 2 && ChannelFixup(8, 3) == 3 &&
              ChannelFixup(8, 4) == 6 && ChannelFixup(8, 5) == 7 && ChannelFixup(8, 6) == 4 && ChannelFixup(8, 7) == 5,
          "ChannelFixup reorders the 8-channel surround speakers (4<->6, 5<->7)");

    // --- Stereo dry matrix: mono source, a left/right pan straight through, destination-major. ---
    {
        // A mono voice's speaker map for a 2-channel device: volumes[out + 2*in], in=0 only -> {L, R}.
        const float pan[16] = {0.8f, 0.3f};
        VoiceSends s;
        BuildVoiceSends(pan, 1, 2, 0.5f /*dry*/, 0.0f /*wet*/, 0 /*flags*/, &s);
        Check(s.inputChannels == 1 && s.outputChannels == 2, "sends carry the channel counts");
        // dry[dest*in + src]: dest 0 = L*dry, dest 1 = R*dry.
        Check(Near(s.dry[0], 0.8f * 0.5f), "dry left = pan L * dryLevel (destination-major index 0)");
        Check(Near(s.dry[1], 0.3f * 0.5f), "dry right = pan R * dryLevel (destination-major index 1)");
        // wet is zero when wetLevel is zero.
        bool wetZero = true;
        for (int i = 0; i < 2 * 4; ++i)
            wetZero = wetZero && s.wet[i] == 0.0f;
        Check(wetZero, "wet matrix is all zero at wetLevel 0");
    }

    // --- Stereo wet (reverb bus) matrix: the 0,1,1,0 cross with the 1/sqrt(2) factor. ---
    {
        const float pan[16] = {0.8f, 0.3f};
        VoiceSends s;
        BuildVoiceSends(pan, 1, 2, 0.0f, 1.0f /*wet*/, 0, &s);
        const float r = 0.70710677f;
        Check(Near(s.wet[0], 0.8f * r), "reverb dest0 = pan L * 1/sqrt2");
        Check(Near(s.wet[1], 0.3f * r), "reverb dest1 = pan R * 1/sqrt2");
        Check(Near(s.wet[2], 0.3f * r), "reverb dest2 = pan R * 1/sqrt2");
        Check(Near(s.wet[3], 0.8f * r), "reverb dest3 = pan L * 1/sqrt2");
    }

    // --- Bus routing branch: (aliasFlags & 0x3000) >> 12 == 2 picks the master bus, else the novoice bus. ---
    {
        const float pan[16] = {1.0f, 1.0f};
        VoiceSends s;
        BuildVoiceSends(pan, 1, 2, 1.0f, 0.0f, 0x2000, &s);
        Check(s.toMasterBus, "flags field 2 routes to the master bus");
        BuildVoiceSends(pan, 1, 2, 1.0f, 0.0f, 0x1000, &s);
        Check(!s.toMasterBus, "flags field 1 routes to the novoice bus");
        BuildVoiceSends(pan, 1, 2, 1.0f, 0.0f, 0x0000, &s);
        Check(!s.toMasterBus, "flags field 0 routes to the novoice bus");
    }

    // --- Stereo-source dry matrix: the asset's two channels placed on the two speakers, destination-major. ---
    {
        // in=2, out=2. volumes[out + 2*in]: (in0->L,R)=(0.9,0.1), (in1->L,R)=(0.2,0.7).
        const float pan[16] = {0.9f, 0.1f, 0.2f, 0.7f};
        VoiceSends s;
        BuildVoiceSends(pan, 2, 2, 1.0f, 0.0f, 0, &s);
        // dry[dest*in + src]
        Check(Near(s.dry[0 * 2 + 0], 0.9f), "stereo dry: src0 -> dest0 (L of in0)");
        Check(Near(s.dry[0 * 2 + 1], 0.2f), "stereo dry: src1 -> dest0 (L of in1)");
        Check(Near(s.dry[1 * 2 + 0], 0.1f), "stereo dry: src0 -> dest1 (R of in0)");
        Check(Near(s.dry[1 * 2 + 1], 0.7f), "stereo dry: src1 -> dest1 (R of in1)");
    }

    // --- Offline oracle: two mono voices panned and summed into a stereo bus, vs a computed reference. ---
    const int frames = 4800; // 0.1 s at 48 kHz
    const int rate = 48000;
    std::vector<float> voiceA(frames), voiceB(frames);
    for (int i = 0; i < frames; ++i)
    {
        voiceA[i] = 0.5f * std::sin(2.0f * 3.14159265f * 440.0f * i / rate);
        voiceB[i] = 0.3f * std::sin(2.0f * 3.14159265f * 660.0f * i / rate);
    }
    const float panA[16] = {0.9f, 0.2f}; // mostly left
    const float panB[16] = {0.1f, 0.8f}; // mostly right
    const float dryA = 0.8f, dryB = 0.6f;

    VoiceSends sA, sB;
    BuildVoiceSends(panA, 1, 2, dryA, 0.0f, 0, &sA);
    BuildVoiceSends(panB, 1, 2, dryB, 0.0f, 0, &sB);

    std::vector<float> bus(size_t(frames) * 2, 0.0f);
    MixAccumulate(voiceA.data(), frames, 1, sA.dry, 2, bus.data());
    MixAccumulate(voiceB.data(), frames, 1, sB.dry, 2, bus.data());

    // Reference: each voice is src * (pan * dryLevel) on each speaker, voices summed. Same float ops as the mixer so
    // the two agree exactly.
    const float gAL = float(double(panA[0]) * dryA), gAR = float(double(panA[1]) * dryA);
    const float gBL = float(double(panB[0]) * dryB), gBR = float(double(panB[1]) * dryB);
    int mism = 0;
    for (int f = 0; f < frames; ++f)
    {
        float refL = voiceA[f] * gAL + voiceB[f] * gBL;
        float refR = voiceA[f] * gAR + voiceB[f] * gBR;
        if (bus[f * 2] != refL || bus[f * 2 + 1] != refR)
            ++mism;
    }
    Check(mism == 0, "two voices mix to the computed left/right reference, sample for sample");

    if (argc > 1)
        WriteWavS16Stereo(argv[1], bus.data(), frames, rate);

    std::printf("snd_mix_test: %s%s\n", g_failures ? "FAILURES" : "all checks passed",
                argc > 1 ? " (mix written to the given WAV)" : "");
    return g_failures ? 1 : 0;
}
