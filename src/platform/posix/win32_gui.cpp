// Win32 windowing on POSIX, on SDL2. Windows belong to this layer (class, window procedure, styles, placement), each
// backed by an SDL window, and SDL's window events become the messages the engine's window procedure expects, in
// the order Windows sends them (input events: win32_input.cpp). Monitors are SDL's displays. Windows have no frame here: the window rect is the
// client area. In a headless run (Sys_IsHeadless) the SDL windows exist but are never shown, as the Windows build
// keeps its headless client on a private desktop; the messages are the same.

#include "posix_video.h"
#include "win32_handles.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

bool __cdecl Sys_IsHeadless();

namespace
{
// The messages, styles and flags this layer sends or reads (winuser.h).
constexpr UINT kWmCreate = 0x0001, kWmDestroy = 0x0002, kWmMove = 0x0003, kWmSize = 0x0005, kWmActivate = 0x0006,
               kWmSetFocus = 0x0007, kWmKillFocus = 0x0008, kWmClose = 0x0010, kWmQuit = 0x0012,
               kWmShowWindow = 0x0018, kWmActivateApp = 0x001C, kWmNcCreate = 0x0081, kWmNcDestroy = 0x0082,
               kWmSysKeyDown = 0x0104, kWmSysCommand = 0x0112, kFirstRegisteredMessage = 0xC000;
constexpr WPARAM kVkF4 = 0x73;
constexpr WPARAM kScClose = 0xF060;
constexpr WPARAM kSizeRestored = 0, kSizeMinimized = 1, kSizeMaximized = 2;
constexpr DWORD kWsCaption = 0x00C00000, kWsVisible = 0x10000000, kWsMinimize = 0x20000000;
constexpr UINT kSwpNoSize = 0x1, kSwpNoMove = 0x2, kSwpNoZOrder = 0x4, kSwpNoActivate = 0x10, kSwpShowWindow = 0x40,
               kSwpHideWindow = 0x80;
constexpr int kSwHide = 0, kSwShowMinimized = 2, kSwMaximize = 3, kSwShowNoActivate = 4, kSwShow = 5,
              kSwMinimize = 6, kSwShowMinNoActive = 7, kSwShowNa = 8;
constexpr int kGwlpWndProc = -4, kGwlpUserData = -21;
constexpr UINT kPmRemove = 0x1;
constexpr DWORD kMonitorDefaultToNull = 0, kMonitorInfoPrimary = 1;
constexpr int kSmCMonitors = 80, kSmRemoteSession = 0x1000;
constexpr DWORD kErrorInvalidWindowHandle = 1400, kErrorCannotFindWndClass = 1407;
constexpr DWORD kDmPosition = 0x20, kDmBitsPerPel = 0x40000, kDmPelsWidth = 0x80000, kDmPelsHeight = 0x100000,
                kDmDisplayFrequency = 0x400000;
constexpr LONG kDispChangeSuccessful = 0, kDispChangeFailed = -1;
const HWND kHwndTopmost = reinterpret_cast<HWND>(intptr_t(-1));
const HWND kHwndNoTopmost = reinterpret_cast<HWND>(intptr_t(-2));

// CREATESTRUCTA, which the window procedure receives with WM_NCCREATE and WM_CREATE.
struct CreateStruct
{
    LPVOID lpCreateParams;
    HINSTANCE hInstance;
    HMENU hMenu;
    HWND hwndParent;
    int cy, cx, y, x;
    LONG style;
    LPCSTR lpszName;
    LPCSTR lpszClass;
    DWORD dwExStyle;
};

struct WindowClass
{
    std::string name;
    WNDPROC proc;
};

struct Window
{
    HWND handle;
    WNDPROC proc;
    std::string title;
    DWORD style;
    DWORD exStyle;
    RECT rect; // the client area, on screen
    LONG userData = 0;
    bool visible = false;
    bool iconic = false;
    DWORD thread;
    SDL_Window *sdl = nullptr;
};

struct Layer
{
    std::recursive_mutex lock;
    std::vector<WindowClass> classes;
    std::vector<std::unique_ptr<Window>> windows;
    std::vector<std::string> registeredMessages;
    // Each thread has its own queue, as on Windows: a window's messages go to the thread that made it.
    std::map<DWORD, std::deque<MSG>> queues;
    std::condition_variable_any posted;
    HWND active = nullptr;
    HWND focus = nullptr;
    uintptr_t nextHandle = 0x10000;
    bool videoTried = false;
    bool video = false;
    bool glWindows = false;
    std::thread::id videoThread;
};

Layer &L()
{
    static Layer *layer = new Layer();
    return *layer;
}

using Lock = std::lock_guard<std::recursive_mutex>;

[[noreturn]] void Unsupported(const char *function)
{
    fprintf(stderr, "%s: not in the posix window layer\n", function);
    fflush(nullptr);
    abort();
}

bool Headless()
{
    return Sys_IsHeadless();
}

Window *Find(HWND hwnd)
{
    for (const std::unique_ptr<Window> &w : L().windows)
        if (w->handle == hwnd)
            return w.get();
    return nullptr;
}

Window *FindSdl(Uint32 id)
{
    for (const std::unique_ptr<Window> &w : L().windows)
        if (w->sdl && SDL_GetWindowID(w->sdl) == id)
            return w.get();
    return nullptr;
}

bool Visible(HWND hwnd)
{
    Lock lock(L().lock);
    Window *w = Find(hwnd);
    return w && w->visible;
}

LPARAM PackPoint(int x, int y)
{
    return LPARAM(uint32_t(uint16_t(x)) | uint32_t(uint16_t(y)) << 16);
}

// Calls a window's procedure, without the layer's lock: the procedure may call back into the layer, or into engine
// code that takes its own locks.
LRESULT Send(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    WNDPROC proc;
    {
        Lock lock(L().lock);
        Window *w = Find(hwnd);
        if (!w)
            return 0;
        proc = w->proc;
    }
    return proc ? proc(hwnd, message, wParam, lParam) : 0;
}

int DisplayCount()
{
    return Bo1_VideoReady() ? SDL_GetNumVideoDisplays() : 0;
}

bool DisplayBounds(int display, RECT *rect, bool usable = false)
{
    SDL_Rect r;
    if (display < 0 || display >= DisplayCount() ||
        (usable ? SDL_GetDisplayUsableBounds(display, &r) : SDL_GetDisplayBounds(display, &r)))
        return false;
    *rect = {r.x, r.y, r.x + r.w, r.y + r.h};
    return true;
}

int DisplayOf(HMONITOR monitor)
{
    const intptr_t display = reinterpret_cast<intptr_t>(monitor) - 1;
    return display >= 0 && display < DisplayCount() ? int(display) : -1;
}

int DisplayFromDevice(LPCSTR device)
{
    if (!device)
        return 0;
    int number;
    if (sscanf(device, "\\\\.\\DISPLAY%d", &number) != 1 || number < 1 || number > DisplayCount())
        return -1;
    return number - 1;
}

HMONITOR MonitorAtRect(const RECT &rect, DWORD flags)
{
    int best = -1;
    int64_t bestArea = 0;
    int64_t bestDistance = INT64_MAX;
    int nearest = -1;
    for (int d = 0; d < DisplayCount(); ++d)
    {
        RECT b;
        if (!DisplayBounds(d, &b))
            continue;
        const int64_t w = int64_t(std::min(rect.right, b.right)) - std::max(rect.left, b.left);
        const int64_t h = int64_t(std::min(rect.bottom, b.bottom)) - std::max(rect.top, b.top);
        if (w > 0 && h > 0 && w * h > bestArea)
        {
            bestArea = w * h;
            best = d;
        }
        const int64_t dx = rect.left > b.right ? rect.left - b.right : b.left > rect.right ? b.left - rect.right : 0;
        const int64_t dy = rect.top > b.bottom ? rect.top - b.bottom : b.top > rect.bottom ? b.top - rect.bottom : 0;
        if (dx * dx + dy * dy < bestDistance)
        {
            bestDistance = dx * dx + dy * dy;
            nearest = d;
        }
    }
    if (best >= 0)
        return Bo1_DisplayMonitor(best);
    if (flags == kMonitorDefaultToNull || nearest < 0)
        return nullptr;
    return Bo1_DisplayMonitor(flags == MONITOR_DEFAULTTOPRIMARY ? 0 : nearest);
}

void Activate(HWND hwnd, bool minimized)
{
    HWND previous;
    {
        Lock lock(L().lock);
        previous = L().active;
        if (previous == hwnd || !Find(hwnd))
            return;
        L().active = hwnd;
    }
    if (previous)
        Send(previous, kWmActivate, WA_INACTIVE, LPARAM(hwnd));
    else
        Send(hwnd, kWmActivateApp, TRUE, 0);
    Send(hwnd, kWmActivate, WPARAM(WA_ACTIVE | (minimized ? 1u << 16 : 0)), LPARAM(previous));
    SetFocus(hwnd);
}

// The application loses its active window (focus went to another application, or the window went).
void Deactivate(HWND hwnd, bool minimized)
{
    {
        Lock lock(L().lock);
        if (L().active != hwnd)
            return;
        L().active = nullptr;
    }
    Send(hwnd, kWmActivate, WPARAM(WA_INACTIVE | (minimized ? 1u << 16 : 0)), 0);
    Send(hwnd, kWmActivateApp, FALSE, 0);
    SetFocus(nullptr);
}

void Show(HWND hwnd)
{
    SDL_Window *sdl;
    {
        Lock lock(L().lock);
        Window *w = Find(hwnd);
        if (!w || w->visible)
            return;
        w->visible = true;
        w->style |= kWsVisible;
        sdl = w->sdl;
    }
    if (sdl && !Headless())
        SDL_ShowWindow(sdl);
    Send(hwnd, kWmShowWindow, TRUE, 0);
}

void Hide(HWND hwnd)
{
    SDL_Window *sdl;
    {
        Lock lock(L().lock);
        Window *w = Find(hwnd);
        if (!w || !w->visible)
            return;
        w->visible = false;
        w->style &= ~kWsVisible;
        sdl = w->sdl;
    }
    if (sdl)
        SDL_HideWindow(sdl);
    Send(hwnd, kWmShowWindow, FALSE, 0);
    Deactivate(hwnd, false);
}

// The SDL events for the layer's windows, as messages: sent at once for activation and placement (Windows sends
// those to the procedure from inside the message calls), posted otherwise.
void Translate(const SDL_Event &e)
{
    if (e.type != SDL_WINDOWEVENT)
    {
        Bo1_InputEvent(e);
        return;
    }
    HWND hwnd;
    RECT rect;
    {
        Lock lock(L().lock);
        Window *w = FindSdl(e.window.windowID);
        if (!w)
            return;
        hwnd = w->handle;
        rect = w->rect;
        switch (e.window.event)
        {
        case SDL_WINDOWEVENT_MOVED:
            if (e.window.data1 == rect.left && e.window.data2 == rect.top)
                return;
            w->rect = {e.window.data1, e.window.data2, e.window.data1 + (rect.right - rect.left),
                       e.window.data2 + (rect.bottom - rect.top)};
            rect = w->rect;
            break;
        case SDL_WINDOWEVENT_SIZE_CHANGED:
            if (e.window.data1 == rect.right - rect.left && e.window.data2 == rect.bottom - rect.top)
                return;
            w->rect.right = w->rect.left + e.window.data1;
            w->rect.bottom = w->rect.top + e.window.data2;
            rect = w->rect;
            break;
        case SDL_WINDOWEVENT_MINIMIZED:
            w->iconic = true;
            break;
        case SDL_WINDOWEVENT_RESTORED:
        case SDL_WINDOWEVENT_MAXIMIZED:
            w->iconic = false;
            break;
        default:
            break;
        }
    }
    switch (e.window.event)
    {
    case SDL_WINDOWEVENT_CLOSE:
        PostMessageA(hwnd, kWmClose, 0, 0);
        break;
    case SDL_WINDOWEVENT_FOCUS_GAINED:
        Activate(hwnd, false);
        break;
    case SDL_WINDOWEVENT_FOCUS_LOST:
        Bo1_InputFocusLost();
        Deactivate(hwnd, false);
        break;
    case SDL_WINDOWEVENT_MOVED:
        Send(hwnd, kWmMove, 0, PackPoint(rect.left, rect.top));
        break;
    case SDL_WINDOWEVENT_SIZE_CHANGED:
        Send(hwnd, kWmSize, kSizeRestored, PackPoint(rect.right - rect.left, rect.bottom - rect.top));
        break;
    case SDL_WINDOWEVENT_MINIMIZED:
        Send(hwnd, kWmSize, kSizeMinimized, 0);
        Deactivate(hwnd, true);
        break;
    case SDL_WINDOWEVENT_RESTORED:
    case SDL_WINDOWEVENT_MAXIMIZED:
        Send(hwnd, kWmSize, e.window.event == SDL_WINDOWEVENT_MAXIMIZED ? kSizeMaximized : kSizeRestored,
             PackPoint(rect.right - rect.left, rect.bottom - rect.top));
        break;
    default:
        break;
    }
}

void Pump()
{
    if (!Bo1_OnVideoThread())
        return;
    Bo1_InputPump();
    SDL_Event e;
    while (SDL_PollEvent(&e))
        Translate(e);
    Bo1_GamepadPump();
}

bool Matches(const MSG &m, HWND hwnd, UINT first, UINT last)
{
    return (!hwnd || m.hwnd == hwnd) && ((!first && !last) || (m.message >= first && m.message <= last));
}

} // namespace

