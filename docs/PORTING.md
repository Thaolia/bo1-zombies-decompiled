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
| 2026-10-21 | `ffconv` runs the real `db_load.cpp` (32-bit), transformed at build time | Each `Load_Stream` gets a typed hook, so each struct is captured with its C++ type; the engine's side-effect hooks (registration, D3D, sound, script strings) are stubbed. `db_load.cpp` itself is not edited |
| 2026-10-21 | 64-bit stream allocations are aligned to max(retail alignment, 8), replacing max(retail, alignof of the allocated type) | The typed rule needed the type at each of the 332 `AllocLoad_*` calls of `db_load.cpp`; no streamed type needs more than 8 without retail already asking for it, so the 64-bit loader only changes `DB_AllocStreamPos`. All zones were converted and verified again |
| 2026-10-21 | Struct layouts come from clang's record-layout dump of `db_load.cpp` (`-fdump-record-layouts-complete`) for the 32-bit and the 64-bit target | The same compiler front end and headers as the engine. The 32-bit table is checked against every `Load_Stream` size of the real loader; the 64-bit one against the phase 3 `static_assert`s. Bitfields exist in some records, so each 64-bit ABI (MSVC x64 for phase 3, AArch64 for the Switch) gets its own dump |
| 2026-10-21 | 32-bit padding is copied into the 64-bit layout where it fits | `Glass` keeps a value in byte 62, which the decompiled struct calls padding: probably a field no code reads. Copying padding keeps the round trip strict |
| 2026-10-21 | Leftover bytes outside the member of a union the loader walks are not kept | `cLeafBrushNode_s` leaves hold stale `children` bytes after their 4-byte `brushes` pointer, which needs 8 bytes on 64-bit. No code reads an inactive member. `--verify` counts these bytes apart |
| 2026-10-21 | Shaders: one `shaders.pack` for all zones, keyed by FNV-1a 64 of the D3D9 bytecode (`src/gfx_d3d9emu/shader_pack.h`) | 13 704 unique shaders across 65 zones; one file instead of thousands on the SD card |
| 2026-10-21 | MojoShader vendored unmodified (commit `ad5dff8`, GLSL profile only); `centroid` is cleared in the bytecode before translation and put back by ff_glsl.cpp | Its GLSL profile rejects `centroid` (28 pixel and 2 vertex shaders), which GLSL 4.60 supports |
| 2026-10-21 | Phase 3 builds with MSVC x64 (`win64-msvc`), not clang-cl; 64-bit problems are listed with clang for x86_64 MinGW (`mingw64-clang`) | The VS Build Tools here have no clang-cl (only the v143 toolset). clang rejects every pointer cast to a smaller integer in C++, which gives the full list; MSVC x64 then builds the exe that runs with D3D9 |
| 2026-10-21 | `BO1_OFFLINE` is forced on 64-bit Windows | The Steam, Bink and NVAPI libraries the 32-bit build links have no 64-bit counterpart there |
| 2026-10-21 | `phys_gjk.cpp`'s x87 requirement is only enforced on 32-bit MSVC | x64 and AArch64 have no x87. Upstream says this brings back gjk max-iteration warnings on `mp_array`; watch for them from phase 3 on |
| 2026-10-21 | Phase 3 edits must leave the 32-bit machine code unchanged, checked per file with `tools/codediff.py` (mingw32-gcc, `.text` of the file at `HEAD` vs the working tree) | MSVC x86 is the reference and cannot be diffed from WSL; GCC i686 compiles the same source. An "identical" result proves the edit is a pure retyping. Any difference is read and explained before it is kept |
| 2026-10-21 | Struct-size `static_assert`s become `== BO1_ARCH_SIZE(size32, size64)` (`src/platform/arch_size.h`), with the 64-bit size clang measures for x86_64 MinGW | Deleting them would lose the layout check on both widths. Each growth comes from pointer members and the alignment they bring. The Switch (LP64) can differ only through `long`/`long double`, and the assert then fires there |
| 2026-10-21 | The save-format asserts (`cscr_save.cpp`, `game_sp/g_sp_save*`) keep their 32-bit sizes for now | They pin the layout of save images, not just memory. The 64-bit save format is decided with the script VM's 8-byte `VariableUnion` |
| 2026-10-21 | Decompiled offsets are replaced by the fields they name, after checking the offset against the 32-bit layout (`*(float *)(p + 240)` → `p->m_inv_mass`) | The field is the only form that is right on both widths. codediff proves the 32-bit offset was the field's |
| 2026-10-22 | Script bytecode: pointer-sized operands are `sizeof(uintptr_t)` (code positions, function position/scope, thread-call parameter count, `case` value and position pairs, animation slot), written by `EmitCodepos` and read by `Scr_ReadCodePos`/`Scr_ReadUnsigned`; `OP_GetInteger`, `OP_object` and jump/switch offsets stay 4 bytes (`EmitInt`/`Scr_ReadInt`) | Every reader advances by the width its writer used, so `Scr_GetNextCodepos` and the VM stay in step. The stream is packed (`TempMallocAlignStrict` aligns to 1): x86-64 and AArch64 allow unaligned plain loads, and no bytecode operand is used atomically |
| 2026-10-22 | Script function handles (`Scr_GetFunctionHandle`, `animscriptfunc`, `func_table` users) stay `int` | They are offsets into `programBuffer` (`Scr_ExecThread` reads `programBuffer[handle]`), not pointers |
| 2026-10-22 | `phys_mem_new` lock-free free list: on 64-bit the pointer takes the low 48 bits and a 16-bit ABA tag the high bits of one word, still updated by the 64-bit CAS; on 32-bit it is the retail `{pointer, tag}` pair | User-space addresses fit in 47 bits on Windows x64 and 39 on the Switch, so no 16-byte CAS is needed; `phys_tagged_pack` asserts it |
| 2026-10-22 | Worker commands (`jqWorkerCmd::dataSize`) and render commands (`R_GetCommandBuffer`) are sized with `sizeof` of the struct the consumer reads | `Sys_AddWorkerCmdInternal` copies `dataSize` bytes: the 32-bit literals cut the 64-bit command structs short |
| 2026-10-22 | Capacity asserts: `cg_s` gets `CG_ARRAY_SLOT = BO1_ARCH_SIZE(466048, sizeof(cg_s))`, `clientActive_t` gets `CL_ACTIVE_RETAIL_SIZE = BO1_ARCH_SIZE(0x1A6100, 1737216)` | Both structs outgrow the retail budget on 64-bit (`cg_s` 465 920 → 475 008). A bare `sizeof` would shrink the 32-bit allocations by 128 bytes and change the reference build |
| 2026-10-22 | Expression compile buffers are `BO1_ARCH_SIZE` doubles (`cmd.cpp` 3072/6144, `live_news.cpp` 1024/2048) | `expressionEntry` (16 → 32) and `expressionRpn` (12 → 24) double on 64-bit, so the retail buffers would run out (`Expression_Alloc` asserts) |
| 2026-10-22 | A struct with a base class is pinned with `BO1_ABI_SIZE(msvc32, msvc64, itanium32, itanium64)` (`gjk_brush_t`, `gjk_partition_t`, `phys_free_list<user_rigid_body>::T_internal`) | MSVC x64 and the Itanium ABI (MinGW, Linux, the Switch) differ there: Itanium reuses the base's tail padding (`gjk_brush_t` 128 vs 112). The MSVC-ABI sizes are measured with `clang --target=x86_64-pc-windows-msvc` against the VS headers, which reproduces MSVC's own assert failures |
| 2026-10-22 | `ffconv`'s 64-bit tables (MinGW target) are valid for the MSVC x64 engine | All 241 streamed types flatten to the same layout under `x86_64-w64-mingw32 -mms-bitfields` and `x86_64-pc-windows-msvc` (no base classes, MS bitfields, no `long double`) |
| 2026-10-22 | 64-bit builds skip SP saves (`G_SP_SaveGame` returns `SP_SAVE_MOD_SKIPPED`); the save-image asserts become `BO1_SAVE_IMAGE_ASSERT`, binding 32-bit builds only | The SP save system writes the 32-bit image through field tables with 4-byte pointers (`s_tagFields`, `VariableUnion` of 4); a 64-bit save would be corrupt. Skipping is a state scripts already handle silently (the horde mod's skip). Zombies does not need saves; a 64-bit format is future work |
| 2026-10-22 | MMX/SSE skinning (`r_model_skin_sse.cpp`) is 32-bit x86 only (`R_SKIN_MMX`); other targets use the C skinning path | MSVC x64 has no MMX intrinsics (`__m64`, `_mm_empty`). A SSE2/NEON port is a phase 5 performance item |
| 2026-10-22 | 64-bit slot pools: `get_slot_pool` raises the alignment to 8 and rounds the slot size to 8 | `extra_info::m_allocation_owner` is CASed and the free-list link is 8 bytes: callers passing alignment 4 (`chull_t`, `inds`, `pathlink_s`, transient blocks) gave misaligned atomics, which AArch64 rejects, and a 4-byte `inds` slot got its link written over `m_slot_pool_owner` |
| 2026-10-22 | Physics, ragdoll, gjk-cache and foliage handles are `intptr_t`/`uintptr_t` end to end; `FxElem` is `#pragma pack(4)` so it stays 44 bytes | The handles are pointers on 64-bit. `FxElem` growing to 56 would overflow the 16-bit FX pool handles (`4096 × 64 / 4`); packed, its 8-byte `physObjId` is unaligned, which x64 and AArch64 allow for plain loads |
| 2026-10-22 | `DynEntityClient::physObjId` and `PhysConstraint::constraintHandle` stay `int` for now | Both are streamed (`Load_DynEntityClientArray`, `Load_PhysConstraintArray`); `layouts.py` refuses an integer that grows from 4 to 8 bytes. Options: a `LEAF_HANDLE` kind in ffconv (copy 4 → 8 bytes with sign extension), or keep the streamed field and hold the real handle in a parallel runtime array |
| 2026-10-22 | `ffconv` rescales integer fields that hold a byte distance inside a widened array (value fixups, `layouts.py` `VALUE_FIXUPS`); `GfxAabbTree::childrenOffset` is the only one | The retail value is `children - tree` in bytes of 40-byte nodes; copied as is, the 64-bit renderer walked into garbage (`R_AddAabbTreeSurfacesInFrustum_r`). Rescaling in the converter keeps `db_load.cpp` and the renderer unchanged, and `--verify` narrows it back byte for byte |
| 2026-10-22 | `broad_phase_base` users read the real fields, not `x[-1].field` | The Itanium ABI (GCC, clang) reuses its tail padding (`m_list_bpi_head` at 76 instead of MSVC's 80); the typed form is right for both ABIs and leaves MSVC's code unchanged |

## Phases
| # | Phase | Status |
|---|---|---|
| 0 | Repo, toolchains, CMake presets, platform branches in `CMakeLists.txt` | done (2026-10-21) |
| 1 | Compiler compat layer (`src/platform/compat.h`), no behaviour change | done (2026-10-21) |
| 1c | `BO1_OFFLINE` build (Steam, DemonWare, voice, NVAPI, Bink, mjpeg/vpx, monkey, Tracy out; `src/platform/stubs/`) | done (2026-10-21) |
| 2 | `tools/ffconv`: PC fastfiles → native-64 `IWffs` + GLSL 4.60/UBO shaders | done for the win64 ABI (2026-10-21); AArch64 layout tables wait for phase 4 |
| 3 | 64-bit clean engine on Windows x64 (still D3D9) | in progress: `win64-msvc` runs Kino as a dedicated server (log identical to the reference) and as a client (60 s, same pictures as the reference); 10 rounds next |
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
  - The final proof that a 64-bit engine loads the zones belongs to phase 3 (`win64-msvc` plays Kino on them).
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
- **3**: `win64-msvc` plays 10 rounds of Kino (box, Pack-a-Punch, perks, teleporter) with no assert.
  - Inventory (Linux): `cmake --preset mingw64-clang -DDXSDK_DIR=<SDK>`, then
    `tools/syntax_check.py build/mingw64-clang --log-dir <dir>` (add `-ferror-limit=0` to the preset's flags for the
    full list). Literal sizes: `tools/litsizes.py build/mingw64-clang`. 4-byte views of 64-bit data:
    `tools/ptrcasts.py build/mingw64-clang`.
  - Every edited `.cpp`: `tools/codediff.py build/mingw32-gcc <files>`. The 32-bit syntax pass of phase 1 must stay
    at 0 errors outside the two XAudio2 driver files.
  - Progress 2026-10-21 (loader, memory allocators, physics, jobqueue, assorted ui/sound/win32, struct-size
    asserts):
    - unique 64-bit errors 2 190 → 2 098; `db_load.cpp` and every struct-size assert are clean except the save-format
      ones and the `cg_s`/`clientActive_t` capacity checks;
    - 32-bit syntax pass, GCC and clang: 1 250 files, only the XAudio2 driver files fail, as in phase 1;
    - `ffconv` rebuilt from the edited `db_load.cpp` (sizes as `sizeof`/`offsetof`, alignment min 8): 65 zones,
      0 failed `--verify`; `shaders.pack` 13 704 shaders, 0 rejected by `glslangValidator`;
    - codediff over the 34 edited files: 19 identical. The other 15 differ only in register allocation, block
      order and stack frame size, except where the 32-bit code changes on purpose:
      `ui_gameinfo_mp.cpp` (a hard-coded exe address `0x98a4b50` becomes `&ui_arenaInfos[...]`), `assertive.cpp` (the
      decompiled `lineOffset[3] + n + 3` store becomes `filenameBuffer[n - 1]`, the same byte only with MSVC's
      stack layout), `win_stream.cpp` (MinGW's `setjmp` macro adds its frame argument). Values the old
      `unsigned int *` stores could alias (`m_list_count`, `blockSize`) are no longer reloaded.
  - `litsizes.py` over the whole tree (764 files, 97 min): 294 calls. About 170 are allocations, copies or sorts
    (`UI_Alloc` 46 in `ui_shared_obj.cpp`, `Hunk_*` 42, `Alloc` 14 in the collmap/xmodel loaders, `memcpy`, `qsort`,
    `PMM_ALLOC`, `SV_LocateGameData`, ...) and need `sizeof`. The rest are false positives where the literal is a
    buffer length or a stride next to a struct pointer (`MSG_Init` 15, `R_SetStreamSource` 12, `LockRect` 10,
    `XAnimClearTreeGoalWeights`, `fwrite`).
  - Progress 2026-10-22 (script VM and compiler, expression evaluator, hunk allocators, dvars and files, physics
    free lists, renderer, ui/xanim/sound/live, game/client/server, every real literal size):
    - unique 64-bit errors in engine code 2 046 → 69 (`build/mingw64-clang-full/unique_errors3.txt`; DirectX SDK
      header lines are not counted). Of the 69: 16 are the kept save-format asserts, 8 are the XAudio2 driver files'
      SDK header parse errors, 1 is `eval.cpp`'s known upstream bug, and 44 are listed below;
    - 32-bit syntax pass, GCC and clang: 1 250 files, only the two XAudio2 driver files fail, as in phase 1;
    - codediff over all 1 248 translation units (27 min, `xargs -P 6`): every file no one edited is identical
      (the 4 SSE files with `--cflags=-msse2`; the 71 libtomcrypt `.c` files differ only in `.rdata` offsets, because
      `__FILE__` holds the longer path of the codediff tree). 82 edited files differ; each difference was read and
      explained: register allocation, scheduling, block order, frame size, reloads the old `unsigned int *` aliasing
      forced, MinGW's `setjmp` frame argument, `(int)"literal"` stores becoming immediates, and the intended changes
      (decompiled absolute exe addresses and out-of-bounds aliases replaced by their fields, `Scr_EvalMethod` passing
      `scr_entref_t` by value as `VM_Execute` does);
    - `ffconv`: the generated layout tables and the transformed `db_load.cpp` are byte-identical to the build
      verified above, so no streamed struct changed and the 65 zones stay valid.
  - Progress 2026-10-22, second step (physics/ragdoll/foliage handles, slot pool alignment, MSVC x64 build):
    - `win64-msvc` builds and links: `build/win64-msvc/Release/BO1Zombies.exe`, PE32+ x86-64, 0 errors. It needed
      per-ABI sizes for three physics classes, the C skinning path instead of MMX, and the SP save skip (decisions
      above). The `win32-msvc` reference also builds (one MSVC-x86-only fix: `<intrin.h>` in
      `phys_transient_allocator.cpp`);
    - MSVC-ABI check without Windows: `clang++ -fsyntax-only --target=x86_64-pc-windows-msvc -fms-compatibility
      -fms-extensions -fms-compatibility-version=19.43 -isystem <VC>/include -isystem <SDK>/{ucrt,um,shared}
      -idirafter <DXSDK>/Include -DWIN32 -D_CONSOLE -DBO1_MP -DBO1_OFFLINE -I. -Isrc -Isrc/libs <file>` reproduces
      MSVC x64's own static_assert failures;
    - unique 64-bit errors in engine code 69 → 38 (`unique_errors4.txt`): 8 XAudio2 driver SDK-header lines, 1
      `eval.cpp` (upstream bug), and 29 listed below;
    - 32-bit syntax pass, GCC and clang: unchanged (only the XAudio2 driver files fail);
    - codediff over all 1 248 translation units: every unedited file identical (libtomcrypt `.rdata` artifact as
      before); the edited files that differ are the wave 2 set plus `cg_event.cpp`, whose bitfield writes through
      `*((_DWORD *)cent + 201)` became the named bits (`orl $0x100000` → `orb $0x10` on the same bit; on 64-bit the
      old offset landed inside the grown struct).
  - Progress 2026-10-22, third step (first run of the 64-bit exe on Kino, dedicated headless server under `cdb`,
    `+devmap zombie_theater`, IWffs zones through `tools/headless.ps1 -ZoneDir`):
    - it loads every zone, compiles every script, spawns the map and runs 60 s of game time (`level.time 64950`),
      then quits through `bo1_autoquit` with no assert and no fault. Its log matches the `win32-msvc` exe run with
      the same arguments on the retail zones (numbers masked, lines counted): the only differences are the Steam
      init message, `in_monkey` (not in the offline build) and two debug dvars whose 32-bit values are float noise
      (`r_lightTweakSunColor`, `r_materialXYZ`: `1e-N` against 0);
    - the bugs on the way were all code clang accepts on 64-bit, because no pointer is narrowed:
      - Win32 structure sizes written as numbers (`WNDCLASSEXA::cbSize`, `MODULEENTRY32::dwSize`, the mixer
        `cbStruct` in `win_voice.cpp`): now `sizeof`;
      - the asset size table: the retail linker folded identical size functions, so the decompiled table returns
        `sizeof(SndDriverGlobals)` for `GfxImage` and the like; `db_assetnames.cpp` now gives each type its own
        `DB_SizeofXAsset<T>` (and `XAnimTreeSize` `sizeof(XAnimTree_s)`), with `BO1_ARCH_SIZE` asserts;
      - `db_registry.cpp`: the asset free list links through `next`, not `asset.type`; pool indices are pointer
        differences, not `>> 4` of a byte offset;
      - element sizes: 21 files' `qsort` literal sizes, `XAnimCreateAnimsWithValues`, `DObjCreate` (now the `DObj`
        fields), `localModels` (`sizeof(XModel *) + 1` per model), `objBuf` (now `DObj objBuf[3072]`, it was 3072
        rows of 31 dwords, so 64-bit DObjs overlapped), `Hunk_SetDataForFile` (`offsetof(fileData_s, name)`, the
        name overran into the next allocation and the aitype and traversal lookups failed), the script debugger's
        breakpoint array and the file lists of `FS_ListFilteredFiles`/`Sys_ListFiles` (`4 * n` for pointer
        arrays; the list block grows to `0x31000` so 0x4001 pointers still fit one hunk block), the physics debug
        buffers (`R_DebugAlloc` with 32-bit `sizeof`s);
      - offsets: the runtime field tables (`g_spawn_mp`, `g_client_fields`, `sentient_fields`, `actor_fields`,
        `g_hudelem`, `pathnode`, `g_vehicle_path`, `cg_scr_main`, `cg_spawn`, `cg_visionsets`, `flame_system`,
        `bg_misctables`: 699 entries) are `offsetof`, each proven equal to the old literal by an i686
        `static_assert`; `ik_import.cpp` reads the DObj fields through `offsetof`; `_findfirst64i32` handles are
        `intptr_t`;
      - `G_ExitOnComError` tests `g_connectpaths` for NULL before using it (upstream crash on an early error).
    - each fix keeps the 32-bit code identical (`codediff`, or the edit alone built both ways when the file already
      differs from HEAD).
    - `tools/ptrcasts.py` (libclang, x86_64) lists the casts that read 64-bit data through a 4-byte view: pointer
      arrays and pointer-holding structs cast to `int *`/`_DWORD *`, byte offsets into such structs, `qsort`/`bsearch`
      literal element sizes, `4 * n` pointer-array allocations. Whole tree: 844 casts in 100 files (100 min), of
      which 292 in `eval.cpp`, 207 the `centity_s` flag word (`*((_DWORD *)cent + 201)`), 128 in `db_load.cpp`.
      `tools/litsizes.py` now also sees out-parameter allocators (`R_DebugAlloc((void **)&p, N, ...)`).
      Stack buffers sized for a 32-bit struct (`unsigned __int8 dst[9896]` then `memset(dst, 0,
      sizeof(playerState_s))`) are found by replaying the `mingw64-clang` compile commands with `-fsyntax-only
      -Wno-everything -Wfortify-source` ("will always overflow"): one hit, `MSG_ReadDeltaPlayerstate`, now
      `dst[sizeof(playerState_s)]` (the overflow clobbered `msg` and the /GS cookie on the client).
      - `db_load.cpp`'s 128 are all false positives (pointer-sized fields passed to `DB_ConvertOffsetTo*`, which
        take `void *` and read a `uintptr_t`; stream addresses retyped; members at offset 0 on both widths, proven
        by i686/x86_64 asserts): the file is unchanged and the ffconv transform still applies;
      - `eval.cpp`'s 292 land on the same `Eval` fields on MSVC x86 and x64 (the struct's only pointer is in an
        8-byte union); the one pointer store through a dword is now `uintptr_t`, which removes the last 64-bit
        error of the inventory there;
      - the `centity_s` (206 sites in 31 files, plus 10 in other subsystems) and `flameGeneric_s` (15) flag words
        are a `flagWord` member in an anonymous union over the bitfields, with `offsetof` asserts (804/864,
        92/112); the other 300 hits were fixed or classified: literal struct sizes, `qsort` element sizes
        (`live_stats` ×21), pointer-array allocations and alignments, `worldModel[1]`/`lodInfo` reads, fields
        written at their 32-bit byte offsets (`lure->script_noteworthy`, `gameMsgDef`, `viewInfoIndex`, the deferred
        `CG_DestructibleRadiusDamage` event); 124 hits remain, all false positives (same field on both widths,
        pointer locals reused as float scratch, fixed-size records), upstream bugs or the known render streams;
      - 32-bit code: identical for every edit except register/block-order differences, gcc store merging from the
        unions (see `-fno-strict-aliasing` below) and `destructible.cpp`, where two coordinates of the deferred
        radius-damage event are now copied as floats instead of dwords (same bits for every non-signalling value).
  - Progress 2026-10-22, fourth step (first 64-bit crash on Kino's world, `ffconv` value fixups):
    - `GfxAabbTree::childrenOffset` (byte offset from a node to its first child, same array) was copied with its
      32-bit value; the nodes grow 40 → 48 bytes, so the 64-bit renderer read garbage children. `ffconv` now
      rescales it (`docs/ARCHITECTURE.md`, value fixups);
    - audit of the 1 715 non-pointer fields of the 312 records the 241 streamed types reach (libclang, names with
      offset/size/bytes/stride/count/index/handle, then every integer field read by hand). Only `childrenOffset`
      measures a widened array. The others keep their value:
      - byte sizes or offsets into raw data that does not change: `srfTriangles_t::vertexLayerData`/
        `stream2ByteOffset`, `GfxWorldDraw::vertexLayerDataSize`/`vertexStream2DataSize`,
        `GfxLightGrid::rawRowDataSize` (12-byte `GfxLightGridRow`s of shorts and ints), `GfxWaterBuffer::bufferSize`,
        `GfxImage::baseSize`/`loadedSize`, `GfxImageLoadDef::resourceSize`, `Gfx*ShaderLoadDef::programSize`
        (dwords), `PathData::visBytes`, `clipMap_t::clusterBytes`, `MapEnts::numEntityChars`, `RawFile::len`,
        `snd_asset::*_size`, `PrimedSound::size`, `SndBank::packLocation`, `XAnimParts::*Count`/`streamedFileSize`,
        `ddlMemberDef_t`/`ddlStructDef_t`/`ddlDef_t` sizes and offsets (bits of a serialized DDL buffer),
        `MaterialMemory::memory` (vertex memory);
      - `XRigidVertList::boneOffset`: bytes into the runtime `DObjSkelMat` array, 64 bytes of floats on both widths;
      - element indices (unchanged by element size): `XSurface::baseTriIndex`/`baseVertIndex`,
        `XRigidVertList::triOffset`, `cLeafBrushNodeChildren_t::childOffset` (`&node[childOffset]`, element units),
        `XSurfaceCollisionNode::childBeginIndex`, `CollisionAabbTreeIndex::firstChildIndex`,
        `GfxStreamingAabbTree::firstChild`, `CollisionPartition::firstTri`/`fuind`, `GfxCullGroup`/`GfxBrushModel`/
        `GfxAabbTree::startSurfIndex`, light/probe/lighting handles (`u16`/`u8` indices);
      - statistics or build-time values no fastfile path reads: `XModel::memUsage`, `FxEffectDef::totalSize` (only
        `fx_convert.cpp` asserts it when building an effect from `.efx`);
      - runtime budgets sized by the 32-bit linker, not stream layout: `Glasses::workMemorySize` (`Allocate` uses
        `Z_Malloc` anyway) and `Glasses::shardMemorySize` (see below);
      - pointer-sized handles typed `int`: `DynEntityClient::physObjId`, `PhysConstraint::constraintHandle` are 0 in
        the zone and written at runtime; they are the open item below, not a converter value.
    - `ffconv --verify --game <install> --out build/iwffs-v2`: 65 zones, 0 failed, 295 s; 29 098 values rescaled in
      12 zones (`frontend` and the 11 maps), which are the only zones that differ from `build/iwffs-full`; the other
      53 zones and `shaders.pack` are byte-identical. `check_shader_pack.py`: 13 704 shaders, 0 rejected.
  - Progress 2026-10-22, fifth step (the 64-bit client plays Kino):
    - `tools/headless.ps1 -Client -RunDir build\run\win64c3 -ExeDir build\win64-msvc\Release -ZoneDir
      build\zones64\zone -Zombies -Commands "+devmap zombie_theater" -AutoQuitMs 60000 -ShotsAtMs "20000 40000
      55000"` runs 60 s and quits normally (exit 0). The `win32-msvc` reference with the same arguments gives the
      same pictures: world, view model, blood decals, HUD (points, ammo, compass, round tally). The only visible
      difference is timing (the 64-bit round intro is 2 s behind at the 20 s shot, `level.time` 5050 vs 7050);
    - `GfxSurface` is `alignas(16)` on 64-bit only (0x58 → 96 bytes): the streaming code reads `tris.mins` with
      aligned SSE, which the 32-bit size 0x50 kept 16-aligned in every array element. Streamed, so `ffconv` was
      rerun into `build/zones64` (the zones the 64-bit exe uses from now on), 65 zones, 0 failed `--verify`, 13 704 shaders;
    - `ffconv`'s layout tables now rebuild when an engine header changes (`layouts.py --depfile`); before, a stale
      64-bit layout was kept silently;
    - fixes on the way, each 32-bit identical (or register/block order only):
      - `float4` is `alignas(16)`: casts of unaligned `float[4]` storage go through `Float4Load`/`Float4Store`
        (`com_math.h`), so MSVC x64 no longer emits `movaps` on 8-aligned addresses (`r_stream`, `r_draw_xmodel`,
        `r_water_sim`);
      - calls through casted function pointers that x64 passes differently: `rb_tessTable` (a 16-byte
        `GfxCmdBufContext` was passed as two arguments), a `devgui` graph callback and `Nitrous_ForEachBody` (floats
        passed as `LODWORD`, x64 passes floats in XMM registers), `MainWndProc` returns `LRESULT`. Inventory:
        replaying the compile commands with `-Wcast-function-type-mismatch` lists 96 casts; the others add or drop
        trailing arguments or return `bool` as `int`, which behave as on 32-bit;
      - 32-bit strides and sizes in runtime buffers: menu expression operands copied through `intVal` (9 sites),
        `importance_merge_sort` sorting `int` indices as `void *`, `DevMenuItem` handles (`/ 40`),
        `XAnimClientNotifyList` (24-byte stride, 1536-byte buffer), the skinned-surface record sizes in
        `r_scene.cpp` (56/24) and the skinning scratch buffer, the scene clears (`132`, `76`, `44`, `40` × count),
        the 2D text and quad-list render commands (`len + 96`, `80 * n + 12`), surface `gfxEntIndex` at `+ 14`, the
        `MSG_ReadDeltaPlayerstate` stack buffer, `R_EnumDisplayModes`' refresh-rate default, `IKState` arrays
        (`IK_STATE_ALLOC_SIZE`, upstream mismatch kept on 32-bit), the physics list walks (`(T *)&node[2]` is
        `phys_free_list<T>::data_of(node)`), `Nitrous_ForEachBody`'s node walk;
      - the debug hunk is 32 MB on 64-bit (a client run peaks at 24.3 MB, retail reserves 24 MB);
    - `tools/litsizes.py` now also looks under explicit casts and at array arguments (`Com_Memset((unsigned int
      *)scene.sceneDObj, 0, 132 * n)` was invisible).
    - `Play.bat x64` (the main menu, then Solo, Kino) crashed where the headless runs, which started the map with
      `+devmap`, never went: unloading the menu zones, `DB_FreeUnusedResources` read `XAssetEntry` fields as
      `asset[1]` (`nextOverride` was `LOWORD(asset[1].header)`, 8 bytes off on 64-bit); it now names the fields.
      The same flow headless: `-Commands '+set bo1_clientCommands "8000:devmap zombie_theater"'` (the front end
      loads, then the map 8 s after the client is active). It also showed `g_animRateOffsets` (`cg_weapons.cpp`),
      the weapon animation times read at 32-bit `WeaponDef`/`WeaponVariantDef` offsets: now `offsetof`, the table's
      `.data` identical on 32-bit.
    - `Play.bat x64` then ran out of physical memory loading `zombie_theater` (`physicalmemory.cpp`, line 1056),
      while the headless repro (no sound) did not: the widened zones take ~8% more (Kino's chain 273.5 -> 295 MB
      in `DB alloc`, `patch_mp` and `ui_mp` +50-70%), so the 64-bit PMem pool is 392 MB instead of 328 (32-bit
      unchanged). Measure the high-water mark per map (sound on) before phase 5 fixes the Switch budget.
  - Progress 2026-10-22, sixth step (sound on, front end swept):
    - streamed sounds: `Snd_StreamLoadHeader` (`snd_stream.cpp`) read the on-disk header, written by the 32-bit
      tools, straight into the 64-bit struct; it now reads it field by field on 64-bit (`if constexpr`). A Kino run
      with sound went from 748 "not found at load time" errors to 0;
    - runtime views found by the `Play.bat x64` flow: `UILocalVar_Find`/`FindOrCreate` index `context->table[hash]`,
      `BG_GetAnimationForIndex` strides by `sizeof(animation_s)`;
    - `tools/litsizes.py`, second triage (76 hits, 23 real, 10 more found nearby, 16 files): impact table
      `21 * sizeof(FxImpactEntry)`, clears and copies in `cg_weapons`, `cg_main_mp`, `flames`, `r_cinematic`,
      `destructible`, `com_clients`, `sv_msg_write`'s `memcmp`, frees sized by `MT_Free`, the load_obj loaders. Left:
      `RB_DrawText3DCmd` (`rb_backend.cpp:4534`) reads 4-byte offsets, but nothing emits that command;
    - glass on `zombie_pentagon`: `GlassRenderer`'s small allocator had 16-byte blocks, which hold a `std::list`
      node of a pointer on 32-bit only (24 bytes on 64-bit); blocks are now `4 * sizeof(void *)`. Then
      `R_StreamUpdateDynamicModels` read the glass brushes through `scene.glassBrushVisData[40 * i - 40924]`, a
      32-bit offset back into `scene.glassBrush[]`: it names the fields now. The bug family (an offset from a
      neighbouring array) is being inventoried tree-wide;
    - menu sweep (`bo1_menusweep`, the 195 front-end menus in 5 batches, 32-bit reference and 64-bit side by side):
      the same menus are driven and skipped on both widths (batch 0: 37/294, batch 1: 72/1022, batch 3: 29/385).
      The remaining diffs are the order in which an asynchronous popup shows up on the menu stack. Both widths stop in
      batch 2 on the file share "edit info" menu and in batch 4 on the leaderboards (upstream, below);
    - fire test (`+attack` every 1.5 s for 60 s on Kino, sound on, 32-bit and 64-bit): no assert on either. The
      user's assert on fire (`SL_AddRefToString` from `CScr_SetFlaggedAnimKnobRestart`) is still not reproduced
      headless;
    - ragdolls: `+devmap zombie_pentagon` for 150 s (the player goes down, zombies die and ragdoll) asserted on
      64-bit only, in `PHYS_ASSERT_UNIT` (`phys_collision.cpp`, contact normal not unit length). A probe showed
      `m_mat.x.x` of every ragdoll body was exactly 1, and a hardware watchpoint under cdb caught the writer:
      `physics_system::time_step` passed `(rigid_body *)&node[2]` to `rbint::collision_prolog`, 16 bytes past the
      body on 64-bit, so `m_smallest_lambda = 1.0` landed on `m_mat.x.x`. It is `data_of(node)` now (the earlier
      sweep of that family missed this `*const` cast); the rest of the family is being inventoried. On the way:
      `PMM_VALIDATE` with a literal alignment (`4u`, `16`, `0x10u`) looked in another slot pool than the free
      list's `T_internal_alignment()` (8 on 64-bit), 16 sites; the contact manifold counted pointers as
      `(bytes - 4) >> 2` and compacted with `(bytes - 1) / 4 + 1` (a `memmove` of twice the pointers on 64-bit);
      `rbint::take_next_step` built its rotation in a 12-byte `_BYTE` local (a Hex-Rays split of a `phys_mat44`).
      Longer runs then showed NaN bone translations and NaN LOD distances on the dying zombies (2 runs in 4,
      never on 32-bit). Probes per solver phase placed the first NaN in the contact relaxation:
      `contact_point_info::create_cpi` (`phys_collision.cpp`) sized its buffer as `(32 * n + 95) & ~15`, 95 being the
      32-bit `sizeof(contact_point_info)` (80) + 15; the struct is 112 bytes on 64-bit, so the pulse-sum cache
      overlapped the tail of `m_list_b2_r_loc`. It is written with `sizeof` now. Same family: `Image_Alloc`
      (`Hunk_Alloc(len + 53)`, `GfxImage` is 72 bytes on 64-bit), `create_pulse_sum_contact` (`208 * n + 96`), the
      font load_obj header (`+ 24`). Wave 8 (free-list node views, `phys_free_list<T>::node_of` added next to
      `data_of`) fixed `destroy_all_constraint` (`b1`/`b2` read 16 bytes off, and its distance-constraint loop typed
      as the `broad_phase_group` list), `fixup_wheel_constraints`, `G_ClearVehicleInputs` (wrong owner, wrong bytes
      zeroed) and spelled `broad_phase_process_collision_pair` and `ptr_array_read/write` with named fields.
      Pentagon then runs 200 s twice and quits normally, with the same events as the 32-bit reference
      (`player0downs` 1 then 0, `aa_enemy_deaths` 2 to 6), and Kino still matches.
    - the user's assert on fire (shooting through Kino's barricades): the minidump (`%LOCALAPPDATA%\CrashDumps`)
      showed the client script VM (`CG_DrawActiveFrame` → `Scr_RunCurrentThreads` → `CScr_SetFlaggedAnimKnobRestart`).
      `DObjCreateExt` wrote `localClientIndex` and `ikState` at their 32-bit offsets (`buf[112]`, `+ 29` dwords), so
      on 64-bit every client DObj kept `localClientIndex == -1`, `DObjIsServer()` was true and its anim tree got
      `inst = 0`: client-script notify strings were referenced in the server's string table. Named fields now.
    - `Z_TryMallocGarbage` returns `malloc() + 164`, so `Z_Malloc` blocks are only 4-aligned (as on 32-bit). Kept;
      anything that needs 8- or 16-byte alignment from `Z_Malloc` (64-bit `Interlocked*`, aligned SSE) is suspect.
    - map → menu quit crashed on every map in `Mark_XModelPtrArray` (`DB_FreeUnusedResources`): the retail
      `zombie_knuckle_crack` `WeaponDef::worldModel` (16 null `XModel *`) is an offset to 64 zero bytes of another
      weapon's script string array, followed by a material name in the 64-bit stream. `ffconv` now checks every
      offset pointer's target against the pointee its inline branch streams, and streams a widened copy inline where
      they differ (`docs/ARCHITECTURE.md`, shared targets). Over the 65 zones: 376 mismatches in 20 zones, all
      streamed inline: 303 `BrushWrapper::planes` (planes 8-aligned apart on 64-bit), 55 `WeaponVariantDef::hideTags`
      over null pointer arrays, 12 `WeaponDef::worldModel` and 6 `gunXModel` over zero script strings. The first
      pass also flagged 524 `CollisionPartition::borders` (past-the-end pointers with `borderCount` 0, now checked
      with `borderCount`) and ~3 500 `ExpressionStatement::rpn` (unions compared member by member, now as a whole):
      false positives of the check. `ffconv --verify --game <install> --out build/zones64`: 65 zones, 0 failed,
      403-519 s, no mismatch left in the 64-bit zones, every inline copy equal to its retail target; only the 20 zones
      with mismatches changed, `shaders.pack` is identical (13 704 shaders, 0 rejected by `check_shader_pack.py`).
  - Left for the next steps:
    - GNU builds (`linux64-clang`, `switch`) must add `-fno-strict-aliasing` before phase 4 runs anything: the
      decompiled code reads the same bytes through unrelated types everywhere, which MSVC compiles as written but
      gcc/clang may reorder under type-based alias analysis. Wave 4 showed it on mingw32-gcc: wrapping the
      `centity_s` and `flameGeneric_s` bitfields in a union let gcc merge or reorder stores in `cg_event.cpp`,
      `flame_class_fire.cpp`, `flame_class_smoke.cpp`, `flame_class_chunk.cpp`, `flame_drips.cpp` and
      `flame_stream.cpp` (no behaviour change there, since the objects are distinct);
    - `cg_view_mp.cpp` `CG_OffsetThirdPersonView` keeps pointers in floats (`LODWORD(viewAngles[2]) =
      CG_GetEntity(...)`, then `+ 804`/`+ 488`): third-person view only, needs a rewrite with real locals;
    - `DynEntityClient::physObjId` and `PhysConstraint::constraintHandle` (decision above; `DynEntity_client.cpp`
      9 errors, `g_mover.cpp:307/324`);
    - glass: `GlassClient::Outlines::CalcMemorySize` now uses `sizeof(Outline)`/`sizeof(Outlines)` (12 → 16,
      8 → 16), but the shard pool is still `Glasses::shardMemorySize` from the zone, a 32-bit budget: a 64-bit
      shatter may run out sooner, which `AllocateShardMemory` reports as NULL and the caller skips. Measure on a map
      with breakable glass. (`GlassShard::Mesh::GetMemorySize` is declared but has no definition or caller.);
    - render streams of 32-bit words holding pointers: `XSurface *` in `primDrawSurfsBuf`
      (`r_add_staticmodel.cpp`, `r_draw_staticmodel.cpp`), `Stream2Usage *` in `srfTriangles_t::stream2ByteOffset`
      (`r_bsp_load_obj.cpp`, load_obj path), `GfxPixelCostKey::packed` (debug tool), the console icon `Material *`
      (`cl_console.cpp:1799`);
    - `playerStateFields` NetField offsets of `deltaTime`, `killCamEntity`, `killCamTargetEntity` (the only entries
      that move on 64-bit, after `objective_t` grows 48 → 56);
    - the fixed 3 670 016-byte physics buffer (`phys_main.cpp:426`) is the same on both widths while the structs in
      it grow: measure its high-water mark on a map before phase 5;
    - `dvar->current.integer` null tests on string dvars only test the low 32 bits on 64-bit;
    - load_obj paths only (not used with fastfiles): `cspField_t` tables with 32-bit offsets, `PhysPreset` filled as
      a float array, `CG_DestructibleRadiusDamage`'s mistyped `point`;
    - the 64-bit save format (saves are skipped on 64-bit for now);
    - MSVC x64 warnings were not reviewed yet (the builds above print errors only): a `/W4` pass for C4311/C4312
      pointer truncations is the next cheap check.
