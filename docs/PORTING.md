# Switch port — plan and status

Target: Nintendo Switch 1 homebrew (`.nro`, Atmosphère, AArch64, libnx), solo offline zombies on the base maps.
Base: upstream commit `102f699` (2026-10-03).

## Decisions
| Date | Decision | Why |
|---|---|---|
| 2026-10-21 | Fork with `upstream` remote, work on branch `switch` | Keep merging upstream engine fixes; upstream already has `src/` at the root |
| 2026-10-21 | v1 scope: solo offline zombies, base maps, no mods, no network | Smallest playable target. Solo already runs over in-memory loopback (`net_chan_mp.cpp:888-899`) |
| 2026-10-21 | Renderer: OpenGL (SDL2 + mesa/nouveau) now, designed for a later deko3d backend | GL can be debugged on desktop with RenderDoc. deko3d is added only if profiling shows the GL driver is CPU-bound |
| 2026-10-21 | Gallium Nine rejected | Removed from Mesa `main` (unmaintained). devkitPro `switch-mesa` is 20.1.0 and has no `ID3DPresent` backend for the Switch |
| 2026-10-21 | `BO1_OFFLINE` stubs moved from phase 0 to phase 1c | Even the "small" subsystems are referenced across the engine (`monkey/` from 7 files, `mjpeg/` from 8). The exact symbol list comes from the first clang link on Linux, so it is not guessed by hand |
| 2026-10-21 | `/Ob1` on `src/gfx_d3d/rb_draw3d.cpp`, only when `MSVC_VERSION == 1943` | MSVC 19.43 (VS 17.13) crashes on this file at `/O2 /Ob2`: C1001 in `p2/main.cpp` line 255, at `rb_draw3d.cpp:244`, and `/MP` reports it as `D8040`. `/O2 /Ob1`, `/O1` and `/Od` all compile. MSVC 19.29 (v142) compiles the file but rejects upstream literal→`char*` conversions in 3 other files |
| 2026-10-21 | `win32-msvc` preset builds into `build/` | `setup.ps1` and `Play.bat` hardcode `build\Release` |
| 2026-10-21 | Fastfiles converted offline (`ffconv`) instead of patched at runtime | The loader copies 32-bit structs in place and stores pointers as packed 32-bit offsets (`db_stream_load.cpp:63-85`) |
| 2026-10-21 | Phase 1 is checked with MinGW i686 GCC and clang (`mingw32-gcc`, `mingw32-clang`), not `linux64-clang` | A GNU-dialect compiler that still has `windows.h` and D3D9 headers, in 32-bit: only compiler-dialect errors show up, not OS-layer (phase 4) or pointer-width (phase 3) ones. The Linux syntax pass moves to phase 4 |
| 2026-10-21 | `MSG_RoundFloatToInt_C` emulates x87 **24-bit** precision | `R_CreateDevice` has no `D3DCREATE_FPU_PRESERVE`, so D3D9 sets the main thread (device creation, `SV_Frame`, snapshot parsing) to single precision. 8 388 608 of the 2^32 inputs round differently at 53-bit |
| 2026-10-21 | XAudio2 split: `sound/snd_driver.h` (engine-facing `SD_*` API) and `sound/snd_dsp_types.h` (plain DSP structs) | `snd.h` pulled XAudio2/XAPO into ~490 files. The June 2010 XAudio2 headers only parse with MSVC's preprocessor, so only `snd_driver_xaudio2.cpp` and `snd_driver_xaudio2_dsp.cpp` include them now; phase 4 replaces those two |
| 2026-10-21 | `static_assert(sizeof(gjk_partition_t) == 112)` is MSVC-only | The Itanium C++ ABI (GCC, clang) reuses `gjk_base_t`'s tail padding: `ninds` lands at 0x48 and the size is 0x60. The struct is only allocated with `sizeof` |
| 2026-10-21 | `phys_gjk.cpp`'s x87 requirement is only enforced on 32-bit MSVC | x64 and AArch64 have no x87. Upstream says this brings back gjk max-iteration warnings on `mp_array`; watch for them from phase 3 on |

