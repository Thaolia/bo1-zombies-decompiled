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
| `linux64-clang` | Portable platform layer + GL backend, desktop debugging |
| `switch` | devkitA64 + libnx, `.nro` |

## Runtime data on the Switch
`sdmc:/switch/bo1zm/`: converted `zone/` from `ffconv`, `main/*.iwd` from the user's install, and the shader
cache. The app must run in application mode (title takeover). Applet mode only gives about 400 MB.
