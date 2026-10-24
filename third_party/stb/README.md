# stb_image_write

`stb_image_write.h` v1.16 from https://github.com/nothings/stb, unmodified (last changed upstream in commit
`1ee679c`, 2021-07-12). MIT or public domain, at the user's choice: see the end of the header.

The D3D9 frontend uses it for `D3DXSaveSurfaceToFileA` (`src/gfx_d3d9emu/d3dx9emu.cpp`): the engine's `screenshot`
and `screenshotJpeg` commands on platforms without D3DX.