## Phases
| # | Phase | Status |
|---|---|---|
| 0 | Repo, toolchains, CMake presets, platform branches in `CMakeLists.txt` | done (2026-10-21) |
| 1 | Compiler compat layer (`src/platform/compat.h`), no behaviour change | done (2026-10-21) |
| 1c | `BO1_OFFLINE` stubs (Steam, DemonWare, live, voice, nvapi, Bink, mjpeg/vpx, monkey, CubeMapGenLib) | todo |
| 2 | `tools/ffconv`: PC fastfiles → native-64 `IWffs` + GLSL 4.60/UBO shaders | todo |
| 3 | 64-bit clean engine on Windows x64 (still D3D9) | todo |
| 4 | Portable platform layer + D3D9-emu frontend + GL backend on Linux x86_64 | todo |
| 5 | Switch build (`.nro`), memory/threads/applet checks, profiling | todo |
| 6 | Stabilisation, all base maps, docs | todo |
| 7 | (optional) deko3d backend behind the same `gfx_backend.h` | todo |

## Verification per phase
- **0**: the `win32-msvc` preset still builds and `Play.bat` reaches the zombies menu and plays Kino der Toten.
  - Windows commands: `cmake --preset win32-msvc` then `cmake --build --preset win32-msvc-release`, then
    `Play.bat`.
  - Linux side, verified 2026-10-21: `cmake --preset linux64-clang` and `cmake --preset switch` (with
    `DEVKITPRO=/opt/devkitpro`) configure and generate. They report platform posix/switch, 64-bit, Clang/GNU.
    Compiling there is expected to fail until phase 1.
  - Windows side, verified 2026-10-21 from WSL with VS Build Tools 2022 17.13 (MSVC 19.43) and the June 2010
    DirectX SDK in `C:\SDKs\DXSDK_Jun10\DXSDK` (passed as `-DDXSDK_DIR`):
    - the generated `BO1Zombies.vcxproj` matches the one from the upstream `CMakeLists.txt`, apart from the order
      of the defines and a path-dependent GUID;
    - `cmake --build --preset win32-msvc-release` links `build/Release/BO1Zombies.exe` (PE32 i386) with 0
      errors.
  - `setup.ps1` copied a PC install (zones `IWffu100` v473, 11.2 GB) into `build/Release` on 2026-10-21. That
    install has no `Redist\DirectX`, so `setup.ps1` now takes `-DxRedist` and was given the SDK's
    `C:\SDKs\DXSDK_Jun10\DXSDK\Redist`.
  - `Play.bat` runs the build and the user played Kino der Toten (2026-10-21).

- **1**: `win32-msvc` builds and plays the same. Every engine file except the two XAudio2 driver files passes
  `-fsyntax-only` with MinGW GCC and clang.
  The `msg.cpp` rounding test passes on all 2^32 floats.
  - Commands (Linux): `cmake --preset mingw32-gcc -DDXSDK_DIR=<June 2010 SDK>`, then
    `tools/syntax_check.py build/mingw32-gcc --exclude sound/snd_driver_xaudio2`; same with `mingw32-clang`.
    Requires `mingw-w64` (apt).
  - Rounding test: `g++ -std=c++20 -O2 -Isrc tests/msg_round/msg_round_test.cpp -o msg_round && ./msg_round`
    (about 2.5 min on 16 threads). On x86 it compares with the real x87 sequence; elsewhere it checks the hash.
  - Verified 2026-10-21:
    - `mingw32-gcc`: 1250 files, only the two XAudio2 driver files fail (excluded on purpose, see decisions);
    - `mingw32-clang`: 1248 files with the exclusion, 0 errors;
    - rounding test with GCC 16 and clang 21 on x86-64: 0 mismatches, hash `0xac2adb9bc5e25a37`.
    - `win32-msvc` Release: 0 errors, same vcxproj options as phase 0; Kino played by the user, same behaviour.
  - clang warnings left for phase 3: `-Warray-bounds` on decompiled vector code, `-Wint-to-pointer-cast`,
    `-Winvalid-offsetof` on `actor_s`.
