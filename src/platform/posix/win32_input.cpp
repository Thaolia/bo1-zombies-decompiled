// Keyboard, mouse, cursor and clipboard on POSIX, on SDL2: SDL's input events become the messages Windows posts for
// them (virtual-key codes, set 1 scan codes and the extended-key flag in WM_KEYDOWN's lParam, WM_CHAR in the ANSI
// code page, the MK_ button flags of the mouse messages), and the Win32 calls the engine reads input with answer from
// that state. The engine reads mouse movement by polling the cursor (GetCursorPos, then SetCursorPos back to the
// window's centre); while it hides the cursor over its focused window, the cursor here is virtual and moves by SDL's
// relative motion, in SDL's relative mouse mode where the video driver has one.

#include "posix_video.h"
#include "win32_handles.h"

#include <SDL.h>
#include <stdio.h>
#include <string.h>

#include <deque>
#include <mutex>
#include <string>

bool __cdecl Sys_IsHeadless();

namespace
{
// The messages, flags and virtual keys this file sends or reads (winuser.h).
constexpr UINT kWmKeyDown = 0x0100, kWmKeyUp = 0x0101, kWmChar = 0x0102, kWmSysKeyDown = 0x0104,
               kWmSysKeyUp = 0x0105, kWmSysChar = 0x0106, kWmMouseMove = 0x0200, kWmLButtonDown = 0x0201,
               kWmLButtonUp = 0x0202, kWmRButtonDown = 0x0204, kWmRButtonUp = 0x0205, kWmMButtonDown = 0x0207,
               kWmMButtonUp = 0x0208, kWmMouseWheel = 0x020A, kWmXButtonDown = 0x020B, kWmXButtonUp = 0x020C;
constexpr WPARAM kMkLButton = 0x1, kMkRButton = 0x2, kMkShift = 0x4, kMkControl = 0x8, kMkMButton = 0x10,
                 kMkXButton1 = 0x20, kMkXButton2 = 0x40;
constexpr int kWheelDelta = 120;
constexpr UINT kCfText = 1;
constexpr UINT kMapVkToChar = 2;
constexpr uint8_t kVkLButton = 0x01, kVkRButton = 0x02, kVkMButton = 0x04, kVkXButton1 = 0x05, kVkXButton2 = 0x06,
                  kVkBack = 0x08, kVkTab = 0x09, kVkClear = 0x0C, kVkReturn = 0x0D, kVkShift = 0x10,
                  kVkControl = 0x11, kVkMenu = 0x12, kVkPause = 0x13, kVkCapital = 0x14, kVkEscape = 0x1B,
                  kVkSpace = 0x20, kVkPrior = 0x21, kVkNext = 0x22, kVkEnd = 0x23, kVkHome = 0x24, kVkLeft = 0x25,
                  kVkUp = 0x26, kVkRight = 0x27, kVkDown = 0x28, kVkSnapshot = 0x2C, kVkInsert = 0x2D,
                  kVkDelete = 0x2E, kVkLWin = 0x5B, kVkRWin = 0x5C, kVkApps = 0x5D, kVkNumpad0 = 0x60,
                  kVkMultiply = 0x6A, kVkAdd = 0x6B, kVkSubtract = 0x6D, kVkDecimal = 0x6E, kVkDivide = 0x6F,
                  kVkF1 = 0x70, kVkF4 = 0x73, kVkF10 = 0x79, kVkNumLock = 0x90, kVkScroll = 0x91, kVkLShift = 0xA0,
                  kVkRShift = 0xA1, kVkLControl = 0xA2, kVkRControl = 0xA3, kVkLMenu = 0xA4, kVkRMenu = 0xA5,
                  kVkOem1 = 0xBA, kVkOemPlus = 0xBB, kVkOemComma = 0xBC, kVkOemMinus = 0xBD, kVkOemPeriod = 0xBE,
                  kVkOem2 = 0xBF, kVkOem3 = 0xC0, kVkOem4 = 0xDB, kVkOem5 = 0xDC, kVkOem6 = 0xDD, kVkOem7 = 0xDE,
                  kVkOem102 = 0xE2;

// A key as Windows reports it: virtual-key code, set 1 scan code, and whether it is one of the enhanced keyboard's
// extra keys (bit 24 of lParam).
struct Key
{
    uint8_t vk;
    uint8_t scan;
    bool extended;
};

// The keypad sends its digit keys with Num Lock on, the navigation keys printed under them otherwise.
Key KeypadKey(uint8_t digitVk, uint8_t navigationVk, uint8_t scan, bool numLock)
{
    return {numLock ? digitVk : navigationVk, scan, false};
}

// By position, the US layout's keys; the layout's letters move the letter keys' codes (KeyOf).
Key PositionKey(SDL_Scancode sc, bool numLock)
{
    static constexpr uint8_t kLetterScan[26] = {0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17,
                                                0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13,
                                                0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C};
    static constexpr uint8_t kFunctionScan[12] = {0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40,
                                                  0x41, 0x42, 0x43, 0x44, 0x57, 0x58};
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z)
        return {uint8_t('A' + (sc - SDL_SCANCODE_A)), kLetterScan[sc - SDL_SCANCODE_A], false};
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9)
        return {uint8_t('1' + (sc - SDL_SCANCODE_1)), uint8_t(0x02 + (sc - SDL_SCANCODE_1)), false};
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12)
        return {uint8_t(kVkF1 + (sc - SDL_SCANCODE_F1)), kFunctionScan[sc - SDL_SCANCODE_F1], false};
    switch (sc)
    {
    case SDL_SCANCODE_0: return {'0', 0x0B, false};
    case SDL_SCANCODE_RETURN: return {kVkReturn, 0x1C, false};
    case SDL_SCANCODE_ESCAPE: return {kVkEscape, 0x01, false};
    case SDL_SCANCODE_BACKSPACE: return {kVkBack, 0x0E, false};
    case SDL_SCANCODE_TAB: return {kVkTab, 0x0F, false};
    case SDL_SCANCODE_SPACE: return {kVkSpace, 0x39, false};
    case SDL_SCANCODE_MINUS: return {kVkOemMinus, 0x0C, false};
    case SDL_SCANCODE_EQUALS: return {kVkOemPlus, 0x0D, false};
    case SDL_SCANCODE_LEFTBRACKET: return {kVkOem4, 0x1A, false};
    case SDL_SCANCODE_RIGHTBRACKET: return {kVkOem6, 0x1B, false};
    case SDL_SCANCODE_BACKSLASH: return {kVkOem5, 0x2B, false};
    case SDL_SCANCODE_NONUSHASH: return {kVkOem5, 0x2B, false};
    case SDL_SCANCODE_SEMICOLON: return {kVkOem1, 0x27, false};
    case SDL_SCANCODE_APOSTROPHE: return {kVkOem7, 0x28, false};
    case SDL_SCANCODE_GRAVE: return {kVkOem3, 0x29, false};
    case SDL_SCANCODE_COMMA: return {kVkOemComma, 0x33, false};
    case SDL_SCANCODE_PERIOD: return {kVkOemPeriod, 0x34, false};
    case SDL_SCANCODE_SLASH: return {kVkOem2, 0x35, false};
    case SDL_SCANCODE_NONUSBACKSLASH: return {kVkOem102, 0x56, false};
    case SDL_SCANCODE_CAPSLOCK: return {kVkCapital, 0x3A, false};
    case SDL_SCANCODE_PRINTSCREEN: return {kVkSnapshot, 0x37, true};
    case SDL_SCANCODE_SCROLLLOCK: return {kVkScroll, 0x46, false};
    case SDL_SCANCODE_PAUSE: return {kVkPause, 0x45, false};
    case SDL_SCANCODE_INSERT: return {kVkInsert, 0x52, true};
    case SDL_SCANCODE_HOME: return {kVkHome, 0x47, true};
    case SDL_SCANCODE_PAGEUP: return {kVkPrior, 0x49, true};
    case SDL_SCANCODE_DELETE: return {kVkDelete, 0x53, true};
    case SDL_SCANCODE_END: return {kVkEnd, 0x4F, true};
    case SDL_SCANCODE_PAGEDOWN: return {kVkNext, 0x51, true};
    case SDL_SCANCODE_RIGHT: return {kVkRight, 0x4D, true};
    case SDL_SCANCODE_LEFT: return {kVkLeft, 0x4B, true};
    case SDL_SCANCODE_DOWN: return {kVkDown, 0x50, true};
    case SDL_SCANCODE_UP: return {kVkUp, 0x48, true};
    case SDL_SCANCODE_NUMLOCKCLEAR: return {kVkNumLock, 0x45, true};
    case SDL_SCANCODE_KP_DIVIDE: return {kVkDivide, 0x35, true};
    case SDL_SCANCODE_KP_MULTIPLY: return {kVkMultiply, 0x37, false};
    case SDL_SCANCODE_KP_MINUS: return {kVkSubtract, 0x4A, false};
    case SDL_SCANCODE_KP_PLUS: return {kVkAdd, 0x4E, false};
    case SDL_SCANCODE_KP_ENTER: return {kVkReturn, 0x1C, true};
    case SDL_SCANCODE_KP_1: return KeypadKey(kVkNumpad0 + 1, kVkEnd, 0x4F, numLock);
    case SDL_SCANCODE_KP_2: return KeypadKey(kVkNumpad0 + 2, kVkDown, 0x50, numLock);
    case SDL_SCANCODE_KP_3: return KeypadKey(kVkNumpad0 + 3, kVkNext, 0x51, numLock);
    case SDL_SCANCODE_KP_4: return KeypadKey(kVkNumpad0 + 4, kVkLeft, 0x4B, numLock);
    case SDL_SCANCODE_KP_5: return KeypadKey(kVkNumpad0 + 5, kVkClear, 0x4C, numLock);
    case SDL_SCANCODE_KP_6: return KeypadKey(kVkNumpad0 + 6, kVkRight, 0x4D, numLock);
    case SDL_SCANCODE_KP_7: return KeypadKey(kVkNumpad0 + 7, kVkHome, 0x47, numLock);
    case SDL_SCANCODE_KP_8: return KeypadKey(kVkNumpad0 + 8, kVkUp, 0x48, numLock);
    case SDL_SCANCODE_KP_9: return KeypadKey(kVkNumpad0 + 9, kVkPrior, 0x49, numLock);
    case SDL_SCANCODE_KP_0: return KeypadKey(kVkNumpad0, kVkInsert, 0x52, numLock);
    case SDL_SCANCODE_KP_PERIOD: return KeypadKey(kVkDecimal, kVkDelete, 0x53, numLock);
    case SDL_SCANCODE_APPLICATION: return {kVkApps, 0x5D, true};
    case SDL_SCANCODE_LCTRL: return {kVkControl, 0x1D, false};
    case SDL_SCANCODE_LSHIFT: return {kVkShift, 0x2A, false};
    case SDL_SCANCODE_LALT: return {kVkMenu, 0x38, false};
    case SDL_SCANCODE_LGUI: return {kVkLWin, 0x5B, true};
    case SDL_SCANCODE_RCTRL: return {kVkControl, 0x1D, true};
    case SDL_SCANCODE_RSHIFT: return {kVkShift, 0x36, false};
    case SDL_SCANCODE_RALT: return {kVkMenu, 0x38, true};
    case SDL_SCANCODE_RGUI: return {kVkRWin, 0x5C, true};
    default: return {0, 0, false};
    }
}

