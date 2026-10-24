#pragma once

// MinGW's <dxerr9.h> (r_init.cpp's R_ErrorDescription): the D3D9 error text, which the D3D9 layer provides.

#include "windows.h"

extern "C" const char *DXGetErrorDescription9A(HRESULT hr);
