#pragma once

// The deko3d backend (Nintendo Switch only): the same gfx_backend.h contract as the GL backend, on deko3d instead of
// Mesa/nouveau. Its reason to exist is that the GL path compiles the map's ~13k GLSL shaders at run time and Mesa's
// compiler OOMs doing it (docs/PORTING.md, 2026-10-25); deko3d loads shaders as DKSH that uam compiled offline, so
// there is no runtime GLSL compiler on the console at all. Selected with BO1_GFX_BACKEND=deko3d; the build defines
// BO1_GB_DEKO3D only for the dispatcher, as with BO1_GB_GL.

#include "../gb_backend.h"

namespace gb::deko3d
{
const Backend &Table();
}