// The US key a character is on, for the layouts that put punctuation where the US layout has a letter.
uint8_t PunctuationVk(SDL_Keycode sym)
{
    switch (sym)
    {
    case ';': return kVkOem1;
    case '=': return kVkOemPlus;
    case ',': return kVkOemComma;
    case '-': return kVkOemMinus;
    case '.': return kVkOemPeriod;
    case '/': return kVkOem2;
    case '`': return kVkOem3;
    case '[': return kVkOem4;
    case '\\': return kVkOem5;
    case ']': return kVkOem6;
    case '\'': return kVkOem7;
    default: return 0;
    }
}

bool IsLetterKey(SDL_Scancode sc)
{
    return sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z;
}

// The virtual-key code follows the layout, as on Windows: the key that types a letter is that letter's key, and a
// letter-position key that types punctuation is that punctuation's OEM key. The scan code stays the key's position.
Key KeyOf(const SDL_Keysym &keysym)
{
    Key key = PositionKey(keysym.scancode, (keysym.mod & KMOD_NUM) != 0);
    const bool letterPosition = IsLetterKey(keysym.scancode);
    if (!letterPosition && !(key.vk >= kVkOem1 && key.vk <= kVkOem102))
        return key;
    const SDL_Keycode sym = keysym.sym;
    if (sym >= 'a' && sym <= 'z')
        key.vk = uint8_t(sym - 'a' + 'A');
    else if (letterPosition && PunctuationVk(sym))
        key.vk = PunctuationVk(sym);
    return key;
}

