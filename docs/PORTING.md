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
| 2026-10-21 | `BO1_OFFLINE` stubs moved from phase 0 to phase 1c | Even the "small" subsystems are referenced across the engine (`monkey/` from 7 files, `mjpeg/` from 8). The exact symbol list comes from a linker, so it is not guessed by hand |
| 2026-10-21 | `/Ob1` on `src/gfx_d3d/rb_draw3d.cpp`, only when `MSVC_VERSION == 1943` | MSVC 19.43 (VS 17.13) crashes on this file at `/O2 /Ob2`: C1001 in `p2/main.cpp` line 255, at `rb_draw3d.cpp:244`, and `/MP` reports it as `D8040`. `/O2 /Ob1`, `/O1` and `/Od` all compile. MSVC 19.29 (v142) compiles the file but rejects upstream literal→`char*` conversions in 3 other files |
| 2026-10-21 | `win32-msvc` preset builds into `build/` | `setup.ps1` and `Play.bat` hardcode `build\Release` |
| 2026-10-21 | Fastfiles converted offline (`ffconv`) instead of patched at runtime | The loader copies 32-bit structs in place and stores pointers as packed 32-bit offsets (`db_stream_load.cpp:63-85`) |
| 2026-10-21 | Phase 1 is checked with MinGW i686 GCC and clang (`mingw32-gcc`, `mingw32-clang`), not `linux64-clang` | A GNU-dialect compiler that still has `windows.h` and D3D9 headers, in 32-bit: only compiler-dialect errors show up, not OS-layer (phase 4) or pointer-width (phase 3) ones. The Linux syntax pass moves to phase 4 |
| 2026-10-21 | `MSG_RoundFloatToInt_C` emulates x87 **24-bit** precision | `R_CreateDevice` has no `D3DCREATE_FPU_PRESERVE`, so D3D9 sets the main thread (device creation, `SV_Frame`, snapshot parsing) to single precision. 8 388 608 of the 2^32 inputs round differently at 53-bit |
| 2026-10-21 | XAudio2 split: `sound/snd_driver.h` (engine-facing `SD_*` API) and `sound/snd_dsp_types.h` (plain DSP structs) | `snd.h` pulled XAudio2/XAPO into ~490 files. The June 2010 XAudio2 headers only parse with MSVC's preprocessor, so only `snd_driver_xaudio2.cpp` and `snd_driver_xaudio2_dsp.cpp` include them now; phase 4 replaces those two |
| 2026-10-21 | `static_assert(sizeof(gjk_partition_t) == 112)` is MSVC-only | The Itanium C++ ABI (GCC, clang) reuses `gjk_base_t`'s tail padding: `ninds` lands at 0x48 and the size is 0x60. The struct is only allocated with `sizeof` |
| 2026-10-21 | Phase 1c symbol list comes from the MSVC link of a `win32-msvc-offline` preset, not from a Linux link | Linux cannot link before the phase 4 OS layer. On Windows the offline exe also runs, so each stub is played against the reference build on the same machine |
| 2026-10-21 | Binary SDKs (Steam, Bink, NVAPI) are stubbed at their own API, not in the engine | The stubs reproduce states the engine already handles: Steam client not running (`win_steam.cpp` no-steam path), movie that fails to open (`R_Cinematic_StartPlayback_Now`), non-NVIDIA adapter (`dx.nvInitialized` 0). No engine file changes. Movies, including the main menu background, no longer play; ffmpeg has a Bink decoder if they are wanted back |
| 2026-10-21 | `DemonWare/`, `src/DW/`, Tracy and `src/vpx/` are dropped with no stub | With them out of the link, nothing else is unresolved: no engine code calls into them in this build |
| 2026-10-21 | `src/live/` stays in the offline build | It holds game state (stats, storage, 35k lines), not network code, and links without DemonWare |
| 2026-10-21 | Voice chat stubbed at the `groupvoice` entry points | `Sound_Init` fails as without a sound device, so `Voice_Init` leaves `g_voice_initialized` at 0. `win_voice.cpp` also holds `Live_GetClientNumForXuid`, so it stays |
| 2026-10-21 | `CubeMapGenLib` stays in the offline build for now | It is neither online nor a binary SDK. Only reflection-probe generation (`r_reflectionProbeGenerate`, needs the map's BSP) uses it. Its filter threads use Win32, so phase 4 decides between porting and a guard |
| 2026-10-21 | ccache wraps every compile when it is installed (`CMAKE_<LANG>_COMPILER_LAUNCHER`, set before `project()`) | It speeds up rebuilds of the ~1250 files on the Ninja presets (`mingw32-*`, `linux64-clang`, `switch`). The Visual Studio generator of the `win32-msvc*` presets ignores launchers, and the Windows side has no ccache. `compile_commands.json` does not list the launcher, so `tools/syntax_check.py` is unchanged |
| 2026-10-21 | v1 zones: every zombies map (the 11 maps, Dead Ops Arcade included) and the zones they load; no campaign | The engine is the MP code base with the SP zombies parts: `+devmap cuba` stops with `Com_ERROR: Exceeded limit of 16 'font' assets` |
| 2026-10-21 | `IWffs` zones are the retail zlib stream with every struct widened to its 64-bit layout, read by the same `db_load.cpp` traversal compiled 64-bit. Replaces the relocation-table format | Loading is more than copying: pool registration (`DB_AddXAsset` copies headers into pools), aliases to assets of other zones (`DB_ConvertOffsetToAlias`), insert pointers, script-string remapping, textures, shaders and sound banks all happen inside the `Load_*` traversal. A relocation loader would re-implement all of it |
| 2026-10-21 | `ffconv` runs the real `db_load.cpp` (32-bit), transformed at build time | `Load_Stream`, `AllocLoad_*` and the pointer conversions get typed hooks, so each struct is captured with its C++ type; the engine's side-effect hooks (registration, D3D, sound, script strings) are stubbed. `db_load.cpp` itself is not edited |
| 2026-10-21 | Struct layouts come from clang's record-layout dump of `db_load.cpp` (`-fdump-record-layouts-complete`) for the 32-bit and the 64-bit target | The same compiler front end and headers as the engine. The 32-bit table is checked against every `Load_Stream` size of the real loader; the 64-bit one against the phase 3 `static_assert`s. Bitfields exist in some records, so each 64-bit ABI (MSVC x64 for phase 3, AArch64 for the Switch) gets its own dump |
| 2026-10-21 | 32-bit padding is copied into the 64-bit layout where it fits | `Glass` keeps a value in byte 62, which the decompiled struct calls padding: probably a field no code reads. Copying padding keeps the round trip strict |
| 2026-10-21 | Leftover bytes outside the member of a union the loader walks are not kept | `cLeafBrushNode_s` leaves hold stale `children` bytes after their 4-byte `brushes` pointer, which needs 8 bytes on 64-bit. No code reads an inactive member. `--verify` counts these bytes apart |
| 2026-10-21 | Shaders: one `shaders.pack` for all zones, keyed by FNV-1a 64 of the D3D9 bytecode (`src/gfx_d3d9emu/shader_pack.h`) | 13 704 unique shaders across 65 zones; one file instead of thousands on the SD card |
| 2026-10-21 | MojoShader vendored unmodified (commit `ad5dff8`, GLSL profile only); `centroid` is cleared in the bytecode before translation and put back by ff_glsl.cpp | Its GLSL profile rejects `centroid` (28 pixel and 2 vertex shaders), which GLSL 4.60 supports |
| 2026-10-21 | `phys_gjk.cpp`'s x87 requirement is only enforced on 32-bit MSVC | x64 and AArch64 have no x87. Upstream says this brings back gjk max-iteration warnings on `mp_array`; watch for them from phase 3 on |

## Phases
| # | Phase | Status |
|---|---|---|
| 0 | Repo, toolchains, CMake presets, platform branches in `CMakeLists.txt` | done (2026-10-21) |
| 1 | Compiler compat layer (`src/platform/compat.h`), no behaviour change | done (2026-10-21) |
| 1c | `BO1_OFFLINE` build (Steam, DemonWare, voice, NVAPI, Bink, mjpeg/vpx, monkey, Tracy out; `src/platform/stubs/`) | done (2026-10-21) |
| 2 | `tools/ffconv`: PC fastfiles → native-64 `IWffs` + GLSL 4.60/UBO shaders | done for the win64 ABI (2026-10-21); AArch64 layout tables wait for phase 4 |
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
- **1c**: the `win32-msvc-offline` build links with no Steam, Bink, NVAPI or libvpx library and plays Kino like the
  reference. The `win32-msvc` vcxproj is unchanged.
  - Windows commands: `cmake --preset win32-msvc-offline -DDXSDK_DIR=<June 2010 SDK>` then
    `cmake --build --preset win32-msvc-offline-release`; play with `Play.bat offline`.
  - Headless comparison (from the worktree root, one run per build, then compare the two logs):
    `powershell -File tools\headless.ps1 -RunDir build\run\offline -ExeDir build\win32-offline\Release -Zombies -Client
    -ShotsAtMs "30000 55000" -Commands "+set QuitOnError 1 +devmap zombie_theater" -AutoQuitMs 60000
    -SaveLog build\run\offline.log`; the same without `-ExeDir`, with `reference` in place of `offline`.
  - Main menu (it plays the `frontend` movie, which a `+devmap` run skips): the same command with
    `-Commands "+set QuitOnError 1"` and `-AutoQuitMs 60000`.
  - Verified 2026-10-21:
    - the first offline link listed 84 unresolved externals: Steam (14), Bink (18), NVAPI (5), groupvoice (22),
      monkey (9), mjpeg/yuv (9), CubeMapGenLib (7). None from DemonWare, `src/DW`, Tracy or `src/vpx`;
    - with the stubs: 0 errors. The exe imports neither `steam_api.dll` nor `binkw32.dll` (`Secur32.dll` went with
      DemonWare);
    - `win32-msvc` vcxproj identical to phase 1 apart from CMake's own regeneration inputs;
    - `tools/syntax_check.py` with MinGW GCC and clang, `-DBO1_OFFLINE=ON`, XAudio2 driver excluded: 1181 files
      each, 0 errors (stubs included);
    - headless client on Kino, two runs per build, Intel Iris Xe: all exit 0, no window, no crash. Error,
      warning, Steam, voice and Bink lines are the same except the Steam init message. The other log differences
      are timing (AI path warnings, random loading tips, frame counts), `in_monkey` no longer registered, and the
      `SaveGame check` line (see known upstream issues). The 55 s screenshot shows Kino with the HUD (500 points,
      M1911 8/24).
    - the user's play test found an assert on the zombies map list (`r_cinematic.cpp(504)`,
      `!binkError || binkError[0] == '\0'`): the first Bink stub returned an error string from `BinkGetError`, and
      the engine checks that no error is pending before each movie. The headless main-menu run reproduced it in
      23 s. `BinkGetError` now stays empty and the stub logs once that movies are skipped; the main-menu run then
      quits normally after 60 s and Kino still runs. The user then played the fixed offline build (map list
      included) with no problem.
