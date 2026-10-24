// The controller mapping (src/platform/posix/win32_gamepad.h): SDL_GameController button and axis state to XInput
// values. Pure, so it needs no device: ./gamepad_test in the build folder.

#include <platform/posix/win32_gamepad.h>

#include <cstdio>
#include <cstring>

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

XINPUT_GAMEPAD Map(const Uint8 buttons[SDL_CONTROLLER_BUTTON_MAX], const Sint16 axes[SDL_CONTROLLER_AXIS_MAX])
{
    XINPUT_GAMEPAD out;
    std::memset(&out, 0, sizeof(out));
    Bo1_MapGamepadState(buttons, axes, &out);
    return out;
}
} // namespace

int main()
{
    Uint8 none[SDL_CONTROLLER_BUTTON_MAX] = {};
    Sint16 zero[SDL_CONTROLLER_AXIS_MAX] = {};

    XINPUT_GAMEPAD idle = Map(none, zero);
    Check(idle.wButtons == 0 && idle.bLeftTrigger == 0 && idle.bRightTrigger == 0 && idle.sThumbLX == 0 &&
              idle.sThumbLY == 0 && idle.sThumbRX == 0 && idle.sThumbRY == 0,
          "nothing pressed maps to an all-zero gamepad");

    struct
    {
        SDL_GameControllerButton sdl;
        unsigned short bit;
        const char *name;
    } cases[] = {
        {SDL_CONTROLLER_BUTTON_A, BO1_XINPUT_A, "A"},
        {SDL_CONTROLLER_BUTTON_B, BO1_XINPUT_B, "B"},
        {SDL_CONTROLLER_BUTTON_X, BO1_XINPUT_X, "X"},
        {SDL_CONTROLLER_BUTTON_Y, BO1_XINPUT_Y, "Y"},
        {SDL_CONTROLLER_BUTTON_BACK, BO1_XINPUT_BACK, "Back"},
        {SDL_CONTROLLER_BUTTON_START, BO1_XINPUT_START, "Start"},
        {SDL_CONTROLLER_BUTTON_LEFTSTICK, BO1_XINPUT_LEFT_THUMB, "Left thumb"},
        {SDL_CONTROLLER_BUTTON_RIGHTSTICK, BO1_XINPUT_RIGHT_THUMB, "Right thumb"},
        {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, BO1_XINPUT_LEFT_SHOULDER, "Left shoulder"},
        {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, BO1_XINPUT_RIGHT_SHOULDER, "Right shoulder"},
        {SDL_CONTROLLER_BUTTON_DPAD_UP, BO1_XINPUT_DPAD_UP, "D-pad up"},
        {SDL_CONTROLLER_BUTTON_DPAD_DOWN, BO1_XINPUT_DPAD_DOWN, "D-pad down"},
        {SDL_CONTROLLER_BUTTON_DPAD_LEFT, BO1_XINPUT_DPAD_LEFT, "D-pad left"},
        {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, BO1_XINPUT_DPAD_RIGHT, "D-pad right"},
    };
    for (const auto &c : cases)
    {
        Uint8 b[SDL_CONTROLLER_BUTTON_MAX] = {};
        b[c.sdl] = 1;
        char what[64];
        std::snprintf(what, sizeof(what), "%s maps to its XInput bit alone", c.name);
        Check(Map(b, zero).wButtons == c.bit, what);
    }

    // The guide button has no XInput bit.
    {
        Uint8 b[SDL_CONTROLLER_BUTTON_MAX] = {};
        b[SDL_CONTROLLER_BUTTON_GUIDE] = 1;
        Check(Map(b, zero).wButtons == 0, "the guide button has no XInput bit");
    }

    // Triggers: SDL 0..32767 to XInput 0..255.
    {
        Sint16 a[SDL_CONTROLLER_AXIS_MAX] = {};
        a[SDL_CONTROLLER_AXIS_TRIGGERLEFT] = 32767;
        a[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] = 16384;
        XINPUT_GAMEPAD g = Map(none, a);
        Check(g.bLeftTrigger == 255 && g.bRightTrigger == 128, "triggers scale from 0..32767 to 0..255");
    }

    // Sticks: X through, Y pointing up (SDL points down), and -32768 does not overflow.
    {
        Sint16 a[SDL_CONTROLLER_AXIS_MAX] = {};
        a[SDL_CONTROLLER_AXIS_LEFTX] = 12345;
        a[SDL_CONTROLLER_AXIS_LEFTY] = 10000;
        a[SDL_CONTROLLER_AXIS_RIGHTX] = -4000;
        a[SDL_CONTROLLER_AXIS_RIGHTY] = -32768;
        XINPUT_GAMEPAD g = Map(none, a);
        Check(g.sThumbLX == 12345, "left stick X passes through");
        Check(g.sThumbLY == -10000, "left stick Y points up (negated)");
        Check(g.sThumbRX == -4000, "right stick X passes through");
        Check(g.sThumbRY == 32767, "SDL -32768 on Y maps to 32767, not an overflow");
    }

    std::printf("gamepad_test: %s\n", g_failures ? "FAILURES" : "all checks passed");
    return g_failures ? 1 : 0;
}