// The character of a layout key code in Windows-1252, the engine's ANSI code page, or 0.
unsigned char Ansi(uint32_t codePoint)
{
    if (codePoint < 0x80 || (codePoint >= 0xA0 && codePoint <= 0xFF))
        return (unsigned char)codePoint;
    static constexpr struct
    {
        uint16_t codePoint;
        unsigned char ansi;
    } kWindows1252[] = {{0x20AC, 0x80}, {0x201A, 0x82}, {0x0192, 0x83}, {0x201E, 0x84}, {0x2026, 0x85},
                        {0x2020, 0x86}, {0x2021, 0x87}, {0x02C6, 0x88}, {0x2030, 0x89}, {0x0160, 0x8A},
                        {0x2039, 0x8B}, {0x0152, 0x8C}, {0x017D, 0x8E}, {0x2018, 0x91}, {0x2019, 0x92},
                        {0x201C, 0x93}, {0x201D, 0x94}, {0x2022, 0x95}, {0x2013, 0x96}, {0x2014, 0x97},
                        {0x02DC, 0x98}, {0x2122, 0x99}, {0x0161, 0x9A}, {0x203A, 0x9B}, {0x0153, 0x9C},
                        {0x017E, 0x9E}, {0x0178, 0x9F}};
    for (const auto &c : kWindows1252)
        if (c.codePoint == codePoint)
            return c.ansi;
    return 0;
}

