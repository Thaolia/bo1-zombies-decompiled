#pragma once

// <mmsystem.h>: the audio mixer and wave input queries of win32/win_voice.cpp (microphone source, level and mute).
// <windows.h> includes it, as the Windows SDK's does. The platform layer defines the functions (win32_system.cpp).

#include "windows.h"

typedef UINT MMRESULT;
typedef UINT MMVERSION;
DECLARE_HANDLE(HMIXEROBJ);
DECLARE_HANDLE(HMIXER);
typedef HMIXER *LPHMIXER;

#define MMSYSERR_NOERROR 0
#define MMSYSERR_BADDEVICEID 2
#define MMSYSERR_INVALHANDLE 5

#define MAXPNAMELEN 32
#define MIXER_SHORT_NAME_CHARS 16
#define MIXER_LONG_NAME_CHARS 64

typedef struct tagMIXERLINEA
{
    DWORD cbStruct;
    DWORD dwDestination;
    DWORD dwSource;
    DWORD dwLineID;
    DWORD fdwLine;
    DWORD_PTR dwUser;
    DWORD dwComponentType;
    DWORD cChannels;
    DWORD cConnections;
    DWORD cControls;
    CHAR szShortName[MIXER_SHORT_NAME_CHARS];
    CHAR szName[MIXER_LONG_NAME_CHARS];
    struct
    {
        DWORD dwType;
        DWORD dwDeviceID;
        WORD wMid;
        WORD wPid;
        MMVERSION vDriverVersion;
        CHAR szPname[MAXPNAMELEN];
    } Target;
} MIXERLINEA, *LPMIXERLINEA;

typedef struct tagMIXERCONTROLA
{
    DWORD cbStruct;
    DWORD dwControlID;
    DWORD dwControlType;
    DWORD fdwControl;
    DWORD cMultipleItems;
    CHAR szShortName[MIXER_SHORT_NAME_CHARS];
    CHAR szName[MIXER_LONG_NAME_CHARS];
    union
    {
        struct
        {
            LONG lMinimum;
            LONG lMaximum;
        };
        struct
        {
            DWORD dwMinimum;
            DWORD dwMaximum;
        };
        DWORD dwReserved[6];
    } Bounds;
    union
    {
        DWORD cSteps;
        DWORD cbCustomData;
        DWORD dwReserved[6];
    } Metrics;
} MIXERCONTROLA, *LPMIXERCONTROLA;

typedef struct tagMIXERLINECONTROLSA
{
    DWORD cbStruct;
    DWORD dwLineID;
    union
    {
        DWORD dwControlID;
        DWORD dwControlType;
    };
    DWORD cControls;
    DWORD cbmxctrl;
    LPMIXERCONTROLA pamxctrl;
} MIXERLINECONTROLSA, *LPMIXERLINECONTROLSA;

typedef struct tMIXERCONTROLDETAILS
{
    DWORD cbStruct;
    DWORD dwControlID;
    DWORD cChannels;
    union
    {
        HWND hwndOwner;
        DWORD cMultipleItems;
    };
    DWORD cbDetails;
    LPVOID paDetails;
} MIXERCONTROLDETAILS, *LPMIXERCONTROLDETAILS;

typedef struct tagMIXERCONTROLDETAILS_LISTTEXTA
{
    DWORD dwParam1;
    DWORD dwParam2;
    CHAR szName[MIXER_LONG_NAME_CHARS];
} MIXERCONTROLDETAILS_LISTTEXTA;

typedef struct tMIXERCONTROLDETAILS_BOOLEAN
{
    LONG fValue;
} MIXERCONTROLDETAILS_BOOLEAN;

typedef struct tMIXERCONTROLDETAILS_UNSIGNED
{
    DWORD dwValue;
} MIXERCONTROLDETAILS_UNSIGNED;

extern "C" {
UINT waveInGetNumDevs(void);
UINT mixerGetNumDevs(void);
MMRESULT mixerOpen(LPHMIXER phmx, UINT uMxId, DWORD_PTR dwCallback, DWORD_PTR dwInstance, DWORD fdwOpen);
MMRESULT mixerClose(HMIXER hmx);
MMRESULT mixerGetLineInfoA(HMIXEROBJ hmxobj, LPMIXERLINEA pmxl, DWORD fdwInfo);
MMRESULT mixerGetLineControlsA(HMIXEROBJ hmxobj, LPMIXERLINECONTROLSA pmxlc, DWORD fdwControls);
MMRESULT mixerGetControlDetailsA(HMIXEROBJ hmxobj, LPMIXERCONTROLDETAILS pmxcd, DWORD fdwDetails);
MMRESULT mixerSetControlDetails(HMIXEROBJ hmxobj, LPMIXERCONTROLDETAILS pmxcd, DWORD fdwDetails);
}
