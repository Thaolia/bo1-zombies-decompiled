#pragma once

// The pure part of the controller layer (win32_gamepad.cpp): SDL_GameController button and axis state turned into an
// XInput gamepad, inline so tests/platform can check it without the SDL device code. The engine reads the raw XInput
// values (`win_gamepad.cpp`: `digitals = wButtons`, its own dead zones on the sticks and triggers), so this only
// translates, it does not filter.

#include "../gamepad_remap.h"

#include <XInput.h>

#include <SDL.h>

// The standard XInput button bits (not in our XInput.h shim, which only declares the API). The engine's GPAD button
// constants are these bits.
enum
{
    BO1_XINPUT_DPAD_UP = 0x0001,
    BO1_XINPUT_DPAD_DOWN = 0x0002,
    BO1_XINPUT_DPAD_LEFT = 0x0004,
    BO1_XINPUT_DPAD_RIGHT = 0x0008,
    BO1_XINPUT_START = 0x0010,
    BO1_XINPUT_BACK = 0x0020,
    BO1_XINPUT_LEFT_THUMB = 0x0040,
    BO1_XINPUT_RIGHT_THUMB = 0x0080,
    BO1_XINPUT_LEFT_SHOULDER = 0x0100,
    BO1_XINPUT_RIGHT_SHOULDER = 0x0200,
    BO1_XINPUT_A = 0x1000,
    BO1_XINPUT_B = 0x2000,
    BO1_XINPUT_X = 0x4000,
    BO1_XINPUT_Y = 0x8000,
};

// `buttons[i]` is non-zero when SDL button i is down; `axes[i]` is SDL axis i. Fills `out` with the XInput values:
// the button bitmask, triggers 0-255, sticks -32768..32767 with Y pointing up as XInput does (SDL's points down).
// `remap` (gamepad_remap.h) says which physical control drives each game control; null is the identity.
inline void Bo1_MapGamepadState(const Uint8 buttons[SDL_CONTROLLER_BUTTON_MAX],
                                const Sint16 axes[SDL_CONTROLLER_AXIS_MAX], XINPUT_GAMEPAD *out,
                                const Bo1PadRemap *remap = nullptr)
{
    // The SDL button behind each physical control (Bo1PadControl order); -1 for the two triggers, which are axes.
    static const int kPhysical[BO1_PAD_COUNT] = {
#if defined(__SWITCH__)
        // SDL2-switch names the face buttons by position, as on an Xbox pad (its A is the console's bottom B, its X
        // the console's left Y), while the game's prompts name the letter to press ("press X to rebuild"): a player
        // pressed the console's X and got the Xbox Y. Read them by the letters printed on the Joy-Cons instead.
        SDL_CONTROLLER_BUTTON_B,
        SDL_CONTROLLER_BUTTON_A,
        SDL_CONTROLLER_BUTTON_Y,
        SDL_CONTROLLER_BUTTON_X,
#else
        SDL_CONTROLLER_BUTTON_A,
        SDL_CONTROLLER_BUTTON_B,
        SDL_CONTROLLER_BUTTON_X,
        SDL_CONTROLLER_BUTTON_Y,
#endif
        SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
        SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
        -1,
        -1,
        SDL_CONTROLLER_BUTTON_START,
        SDL_CONTROLLER_BUTTON_BACK,
        SDL_CONTROLLER_BUTTON_LEFTSTICK,
        SDL_CONTROLLER_BUTTON_RIGHTSTICK,
        SDL_CONTROLLER_BUTTON_DPAD_UP,
        SDL_CONTROLLER_BUTTON_DPAD_DOWN,
        SDL_CONTROLLER_BUTTON_DPAD_LEFT,
        SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
    };
    // The XInput bit of each game control; 0 for the triggers, which are values.
    static const unsigned short kBit[BO1_PAD_COUNT] = {
        BO1_XINPUT_A,          BO1_XINPUT_B,           BO1_XINPUT_X,          BO1_XINPUT_Y,
        BO1_XINPUT_LEFT_SHOULDER, BO1_XINPUT_RIGHT_SHOULDER, 0,             0,
        BO1_XINPUT_START,      BO1_XINPUT_BACK,        BO1_XINPUT_LEFT_THUMB, BO1_XINPUT_RIGHT_THUMB,
        BO1_XINPUT_DPAD_UP,    BO1_XINPUT_DPAD_DOWN,   BO1_XINPUT_DPAD_LEFT,  BO1_XINPUT_DPAD_RIGHT,
    };
    // A trigger read as a button is down past XInput's own XINPUT_GAMEPAD_TRIGGER_THRESHOLD.
    const int kTriggerPressed = 30;

    // SDL triggers are 0..32767; XInput's are 0..255 (32767 >> 7 == 255).
    auto trigger = [](Sint16 v) -> BYTE { return v > 0 ? BYTE(v >> 7) : 0; };
    BYTE physical[BO1_PAD_COUNT];
    for (int i = 0; i < BO1_PAD_COUNT; ++i)
        physical[i] = kPhysical[i] >= 0 ? (buttons[kPhysical[i]] ? 255 : 0) : 0;
    physical[BO1_PAD_LTRIGGER] = trigger(axes[SDL_CONTROLLER_AXIS_TRIGGERLEFT]);
    physical[BO1_PAD_RTRIGGER] = trigger(axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT]);

    unsigned short mask = 0;
    for (int i = 0; i < BO1_PAD_COUNT; ++i)
    {
        const BYTE value = physical[remap ? remap->source[i] : i];
        if (i == BO1_PAD_LTRIGGER)
            out->bLeftTrigger = value;
        else if (i == BO1_PAD_RTRIGGER)
            out->bRightTrigger = value;
        else if (value >= kTriggerPressed)
            mask |= kBit[i];
    }
    out->wButtons = mask;

    // Up is positive in XInput, down in SDL: negate Y, guarding the -32768 that negates out of range.
    auto flipY = [](Sint16 v) -> SHORT { return v == -32768 ? SHORT(32767) : SHORT(-v); };
    out->sThumbLX = axes[SDL_CONTROLLER_AXIS_LEFTX];
    out->sThumbLY = flipY(axes[SDL_CONTROLLER_AXIS_LEFTY]);
    out->sThumbRX = axes[SDL_CONTROLLER_AXIS_RIGHTX];
    out->sThumbRY = flipY(axes[SDL_CONTROLLER_AXIS_RIGHTY]);
}