// Windows-1252 text from UTF-8; a character the code page lacks becomes '?', as WideCharToMultiByte makes it.
std::string AnsiFromUtf8(const char *text)
{
    std::string out;
    const unsigned char *p = reinterpret_cast<const unsigned char *>(text);
    while (*p)
    {
        uint32_t c = *p++;
        int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
        c &= extra == 3 ? 0x07 : extra == 2 ? 0x0F : extra == 1 ? 0x1F : 0x7F;
        for (; extra > 0 && (*p & 0xC0) == 0x80; --extra)
            c = (c << 6) | (*p++ & 0x3F);
        const unsigned char ansi = Ansi(c);
        out += ansi ? char(ansi) : '?';
    }
    return out;
}

std::string Utf8FromAnsi(const char *text)
{
    static constexpr uint16_t kHigh[32] = {0x20AC, 0x81,   0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                                           0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D,   0x017D, 0x8F,
                                           0x90,   0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                                           0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D,   0x017E, 0x0178};
    std::string out;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(text); *p; ++p)
    {
        const uint32_t c = *p >= 0x80 && *p < 0xA0 ? kHigh[*p - 0x80] : *p;
        if (c < 0x80)
            out += char(c);
        else if (c < 0x800)
        {
            out += char(0xC0 | (c >> 6));
            out += char(0x80 | (c & 0x3F));
        }
        else
        {
            out += char(0xE0 | (c >> 12));
            out += char(0x80 | ((c >> 6) & 0x3F));
            out += char(0x80 | (c & 0x3F));
        }
    }
    return out;
}

struct Input
{
    std::mutex lock;
    bool down[256] = {};
    bool pressedSinceRead[256] = {};
    // The unshifted character each virtual key typed at its last press (MapVirtualKey's MAPVK_VK_TO_CHAR).
    unsigned char vkChar[256] = {};
    POINT cursor = {0, 0};
    bool cursorKnown = false;
    int cursorDisplayCount = 0;
    bool captured = false;
    bool warpPending = false;
    // The character TranslateMessage posts for each key-down message waiting in a queue, decided with the modifiers
    // held when the key went down, as Windows keeps the key state of each message.
    struct Translation
    {
        HWND hwnd;
        UINT message;
        WPARAM wParam;
        LPARAM lParam;
        int character;
    };
    std::deque<Translation> translations;
    // SDL's clipboard may only be used on the video thread, which the engine's main thread must never wait for (the
    // video thread waits for it every frame): the video thread keeps a copy of the clipboard's text, refreshed when
    // SDL reports a change, and sends the text the engine sets at its next pump.
    std::string clipboardText; // UTF-8
    bool clipboardKnown = false;
    bool clipboardOutgoing = false;
    std::string outgoingText;
    bool clipboardOpen = false;
    HGLOBAL clipboardData = nullptr;
};

