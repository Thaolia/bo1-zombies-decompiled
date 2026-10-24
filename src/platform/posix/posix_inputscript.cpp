// Scripted input for tests: BO1_INPUT_SCRIPT="ms action args; ..." makes SDL events that many milliseconds after the
// game window first has the keyboard and hands them to the input layer as SDL's queue would, so they take the path a
// real keyboard and mouse take from there (win32_input.cpp, then the engine's window procedure). They do not go
// through SDL_PushEvent: sdl2-compat's conversion of a hand-made key event to SDL3 faults. It needs a shown window:
// run a client under SDL_VIDEODRIVER=offscreen, which shows nothing on the user's display. Actions:
//   key NAME | down NAME | up NAME    an SDL scancode name (A, `, Return, Backspace, Left Shift, F1)
//   text STRING                       typed text, up to the next ';'
//   move DX DY                        relative mouse motion
//   click left|right|middle|x1|x2     a press and a release
//   wheel N                           N notches, negative towards the user
//   clipboard STRING                  puts the text on the clipboard, for a paste to read
// The engine drops input while it loads (Sys_LoadingKeepAlive, DB_Sleep), and nothing here tells loading from playing
// (the main loop reads the mouse during the asynchronous fastfile loads too): give the steps times well after the map
// load.

#include "posix_video.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

void Com_Printf(int channel, const char *fmt, ...);