- **4**: `linux64-clang` plays 10 rounds of Kino. RenderDoc captures for each pass. No GL debug errors. The
  null-backend test of `gfx_d3d9emu` passes.
- **5**: Kino plays 10 rounds docked and handheld with no crash, RAM logged every round, target 30 fps at 720p
  docked. Render-thread CPU time is recorded here to decide on phase 7.
  - Switch constraints from `.claude/rules/switch-homebrew.md` (learned on another libnx port) that this plan must
    meet:
    - **GLSL version (open risk)**: the installed `switch-mesa` is 20.1.0 (nouveau), where a desktop GL 4.3 core
      context is known to work; `ffconv` writes `#version 460 core`. The first console run must log
      `GL_VERSION`/`GL_SHADING_LANGUAGE_VERSION`. If 4.60 is refused, `ffconv` writes `#version 430 core`: the
      shaders only need explicit bindings (4.20) and std140 UBOs (3.10), and on 2026-10-22 all 13 704 shaders of
      the pack, with only the version line changed to `430 core`, passed `glslangValidator --target-env opengl`;
    - draw calls cost about 20 µs each in that driver: the `gfx_backend` GL path batches and caches state, and the
      phase 5 profile counts draws per frame;
    - application mode only (`appletGetAppletType()`, refuse cleanly in applet mode); measure memory with
      `mallinfo().uordblks` (libnx maps the whole heap at start, so `svcGetInfo` always reports the total);
    - threads start on their creator's core: pin the job-queue workers to cores 0-2 explicitly;
    - newlib has a single malloc lock: no large free loops on one thread while others allocate (level unload);
    - never call `appletMainLoop()` again after it returned false; on exit join every thread, close audio, then
      services, then EGL, and log a final "Shutdown complete";
    - GCC 16 libgcc carries GCS/SME instructions that abort Ryujinx's JIT during C++ unwinding: patch them out
      after linking before an emulator run;
    - builds: `export DEVKITPRO=/opt/devkitpro` before `switchvars.sh`, ccache, no per-build value in compile
      definitions (build id goes in a romfs file logged at start), build tree on ext4 rather than `/mnt/c`.

