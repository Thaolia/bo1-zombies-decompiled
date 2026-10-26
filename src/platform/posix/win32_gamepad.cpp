// The controller layer on posix (the XInput API win32/win_gamepad.cpp polls), on SDL_GameController. The video thread
// reads the open controller each pump into a snapshot (win32_gamepad.h maps it to XInput values); the XInput entry
// points, called from the engine's input code on another thread, read the snapshot under a lock. One controller,
// player 1, as the engine only uses port 0 on this build. A headless run or a machine with no pad reports no device,
// and the engine runs without a controller as on a PC without one.

#include "posix_video.h"
#include "win32_gamepad.h"

#include <SDL.h>

#include <cstring>
#include <mutex>

bool __cdecl Sys_IsHeadless();
void Com_Printf(int channel, const char *fmt, ...);

namespace
{
std::mutex s_lock;
SDL_GameController *s_controller = nullptr;
SDL_JoystickID s_instance = -1;
bool s_connected = false;
unsigned int s_packet = 0;
XINPUT_GAMEPAD s_state = {};
bool s_rumbleWanted = false;
Uint16 s_rumbleLow = 0, s_rumbleHigh = 0;
bool s_triedInit = false;
bool s_reportedNone = false;
constexpr int kConsoleChannel = 16;

SDL_GameController *OpenFirstController()
{
    for (int i = 0; i < SDL_NumJoysticks(); ++i)
        if (SDL_IsGameController(i))
            return SDL_GameControllerOpen(i);
    return nullptr;
}
} // namespace

// On the video thread, after the event pump: opens or drops the controller and refreshes the snapshot.
void Bo1_GamepadPump()
{
    if (!s_triedInit)
    {
        s_triedInit = true;
        if (Sys_IsHeadless())
            return;
        if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
        {
            Com_Printf(kConsoleChannel, "gamepad: SDL game controller init failed: %s\n", SDL_GetError());
            return;
        }
    }
    if (!SDL_WasInit(SDL_INIT_GAMECONTROLLER))
        return;

    if (s_controller && !SDL_GameControllerGetAttached(s_controller))
    {
        Com_Printf(kConsoleChannel, "gamepad: controller detached\n");
        SDL_GameControllerClose(s_controller);
        s_controller = nullptr;
        s_instance = -1;
    }
    if (!s_controller)
    {
        s_controller = OpenFirstController();
        if (s_controller)
        {
            s_instance = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(s_controller));
            const char *name = SDL_GameControllerName(s_controller);
            Com_Printf(kConsoleChannel, "gamepad: opened '%s'\n", name ? name : "?");
            s_reportedNone = false;
        }
        else if (!s_reportedNone)
        {
            Com_Printf(kConsoleChannel, "gamepad: no game controller (%d joysticks)\n", SDL_NumJoysticks());
            s_reportedNone = true;
        }
    }

    if (!s_controller)
    {
        std::lock_guard<std::mutex> lock(s_lock);
        s_connected = false;
        return;
    }

    Uint8 buttons[SDL_CONTROLLER_BUTTON_MAX];
    for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX; ++i)
        buttons[i] = SDL_GameControllerGetButton(s_controller, SDL_GameControllerButton(i));
    Sint16 axes[SDL_CONTROLLER_AXIS_MAX];
    for (int i = 0; i < SDL_CONTROLLER_AXIS_MAX; ++i)
        axes[i] = SDL_GameControllerGetAxis(s_controller, SDL_GameControllerAxis(i));
    XINPUT_GAMEPAD mapped;
    Bo1_MapGamepadState(buttons, axes, &mapped);

    bool rumble;
    Uint16 low, high;
    {
        std::lock_guard<std::mutex> lock(s_lock);
        if (!s_connected || std::memcmp(&mapped, &s_state, sizeof(mapped)) != 0)
            ++s_packet;
        s_state = mapped;
        s_connected = true;
        rumble = s_rumbleWanted;
        s_rumbleWanted = false;
        low = s_rumbleLow;
        high = s_rumbleHigh;
    }
    if (rumble)
        SDL_GameControllerRumble(s_controller, low, high, 0); // 0: until the next request, as XInput holds it
}

extern "C" {

DWORD XInputGetState(DWORD dwUserIndex, XINPUT_STATE *pState)
{
    if (dwUserIndex != 0 || !pState)
        return ERROR_DEVICE_NOT_CONNECTED;
    std::lock_guard<std::mutex> lock(s_lock);
    if (!s_connected)
        return ERROR_DEVICE_NOT_CONNECTED;
    pState->dwPacketNumber = s_packet;
    pState->Gamepad = s_state;
    return ERROR_SUCCESS;
}

DWORD XInputSetState(DWORD dwUserIndex, XINPUT_VIBRATION *pVibration)
{
    if (dwUserIndex != 0 || !pVibration)
        return ERROR_DEVICE_NOT_CONNECTED;
    std::lock_guard<std::mutex> lock(s_lock);
    if (!s_connected)
        return ERROR_DEVICE_NOT_CONNECTED;
    s_rumbleWanted = true;
    s_rumbleLow = pVibration->wLeftMotorSpeed;
    s_rumbleHigh = pVibration->wRightMotorSpeed;
    return ERROR_SUCCESS;
}

DWORD XInputGetCapabilities(DWORD dwUserIndex, DWORD, XINPUT_CAPABILITIES *pCapabilities)
{
    if (dwUserIndex != 0 || !pCapabilities)
        return ERROR_DEVICE_NOT_CONNECTED;
    std::lock_guard<std::mutex> lock(s_lock);
    if (!s_connected)
        return ERROR_DEVICE_NOT_CONNECTED;
    std::memset(pCapabilities, 0, sizeof(*pCapabilities));
    pCapabilities->Type = 1;    // XINPUT_DEVTYPE_GAMEPAD
    pCapabilities->SubType = 1; // XINPUT_DEVSUBTYPE_GAMEPAD
    // The engine only checks that the call succeeds; report every button and axis, and both motors.
    pCapabilities->Gamepad.wButtons = 0xF3FF;
    pCapabilities->Gamepad.bLeftTrigger = 0xFF;
    pCapabilities->Gamepad.bRightTrigger = 0xFF;
    pCapabilities->Gamepad.sThumbLX = SHORT(0xFFC0);
    pCapabilities->Gamepad.sThumbLY = SHORT(0xFFC0);
    pCapabilities->Gamepad.sThumbRX = SHORT(0xFFC0);
    pCapabilities->Gamepad.sThumbRY = SHORT(0xFFC0);
    pCapabilities->Vibration.wLeftMotorSpeed = 0xFF;
    pCapabilities->Vibration.wRightMotorSpeed = 0xFF;
    return ERROR_SUCCESS;
}

} // extern "C"
