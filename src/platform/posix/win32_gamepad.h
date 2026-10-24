#pragma once

// The pure part of the controller layer (win32_gamepad.cpp): SDL_GameController button and axis state turned into an
// XInput gamepad, inline so tests/platform can check it without the SDL device code. The engine reads the raw XInput
// values (`win_gamepad.cpp`: `digitals = wButtons`, its own dead zones on the sticks and triggers), so this only
// translates, it does not filter.

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
inline void Bo1_MapGamepadState(const Uint8 buttons[SDL_CONTROLLER_BUTTON_MAX],
                                const Sint16 axes[SDL_CONTROLLER_AXIS_MAX], XINPUT_GAMEPAD *out)
{
    static const struct
    {
        SDL_GameControllerButton sdl;
        unsigned short bit;
    } kButtons[] = {
        {SDL_CONTROLLER_BUTTON_A, BO1_XINPUT_A},
        {SDL_CONTROLLER_BUTTON_B, BO1_XINPUT_B},
        {SDL_CONTROLLER_BUTTON_X, BO1_XINPUT_X},
        {SDL_CONTROLLER_BUTTON_Y, BO1_XINPUT_Y},
        {SDL_CONTROLLER_BUTTON_BACK, BO1_XINPUT_BACK},
        {SDL_CONTROLLER_BUTTON_START, BO1_XINPUT_START},
        {SDL_CONTROLLER_BUTTON_LEFTSTICK, BO1_XINPUT_LEFT_THUMB},
        {SDL_CONTROLLER_BUTTON_RIGHTSTICK, BO1_XINPUT_RIGHT_THUMB},
        {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, BO1_XINPUT_LEFT_SHOULDER},
        {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, BO1_XINPUT_RIGHT_SHOULDER},
        {SDL_CONTROLLER_BUTTON_DPAD_UP, BO1_XINPUT_DPAD_UP},
        {SDL_CONTROLLER_BUTTON_DPAD_DOWN, BO1_XINPUT_DPAD_DOWN},
        {SDL_CONTROLLER_BUTTON_DPAD_LEFT, BO1_XINPUT_DPAD_LEFT},
        {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, BO1_XINPUT_DPAD_RIGHT},
    };
    unsigned short mask = 0;
    for (const auto &b : kButtons)
        if (buttons[b.sdl])
            mask |= b.bit;
    out->wButtons = mask;

    // SDL triggers are 0..32767; XInput's are 0..255 (32767 >> 7 == 255).
    auto trigger = [](Sint16 v) -> BYTE { return v > 0 ? BYTE(v >> 7) : 0; };
    out->bLeftTrigger = trigger(axes[SDL_CONTROLLER_AXIS_TRIGGERLEFT]);
    out->bRightTrigger = trigger(axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT]);

    // Up is positive in XInput, down in SDL: negate Y, guarding the -32768 that negates out of range.
    auto flipY = [](Sint16 v) -> SHORT { return v == -32768 ? SHORT(32767) : SHORT(-v); };
    out->sThumbLX = axes[SDL_CONTROLLER_AXIS_LEFTX];
    out->sThumbLY = flipY(axes[SDL_CONTROLLER_AXIS_LEFTY]);
    out->sThumbRX = axes[SDL_CONTROLLER_AXIS_RIGHTX];
    out->sThumbRY = flipY(axes[SDL_CONTROLLER_AXIS_RIGHTY]);
}