## Known upstream bugs (not fixed, noted for later)
- `src/gfx_d3d/rb_draw3d.cpp:239`: `memcpy(gfxCmdBufState.refSamplerState, gfxCmdBufState.refSamplerState,
  sizeof(gfxCmdBufState))` copies a whole struct's size onto one of its own members (overlap and overflow). This
  is decompiler output and must be checked against the binary before being fixed.
- `src/cgame/cg_draw_reticles.cpp` (vehicle gun yaw arc test): the `&&` before `-(weapDef->rightArc)` is missing,
  so the code compares the bool `x < leftArc - rightArc` with a float. The pitch test just below has it. Phase 1
  only added the parentheses C++ already applies.
- `src/universal/eval.cpp` (`Eval_EvaluationStep`, from line ~250): the decompiled body treats its `Eval *eval`
  parameter as a byte address (`*(_DWORD *)(eval + 20484)` for the value stack position), so each access lands
  20484 × `sizeof(Eval)` bytes away, out of bounds on 32-bit too. Any caller of `Eval_Solve` would crash. Phase 3
  leaves it: the fix is a rewrite with `Eval`'s fields, checked against the binary. It is unreachable by default:
  the only callers are the `.menu` text parser's `(`-expressions (`PC_Int/Float_Expression_Parse`), used only with
  `useFastFile 0`. With MSVC the `Eval` layout is the same on x86 and x64; with GNU compilers `EvalValue::u.d` is a
  `long double` (12 or 16 bytes, not 8), so the rewrite should make it `double`, which is what MSVC compiles.
