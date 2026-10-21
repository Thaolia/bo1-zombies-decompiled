// binkw32.dll without a decoder: every BinkOpen fails and R_Cinematic_StartPlayback_Now takes its "not playing
// movie" path. Nothing gets an HBINK, so the per-movie calls are never reached with a real handle.

#include <binklib/bink.h>
#include <qcommon/common.h>

RADEXPFUNC HBINK RADEXPLINK BinkOpen(const char *, U32)
{
    static bool reported;
    if (!reported)
    {
        reported = true;
        Com_PrintWarning(8, "Bink is not part of this build (BO1_OFFLINE): movies are skipped\n");
    }
    return nullptr;
}

// Empty even after a failed open: R_Cinematic_CheckBinkError asserts that no error is pending before each movie,
// which the real library only leaves set after an open that should not have failed.
RADEXPFUNC char *RADEXPLINK BinkGetError(void)
{
    static char none[] = "";
    return none;
}

RADEXPFUNC void RADEXPLINK BinkSetMemory(BINKMEMALLOC, BINKMEMFREE)
{
}

RADEXPFUNC void RADEXPLINK BinkSetIOSize(U32)
{
}

RADEXPFUNC void RADEXPLINK BinkSetSoundTrack(U32, U32 *)
{
}

RADEXPFUNC BINKSNDOPEN RADEXPLINK BinkOpenDirectSound(UINTa)
{
    return nullptr;
}

RADEXPFUNC S32 RADEXPLINK BinkSetSoundSystem(BINKSNDSYSOPEN, UINTa)
{
    return 0;
}

RADEXPFUNC void RADEXPLINK BinkClose(HBINK)
{
}

RADEXPFUNC S32 RADEXPLINK BinkControlBackgroundIO(HBINK, U32)
{
    return 0;
}

RADEXPFUNC S32 RADEXPLINK BinkDoFrame(HBINK)
{
    return 0;
}

RADEXPFUNC void RADEXPLINK BinkNextFrame(HBINK)
{
}

RADEXPFUNC S32 RADEXPLINK BinkWait(HBINK)
{
    return 0;
}

RADEXPFUNC S32 RADEXPLINK BinkPause(HBINK, S32)
{
    return 0;
}

RADEXPFUNC S32 RADEXPLINK BinkShouldSkip(HBINK)
{
    return 0;
}

RADEXPFUNC void RADEXPLINK BinkSetVolume(HBINK, U32, S32)
{
}

RADEXPFUNC void RADEXPLINK BinkGetFrameBuffersInfo(HBINK, BINKFRAMEBUFFERS *)
{
}

RADEXPFUNC void RADEXPLINK BinkRegisterFrameBuffers(HBINK, BINKFRAMEBUFFERS *)
{
}

RADEXPFUNC void RADEXPLINK BinkGetRealtime(HBINK, BINKREALTIME *, U32)
{
}