Input &In()
{
    static Input *input = new Input();
    return *input;
}

using Lock = std::lock_guard<std::mutex>;

bool Headless()
{
    return Sys_IsHeadless();
}

// On the video thread: the clipboard's text, now.
void ReadClipboard()
{
    char *utf8 = SDL_HasClipboardText() ? SDL_GetClipboardText() : nullptr;
    Lock lock(In().lock);
    In().clipboardText = utf8 ? utf8 : "";
    In().clipboardKnown = true;
    SDL_free(utf8);
}

WPARAM ModifierFlags(const Input &in)
{
    WPARAM flags = 0;
    if (in.down[kVkShift])
        flags |= kMkShift;
    if (in.down[kVkControl])
        flags |= kMkControl;
    return flags;
}

WPARAM MouseFlags(const Input &in)
{
    WPARAM flags = ModifierFlags(in);
    if (in.down[kVkLButton])
        flags |= kMkLButton;
    if (in.down[kVkRButton])
        flags |= kMkRButton;
    if (in.down[kVkMButton])
        flags |= kMkMButton;
    if (in.down[kVkXButton1])
        flags |= kMkXButton1;
    if (in.down[kVkXButton2])
        flags |= kMkXButton2;
    return flags;
}

// The left/right virtual keys behind a modifier key, which GetAsyncKeyState also reports.
uint8_t SidedVk(SDL_Scancode sc)
{
    switch (sc)
    {
    case SDL_SCANCODE_LSHIFT: return kVkLShift;
    case SDL_SCANCODE_RSHIFT: return kVkRShift;
    case SDL_SCANCODE_LCTRL: return kVkLControl;
    case SDL_SCANCODE_RCTRL: return kVkRControl;
    case SDL_SCANCODE_LALT: return kVkLMenu;
    case SDL_SCANCODE_RALT: return kVkRMenu;
    default: return 0;
    }
}

void SetDown(Input &in, uint8_t vk, bool isDown)
{
    if (isDown && !in.down[vk])
        in.pressedSinceRead[vk] = true;
    in.down[vk] = isDown;
}

// The control characters a key types: printable ones come from SDL's text input, which knows the layout, dead keys
// and input methods.
int ControlCharacter(WPARAM vk, bool control, bool alt)
{
    if (control && !alt)
    {
        if (vk >= 'A' && vk <= 'Z')
            return int(vk - 'A' + 1);
        if (vk == kVkOem4)
            return 0x1B;
        if (vk == kVkOem5)
            return 0x1C;
        if (vk == kVkOem6)
            return 0x1D;
        if (vk == kVkBack)
            return 0x7F;
        if (vk == kVkReturn)
            return '\n';
        return 0;
    }
    switch (vk)
    {
    case kVkBack: return '\b';
    case kVkTab: return '\t';
    case kVkReturn: return '\r';
    case kVkEscape: return 0x1B;
    default: return 0;
    }
}