- Found during phase 3 and kept as they are (each still needs checking against the binary):
  - `src/universal/mem_fixed.cpp`: `Hunk_FixedInit`/`Hunk_FixedReset` never advance `node` in the free-list loop, so
    only one block is linked; Init links with stride `block_size`, Reset with `block_size + 4`. `HU_SCHEME_FIXED` has
    no callers.
  - `src/gfx_d3d/r_image.cpp` `R_AddImageToList`, `src/gfx_d3d/r_model.cpp` `R_GetModelList`:
    `data->xmodelPieces++` steps the counter by `sizeof(XModelPieces)`, so the `imagelist`/`gfx_model` lists are
    sparse.
  - `src/gfx_d3d/r_material.cpp` `R_MaterialList_f`/`R_GetMaterialList`: assume `inData` and `v6[]` are adjacent
    on the stack; on 64-bit the `XModelPieces *` store overflows (debug command).
  - `src/gfx_d3d/r_material_load_obj.cpp` `Material_LoadVertexShader`: `memcpy(program, v2, (unsigned int)v4)` uses
    the `ID3DXBuffer` pointer as the size (the pixel shader path uses `programSize`).
  - `src/client/cl_keys.cpp:1241`: `keynames_localized[92]` + keynum 48..57 reads past the 116-entry array.
  - `src/game/g_missile.cpp:3138/3140`: `actorXAnimTrees[376 * corpseIndex - 1495]` lands at byte 36 of
    `playerCorpseInfo[corpseIndex]`, not on `.entnum` (`g_player_corpse.cpp:20` uses `.entnum`). Same family at
    `g_missile.cpp:1964/1966/3234`: `&g_scr_data.playerCorpseInfo[1504 * corpseIndex + 4]` uses the byte offset of
    `.entnum` (`sizeof` 1504, `entnum` at +4) as an element index, out of bounds on 32-bit too.
  - `src/ik/ik.h` `IKState`: the decompiled struct is 3712 bytes with MSVC x86 (3680, `0xE60`, in retail), while the
    server and client IK arrays reserve 32 × 3680 bytes, so the last entries overrun by up to 1 KB on 32-bit.
    `IK_STATE_ALLOC_SIZE` keeps 3680 on 32-bit and the real `sizeof` on 64-bit.
  - Quitting after front end → map (`bo1_autoquit` with the flow above) asserts in `SL_Shutdown`
    (`cscr_stringlist.cpp(142)`, `!totalRefCount`): script strings still referenced at shutdown. The `win32-msvc`
    reference asserts the same way, so it is not a 64-bit issue.
  - `src/ui/ui_server.cpp:645`: `qsort(&sharedUiInfo.serverStatusInfo.lines[31][1], ..., 4u)` lands exactly on
    `serverStatus.displayServers`, consistent with the one-field-off `sharedUiInfo_t` layout below.
  - `src/qcommon/com_bsp_load_obj.cpp:73/222/352`: `Com_GetBspLumpCountForVersion` returns a string of the `CA_*`
    name table, used as a count (load_obj path only).
  - `src/client_mp/cl_main_mp.cpp:4832/4850`: the asserts compare the index with the `count` pointer, not `*count`.
  - `src/physics/physics_system_internal.cpp`: the retail partition code adds 8 per constraint to the body counters
    (and to b2's `m_contact_count` for wheels), kept as `PHYS_PARTITION_CONSTRAINT_WEIGHT`.
  - `src/vehicle/nitrous_vehicle.cpp` `add_vehicle`: `refcount += 0x440`.
  - `src/physics/xdoll.cpp:425`: `surfFlags = (int)"t"`, a string address used as flags (`DynEntity_pieces` uses
    `13631488` in the same place).
  - `src/ui/ui_feeders.cpp:679/700`, `src/ui/ui_main_pc.cpp:987/992/1271/1274`: `sharedUiInfo_t`/`mapInfo` look one
    4-byte field off from retail (`timeToBeat[31]` is `levelShot`, `mapPackTypeIndex` is `imageName`,
    `modList[63].modDescr` is the mod count); fixing them changes the 32-bit layout.
  - `src/ui/ui_shared_obj.cpp:1751`, `src/ui/ui_viewer.cpp:918`: `&objBuf[1758][2]` and `&objBuf[1758][1] + 3` are
    decompiled constants (probably `0x04000000` and the mask `0x03FFFFFF`). Since `objBuf` became a `DObj` array
    they are spelled `(unsigned int *)objBuf + 1758 * 31 + 2`, the same address on 32-bit.
  - `src/database/db_registry.cpp`: `g_freeAssetEntryHead = g_assetEntryPool + 16` starts the free list 16 entries
    in, while the list is chained from entry 1: the exe most likely added 16 bytes (one 32-bit entry), i.e.
    `&g_assetEntryPool[1]`. Entries 1 to 15 are never used.
  - `src/ui/ui_viewer.cpp` `GetWeaponVariantDef`, `src/live/live_stats.cpp:1528`: inlined `strcat` calls that
    depend on the stack layout.
  - Offline front end, seen with the menu sweep on both widths: the file share "edit info" menu
    (`menu_fileshare_myshare_editinfo`) asserts `index < data->descriptorCount` (`live_fileshare.cpp:3299`) or
    crashes on `fsSelectedFileTagIndex`, and the leaderboards menu asserts `onlinegame && onlinegame->current.enabled`
    (`com_gamemodes.cpp:52`). Both are online-only screens reached without a connection.

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
