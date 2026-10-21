# Switch port — architecture

The port adds layers around the decompiled engine and keeps the engine code as close to upstream as possible.

```
 retail PC install (zone/*.ff, main/*.iwd)
        │  run once on the user's PC
        ▼
 tools/ffconv  (x86 32-bit, reuses the engine's own Load_* code)
        │  native-64 IWffs zones + relocation table
        │  shaders: D3D9 SM3 bytecode → MojoShader → GLSL 4.60 core (std140 UBOs, explicit bindings)
        ▼
 engine (src/, tl/) ── src/platform/compat.h       MSVC-isms for GCC/clang
        │            ── src/platform/{win32,posix,switch}/  Sys_* (threads, files, memory, time)
        │            ── src/platform/stubs/         Steam, DemonWare, live, voice, Bink (BO1_OFFLINE)
        ▼
 src/gfx_d3d (unchanged IDirect3DDevice9 calls)
        ▼
 src/gfx_d3d9emu   D3D9 state, constants → UBOs, resources, Lock, formats   (no GL calls)
        ▼
 src/gfx_backend/gfx_backend.h   explicit low-level contract (see RENDERER.md)
        ├── gl/       v1: GL 4.3 core via SDL2 + glad (desktop and Switch mesa)
        ├── null/     records commands, used by tests
        └── deko3d/   optional phase 7, shaders compiled offline by uam
```

## Build presets
| Preset | Purpose |
|---|---|
| `win32-msvc` | Upstream reference build, D3D9, retail fastfiles |
| `win64-clangcl` | 64-bit cleanup with D3D9 still in place, converted zones |
| `mingw32-gcc`, `mingw32-clang` | Phase 1 syntax pass only (`tools/syntax_check.py`): GNU dialect, 32-bit, MinGW Windows headers |
| `linux64-clang` | Portable platform layer + GL backend, desktop debugging |
| `switch` | devkitA64 + libnx, `.nro` |

## Compiler compatibility (phase 1)
- `src/platform/compat.h` is force-included for GCC and clang (never for MSVC or clang-cl). It maps `__declspec` forms
  explicitly (`align(n)` must become `aligned(n)`; unknown forms fail to compile) and, on MinGW, pulls in `_mingw.h`
  for `__int64` and the calling conventions.
- `src/platform/minmax.h` gives files that relied on the `<windows.h>` `min`/`max` macros the same definitions.
- MSVC-only code stays behind `#if defined(_MSC_VER)` (x87 asm, SEH, `dxerr.h`) with a portable twin when the engine
  needs the result: `qcommon/msg_round.h` reproduces the x87 snapshot rounding bit for bit (`tests/msg_round`).
- Sound: the engine includes `sound/snd_driver.h` (driver API) and `sound/snd_dsp_types.h` (DSP structs). Only
  `snd_driver_xaudio2*.cpp` see XAudio2; a phase 4 backend implements the same `snd_driver.h`.

## Runtime data on the Switch
`sdmc:/switch/bo1zm/`: converted `zone/` from `ffconv`, `main/*.iwd` from the user's install, and the shader
cache. The app must run in application mode (title takeover). Applet mode only gives about 400 MB.