void KeyEvent(const SDL_KeyboardEvent &e)
{
    const Key key = KeyOf(e.keysym);
    if (!key.vk)
        return;
    const bool isDown = e.state == SDL_PRESSED;
    HWND hwnd = Bo1_WindowFromSdl(e.windowID);
    if (!hwnd)
        hwnd = Bo1_FocusWindow();
    bool wasDown, altDown, controlDown;
    {
        Lock lock(In().lock);
        Input &in = In();
        wasDown = in.down[key.vk];
        if (const uint8_t sided = SidedVk(e.keysym.scancode))
        {
            SetDown(in, sided, isDown);
            const uint8_t other = sided ^ 1;
            SetDown(in, key.vk, isDown || in.down[other]);
        }
        else
            SetDown(in, key.vk, isDown);
        if (isDown)
        {
            const SDL_Keycode layoutKey = SDL_GetKeyFromScancode(e.keysym.scancode);
            const unsigned char c = layoutKey >= 0 && layoutKey < 0x100 ? Ansi(uint32_t(layoutKey)) : 0;
            in.vkChar[key.vk] = c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c;
        }
        altDown = in.down[kVkMenu];
        controlDown = in.down[kVkControl];
    }
    if (!hwnd)
        return;
    // Alt without Ctrl, and F10, are system keys; AltGr (Ctrl+Alt) types characters.
    const bool system = (altDown && !controlDown) || key.vk == kVkF10 || (key.vk == kVkMenu && !controlDown);
    const UINT message = system ? (isDown ? kWmSysKeyDown : kWmSysKeyUp) : (isDown ? kWmKeyDown : kWmKeyUp);
    uint32_t lParam = 1u | uint32_t(key.scan) << 16;
    if (key.extended)
        lParam |= 1u << 24;
    if (altDown)
        lParam |= 1u << 29;
    if (!isDown || wasDown || e.repeat)
        lParam |= 1u << 30;
    if (!isDown)
        lParam |= 1u << 31;
    if (isDown)
    {
        if (const int c = ControlCharacter(key.vk, controlDown, altDown))
        {
            Lock lock(In().lock);
            In().translations.push_back({hwnd, message, key.vk, LPARAM(lParam), c});
        }
    }
    PostMessageA(hwnd, message, key.vk, LPARAM(lParam));
}

void TextEvent(const SDL_TextInputEvent &e)
{
    HWND hwnd = Bo1_WindowFromSdl(e.windowID);
    if (!hwnd)
        hwnd = Bo1_FocusWindow();
    if (!hwnd)
        return;
    for (char c : AnsiFromUtf8(e.text))
        PostMessageA(hwnd, kWmChar, WPARAM((unsigned char)c), 1);
}

void MotionEvent(const SDL_MouseMotionEvent &e)
{
    HWND hwnd = Bo1_WindowFromSdl(e.windowID);
    RECT rect = {0, 0, 0, 0};
    if (hwnd)
        Bo1_ClientRect(hwnd, &rect);
    WPARAM flags;
    POINT client;
    {
        Lock lock(In().lock);
        Input &in = In();
        if (in.captured)
        {
            in.cursor.x += e.xrel;
            in.cursor.y += e.yrel;
        }
        else
            in.cursor = {rect.left + e.x, rect.top + e.y};
        in.cursorKnown = true;
        flags = MouseFlags(in);
        client = {in.cursor.x - rect.left, in.cursor.y - rect.top};
    }
    if (hwnd)
        PostMessageA(hwnd, kWmMouseMove, flags,
                     LPARAM(uint32_t(uint16_t(client.x)) | uint32_t(uint16_t(client.y)) << 16));
}

void ButtonEvent(const SDL_MouseButtonEvent &e)
{
    const bool isDown = e.state == SDL_PRESSED;
    uint8_t vk;
    UINT message;
    WPARAM xButton = 0;
    switch (e.button)
    {
    case SDL_BUTTON_LEFT: vk = kVkLButton, message = isDown ? kWmLButtonDown : kWmLButtonUp; break;
    case SDL_BUTTON_RIGHT: vk = kVkRButton, message = isDown ? kWmRButtonDown : kWmRButtonUp; break;
    case SDL_BUTTON_MIDDLE: vk = kVkMButton, message = isDown ? kWmMButtonDown : kWmMButtonUp; break;
    case SDL_BUTTON_X1: vk = kVkXButton1, message = isDown ? kWmXButtonDown : kWmXButtonUp, xButton = 1; break;
    case SDL_BUTTON_X2: vk = kVkXButton2, message = isDown ? kWmXButtonDown : kWmXButtonUp, xButton = 2; break;
    default: return;
    }
    HWND hwnd = Bo1_WindowFromSdl(e.windowID);
    RECT rect = {0, 0, 0, 0};
    if (hwnd)
        Bo1_ClientRect(hwnd, &rect);
    WPARAM flags;
    POINT client;
    {
        Lock lock(In().lock);
        Input &in = In();
        SetDown(in, vk, isDown);
        flags = MouseFlags(in) | xButton << 16;
        client = {in.cursor.x - rect.left, in.cursor.y - rect.top};
    }
    if (hwnd)
        PostMessageA(hwnd, message, flags, LPARAM(uint32_t(uint16_t(client.x)) | uint32_t(uint16_t(client.y)) << 16));
}