- **2**, in steps:
  - **2a** layout tables: `tools/ffconv/gen/layouts.py` turns the clang dumps into per-type tables (32-bit and
    64-bit offset of every leaf field, pointer or bytes). Check: every 32-bit size matches the real `sizeof` and the
    sizes `db_load.cpp` streams.
  - **2b** traversal: `ffconv` (32-bit, MinGW) inflates a zone and walks it with the transformed `db_load.cpp`.
    Check: on every zombies zone the walk consumes exactly `XFile.size` bytes and ends each block at
    `XFile.blockSize`, with the asset count and names of the real load.
  - **2c** widening: write the `IWffs` stream (64-bit structs, translated pointer offsets, `db_ff64.h` alignment).
    Check: narrowing it back gives the retail stream byte for byte, on every zombies zone.
  - **2d** shaders: MojoShader turns every unique vs_3_0/ps_3_0 into GLSL 4.60 (`vec4 c[256]` std140 UBO per
    stage, samplers at their `s#` binding, alpha test as `discard`), in a shader pack keyed by bytecode hash.
    Check: every GLSL passes `glslangValidator`; rejected shaders are listed.
  - **2e** the `ffconv` command line over a whole install, and its README section.
  - The final proof that a 64-bit engine loads the zones belongs to phase 3 (`win64-clangcl` plays Kino on them).
  - Commands (WSL): `cd tools/ffconv && cmake --preset ffconv && cmake --build --preset ffconv`, then
    `build/ffconv/ffconv.exe --verify --game <install> --out <dir> < /dev/null` (Windows paths) and
    `tools/ffconv/gen/check_shader_pack.py <dir>/shaders.pack`. Needs `python3-clang` and `glslang-tools`.
  - Verified 2026-10-21 on the PC install (65 zones: code, patch, menus and the 11 zombies maps with their
    `_patch` and `en_` zones):
    - layouts: 240 streamed types; every generated 32-bit size and alignment `static_assert`s against the i686
      compiler;
    - walk: every zone read to its last body byte, every block except temp ending exactly at its retail size;
      36 036 assets, 2 486 702 pointers translated, no untyped allocation, no misaligned 64-bit chunk;
    - round trip: 65/65 zones narrow back to the retail stream (1 429 254 369 body bytes), apart from 1 058 560
      bytes outside walked union members, 149 201 of which held leftover data;
    - output: 640 MB of zones (3.3 GB retail), 6 minutes with `--verify`;
    - shaders: 13 704 unique (356 vs_3_0, 13 348 ps_3_0), all translated, all accepted by `glslangValidator
      --target-env opengl`; `shaders.pack` is 127 MB.
  - Not done in phase 2: AArch64 layout tables (`--abi aarch64`), which need the engine headers to parse for
    AArch64 (phase 4); texture size reduction (`--max-tex`), left until Switch memory is measured (phase 5).
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

- `SaveGame check: segment 2 ... DIFFERENT` (`g_sp_savegame.cpp`, KB diagnostic): `r_exposureValue` is a
  `DVAR_SAVED` dvar that the renderer frontend (`r_scene.cpp:4126`) updates while the server thread saves and
  rewrites the globals, so the check can see two values. Seen on both reference runs and neither offline run
  (2026-10-21). It is a race in the diagnostic, not in the save.

- `/PDBALTPATH:%_PDB%` (upstream `CMakeLists.txt`): the Visual Studio generator escapes `%` to `%%`, so the exe
  records the PDB as `%%%BO1Zombies.pdb%%%` and debuggers or `llvm-symbolizer` do not find `BO1Zombies.pdb`.
  Workaround: copy the PDB next to the exe under that literal name. Fixing it changes the reference build.

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