// SDL's events are read on the thread that started video, as SDL requires; the engine makes its window on that thread
// too (the render thread), so the procedures run on the window's thread as on Windows.
bool Bo1_OnVideoThread()
{
    Lock lock(L().lock);
    return L().video && L().videoThread == std::this_thread::get_id();
}

HWND Bo1_WindowFromSdl(uint32_t windowId)
{
    Lock lock(L().lock);
    Window *w = FindSdl(windowId);
    return w ? w->handle : nullptr;
}

HWND Bo1_FocusWindow()
{
    Lock lock(L().lock);
    return L().focus;
}

bool Bo1_VideoReady()
{
    Lock lock(L().lock);
    Layer &l = L();
    if (!l.videoTried)
    {
        l.videoTried = true;
        // The engine handles its own signals; SDL would turn SIGINT/SIGTERM into SDL_QUIT.
        SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
        if (SDL_InitSubSystem(SDL_INIT_VIDEO) == 0)
        {
            l.video = true;
            l.videoThread = std::this_thread::get_id();
        }
        else
            fprintf(stderr, "SDL video: %s\n", SDL_GetError());
    }
    return l.video;
}

// SDL reads events on the thread that started its video. The engine first makes a Direct3D object on its main thread
// to name the video card (Sys_DetectVideoCard), then its windows on the render thread: video started for that first
// object must stop with it, so that the windows' thread starts it again.
void Bo1_VideoRelease()
{
    Lock lock(L().lock);
    Layer &l = L();
    if (!l.video)
        return;
    for (const auto &w : l.windows)
        if (w->sdl)
            return;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    l.video = false;
    l.videoTried = false;
}