// One WM_MOUSEWHEEL per notch, as a wheel mouse sends them; the delta's sign is the direction.
void WheelEvent(const SDL_MouseWheelEvent &e)
{
    HWND hwnd = Bo1_WindowFromSdl(e.windowID);
    if (!hwnd)
        hwnd = Bo1_FocusWindow();
    if (!hwnd || !e.y)
        return;
    const int notches = e.direction == SDL_MOUSEWHEEL_FLIPPED ? -e.y : e.y;
    WPARAM flags;
    POINT cursor;
    {
        Lock lock(In().lock);
        flags = MouseFlags(In());
        cursor = In().cursor;
    }
    const uint16_t delta = uint16_t(int16_t(notches > 0 ? kWheelDelta : -kWheelDelta));
    const LPARAM position = LPARAM(uint32_t(uint16_t(cursor.x)) | uint32_t(uint16_t(cursor.y)) << 16);
    for (int i = 0; i < (notches > 0 ? notches : -notches); ++i)
        PostMessageA(hwnd, kWmMouseWheel, flags | WPARAM(delta) << 16, position);
}
} // namespace

void Bo1_InputEvent(const SDL_Event &e)
{
    switch (e.type)
    {
    case SDL_KEYDOWN:
    case SDL_KEYUP:
        KeyEvent(e.key);
        break;
    case SDL_TEXTINPUT:
        TextEvent(e.text);
        break;
    case SDL_MOUSEMOTION:
        MotionEvent(e.motion);
        break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        ButtonEvent(e.button);
        break;
    case SDL_MOUSEWHEEL:
        WheelEvent(e.wheel);
        break;
    case SDL_CLIPBOARDUPDATE:
        ReadClipboard();
        break;
    default:
        break;
    }
}

void Bo1_InputFocusLost()
{
    Lock lock(In().lock);
    Input &in = In();
    for (bool &d : in.down)
        d = false;
}

POINT Bo1_CursorPosition()
{
    Lock lock(In().lock);
    return In().cursor;
}


// The cursor is captured while the engine hides it over its focused, shown window: SDL's relative mode then keeps it
// inside, and only motion moves the virtual cursor. Leaving capture puts the real cursor where the virtual one is.
void Bo1_InputPump()
{
    bool capture, wasCaptured, warp, readClipboard, sendClipboard;
    std::string outgoing;
    POINT cursor;
    Bo1_InputScriptPump();
    const HWND foreground = GetForegroundWindow();
    {
        Lock lock(In().lock);
        Input &in = In();
        readClipboard = !in.clipboardKnown && !Headless();
        sendClipboard = in.clipboardOutgoing;
        in.clipboardOutgoing = false;
        outgoing.swap(in.outgoingText);
        if (!in.cursorKnown && !Headless())
        {
            int x, y;
            SDL_GetGlobalMouseState(&x, &y);
            in.cursor = {x, y};
            in.cursorKnown = true;
        }
        capture = in.cursorDisplayCount < 0 && foreground && !Headless();
        wasCaptured = in.captured;
        in.captured = capture;

        warp = in.warpPending && !capture && !Headless();
        in.warpPending = false;
        cursor = in.cursor;
    }
    if (sendClipboard)
        SDL_SetClipboardText(outgoing.c_str());
    if (readClipboard || sendClipboard)
        ReadClipboard();
    if (capture != wasCaptured)
    {
        // Not every video driver has a relative mode (offscreen has none): motion still moves the virtual cursor.
        SDL_SetRelativeMouseMode(capture ? SDL_TRUE : SDL_FALSE);
        SDL_ShowCursor(capture ? SDL_DISABLE : SDL_ENABLE);
        if (!capture)
            warp = true;
    }
    if (warp)
        SDL_WarpMouseGlobal(cursor.x, cursor.y);
}