namespace
{
constexpr int kConsoleChannel = 16;

struct Step
{
    int at;
    std::string action;
    std::string argument;
};

struct Script
{
    std::vector<Step> steps;
    size_t next = 0;
    Uint32 start = 0;
    bool started = false;
    Uint16 modifiers = KMOD_NONE;
    int x = 0, y = 0;
};

std::vector<Step> Parse(const char *text)
{
    std::vector<Step> steps;
    const char *p = text;
    while (*p)
    {
        const char *end = strchr(p, ';');
        std::string item(p, end ? size_t(end - p) : strlen(p));
        p = end ? end + 1 : p + strlen(p);
        const size_t first = item.find_first_not_of(' ');
        if (first == std::string::npos)
            continue;
        item.erase(0, first);
        char *rest;
        const long at = strtol(item.c_str(), &rest, 10);
        while (*rest == ' ')
            ++rest;
        std::string action(rest, strcspn(rest, " "));
        const char *argument = rest + action.size();
        if (*argument == ' ')
            ++argument;
        steps.push_back({int(at), action, argument});
    }
    return steps;
}

Uint16 ModifierOf(SDL_Scancode sc)
{
    switch (sc)
    {
    case SDL_SCANCODE_LSHIFT: return KMOD_LSHIFT;
    case SDL_SCANCODE_RSHIFT: return KMOD_RSHIFT;
    case SDL_SCANCODE_LCTRL: return KMOD_LCTRL;
    case SDL_SCANCODE_RCTRL: return KMOD_RCTRL;
    case SDL_SCANCODE_LALT: return KMOD_LALT;
    case SDL_SCANCODE_RALT: return KMOD_RALT;
    default: return KMOD_NONE;
    }
}

void PushKey(Script &s, Uint32 window, SDL_Scancode sc, bool down)
{
    if (down)
        s.modifiers |= ModifierOf(sc);
    else
        s.modifiers &= Uint16(~ModifierOf(sc));
    SDL_Event e = {};
    e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    e.key.timestamp = SDL_GetTicks();
    e.key.windowID = window;
    e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    e.key.keysym.scancode = sc;
    e.key.keysym.sym = SDL_GetKeyFromScancode(sc);
    e.key.keysym.mod = s.modifiers;
    Bo1_InputEvent(e);
}

void PushButton(Uint32 window, Uint8 button, bool down, int x, int y)
{
    SDL_Event e = {};
    e.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
    e.button.timestamp = SDL_GetTicks();
    e.button.windowID = window;
    e.button.button = button;
    e.button.state = down ? SDL_PRESSED : SDL_RELEASED;
    e.button.clicks = 1;
    e.button.x = x;
    e.button.y = y;
    Bo1_InputEvent(e);
}

bool Run(Script &s, const Step &step, Uint32 window)
{
    if (step.action == "key" || step.action == "down" || step.action == "up")
    {
        const SDL_Scancode sc = SDL_GetScancodeFromName(step.argument.c_str());
        if (sc == SDL_SCANCODE_UNKNOWN)
            return false;
        if (step.action != "up")
            PushKey(s, window, sc, true);
        if (step.action != "down")
            PushKey(s, window, sc, false);
        return true;
    }
    if (step.action == "text")
    {
        // SDL's text events hold at most 31 bytes; split on character boundaries.
        for (size_t i = 0; i < step.argument.size();)
        {
            size_t n = std::min(step.argument.size() - i, sizeof(SDL_TextInputEvent::text) - 1);
            while (n > 0 && i + n < step.argument.size() && (step.argument[i + n] & 0xC0) == 0x80)
                --n;
            SDL_Event e = {};
            e.type = SDL_TEXTINPUT;
            e.text.timestamp = SDL_GetTicks();
            e.text.windowID = window;
            memcpy(e.text.text, step.argument.data() + i, n);
            Bo1_InputEvent(e);
            i += n;
        }
        return true;
    }
    if (step.action == "move")
    {
        int dx = 0, dy = 0;
        if (sscanf(step.argument.c_str(), "%d %d", &dx, &dy) != 2)
            return false;
        s.x += dx;
        s.y += dy;
        SDL_Event e = {};
        e.type = SDL_MOUSEMOTION;
        e.motion.timestamp = SDL_GetTicks();
        e.motion.windowID = window;
        e.motion.x = s.x;
        e.motion.y = s.y;
        e.motion.xrel = dx;
        e.motion.yrel = dy;
        Bo1_InputEvent(e);
        return true;
    }
    if (step.action == "click")
    {
        static const struct
        {
            const char *name;
            Uint8 button;
        } kButtons[] = {{"left", SDL_BUTTON_LEFT},
                        {"right", SDL_BUTTON_RIGHT},
                        {"middle", SDL_BUTTON_MIDDLE},
                        {"x1", SDL_BUTTON_X1},
                        {"x2", SDL_BUTTON_X2}};
        for (const auto &b : kButtons)
        {
            if (step.argument == b.name)
            {
                PushButton(window, b.button, true, s.x, s.y);
                PushButton(window, b.button, false, s.x, s.y);
                return true;
            }
        }
        return false;
    }
    if (step.action == "clipboard")
        return SDL_SetClipboardText(step.argument.c_str()) == 0;
    if (step.action == "wheel")
    {
        SDL_Event e = {};
        e.type = SDL_MOUSEWHEEL;
        e.wheel.timestamp = SDL_GetTicks();
        e.wheel.windowID = window;
        e.wheel.y = atoi(step.argument.c_str());
        e.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
        Bo1_InputEvent(e);
        return true;
    }
    return false;
}
} // namespace

void Bo1_InputScriptPump()
{
    static Script *script = [] {
        const char *text = getenv("BO1_INPUT_SCRIPT");
        Script *s = new Script();
        if (text)
            s->steps = Parse(text);
        return s;
    }();
    if (script->next >= script->steps.size())
        return;
    const HWND foreground = GetForegroundWindow();
    SDL_Window *window = foreground ? Bo1_NativeWindow(foreground) : nullptr;
    if (!window)
        return;
    if (!script->started)
    {
        script->started = true;
        script->start = SDL_GetTicks();
        int w, h;
        SDL_GetWindowSize(window, &w, &h);
        script->x = w / 2;
        script->y = h / 2;
    }
    const int now = int(SDL_GetTicks() - script->start);
    while (script->next < script->steps.size() && script->steps[script->next].at <= now)
    {
        const Step &step = script->steps[script->next++];
        const bool done = Run(*script, step, SDL_GetWindowID(window));
        Com_Printf(kConsoleChannel, "bo1_input: %d ms: %s %s%s\n", now, step.action.c_str(), step.argument.c_str(),
                   done ? "" : " (not understood)");
    }
}
