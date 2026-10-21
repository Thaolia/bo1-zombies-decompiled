#pragma once

// Plain DSP parameter and state structs shared by the mixer (snd.h) and the XAudio2 effect classes in
// snd_driver_xaudio2_dsp.h. Kept free of XAudio2/XAPO headers so the engine can include it on every platform.

struct snd_dsp_master_params // sizeof=0x60
{                                       // XREF: SDXA2MasterBusEffect/r
                                        // SoundState/r
    float lowE;                         // XREF: SD_PreUpdate(int)+314/w
    float lowG;                         // XREF: SD_PreUpdate(int)+324/w
    float lowF;                         // XREF: SD_PreUpdate(int)+334/w
    float lowQ;                         // XREF: SD_PreUpdate(int)+344/w
    float peak1E;                       // XREF: SD_PreUpdate(int)+354/w
    float peak1G;                       // XREF: SD_PreUpdate(int)+364/w
    float peak1F;                       // XREF: SD_PreUpdate(int)+374/w
    float peak1Q;                       // XREF: SD_PreUpdate(int)+384/w
    float peak2E;                       // XREF: SD_PreUpdate(int)+394/w
    float peak2G;                       // XREF: SD_PreUpdate(int)+3A4/w
    float peak2F;                       // XREF: SD_PreUpdate(int)+3B4/w
    float peak2Q;                       // XREF: SD_PreUpdate(int)+3C4/w
    float hiE;                          // XREF: SD_PreUpdate(int)+3D4/w
    float hiG;                          // XREF: SD_PreUpdate(int)+3E4/w
    float hiF;                          // XREF: SD_PreUpdate(int)+3F4/w
    float hiQ;                          // XREF: SD_PreUpdate(int)+404/w
    float eqG;                          // XREF: SD_PreUpdate(int)+414/w
    float limitE;                       // XREF: SD_PreUpdate(int)+427/w
    float limitPG;                      // XREF: SD_PreUpdate(int)+43A/w
    float limitMG;                      // XREF: SD_PreUpdate(int)+44D/w
    float limitT;                       // XREF: SD_PreUpdate(int)+460/w
    float limitR;                       // XREF: SD_PreUpdate(int)+473/w
    float limitTA;                      // XREF: SD_PreUpdate(int)+486/w
    float limitTR;                      // XREF: SD_PreUpdate(int)+499/w
};

struct __declspec(align(128)) snd_dsp_dynamo_state // sizeof=0xF80
{                                       // XREF: snd_dsp_master_no_voice_state/r
                                        // snd_dsp_master_state/r
    float lookahead[960];
    unsigned int offset;
    float yfilter;
    float gfilter;
    float g;
};

struct snd_dsp_master_no_voice_state // sizeof=0xF80
{                                       // XREF: SDXA2MasterNoVoiceBusEffect/r
    snd_dsp_dynamo_state comp;
};

struct snd_dsp_biquad_state // sizeof=0x20
{                                       // XREF: snd_dsp_futz_state/r
                                        // snd_dsp_futz_state/r ...
    float y[4];
    float x[4];
};

struct snd_dsp_squelch_state // sizeof=0x8
{                                       // XREF: snd_dsp_futz_state/r
    float l;
    float g;
};

struct snd_dsp_futz_state // sizeof=0x48
{                                       // XREF: snd_dsp_source_state/r
    snd_dsp_biquad_state bpf;
    snd_dsp_biquad_state ls;
    snd_dsp_squelch_state squelch;
};

struct snd_dsp_master_no_voice_params // sizeof=0x1C
{                                       // XREF: SDXA2MasterNoVoiceBusEffect/r
                                        // SoundState/r
    float compE;                        // XREF: SD_Init(void)+AF/w
                                        // SD_PreUpdate(int)+4C1/w ...
    float compPG;                       // XREF: SD_Init(void)+B5/w
                                        // SD_PreUpdate(int)+4C7/w ...
    float compMG;                       // XREF: SD_Init(void)+BB/w
                                        // SD_PreUpdate(int)+4CD/w ...
    float compT;                        // XREF: SD_Init(void)+C1/w
                                        // SD_PreUpdate(int)+4D3/w ...
    float compR;                        // XREF: SD_Init(void)+C7/w
                                        // SD_PreUpdate(int)+4D9/w ...
    float compTA;                       // XREF: SD_Init(void)+CD/w
                                        // SD_PreUpdate(int)+4DF/w ...
    float compTR;                       // XREF: SD_Init(void)+D3/w
                                        // SD_PreUpdate(int)+4E5/w ...
};

struct snd_dsp_biquad_coef // sizeof=0x14
{                                       // XREF: ?SND_DspFutzMono@@YAXPIBUsnd_dsp_futz_param@@PIAUsnd_dsp_futz_state@@MIPIAM22@Z/r
                                        // ?SND_DspFutzMono@@YAXPIBUsnd_dsp_futz_param@@PIAUsnd_dsp_futz_state@@MIPIAM22@Z/r ...
    float s[5];
};

struct snd_dsp_master_state // sizeof=0x1000
{                                       // XREF: SDXA2MasterBusEffect/r
    snd_dsp_biquad_state low;
    snd_dsp_biquad_state peak1;
    snd_dsp_biquad_state peak2;
    snd_dsp_biquad_state hi;
    snd_dsp_dynamo_state limit;
};

struct snd_dsp_squelch_param // sizeof=0x8
{                                       // XREF: snd_dsp_futz_param/r
    float tg;
    float th;
};

struct snd_dsp_futz_param // sizeof=0x34
{                                       // XREF: snd_dsp_source_params/r
    float blend;
    float bpfF;
    float bpfQ;
    float lsG;
    float lsF;
    float lsQ;
    float distortion;
    float pregain;
    float postgain;
    float preclip;
    float postclip;
    snd_dsp_squelch_param squelch;
};

struct snd_dsp_source_params // sizeof=0x40
{                                       // XREF: SDXA2SourceEffect/r
                                        // VoiceInfo/r
    float frameRate;
    float lpfAttenuation;
    float lpfRatio;
    snd_dsp_futz_param futz;
};

struct snd_dsp_source_state // sizeof=0x4C
{                                       // XREF: SDXA2SourceEffect/r
    float lpfy;
    snd_dsp_futz_state futz;
};
