#pragma once

// The SDL2 side of the Win32 window layer (win32_gui.cpp), for the D3D9 frontend's host (posix_d3d9host.cpp) and the
// input layer (win32_input.cpp).

#include <windows.h>

#include <stdint.h>

struct SDL_Window;
union SDL_Event;

// SDL's video subsystem, started at the first use; false without a display.
bool Bo1_VideoReady();
// Stops SDL's video when no window of the layer uses it, so that the next use starts it on its own thread.
void Bo1_VideoRelease();
// From now on the layer makes its SDL windows OpenGL ones, for the GL backend's context (posix_glhost.cpp).
void Bo1_UseGlWindows();
// The display an HMONITOR stands for (displays count from 0, HMONITORs from 1).
HMONITOR Bo1_DisplayMonitor(int display);
// The SDL window behind a game window, or null.
SDL_Window *Bo1_NativeWindow(HWND hwnd);
// A window's client area, in screen coordinates.
bool Bo1_ClientRect(HWND hwnd, RECT *rect);
// Whether a window shows on a display: a hidden or minimised one does not, nor any window of a headless run.
bool Bo1_WindowShown(HWND hwnd);
// Whether the calling thread started SDL's video, the only one that reads SDL's events.
bool Bo1_OnVideoThread();
// The game window behind an SDL window id, or null.
HWND Bo1_WindowFromSdl(uint32_t windowId);
// The window with the keyboard focus (SetFocus), or null.
HWND Bo1_FocusWindow();

// The input side (win32_input.cpp). Keyboard, mouse and text events, as the messages Windows posts for them:
void Bo1_InputEvent(const SDL_Event &e);
// on the video thread before it reads SDL's events: cursor capture, warps, the clipboard's SDL calls;
void Bo1_InputPump();
// the keyboard focus left the application, so no key stays down;
void Bo1_InputFocusLost();
// the cursor on screen, as GetCursorPos reports it.
POINT Bo1_CursorPosition();
// BO1_INPUT_SCRIPT's events that are due, handed to the input layer (posix_inputscript.cpp, video thread).
void Bo1_InputScriptPump();
// On the video thread, after the event pump: refreshes the controller snapshot (win32_gamepad.cpp).
void Bo1_GamepadPump();
