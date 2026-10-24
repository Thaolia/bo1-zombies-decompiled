#pragma once

// <tlhelp32.h>: the thread snapshot.

#include "windows.h"

typedef struct tagTHREADENTRY32
{
    DWORD dwSize;
    DWORD cntUsage;
    DWORD th32ThreadID;
    DWORD th32OwnerProcessID;
    LONG tpBasePri;
    LONG tpDeltaPri;
    DWORD dwFlags;
} THREADENTRY32, *LPTHREADENTRY32;

#define MAX_MODULE_NAME32 255
typedef struct tagMODULEENTRY32
{
    DWORD dwSize;
    DWORD th32ModuleID;
    DWORD th32ProcessID;
    DWORD GlblcntUsage;
    DWORD ProccntUsage;
    BYTE *modBaseAddr;
    DWORD modBaseSize;
    HMODULE hModule;
    char szModule[MAX_MODULE_NAME32 + 1];
    char szExePath[MAX_PATH];
} MODULEENTRY32, *LPMODULEENTRY32;

#define TH32CS_SNAPTHREAD 0x00000004
#define TH32CS_SNAPMODULE 0x00000008

extern "C" {
HANDLE CreateToolhelp32Snapshot(DWORD dwFlags, DWORD th32ProcessID);
BOOL Thread32First(HANDLE hSnapshot, LPTHREADENTRY32 lpte);
BOOL Thread32Next(HANDLE hSnapshot, LPTHREADENTRY32 lpte);
BOOL Module32First(HANDLE hSnapshot, LPMODULEENTRY32 lpme);
BOOL Module32Next(HANDLE hSnapshot, LPMODULEENTRY32 lpme);
}