void Bo1_UseGlWindows()
{
    Lock lock(L().lock);
    L().glWindows = true;
}

HMONITOR Bo1_DisplayMonitor(int display)
{
    return reinterpret_cast<HMONITOR>(intptr_t(display) + 1);
}

SDL_Window *Bo1_NativeWindow(HWND hwnd)
{
    Lock lock(L().lock);
    Window *w = Find(hwnd);
    return w ? w->sdl : nullptr;
}

bool Bo1_ClientRect(HWND hwnd, RECT *rect)
{
    Lock lock(L().lock);
    Window *w = Find(hwnd);
    if (!w)
        return false;
    *rect = w->rect;
    return true;
}

bool Bo1_WindowShown(HWND hwnd)
{
    Lock lock(L().lock);
    Window *w = Find(hwnd);
    return w && w->visible && !w->iconic && w->sdl && !Headless() && (SDL_GetWindowFlags(w->sdl) & SDL_WINDOW_SHOWN);
}

extern "C" {

// Nobody can answer: the text goes to stderr and the call fails, as when Windows cannot show the box.
int MessageBoxA(HWND, LPCSTR lpText, LPCSTR lpCaption, UINT)
{
    fprintf(stderr, "%s\n%s\n", lpCaption ? lpCaption : "", lpText ? lpText : "");
    Bo1_SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

int MessageBoxW(HWND, LPCWSTR lpText, LPCWSTR lpCaption, UINT)
{
    fprintf(stderr, "%ls\n%ls\n", lpCaption ? lpCaption : L"", lpText ? lpText : L"");
    Bo1_SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

// No cursor images: there was no previous one either.
HCURSOR SetCursor(HCURSOR)
{
    return nullptr;
}

HWND GetActiveWindow(void)
{
    Lock lock(L().lock);
    return L().active;
}

// The foreground window is the one with the keyboard on the display; a window nobody sees never is.
HWND GetForegroundWindow(void)
{
    Lock lock(L().lock);
    Window *w = Find(L().active);
    return w && Bo1_WindowShown(w->handle) && (SDL_GetWindowFlags(w->sdl) & SDL_WINDOW_INPUT_FOCUS) ? w->handle
                                                                                                    : nullptr;
}

HWND GetDesktopWindow(void)
{
    return nullptr;
}

BOOL IsWindow(HWND hwnd)
{
    Lock lock(L().lock);
    return Find(hwnd) != nullptr;
}

BOOL IsIconic(HWND hwnd)
{
    Lock lock(L().lock);
    Window *w = Find(hwnd);
    return w && w->iconic;
}

BOOL EqualRect(const RECT *lprc1, const RECT *lprc2)
{
    return lprc1->left == lprc2->left && lprc1->top == lprc2->top && lprc1->right == lprc2->right &&
           lprc1->bottom == lprc2->bottom;
}

// Class atoms count from 0xC000, as on Windows.
ATOM RegisterClassExA(const WNDCLASSEXA *lpwcx)
{
    if (!lpwcx || lpwcx->cbSize != sizeof(WNDCLASSEXA) || !lpwcx->lpszClassName)
    {
        Bo1_SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    Lock lock(L().lock);
    for (const WindowClass &c : L().classes)
    {
        if (!strcasecmp(c.name.c_str(), lpwcx->lpszClassName))
        {
            Bo1_SetLastError(ERROR_CLASS_ALREADY_EXISTS);
            return 0;
        }
    }
    L().classes.push_back({lpwcx->lpszClassName, lpwcx->lpfnWndProc});
    return (ATOM)(0xC000 + L().classes.size() - 1);
}

// An ELF executable carries no Win32 resources, and there are no system cursors or GDI objects.
HICON LoadIconA(HINSTANCE, LPCSTR)
{
    Bo1_SetLastError(ERROR_RESOURCE_TYPE_NOT_FOUND);
    return nullptr;
}

HCURSOR LoadCursorA(HINSTANCE, LPCSTR)
{
    Bo1_SetLastError(ERROR_RESOURCE_TYPE_NOT_FOUND);
    return nullptr;
}

HBRUSH CreateSolidBrush(DWORD)
{
    return nullptr;
}

// No power management request exists to make: the call fails.
EXECUTION_STATE SetThreadExecutionState(EXECUTION_STATE)
{
    Bo1_SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

HWND CreateWindowExA(DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth,
                     int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)
{
    if (hWndParent)
        Unsupported("CreateWindowExA (child window)");
    HWND handle;
    {
        Lock lock(L().lock);
        const WindowClass *cls = nullptr;
        for (const WindowClass &c : L().classes)
            if (lpClassName && !strcasecmp(c.name.c_str(), lpClassName))
                cls = &c;
        if (!cls)
        {
            Bo1_SetLastError(kErrorCannotFindWndClass);
            return nullptr;
        }
        auto w = std::make_unique<Window>();
        w->handle = handle = reinterpret_cast<HWND>(L().nextHandle += 4);
        w->proc = cls->proc;
        w->title = lpWindowName ? lpWindowName : "";
        w->style = dwStyle & ~(kWsVisible | kWsMinimize);
        w->exStyle = dwExStyle;
        w->rect = {X, Y, X + std::max(nWidth, 1), Y + std::max(nHeight, 1)};
        w->thread = GetCurrentThreadId();
        if (Bo1_VideoReady())
        {
            Uint32 flags = SDL_WINDOW_HIDDEN;
            if (!(dwStyle & kWsCaption))
                flags |= SDL_WINDOW_BORDERLESS;
            if (L().glWindows)
                flags |= SDL_WINDOW_OPENGL;
            w->sdl = SDL_CreateWindow(w->title.c_str(), X, Y, std::max(nWidth, 1), std::max(nHeight, 1), flags);
            if (!w->sdl)
                fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        }
        L().windows.push_back(std::move(w));
    }
    CreateStruct cs = {lpParam, hInstance, hMenu, hWndParent, nHeight, nWidth, Y, X, LONG(dwStyle),
                       lpWindowName, lpClassName, dwExStyle};
    if (!Send(handle, kWmNcCreate, 0, LPARAM(&cs)) || Send(handle, kWmCreate, 0, LPARAM(&cs)) == -1)
    {
        DestroyWindow(handle);
        return nullptr;
    }
    // A visible top-level window is shown and activated before CreateWindowEx returns, then told its placement.
    if (dwStyle & kWsVisible)
    {
        Show(handle);
        Activate(handle, false);
    }
    RECT rect;
    if (Bo1_ClientRect(handle, &rect))
    {
        Send(handle, kWmSize, kSizeRestored, PackPoint(rect.right - rect.left, rect.bottom - rect.top));
        Send(handle, kWmMove, 0, PackPoint(rect.left, rect.top));
    }
    return handle;
}

BOOL DestroyWindow(HWND hWnd)
{
    if (!IsWindow(hWnd))
    {
        Bo1_SetLastError(kErrorInvalidWindowHandle);
        return FALSE;
    }
    Deactivate(hWnd, false);
    Send(hWnd, kWmDestroy, 0, 0);
    Send(hWnd, kWmNcDestroy, 0, 0);
    Lock lock(L().lock);
    Layer &l = L();
    if (l.focus == hWnd)
        l.focus = nullptr;
    for (auto &[thread, queue] : l.queues)
        queue.erase(std::remove_if(queue.begin(), queue.end(), [&](const MSG &m) { return m.hwnd == hWnd; }),
                    queue.end());
    auto it = std::find_if(l.windows.begin(), l.windows.end(), [&](const auto &w) { return w->handle == hWnd; });
    if (it != l.windows.end())
    {
        if ((*it)->sdl)
            SDL_DestroyWindow((*it)->sdl);
        l.windows.erase(it);
    }
    return TRUE;
}

BOOL ShowWindow(HWND hWnd, int nCmdShow)
{
    bool wasVisible;
    bool activate = true;
    bool minimized = false;
    {
        Lock lock(L().lock);
        Window *w = Find(hWnd);
        if (!w)
            return FALSE;
        wasVisible = w->visible;
        switch (nCmdShow)
        {
        case kSwHide:
            activate = false;
            break;
        case kSwShowMinimized:
        case kSwMinimize:
        case kSwShowMinNoActive:
            w->iconic = minimized = true;
            if (w->sdl && !Headless())
                SDL_MinimizeWindow(w->sdl);
            activate = nCmdShow == kSwShowMinimized;
            break;
        case kSwMaximize:
            w->iconic = false;
            if (w->sdl && !Headless())
                SDL_MaximizeWindow(w->sdl);
            break;
        case kSwShowNoActivate:
        case kSwShowNa:
            activate = false;
            break;
        default:
            if (w->iconic && w->sdl && !Headless())
                SDL_RestoreWindow(w->sdl);
            w->iconic = false;
            break;
        }
    }
    if (nCmdShow == kSwHide)
    {
        Hide(hWnd);
        return wasVisible;
    }
    Show(hWnd);
    if (activate)
        Activate(hWnd, minimized);
    return wasVisible;
}

HWND SetFocus(HWND hWnd)
{
    HWND previous;
    {
        Lock lock(L().lock);
        if (hWnd && !Find(hWnd))
        {
            Bo1_SetLastError(kErrorInvalidWindowHandle);
            return nullptr;
        }
        previous = L().focus;
        if (previous == hWnd)
            return previous;
        L().focus = hWnd;
    }
    if (previous)
        Send(previous, kWmKillFocus, WPARAM(hWnd), 0);
    if (hWnd)
        Send(hWnd, kWmSetFocus, WPARAM(previous), 0);
    return previous;
}

LONG GetWindowLongA(HWND hWnd, int nIndex)
{
    Lock lock(L().lock);
    Window *w = Find(hWnd);
    if (!w)
    {
        Bo1_SetLastError(kErrorInvalidWindowHandle);
        return 0;
    }
    switch (nIndex)
    {
    case GWL_STYLE:
        return LONG(w->style);
    case GWL_EXSTYLE:
        return LONG(w->exStyle);
    case kGwlpUserData:
        return w->userData;
    default: // GWLP_WNDPROC does not fit a LONG in a 64-bit process, so Windows refuses it too
        Bo1_SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
}

LONG SetWindowLongA(HWND hWnd, int nIndex, LONG dwNewLong)
{
    Lock lock(L().lock);
    Window *w = Find(hWnd);
    if (!w)
    {
        Bo1_SetLastError(kErrorInvalidWindowHandle);
        return 0;
    }
    LONG previous;
    switch (nIndex)
    {
    case GWL_STYLE:
        previous = LONG(w->style);
        // Visibility follows ShowWindow and SetWindowPos only, as on Windows.
        w->style = (DWORD(dwNewLong) & ~(kWsVisible | kWsMinimize)) | (w->style & (kWsVisible | kWsMinimize));
        if (w->sdl)
            SDL_SetWindowBordered(w->sdl, (w->style & kWsCaption) ? SDL_TRUE : SDL_FALSE);
        return previous;
    case GWL_EXSTYLE:
        previous = LONG(w->exStyle);
        w->exStyle = DWORD(dwNewLong);
        return previous;
    case kGwlpUserData:
        previous = w->userData;
        w->userData = dwNewLong;
        return previous;
    case kGwlpWndProc:
    default:
        Bo1_SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
}

BOOL SetWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags)
{
    bool moved = false, sized = false, show = false, hide = false;
    RECT rect;
    {
        Lock lock(L().lock);
        Window *w = Find(hWnd);
        if (!w)
        {
            Bo1_SetLastError(kErrorInvalidWindowHandle);
            return FALSE;
        }
        rect = w->rect;
        if (!(uFlags & kSwpNoMove) && (X != rect.left || Y != rect.top))
        {
            rect = {X, Y, X + (rect.right - rect.left), Y + (rect.bottom - rect.top)};
            moved = true;
        }
        if (!(uFlags & kSwpNoSize) && (std::max(cx, 1) != rect.right - rect.left ||
                                       std::max(cy, 1) != rect.bottom - rect.top))
        {
            rect.right = rect.left + std::max(cx, 1);
            rect.bottom = rect.top + std::max(cy, 1);
            sized = true;
        }
        w->rect = rect;
        if (w->sdl)
        {
            if (moved)
                SDL_SetWindowPosition(w->sdl, rect.left, rect.top);
            if (sized)
                SDL_SetWindowSize(w->sdl, rect.right - rect.left, rect.bottom - rect.top);
            if (!(uFlags & kSwpNoZOrder) && (hWndInsertAfter == kHwndTopmost || hWndInsertAfter == kHwndNoTopmost))
                SDL_SetWindowAlwaysOnTop(w->sdl, hWndInsertAfter == kHwndTopmost ? SDL_TRUE : SDL_FALSE);
        }
        show = (uFlags & kSwpShowWindow) && !w->visible;
        hide = (uFlags & kSwpHideWindow) && w->visible;
    }
    if (show)
        ShowWindow(hWnd, (uFlags & kSwpNoActivate) ? kSwShowNa : kSwShow);
    if (hide)
        ShowWindow(hWnd, kSwHide);
    if (!(uFlags & kSwpNoActivate) && !hide && Visible(hWnd))
        Activate(hWnd, false);
    if (sized)
        Send(hWnd, kWmSize, kSizeRestored, PackPoint(rect.right - rect.left, rect.bottom - rect.top));
    if (moved)
        Send(hWnd, kWmMove, 0, PackPoint(rect.left, rect.top));
    return TRUE;
}

BOOL MoveWindow(HWND hWnd, int X, int Y, int nWidth, int nHeight, BOOL)
{
    return SetWindowPos(hWnd, nullptr, X, Y, nWidth, nHeight, kSwpNoZOrder | kSwpNoActivate);
}

BOOL GetClientRect(HWND hWnd, LPRECT lpRect)
{
    RECT rect;
    if (!Bo1_ClientRect(hWnd, &rect))
    {
        Bo1_SetLastError(kErrorInvalidWindowHandle);
        return FALSE;
    }
    *lpRect = {0, 0, rect.right - rect.left, rect.bottom - rect.top};
    return TRUE;
}

BOOL GetWindowRect(HWND hWnd, LPRECT lpRect)
{
    if (!Bo1_ClientRect(hWnd, lpRect))
    {
        Bo1_SetLastError(kErrorInvalidWindowHandle);
        return FALSE;
    }
    return TRUE;
}

BOOL ClientToScreen(HWND hWnd, LPPOINT lpPoint)
{
    RECT rect;
    if (!Bo1_ClientRect(hWnd, &rect))
        return FALSE;
    lpPoint->x += rect.left;
    lpPoint->y += rect.top;
    return TRUE;
}

BOOL ScreenToClient(HWND hWnd, LPPOINT lpPoint)
{
    RECT rect;
    if (!Bo1_ClientRect(hWnd, &rect))
        return FALSE;
    lpPoint->x -= rect.left;
    lpPoint->y -= rect.top;
    return TRUE;
}

// Windows have no frame in this layer: the window rect is the client rect.
BOOL AdjustWindowRectEx(LPRECT, DWORD, BOOL, DWORD)
{
    return TRUE;
}

BOOL AdjustWindowRect(LPRECT, DWORD, BOOL)
{
    return TRUE;
}

int GetWindowTextA(HWND hWnd, LPSTR lpString, int nMaxCount)
{
    Lock lock(L().lock);
    Window *w = Find(hWnd);
    if (!w || nMaxCount <= 0)
        return 0;
    const int length = std::min(int(w->title.size()), nMaxCount - 1);
    memcpy(lpString, w->title.data(), size_t(length));
    lpString[length] = 0;
    return length;
}

BOOL EnumThreadWindows(DWORD dwThreadId, WNDENUMPROC lpfn, LPARAM lParam)
{
    std::vector<HWND> handles;
    {
        Lock lock(L().lock);
        for (const std::unique_ptr<Window> &w : L().windows)
            if (w->thread == dwThreadId)
                handles.push_back(w->handle);
    }
    for (HWND hwnd : handles)
        if (IsWindow(hwnd) && !lpfn(hwnd, lParam))
            break;
    return TRUE;
}

// Display metrics start SDL's video; the others need no display and answer as for a local session.
int GetSystemMetrics(int nIndex)
{
    RECT b;
    switch (nIndex)
    {
    case SM_CXSCREEN:
        return DisplayBounds(0, &b) ? b.right - b.left : 0;
    case SM_CYSCREEN:
        return DisplayBounds(0, &b) ? b.bottom - b.top : 0;
    case kSmCMonitors:
        return DisplayCount();
    case kSmRemoteSession:
    default:
        return 0;
    }
}

// SDL works in display pixels: nothing is scaled behind the process's back.
BOOL IsProcessDPIAware(void)
{
    return TRUE;
}

HMONITOR MonitorFromWindow(HWND hwnd, DWORD dwFlags)
{
    RECT rect;
    if (!Bo1_ClientRect(hwnd, &rect))
        return dwFlags == kMonitorDefaultToNull ? nullptr : MonitorAtRect({0, 0, 1, 1}, dwFlags);
    return MonitorAtRect(rect, dwFlags);
}

HMONITOR MonitorFromPoint(POINT pt, DWORD dwFlags)
{
    return MonitorAtRect({pt.x, pt.y, pt.x + 1, pt.y + 1}, dwFlags);
}

BOOL GetMonitorInfoA(HMONITOR hMonitor, LPMONITORINFO lpmi)
{
    const int display = DisplayOf(hMonitor);
    if (display < 0 || !lpmi || (lpmi->cbSize != sizeof(MONITORINFO) && lpmi->cbSize != sizeof(MONITORINFOEXA)))
        return FALSE;
    if (!DisplayBounds(display, &lpmi->rcMonitor))
        return FALSE;
    if (!DisplayBounds(display, &lpmi->rcWork, true))
        lpmi->rcWork = lpmi->rcMonitor;
    lpmi->dwFlags = display == 0 ? kMonitorInfoPrimary : 0;
    if (lpmi->cbSize == sizeof(MONITORINFOEXA))
        snprintf(static_cast<MONITORINFOEXA *>(lpmi)->szDevice, CCHDEVICENAME, "\\\\.\\DISPLAY%d", display + 1);
    return TRUE;
}

BOOL EnumDisplayMonitors(HDC hdc, LPCRECT lprcClip, MONITORENUMPROC lpfnEnum, LPARAM dwData)
{
    if (hdc)
        Unsupported("EnumDisplayMonitors (device context)");
    for (int d = 0; d < DisplayCount(); ++d)
    {
        RECT rect;
        if (!DisplayBounds(d, &rect))
            continue;
        if (lprcClip)
        {
            rect = {std::max(rect.left, lprcClip->left), std::max(rect.top, lprcClip->top),
                    std::min(rect.right, lprcClip->right), std::min(rect.bottom, lprcClip->bottom)};
            if (rect.left >= rect.right || rect.top >= rect.bottom)
                continue;
        }
        if (!lpfnEnum(Bo1_DisplayMonitor(d), nullptr, &rect, dwData))
            break;
    }
    return TRUE;
}

BOOL EnumDisplaySettingsA(LPCSTR lpszDeviceName, DWORD iModeNum, DEVMODEA *lpDevMode)
{
    const int display = DisplayFromDevice(lpszDeviceName);
    if (display < 0 || display >= DisplayCount() || !lpDevMode)
        return FALSE;
    SDL_DisplayMode mode;
    const DWORD kEnumRegistrySettings = DWORD(-2);
    const int failed = iModeNum == ENUM_CURRENT_SETTINGS   ? SDL_GetCurrentDisplayMode(display, &mode)
                       : iModeNum == kEnumRegistrySettings ? SDL_GetDesktopDisplayMode(display, &mode)
                                                           : SDL_GetDisplayMode(display, int(iModeNum), &mode);
    if (failed)
        return FALSE;
    RECT bounds = {0, 0, 0, 0};
    DisplayBounds(display, &bounds);
    memset(lpDevMode, 0, sizeof(*lpDevMode));
    snprintf(reinterpret_cast<char *>(lpDevMode->dmDeviceName), CCHDEVICENAME, "\\\\.\\DISPLAY%d", display + 1);
    lpDevMode->dmSize = sizeof(DEVMODEA);
    lpDevMode->dmFields = kDmPosition | kDmBitsPerPel | kDmPelsWidth | kDmPelsHeight | kDmDisplayFrequency;
    lpDevMode->dmPosition = {bounds.left, bounds.top};
    lpDevMode->dmBitsPerPel = 32;
    lpDevMode->dmPelsWidth = DWORD(mode.w);
    lpDevMode->dmPelsHeight = DWORD(mode.h);
    lpDevMode->dmDisplayFrequency = DWORD(mode.refresh_rate);
    return TRUE;
}

// The layer never changes a display mode, so restoring the registry's is done; changing one is not possible.
LONG ChangeDisplaySettingsA(DEVMODEA *lpDevMode, DWORD)
{
    return lpDevMode ? kDispChangeFailed : kDispChangeSuccessful;
}

BOOL PeekMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg)
{
    Pump();
    Lock lock(L().lock);
    std::deque<MSG> &queue = L().queues[GetCurrentThreadId()];
    for (auto it = queue.begin(); it != queue.end(); ++it)
    {
        if (!Matches(*it, hWnd, wMsgFilterMin, wMsgFilterMax))
            continue;
        *lpMsg = *it;
        if (wRemoveMsg & kPmRemove)
            queue.erase(it);
        return TRUE;
    }
    return FALSE;
}

BOOL GetMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax)
{
    for (;;)
    {
        if (PeekMessageA(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, kPmRemove))
            return lpMsg->message != kWmQuit;
        if (Bo1_OnVideoThread())
        {
            SDL_Event e;
            if (SDL_WaitEventTimeout(&e, 50))
                Translate(e);
        }
        else
        {
            std::unique_lock<std::recursive_mutex> lock(L().lock);
            L().posted.wait_for(lock, std::chrono::milliseconds(50));
        }
    }
}

LRESULT DispatchMessageA(const MSG *lpMsg)
{
    if (!lpMsg || !lpMsg->hwnd)
        return 0;
    return Send(lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam);
}

// A message for no window goes to the posting thread, as PostThreadMessage(GetCurrentThreadId()) would send it.
BOOL PostMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    Lock lock(L().lock);
    Window *w = hWnd ? Find(hWnd) : nullptr;
    if (hWnd && !w)
    {
        Bo1_SetLastError(kErrorInvalidWindowHandle);
        return FALSE;
    }
    L().queues[w ? w->thread : GetCurrentThreadId()].push_back(
        {hWnd, Msg, wParam, lParam, GetTickCount(), Bo1_CursorPosition()});
    L().posted.notify_all();
    return TRUE;
}

LRESULT DefWindowProcA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    switch (Msg)
    {
    case kWmNcCreate:
        return TRUE;
    case kWmSysKeyDown:
        if (wParam == kVkF4 && (lParam & (1 << 29)))
            Send(hWnd, kWmSysCommand, kScClose, 0);
        return 0;
    case kWmClose:
        DestroyWindow(hWnd);
        return 0;
    case kWmSysCommand:
        if ((wParam & 0xFFF0) == kScClose)
            Send(hWnd, kWmClose, 0, 0);
        return 0;
    default:
        return 0;
    }
}

UINT RegisterWindowMessageA(LPCSTR lpString)
{
    if (!lpString || !*lpString)
    {
        Bo1_SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    Lock lock(L().lock);
    std::vector<std::string> &names = L().registeredMessages;
    for (size_t i = 0; i < names.size(); ++i)
        if (!strcasecmp(names[i].c_str(), lpString))
            return UINT(kFirstRegisteredMessage + i);
    names.push_back(lpString);
    return UINT(kFirstRegisteredMessage + names.size() - 1);
}

// No GDI: no device context, so no gamma ramp to set through one.
HDC GetDC(HWND)
{
    return nullptr;
}

int ReleaseDC(HWND, HDC)
{
    return 0;
}

BOOL SetDeviceGammaRamp(HDC, LPVOID)
{
    return FALSE;
}

HINSTANCE ShellExecuteA(HWND, LPCSTR, LPCSTR, LPCSTR, LPCSTR, INT)
{
    Unsupported(__func__);
}

LRESULT CallNextHookEx(HHOOK, int, WPARAM, LPARAM)
{
    Unsupported(__func__);
}

BOOL SystemParametersInfoA(UINT, UINT, PVOID, UINT)
{
    Bo1_SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

} // extern "C"