extern "C" {

// The display count Windows keeps: the cursor shows while it is at least 0.
int ShowCursor(BOOL bShow)
{
    Lock lock(In().lock);
    return bShow ? ++In().cursorDisplayCount : --In().cursorDisplayCount;
}

BOOL GetCursorPos(LPPOINT lpPoint)
{
    *lpPoint = Bo1_CursorPosition();
    return TRUE;
}

// A captured cursor only moves virtually; otherwise the real one follows, on the video thread. A headless run never
// moves the user's cursor.
BOOL SetCursorPos(int X, int Y)
{
    Lock lock(In().lock);
    In().cursor = {X, Y};
    In().cursorKnown = true;
    In().warpPending = !In().captured && !Headless();
    return TRUE;
}

// Bit 15: the key is down; bit 0: it was pressed since the previous call.
SHORT GetAsyncKeyState(int vKey)
{
    if (vKey < 0 || vKey > 255)
        return 0;
    Lock lock(In().lock);
    Input &in = In();
    SHORT state = in.down[vKey] ? SHORT(0x8000) : 0;
    if (in.pressedSinceRead[vKey])
        state |= 1;
    in.pressedSinceRead[vKey] = false;
    return state;
}

// Only MAPVK_VK_TO_CHAR, the engine's (MapKey): the unshifted character, upper case for letters; any other mapping
// finds nothing.
UINT MapVirtualKeyA(UINT uCode, UINT uMapType)
{
    if (uMapType != kMapVkToChar || uCode > 255)
        return 0;
    Lock lock(In().lock);
    if (In().vkChar[uCode])
        return In().vkChar[uCode];
    return (uCode >= '0' && uCode <= '9') || (uCode >= 'A' && uCode <= 'Z') ? uCode : 0;
}

// Posts the character a key-down message types (KeyEvent decided it). Messages are read in order, so the entries
// before this one belong to messages nobody translated.
BOOL TranslateMessage(const MSG *lpMsg)
{
    if (!lpMsg || (lpMsg->message != kWmKeyDown && lpMsg->message != kWmSysKeyDown))
        return FALSE;
    int c = 0;
    {
        Lock lock(In().lock);
        std::deque<Input::Translation> &pending = In().translations;
        for (auto it = pending.begin(); it != pending.end(); ++it)
        {
            if (it->hwnd == lpMsg->hwnd && it->message == lpMsg->message && it->wParam == lpMsg->wParam &&
                it->lParam == lpMsg->lParam)
            {
                c = it->character;
                pending.erase(pending.begin(), it + 1);
                break;
            }
        }
    }
    if (c)
        PostMessageA(lpMsg->hwnd, lpMsg->message == kWmSysKeyDown ? kWmSysChar : kWmChar, WPARAM(c), lpMsg->lParam);
    return TRUE;
}

// The clipboard holds text only (CF_TEXT, in the ANSI code page). A headless run never touches the user's.
BOOL OpenClipboard(HWND)
{
    Lock lock(In().lock);
    if (Headless() || In().clipboardOpen)
    {
        Bo1_SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    In().clipboardOpen = true;
    return TRUE;
}

BOOL CloseClipboard(void)
{
    Lock lock(In().lock);
    if (!In().clipboardOpen)
        return FALSE;
    In().clipboardOpen = false;
    return TRUE;
}

BOOL EmptyClipboard(void)
{
    Lock lock(In().lock);
    if (!In().clipboardOpen)
        return FALSE;
    GlobalFree(In().clipboardData);
    In().clipboardData = nullptr;
    return TRUE;
}

// The clipboard owns the block from now on, as on Windows; SDL gets the text at the video thread's next pump.
HANDLE SetClipboardData(UINT uFormat, HANDLE hMem)
{
    Lock lock(In().lock);
    Input &in = In();
    if (!in.clipboardOpen || uFormat != kCfText || !hMem)
        return nullptr;
    in.outgoingText = Utf8FromAnsi(static_cast<const char *>(GlobalLock(hMem)));
    GlobalUnlock(hMem);
    in.clipboardOutgoing = true;
    in.clipboardText = in.outgoingText;
    GlobalFree(in.clipboardData);
    in.clipboardData = hMem;
    return hMem;
}

HANDLE GetClipboardData(UINT uFormat)
{
    Lock lock(In().lock);
    Input &in = In();
    if (!in.clipboardOpen || uFormat != kCfText || in.clipboardText.empty())
        return nullptr;
    const std::string text = AnsiFromUtf8(in.clipboardText.c_str());
    HGLOBAL block = GlobalAlloc(0, text.size() + 1);
    if (!block)
        return nullptr;
    memcpy(GlobalLock(block), text.c_str(), text.size() + 1);
    GlobalUnlock(block);
    GlobalFree(in.clipboardData);
    in.clipboardData = block;
    return block;
}

} // extern "C"
