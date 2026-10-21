// steam_api.dll as it behaves on a machine where the Steam client is not running: init fails with NoSteamClient and
// every interface accessor (SteamUser(), SteamFriends(), ...) returns NULL. win_steam.cpp already handles that state.

#include <steam/steam_api.h>

#include <cstring>

S_API ESteamAPIInitResult S_CALLTYPE SteamInternal_SteamAPI_Init(const char *, SteamErrMsg *pOutErrMsg)
{
    if (pOutErrMsg)
        strncpy(*pOutErrMsg, "Steam is not part of this build (BO1_OFFLINE)", sizeof(*pOutErrMsg) - 1);
    return k_ESteamAPIInitResult_NoSteamClient;
}

S_API void S_CALLTYPE SteamAPI_Shutdown()
{
}

S_API void S_CALLTYPE SteamAPI_RunCallbacks()
{
}

S_API void S_CALLTYPE SteamGameServer_RunCallbacks()
{
}

S_API HSteamUser S_CALLTYPE SteamAPI_GetHSteamUser()
{
    return 0;
}

S_API HSteamUser S_CALLTYPE SteamGameServer_GetHSteamUser()
{
    return 0;
}

// pContextInitData is { init function, counter, interface pointer }, zero-initialised by the accessor; leaving it
// untouched makes the accessor return the NULL interface pointer.
S_API void *S_CALLTYPE SteamInternal_ContextInit(void *pContextInitData)
{
    return static_cast<void **>(pContextInitData) + 2;
}

S_API void *S_CALLTYPE SteamInternal_CreateInterface(const char *)
{
    return nullptr;
}

S_API void *S_CALLTYPE SteamInternal_FindOrCreateUserInterface(HSteamUser, const char *)
{
    return nullptr;
}

S_API void *S_CALLTYPE SteamInternal_FindOrCreateGameServerInterface(HSteamUser, const char *)
{
    return nullptr;
}

S_API void S_CALLTYPE SteamAPI_RegisterCallback(class CCallbackBase *, int)
{
}

S_API void S_CALLTYPE SteamAPI_UnregisterCallback(class CCallbackBase *)
{
}

S_API void S_CALLTYPE SteamAPI_RegisterCallResult(class CCallbackBase *, SteamAPICall_t)
{
}

S_API void S_CALLTYPE SteamAPI_UnregisterCallResult(class CCallbackBase *, SteamAPICall_t)
{
}