- **2**: each base zone round-trips. The native-64 load is compared field by field with the 32-bit load dump.
  Every GLSL passes `glslangValidator`. MojoShader rejections are reported.
- **3**: `win64-clangcl` plays 10 rounds of Kino (box, Pack-a-Punch, perks, teleporter) with no assert.
- **4**: `linux64-clang` plays 10 rounds of Kino. RenderDoc captures for each pass. No GL debug errors. The
  null-backend test of `gfx_d3d9emu` passes.
- **5**: Kino plays 10 rounds docked and handheld with no crash, RAM logged every round, target 30 fps at 720p
  docked. Render-thread CPU time is recorded here to decide on phase 7.

## Known upstream bugs (not fixed, noted for later)
- `src/gfx_d3d/rb_draw3d.cpp:239`: `memcpy(gfxCmdBufState.refSamplerState, gfxCmdBufState.refSamplerState,
  sizeof(gfxCmdBufState))` copies a whole struct's size onto one of its own members (overlap and overflow). This
  is decompiler output and must be checked against the binary before being fixed.
- `src/cgame/cg_draw_reticles.cpp` (vehicle gun yaw arc test): the `&&` before `-(weapDef->rightArc)` is missing,
  so the code compares the bool `x < leftArc - rightArc` with a float. The pitch test just below has it. Phase 1
  only added the parentheses C++ already applies.

## Audit findings (2026-10-21, upstream `102f699`)
- Fastfiles (blocker):
  - zlib-only, unsigned `IWffu100` v473;
  - ~404 generated `Load_*` in `src/database/db_load.cpp`, 43 asset types;
  - 32-bit layouts pinned by `static_assert` in `db_assetnames.cpp:161-295`.
- Shaders (blocker): precompiled D3D9 SM3 bytecode created in `r_material.cpp:454-531`. Runtime HLSL compile
  happens only off-fastfile.
- D3D9 is called directly from ~64 files. The state cache is `gfx_d3d/r_state.cpp`. The render thread is
  `RB_RenderThread` (`rb_backend.cpp:5793`).
- Script VM: code positions are written on 4 bytes (`cscr_compiler.cpp:1785`, `cscr_vm.cpp:2123`).
  `VariableUnion` is `sizeof 4` with pointer members (`cscr_variable.h:104`).
- Pointer-width casts: `(unsigned int*)` ×2445, `LODWORD/HIDWORD` ×3158, ~115 `static_assert(sizeof)`.
- x86 code:
  - x87 asm in `qcommon/msg.cpp:15` (must stay bit-exact), `win32/win_shared.cpp:40`, `qcommon/common.h:253`;
  - `cpuid` in `win32/win_configure.cpp`;
  - SSE ×406, hotspot `gfx_d3d/r_model_skin_sse.cpp`.
- OS layer:
  - threads/events centralised in `qcommon/threads.cpp`;
  - file I/O (283 hits), `VirtualAlloc` (113) and `_Interlocked*` (~520) scattered;
  - `<windows.h>` in 57 files outside `win32/` and `libs/`.
- Audio: XAudio2 only (`sound/snd_driver_xaudio2.cpp`), formats PCM, MS-ADPCM and xWMA.
- Input: XInput confined to `win32/win_gamepad.cpp` behind `GPad_*`.
- Bink: not on the boot path (`COM_PlayIntroMovies` is a stub, `qcommon/common.cpp:2250`). Only
  `R_Cinematic_*` uses it.
- Memory: PMem 328 MB (`universal/physicalmemory.cpp:30`), Hunk 15 MB, total ≈ 350–600 MB.
- `*_mp` directories are required: zombies runs on the MP code paths.
