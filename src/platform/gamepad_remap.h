#pragma once

// Which physical pad control feeds each control the game reads, for the posix controller layer
// (src/platform/posix/win32_gamepad.h) and the Switch start menu, which lets the player choose it
// (src/platform/switch/launcher_config.cpp). No SDL or libnx in here: the menu and the host tests include it alone.
//
// Both sides use the same list. A physical control is named by the label printed on the pad (on the Switch the right
// face button is A, as the console says, although SDL2-switch calls it B); a game control is the XInput control the
// engine reads, which its binds name BUTTON_A, BUTTON_RTRIG...

enum Bo1PadControl
{
    BO1_PAD_A,
    BO1_PAD_B,
    BO1_PAD_X,
    BO1_PAD_Y,
    BO1_PAD_LSHOULDER, // Switch L, Xbox LB
    BO1_PAD_RSHOULDER, // Switch R, Xbox RB
    BO1_PAD_LTRIGGER,  // Switch ZL, Xbox LT
    BO1_PAD_RTRIGGER,  // Switch ZR, Xbox RT
    BO1_PAD_START,     // Switch +
    BO1_PAD_BACK,      // Switch -
    BO1_PAD_LSTICK,    // left stick pressed
    BO1_PAD_RSTICK,    // right stick pressed
    BO1_PAD_DPAD_UP,
    BO1_PAD_DPAD_DOWN,
    BO1_PAD_DPAD_LEFT,
    BO1_PAD_DPAD_RIGHT,
    BO1_PAD_COUNT
};

// source[game control] = the physical control that drives it. Always a permutation of the controls: every game
// control keeps exactly one physical control, so the menus' A and B stay reachable whatever the player chose.
struct Bo1PadRemap
{
    unsigned char source[BO1_PAD_COUNT];
};

inline void Bo1_PadRemapIdentity(Bo1PadRemap *remap)
{
    for (int i = 0; i < BO1_PAD_COUNT; ++i)
        remap->source[i] = (unsigned char)i;
}

inline bool Bo1_PadRemapValid(const Bo1PadRemap &remap)
{
    bool used[BO1_PAD_COUNT] = {};
    for (int i = 0; i < BO1_PAD_COUNT; ++i)
    {
        const int s = remap.source[i];
        if (s >= BO1_PAD_COUNT || used[s])
            return false;
        used[s] = true;
    }
    return true;
}

inline bool Bo1_PadRemapIsIdentity(const Bo1PadRemap &remap)
{
    for (int i = 0; i < BO1_PAD_COUNT; ++i)
        if (remap.source[i] != i)
            return false;
    return true;
}

// Gives game control `control` the physical control `physical`; the game control that had `physical` takes
// `control`'s old one, so the table stays a permutation.
inline void Bo1_PadRemapAssign(Bo1PadRemap *remap, int control, int physical)
{
    for (int i = 0; i < BO1_PAD_COUNT; ++i)
        if (remap->source[i] == physical)
        {
            remap->source[i] = remap->source[control];
            break;
        }
    remap->source[control] = (unsigned char)physical;
}

// The posix controller layer's table, applied from the next pad read on (win32_gamepad.cpp). False, and the table in
// use kept, for a table that is not a permutation.
bool Bo1_SetGamepadRemap(const Bo1PadRemap &remap);
