#pragma once

#include <XAPOBase.h>
#include <XAudio2.h>
#include <universal/assertive.h>
#include "snd_radverb.h"
#include "snd_dsp_types.h"

#define SDXA2_MAX_FRAME_COUNT 480

static const GUID HACK_IID_IXAPOParameters = { 0xA90BC001, 0xE897, 0xE897, { 0x55, 0xE4, 0x9E, 0x47, 0x00, 0x00, 0x00, 0x01 } };

//struct SDXA2Effect : public CXAPOParametersBase  // sizeof=0x3C80
struct SDXA2Effect : public CXAPOBase, public IXAPOParameters // sizeof=0x3C80
{                                       // XREF: SDXA2MasterNoVoiceBusEffect/r
    int locked;
    int started;
    unsigned int frameRate;
    unsigned int frameCount;
    unsigned int channelCount;
    __declspec(align(32)) float interleave[3840];             // XREF: .rdata:stru_D50258/o

    SDXA2Effect(XAPO_REGISTRATION_PROPERTIES *props);
    //SDXA2Effect(XAPO_REGISTRATION_PROPERTIES *pRegistrationProperties, BYTE *pParameterBlocks, UINT32 uParameterBlockByteSize, BOOL fProducer);
    ~SDXA2Effect();

    // IUnknown CANCER

    HRESULT __stdcall QueryInterface(REFIID riid,void **ppvObject)
    {
        if (!ppvObject)
            return E_POINTER;

        *ppvObject = nullptr;

        // IUnknown
        if (riid == __uuidof(IUnknown))
        {
            *ppvObject = static_cast<IUnknown *>(static_cast<CXAPOBase *>(this));
            AddRef();
            return S_OK;
        }

        // IXAPOParameters (your custom IID)
        //if (riid == IID_IXAPOParameters)
        if (riid == HACK_IID_IXAPOParameters)
        {
            *ppvObject = static_cast<IXAPOParameters *>(this);
            AddRef();
            return S_OK;
        }

        // fallback to base implementation
        return CXAPOBase::QueryInterface(riid, ppvObject);
    }

    ULONG __stdcall AddRef()
    {
        return InterlockedIncrement(&this->m_lReferenceCount);
    }

    ULONG __stdcall Release()
    {
        return CXAPOBase::Release();
    }

    HRESULT STDMETHODCALLTYPE LockForProcess(
        unsigned int InputLockedParameterCount,
        const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS *in,
        unsigned int OutputLockedParameterCount,
        const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS *out);;

    //int __stdcall SDXA2Effect::Release(); CXAPOBase
    void STDMETHODCALLTYPE Reset();
    void STDMETHODCALLTYPE UnlockForProcess();

    void STDMETHODCALLTYPE GetParameters(void *, UINT32)
    {
        iassert(0);
    }

    virtual void __stdcall Process(
        unsigned int InputProcessParameterCount,
        const XAPO_PROCESS_BUFFER_PARAMETERS *pInputProcessParameters,
        unsigned int OutputProcessParameterCount,
        XAPO_PROCESS_BUFFER_PARAMETERS *pOutputProcessParameters,
        int IsEnabled);
    
    virtual void Process(unsigned int channelCount, unsigned int frameCount, float *data) = 0;
};

struct SDXA2MasterBusEffect : SDXA2Effect // sizeof=0xBD00
{                                       // XREF: SoundState/r
    snd_dsp_master_params params;
    snd_dsp_master_state state[8];

    SDXA2MasterBusEffect();

    void Process(
        unsigned int channelCount,
        unsigned int frameCount,
        float *data);
    void STDMETHODCALLTYPE SetParameters(const void *pParams, unsigned int cbParams); 
};

struct SDXA2RadverbEffect : SDXA2Effect // sizeof=0x87B80
{                                       // XREF: SoundState/r
    snd_rv_state state;                 // XREF: .data:00E1F218/o
                                        // .data:00E1F42C/o ...
    snd_rv_params params;

    float temp[3840];

    SDXA2RadverbEffect();

    void Process(
        unsigned int channelCount,
        unsigned int frameCount,
        float *data);

    void STDMETHODCALLTYPE SetParameters(const void *pParams, unsigned int cbParams);
};

struct SDXA2SourceEffect : public SDXA2Effect // sizeof=0x5B80
{                                       // XREF: SoundState/r
    snd_dsp_source_params params;
    snd_dsp_source_state state[2];

    float tempa[960];
    float tempb[960];

    SDXA2SourceEffect();
    void Clear();
    void Process(
        unsigned int channelCount,
        unsigned int frameCount,
        float *data);
    void STDMETHODCALLTYPE SetParameters(const void *pParams, unsigned int cbParams);
};

struct SDXA2MasterNoVoiceBusEffect : SDXA2Effect // sizeof=0xB900
{                                       // XREF: SoundState/r
    snd_dsp_master_no_voice_params params;
    snd_dsp_master_no_voice_state state[8];

    SDXA2MasterNoVoiceBusEffect();

    void Process(
        unsigned int channelCount,
        unsigned int frameCount,
        float *data);
    void STDMETHODCALLTYPE SetParameters(const void *pParams, unsigned int cbParams);
};
