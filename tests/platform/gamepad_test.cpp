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

XINPUT_GAMEPAD Map(const Uint8 buttons[SDL_CONTROLLER_BUTTON_MAX], const Sint16 axes[SDL_CONTROLLER_AXIS_MAX],
                   const Bo1PadRemap *remap = nullptr)
{
    XINPUT_GAMEPAD out;
    std::memset(&out, 0, sizeof(out));
    Bo1_MapGamepadState(buttons, axes, &out, remap);
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

    // The remap table (gamepad_remap.h): the identity changes nothing.
    {
        Bo1PadRemap identity;
        Bo1_PadRemapIdentity(&identity);
        bool same = true;
        for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX; ++i)
        {
            Uint8 b[SDL_CONTROLLER_BUTTON_MAX] = {};
            b[i] = 1;
            Sint16 a[SDL_CONTROLLER_AXIS_MAX] = {};
            a[SDL_CONTROLLER_AXIS_TRIGGERLEFT] = 20000;
            const XINPUT_GAMEPAD x = Map(b, a), y = Map(b, a, &identity);
            same = same && std::memcmp(&x, &y, sizeof(x)) == 0;
        }
        Check(same, "the identity remap gives what no remap gives, button by button");
    }

    // Fire on the A button: the game's right trigger comes from A, and the game's A from the right trigger.
    {
        Bo1PadRemap remap;
        Bo1_PadRemapIdentity(&remap);
        Bo1_PadRemapAssign(&remap, BO1_PAD_RTRIGGER, BO1_PAD_A);
        Check(Bo1_PadRemapValid(remap) && remap.source[BO1_PAD_A] == BO1_PAD_RTRIGGER,
              "assigning swaps: the game's A takes the right trigger");
        Uint8 b[SDL_CONTROLLER_BUTTON_MAX] = {};
        b[SDL_CONTROLLER_BUTTON_A] = 1; // the host's physical A (the Switch reads the label, SDL's B)
        XINPUT_GAMEPAD g = Map(b, zero, &remap);
        Check(g.bRightTrigger == 255 && g.wButtons == 0, "the A button pulls the game's right trigger fully");
        Sint16 a[SDL_CONTROLLER_AXIS_MAX] = {};
        a[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] = 32767;
        g = Map(none, a, &remap);
        Check(g.wButtons == BO1_XINPUT_A && g.bRightTrigger == 0, "the right trigger presses the game's A");
        a[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] = 29 << 7;
        Check(Map(none, a, &remap).wButtons == 0, "a trigger below XInput's threshold (30) is not a press");
        a[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] = 30 << 7;
        Check(Map(none, a, &remap).wButtons == BO1_XINPUT_A, "a trigger at the threshold is a press");
    }

    // Two triggers swapped keep their analog values.
    {
        Bo1PadRemap remap;
        Bo1_PadRemapIdentity(&remap);
        Bo1_PadRemapAssign(&remap, BO1_PAD_LTRIGGER, BO1_PAD_RTRIGGER);
        Sint16 a[SDL_CONTROLLER_AXIS_MAX] = {};
        a[SDL_CONTROLLER_AXIS_TRIGGERLEFT] = 8192;
        a[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] = 32767;
        XINPUT_GAMEPAD g = Map(none, a, &remap);
        Check(g.bLeftTrigger == 255 && g.bRightTrigger == 64, "swapped triggers keep their analog values");
    }

    // A table that is not a permutation.
    {
        Bo1PadRemap remap;
        Bo1_PadRemapIdentity(&remap);
        remap.source[BO1_PAD_B] = BO1_PAD_A;
        Check(!Bo1_PadRemapValid(remap), "two game controls on one button are refused");
        remap.source[BO1_PAD_B] = BO1_PAD_COUNT;
        Check(!Bo1_PadRemapValid(remap), "a control past the list is refused");
    }

    std::printf("gamepad_test: %s\n", g_failures ? "FAILURES" : "all checks passed");
    return g_failures ? 1 : 0;
}
