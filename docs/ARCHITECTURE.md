# Switch port — architecture

The port adds layers around the decompiled engine and keeps the engine code as close to upstream as possible.

```
 retail PC install (zone/*.ff, main/*.iwd)
        │  run once on the user's PC
        ▼
 tools/ffconv  (x86 32-bit, reuses the engine's own Load_* code)
        │  IWffs zones: the same zlib stream, every struct widened to its 64-bit layout
        │  shaders: D3D9 SM3 bytecode → MojoShader → GLSL 4.60 core (std140 UBOs, explicit bindings)
        ▼
 engine (src/, tl/) ── src/platform/compat.h       MSVC-isms for GCC/clang
        │            ── src/platform/{win32,posix,switch}/  Sys_* (threads, files, memory, time)
        │            ── src/platform/stubs/         Steam, Bink, NVAPI, voice, mjpeg, monkey (BO1_OFFLINE)
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
| `win32-msvc-offline` | The reference with `BO1_OFFLINE=ON`, into `build/win32-offline`; run it with `Play.bat offline` |
| `win64-clangcl` | 64-bit cleanup with D3D9 still in place, converted zones |
| `mingw32-gcc`, `mingw32-clang` | Phase 1 syntax pass only (`tools/syntax_check.py`): GNU dialect, 32-bit, MinGW Windows headers |
| `linux64-clang` | Portable platform layer + GL backend, desktop debugging |
| `switch` | devkitA64 + libnx, `.nro` |

The Ninja presets compile through ccache when it is installed (`apt install ccache`); the Visual Studio presets do
not.

## Compiler compatibility (phase 1)
- `src/platform/compat.h` is force-included for GCC and clang (never for MSVC or clang-cl). It maps `__declspec` forms
  explicitly (`align(n)` must become `aligned(n)`; unknown forms fail to compile) and, on MinGW, pulls in `_mingw.h`
  for `__int64` and the calling conventions.
- `src/platform/minmax.h` gives files that relied on the `<windows.h>` `min`/`max` macros the same definitions.
- MSVC-only code stays behind `#if defined(_MSC_VER)` (x87 asm, SEH, `dxerr.h`) with a portable twin when the engine
  needs the result: `qcommon/msg_round.h` reproduces the x87 snapshot rounding bit for bit (`tests/msg_round`).
- Sound: the engine includes `sound/snd_driver.h` (driver API) and `sound/snd_dsp_types.h` (DSP structs). Only
  `snd_driver_xaudio2*.cpp` see XAudio2; a phase 4 backend implements the same `snd_driver.h`.

## ffconv (phase 2)
`tools/ffconv` is a 32-bit Windows tool (MinGW, `tools/ffconv/CMakePresets.json`) that turns the retail zones into
`IWffs` zones and one `shaders.pack`. It never writes to the install.

- **Walk**: the engine's own `db_load.cpp`, copied at build time by `gen/transform_db_load.py` with typed hooks:
  each `Load_Stream` passes the `var*` global it streams from, each `AllocLoad_*` the type it allocates for.
  `ff_stream.cpp` implements the engine's stream API (`db_stream.h`, `db_stream_load.h`) with the same 32-bit
  positions as the engine, and tracks the 64-bit positions alongside. `ff_stubs.cpp` replaces registration, D3D,
  sound and mod hooks.
- **Layouts**: `gen/layouts.py` (libclang) parses `db_load.cpp` for the 32-bit and the 64-bit target and writes, per
  streamed type, its leaf fields with both offsets: pointers, unions of pointers, unions of pointers and wider data
  (converted with the member the loader walks), and bytes. 32-bit padding is copied where the 64-bit layout has room.
- **Stream rules** shared with the 64-bit engine: `src/database/db_ff64.h` (header, ABI tag, pointer encoding,
  alignment, block alignment).
- **Proof**: `--verify` reads the written zone back, narrows it with the same walk and requires the retail stream byte
  for byte, apart from leftover bytes outside a union's walked member.
- **Shaders**: `ff_glsl.cpp` turns each vs_3_0/ps_3_0 into GLSL 4.60 with MojoShader (`third_party/mojoshader`, GLSL
  profile only) and the interface of `docs/RENDERER.md`; `shaders.pack` (`src/gfx_d3d9emu/shader_pack.h`) holds
  one translation per bytecode hash. `gen/check_shader_pack.py` validates a pack with `glslangValidator`.

## Offline build (phase 1c)
`BO1_OFFLINE` is always on outside win32 and an option on win32. It drops these modules from the build:
- online: `DemonWare/`, `src/DW/`, `src/groupvoice/`;
- Windows-only binary SDKs: `steam_api.lib`, `binkw32.lib` (with `src/binklib/`), `nvapi.lib`, `libvpx.lib` (with
  `src/vpx/`);
- dev tools: `src/mjpeg/` (movie capture), `src/monkey/` (test harness), Tracy.

`src/platform/stubs/` defines what the rest of the engine still calls, in the state the engine already handles:

| Stub | Behaves like |
|---|---|
| `steam_api_stub.cpp` | Steam client not running: init fails with `NoSteamClient`, every interface accessor returns NULL |
| `bink_stub.cpp` | `BinkOpen` fails, so no movie plays (main menu background included); `BinkGetError` stays empty because the engine asserts on a pending error before each movie |
| `nvapi_stub.cpp` | No NVIDIA driver: `NvAPI_Initialize` returns `NVAPI_LIBRARY_NOT_FOUND` |
| `groupvoice_stub.cpp` | No sound device: `Sound_Init` fails and voice chat stays off |
| `monkey_stub.cpp` | The harness never starts |
| `mjpeg_stub.cpp` | Nothing is ever encoding; the `mjpeg` command prints a warning |

The stubs define the Steam and Bink entry points inside the exe, so `BO1_OFFLINE` also defines `STEAM_API_NODLL` and
`__RADINEXE__` (the SDKs' own switches that drop `dllimport`). `src/live/` and `CubeMapGenLib` stay compiled (see
`docs/PORTING.md`).

## Runtime data on the Switch
`sdmc:/switch/bo1zm/`: converted `zone/` from `ffconv`, `main/*.iwd` from the user's install, and the shader
cache. The app must run in application mode (title takeover). Applet mode only gives about 400 MB.
