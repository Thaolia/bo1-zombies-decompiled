#pragma once

// <ShlObj.h>: the known folder lookup (FS_GetOsFolderPath).

#include "windows.h"

extern "C" HRESULT SHGetFolderPathA(HWND hwnd, int csidl, HANDLE hToken, DWORD dwFlags, LPSTR pszPath);
