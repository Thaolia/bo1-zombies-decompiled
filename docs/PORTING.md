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
| 2026-10-23 | `DynEntityClient::physObjId` and `PhysConstraint::constraintHandle` are `intptr_t` (closes the 2026-10-22 "stay `int`" row); `ffconv` widens them as `LEAF_HANDLE` leaves, listed in `layouts.py` `WIDENED_INTEGERS` | They hold a pointer at runtime (`Phys_ObjCreate`, `rigid_body_constraint_*`); the `(int)` casts truncated on 64-bit and do not compile with clang on Linux. On 32-bit `intptr_t` is `int`: same layout, same code. Both structs are streamed (`Load_DynEntityClientArray` into the runtime block, `Load_PhysConstraintArray`), so ffconv sign-extends them 4 → 8 bytes and narrows them back for `--verify`. The zones stream them as 0, and a non-zero value fails the zone. A leaf of its own rather than a value fixup: the field changes size, so offset mapping, stream-boundary and shared-target checks must know it, like a pointer without the offset translation. Any other integer that grows still stops `layouts.py`. A parallel runtime array was rejected: every use would change |
| 2026-10-22 | `ffconv` rescales integer fields that hold a byte distance inside a widened array (value fixups, `layouts.py` `VALUE_FIXUPS`); `GfxAabbTree::childrenOffset` is the only one | The retail value is `children - tree` in bytes of 40-byte nodes; copied as is, the 64-bit renderer walked into garbage (`R_AddAabbTreeSurfacesInFrustum_r`). Rescaling in the converter keeps `db_load.cpp` and the renderer unchanged, and `--verify` narrows it back byte for byte |
| 2026-10-22 | `broad_phase_base` users read the real fields, not `x[-1].field` | The Itanium ABI (GCC, clang) reuses its tail padding (`m_list_bpi_head` at 76 instead of MSVC's 80); the typed form is right for both ABIs and leaves MSVC's code unchanged |
| 2026-10-23 | posix/switch builds take the D3D9 declarations (`d3d9.h`, `d3d9types.h`, `d3d9caps.h`) from the user's local DirectX SDK (June 2010) at build time; the Win32 declarations come from our own `windows.h` shim (`src/platform/posix/include/`) | Nothing from Microsoft is committed, and the values the engine and the fastfiles depend on (`D3DFORMAT`, `D3DRS_*`, struct layouts) are the SDK's own. Alternatives were vendoring the Wine/mingw-w64 headers (LGPL) or writing D3D9 declarations by hand. Every build machine, the Switch one included, needs the SDK (it is already needed by the Windows builds) |
| 2026-10-23 | The `windows.h` shim only declares Win32 functions (grouped by area, `extern "C"`, Windows signatures); structs and constants the engine names get the Windows layout and values; its 64-bit integer types are `long long` like `__int64` | The platform layer implements the functions; a stub that fakes success would hide what is missing. `int64_t` is `long` on LP64, a different type: the engine passes `unsigned __int64 *` where Windows takes `ULONG64 *` |
| 2026-10-23 | `CRITICAL_SECTION` / `SRWLOCK` contain a `pthread_mutex_t` / `pthread_rwlock_t` (`SRWLOCK_INIT` is `PTHREAD_RWLOCK_INITIALIZER`) | Big enough on every target by construction (glibc x86-64: 40 / 56 bytes, Windows: 40 / 8; newlib differs again). The engine only declares them as globals and never reads their fields |
| 2026-10-23 | The MSVC CRT functions that only compute (`sprintf_s`, `strcpy_s`, `strncpy_s`, `_snprintf_s`, `fopen_s`, `strtok_s`, `localtime_s`, `_ctime64`, `_ui64toa`, `_aligned_realloc`, ...) are defined in `posix/msvc_intrinsics.h` with MSVC's semantics; `_putenv`, `sscanf_s`, `io.h` and `direct.h` are declared | An overflowing `_s` call ends the process, as MSVC's default invalid parameter handler does (the engine installs none); the 64-bit time functions refuse dates before 1970 and after 3000 like MSVC's |
| 2026-10-23 | posix builds the Windows sources as they are (`win_main`, `win_net`, `win_wndproc`, `win_voice`, CubeMapGenLib, the rest of `win32/`) on the Win32 layer of `src/platform/posix`; only `win_syscon` (a terminal console instead, `posix_syscon.cpp`), `win_mini_dumper` and the XAudio2 driver are replaced (closes the row that left the first group out) | One copy of `WinMain` and of all the headless tooling, so posix logs and behaves as Windows does; MSVC compiles the same code. The layer answers what Windows answers without the device (no window, no mixer, no XInput pad, no D3D9 runtime) or ends the process naming a function that needs a window. Windows-only diagnostics that read PE headers or dbghelp (crash stack scan, thread dumps) are `#if defined(_WIN32)`; the console window has no posix meaning, and the minidumper writes nothing in this tree |
| 2026-10-23 | The posix x86-64 build reads the win64 IWffs zones (`DB_FF64_ABI_WIN64`); its build runs `layouts.py --check-target64 x86_64-linux-gnu`, which fails naming any streamed type that lays out differently there | All 241 streamed types have the same tables under SysV x86-64 (LP64, Itanium) and MSVC x64, parsed as each build compiles `db_load.cpp`: one zone set serves both. aarch64 gets its own check or tables in phase 5 |
| 2026-10-23 | GNU builds compile with `-fno-strict-aliasing -fwrapv -fno-delete-null-pointer-checks` | The decompiled code reads objects through other types, does signed arithmetic as the x86 instructions did (wrapping) and may test a pointer after dereferencing it; MSVC optimises on none of these |
| 2026-10-23 | D3DX, DirectSound and Direct3D on posix: the shim declares the D3DX subset the engine uses (`include/d3dx9.h`) and a DirectSound with no device; until `gfx_d3d9emu`, `Direct3DCreate9` returns NULL and D3DX returns `E_NOTIMPL` (`d3d9_absent.cpp`); sound has no device (`snd_driver_absent.cpp`, the engine starts as with `nosnd`) | Closes the 4a step 1 open decision: the SDK's `d3dx9*.h` and `dsound.h` need much more of Win32 (GDI, `IStream`, `sal.h`); every answer is a state the engine already handles (no D3D9 runtime, no sound device) |
| 2026-10-23 | `bo1_fopen` (posix) is `fopen` with MSVC's behaviour: `\` separators and text mode unless the mode says `b` (CR LF read as LF, LF written as CR LF, Ctrl-Z ends a read); `FileWrapper_Open` and the shared FOV file use it on posix | The engine opens its text files (`localization.txt`) with `"rt"` and parses lines at LF: without the translation the language read as `english\r`. Logs written with `"at"` come out CR LF as on Windows |
| 2026-10-23 | `r_texturemem.cpp`: the DirectDraw video-memory queries are `#if defined(_WIN32)`; elsewhere they answer 0, as on a PC without `ddraw.dll` | `ddraw.h` is not in the June 2010 SDK. The texture budget code around them stays shared |
| 2026-10-23 | The globals `index` (`vtos`) and `basename` (`FS_GetMapBaseName`) are renamed `vtosIndex` / `mapBaseName`; the unused Hex-Rays typedef `ulong` is gone; `q_shared.h`'s angle indexes and byte-order helpers are no longer under `#ifdef WIN32` | glibc declares `index()`, `basename()` and its own `ulong`. Renaming keeps the data layout (`codediff.py` identical). Every target is little-endian |
| 2026-10-23 | `gfx_backend.h` is modelled on an explicit API: immutable samplers and pipelines cached by value, passes with load operations, a `DrawState` per draw that holds everything the draw reads besides its pipeline, uploads ordered with the recorded work, and three sync points (`ReadTexture`, `WaitIdle`, `Present`); it keeps D3D9's conventions (row 0 on top, depth 0-1, pixel centres on integers, clockwise front faces) | deko3d implements the same header later without touching the frontend (`docs/RENDERER.md`, review checklist). With D3D9's conventions the frontend passes coordinates through and only a backend converts (GL: clip control, a half-pixel offset, a flipped blit at present) |
| 2026-10-23 | posix's Direct3D 9 is `src/gfx_d3d9emu` on the null backend until the GL one (step 3); `d3d9_absent.cpp` is gone and D3DX stays `E_NOTIMPL` (`d3dx9emu.cpp`, with the D3DPERF markers) | The frontend is checked against the contract on every headless run before GL exists. The engine calls D3DX only where the zones do not lead (`r_material_load_obj.cpp`, materials built from raw files) and to save screenshots, which then fail as a failed D3DX call does on Windows |
| 2026-10-23 | The emulated adapter: the host's first display, no PCI vendor, a shader model 3 device with the caps `r_caps.cpp` requires, the formats the backend supports (`INTZ`, `RESZ`, `NULL`, D24S8/D16 shadow maps; not D24FS8, `ATOC`/`SSAA`, vertex texture fetch, sRGB), up to 4 GB of texture memory | Every refusal is a state the engine handles on a PC without that feature; the vendor id 0 keeps the NVAPI and ATI paths off, as `BO1_OFFLINE` already removed NVAPI. 4 GB is the most `GetAvailableTextureMem`'s `UINT` can say |
| 2026-10-23 | Lockable D3D9 resources (managed, dynamic, system memory, scratch, offscreen plain) keep a CPU copy of each level the engine locks; the unlock uploads the locked box, and a copy the GPU wrote since is read back at the next lock | A D3D9 lock sees the resource's content (the model lighting volume is locked every frame for a few texels, the engine reads render targets back with `GetRenderTargetData` and a lock). Only locked levels have a copy; their memory is measured on the Switch (phase 5) before any other scheme |
| 2026-10-23 | The Win32 window layer runs on SDL2 (`win32_gui.cpp`): layer-owned windows backed by SDL windows, Windows' message order, one message queue per thread, SDL displays as monitors. A headless run never shows its windows; SDL video starts at the first display or window call | The engine's window code (`win_wndproc.cpp`, `r_init.cpp`) runs unchanged. The Windows headless client sits on a private desktop: a hidden window gives the same messages and the same occluded 9Ex `Present`. The dedicated server never opens a display |
| 2026-10-23 | On posix and switch `rand` / `srand` are the UCRT's generator (`bo1_rand`: 214013 / 2531011, bits 16-30, one seed per thread starting at 1; `RAND_MAX` 0x7FFF) through `compat.h` | The decompiled code divides by 32767 or 32768 as compiled constants (`random()`, `crandom()`, `bg_wind`, `cg_main`), so glibc's 31-bit values broke every such range: `GaussianRandom`'s rejection loop in `R_CreateParticleCloudBuffer` never ended. The same seed also gives the Windows sequence. Checked against the Windows CRT |
| 2026-10-23 | The vertex input locations are `ShaderPack_InputLocation(usage, index)` in `shader_pack.h`, shared by `ffconv` (`ff_glsl.cpp`) and the frontend's vertex declarations | One table for the number both sides must agree on (the material stream destinations, `s_streamDestInfo`); `ffconv`'s output is byte-identical |
| 2026-10-23 | `long double` is written `double` everywhere in the engine (231 lines, 78 files) | MSVC compiles `long double` as `double`, so that is the reference's meaning. GCC and clang made it 80-bit x87 in 16 bytes on x86-64 (`token_s`, `script_s`, `EvalValue`, `SysInfo`, `scoped_performance_error` laid out differently from MSVC x64, x87 arithmetic) and 128-bit soft-float on AArch64. `codediff.py --cflags=-mlong-double-64` (the base compiled with MSVC's width) checks the change is only that |
| 2026-10-23 | Over-aligned blocks: an allocation whose retail alignment is below its type's takes `BO1_ALLOC_ALIGN(retail, T)` and its size sum `BO1_ALLOC_SLACK` (`arch_size.h`; the `MatchState` snapshot blocks), and `Z_Malloc`'s header is 176 bytes instead of 164 outside MSVC, so its blocks keep malloc's 16-byte alignment | GCC and clang copy align(16)/align(64) types with `movaps`, which faults on the 4-aligned blocks retail gives them (`SV_BuildClientSnapshot` crashed on the Linux client). MSVC copies with unaligned moves, so the retail values and layout stay there, as the win32 reference requires. Inventory: `tools/overaligned.py` |
| 2026-10-23 | The `windows.h` shim defines minwindef.h's `LOBYTE`/`HIBYTE`/`LOWORD`/`HIWORD`; `q_shared.h` `#undef`s them before its own Hex-Rays ones; engine headers include `windows.h` on posix only where the SDK's headers would (`profile.h` declares `GetCurrentThreadId` instead) | The two definitions disagree beyond 2-byte operands: windows.h's take bits 8-15 or 16-31 of the value, `q_shared.h`'s the last byte or word of the object. With the SDK each file gets the one defined last in its include order. Without them in the shim, every posix file had `q_shared.h`'s, so `GScr_AnimHasNotetrack`'s `HIWORD(anim)` read the top of a 64-bit pointer (anim tree 0) where win64 reads bits 16-31. The 739 preprocessed uses in the 94 files built on both sides now expand to the same definition as with MSVC x64 |
| 2026-10-24 | Keyboard and mouse on posix are SDL's events turned into the messages Windows posts (`win32_input.cpp`): the layout's virtual-key codes with set 1 scan codes and the extended-key flag, `WM_CHAR` in Windows-1252 from SDL's text input, control characters from `TranslateMessage` decided at key-down, a virtual cursor captured while the engine hides it over its focused window | The engine's input code (`MainWndProc`, `MapKey`'s tables, `IN_MouseMove`'s cursor polling and recentring) runs unchanged and sees what it sees on Windows; SDL's text input knows layouts, dead keys and input methods; a cursor warp is unreliable on Wayland and absent from the offscreen driver, so movement while captured comes from SDL's relative motion |
| 2026-10-24 | The clipboard is a copy the video thread keeps (refreshed on `SDL_CLIPBOARDUPDATE`), and the text the engine sets leaves at the video thread's next pump | SDL's clipboard belongs to the video thread (the render thread), which waits for the main thread every frame: a paste that waited for it stalled both threads for the wait's whole timeout (seen: 1 s, then an empty paste) |
| 2026-10-24 | Input tests run a normal windowed client under `SDL_VIDEODRIVER=offscreen` with `BO1_INPUT_SCRIPT`, whose made-up SDL events go straight to the input layer | The offscreen driver gives a shown window, keyboard focus and an EGL OpenGL 4.6 context (llvmpipe) without touching the user's display; a headless client never reads the mouse (`IN_Frame`). The system's SDL2 is sdl2-compat 2.32.74 over SDL3, whose `SDL_PushEvent` faults on a hand-made key event. The engine drops input while it loads (`Sys_LoadingKeepAlive`, `DB_Sleep`), and the main loop polls the mouse during the asynchronous fastfile loads too, so nothing the layer sees tells loading from playing: scripts give their steps times after the map load |
| 2026-10-24 | The backends sit behind a dispatcher (`gb_backend.cpp`): each fills a table of the contract's functions in its own namespace, handles derive from empty `gb::` structs; `BO1_GFX_BACKEND=null`/`gl` picks one, else GL when the platform gives a context, never in a headless run | One binary carries both: the windowed client draws, the headless client and the tests keep the null backend's checks; a table built from qualified names fails the build if a backend misses a function |
| 2026-10-24 | The GL backend owns its context on one thread of its own, fed by command chunks in order, plus a second lane for compiles and links served between commands; it loads its entry points from the platform's context (`gb_gl_host.h`) and links no GL library | The contract lets creation and uploads come from any thread (the database thread loads textures while the render thread draws), which one GL context cannot follow; the second lane keeps a new pipeline from waiting for the frames queued before it; the same host interface is what an EGL Switch host implements |
| 2026-10-24 | D3D9's half-pixel offset is in the shaders: the normaliser ends every vertex shader with `gl_Position.xy += bo1_pos_fixup.xy * gl_Position.w` (the frontend writes 63/128 of a pixel right and down for the viewport's size) and writes `vPos` as `gl_FragCoord.xy - 0.5`; backends apply the viewport as given, and `bo1_vpos_flip` is gone (both constant blocks are 4 112 bytes) | A viewport moved by half a pixel clips geometry half a pixel inside the first column and row: with 4 samples, Kino showed a line of clear colour there, half its samples bare (measured per draw). D3D9 hardware moves vertices after the transform and clips to whole pixels; 63/128 rather than 1/2 keeps an edge through a D3D9 centre on D3D9's side, as wined3d does |
| 2026-10-24 | With `glClipControl(GL_UPPER_LEFT, ...)` the front face is `GL_CW` | Measured by `gb_gl_test` (culling and two-sided stencil): the upper-left origin turns the rows over but GL still judges facing as with its own origin |
| 2026-10-24 | `DestroyShader` ends the pipelines made with the shader (contract; null and GL) | Pipelines are cached by description, which holds shader pointers: a new shader allocated at a freed one's address would otherwise get its stale pipeline |
| 2026-10-24 | SDL video that the GL host started stops when it closes and no window exists (`Bo1_VideoRelease`) | The engine's first Direct3D object (`Sys_DetectVideoCard`) lives on the main thread, its windows on the render thread; SDL reads events on the thread that started video, so the windows' events were never read |
| 2026-10-24 | `D3DXSaveSurfaceToFileA` writes JPG, TGA (uncompressed), BMP and PNG with stb_image_write v1.16 (`third_party/stb`, vendored unmodified); the file goes through the frontend's host (`SaveFile`) | The engine's `screenshot` and `screenshotJpeg` are how runs are compared with the win64 reference; the host keeps file paths a platform matter and lets `d3d9emu_test` check the bytes |
| 2026-10-24 | The normaliser declares the samplers the constant table names `shadowmapSamplerSun` / `shadowmapSamplerSpot` as `sampler2DShadow` and wraps their lookups in `vec4(...)`, giving `texture` and `textureLod` the `.xyz` MojoShader cut to `.xy`; any other lookup on them fails the shader | D3D9 hardware compares a depth texture bound to a sampler with the coordinate's z; GL only does with a shadow sampler, and a plain sampler reading a texture in comparison mode gives undefined results |
| 2026-10-24 | The GL backend's calls that act on a bound target (storage, uploads, parameters) go through `EditTexture`, which binds on the upload unit and makes it active | The binding cache skipped `glActiveTexture` when the upload unit already held the texture, so an upload after a draw went to the draw's last unit: Kino's model lighting atlas (a volume locked when lighting changes) kept its first contents and every model drew black; a 2D upload would have overwritten another texture silently |
| 2026-10-24 | The normaliser clamps each COLOR output (`io_10_#`) of a vertex shader to [0, 1] at the end of `main`, next to `bo1_pos_fixup` | D3D9 clamps COLOR-semantic interpolators to [0, 1] at the vertex output before interpolation; MojoShader passed them through `gl_FrontColor`/`gl_Color` (clamped by default in compatibility GL) and the rename to core-profile varyings dropped it. 63 of 356 vertex shaders compute a COLOR output that can leave [0, 1] (negatives, products), so it is not a no-op; `gb_gl_test` fails without it (a 2-to-0 gradient reads white at the middle, not grey) |
| 2026-10-24 | The controller is `SDL_GameController` (`win32_gamepad.cpp`): the video thread reads one open pad into a snapshot each pump, the XInput entry points read it under a lock; the SDL->XInput map is a pure inline function (`win32_gamepad.h`) tested by `tests/platform/gamepad_test.cpp` | The engine polls XInput and applies its own dead zones, so the layer only translates; factoring the map pure lets it be tested without a device, and the snapshot keeps the engine's reads off the video thread |
| 2026-10-24 | The engine's full-screen window (WS_POPUP + WS_EX_TOPMOST, `r_fullscreen 1`) maps to SDL desktop full screen (`SDL_WINDOW_FULLSCREEN_DESKTOP`); the borderless windowed mode (no WS_EX_TOPMOST) is unchanged | Desktop full screen covers the display with no mode switch to restore, and Present already scales the game's back buffer to the drawable; exclusive full screen would mean a D3D9 device mode switch the frontend does not do |
| 2026-10-25 | `Bo1_PosixPath` (posix file layer) resolves paths case-insensitively, component by component, with an exact-path fast path | Windows' file system is case-insensitive and the engine relies on it (`zone/Common` vs ffconv's `zone/common`); a case-sensitive FS (ext4, the Switch SD) broke zone loading. Resolving at the one choke point fixes every file op without touching the engine |
| 2026-10-24 | All streamed sounds are MS-ADPCM, so the SDL2 audio backend needs only PCM and MS-ADPCM decode, no WMA/ffmpeg | An inventory of every `.wav` in the install's iwds (29 103 files, format field at offset 28 of the Treyarch stream header) found 29 102 MS-ADPCM and 1 PCM16, 0 WMA; loaded sounds are PCM or MS-ADPCM too (`snd_driver_xaudio2.cpp` branches). xWMA was the only thing pulling in ffmpeg |
| 2026-10-24 | A headless run renders on the null backend unless `BO1_GFX_BACKEND=gl` asks for GL (then under SDL's offscreen driver for tests) | The headless client's `bo1_clientCommands` (times after the client is active) and back buffer screenshots are what the win64 comparisons use; absolute times do not survive llvmpipe's slow loads |
| 2026-10-25 | The MS-ADPCM decoder (`src/platform/posix/snd_adpcm`) uses the seven standard coefficient pairs the engine hard-codes (`iSND_CreateVoice`), not ones read from the stream, and is framing-agnostic (reads the block align from the asset) | The zombies assets are standard MS-ADPCM at 262 bytes a channel (512 frames a block, `snd_driver_xaudio2.cpp:410`); `tests/platform/adpcm_test.cpp` decodes mono and stereo vectors and matches ffmpeg's `adpcm_ms` sample for sample, with a fail-first on a corrupted reference. ffmpeg only encodes power-of-two blocks, so 512/1024-byte blocks stand in for the engine's 262, which the same stride code handles |
| 2026-10-25 | The software mixer's send matrices (`src/platform/posix/snd_mix`) are ported from `SDXA2_UpdateVoiceSends`: a dry matrix to the master/novoice bus and a wet matrix to the 4-channel reverb bus, both destination-major (`m[dest*in+src]`), with `SDXA2_ChannelFixup` and the 1/√2 surround down-mix | The layout and the dry/wet split are where an L/R swap or cross-feed hides; `tests/platform/snd_mix_test.cpp` checks the matrices against hand values and mixes two panned voices into a stereo bus matching a computed reference sample for sample, and writes the mix to a WAV for inspection. The wet bus is built but unused until reverb exists |
| 2026-10-25 | posix sound is a software mixer on an SDL2 audio device (`snd_driver_sdl2.cpp`), replacing `snd_driver_absent.cpp`. SDL owns the audio thread and pulls samples through a callback; one mutex guards the voice table, held only briefly and never across an allocation or I/O (decode happens on the game thread at voice start, the callback's scratch is pre-allocated in `SD_Init`) | A game-loop refill starves on long frames (the libnx note in `.claude/rules/switch-homebrew.md`), so the pull model is deliberate; the mutex is correct and simple because the callback allocates nothing and reads only RAM. The engine still owns the voice table and drives the driver through the same `SD_*` contract |
| 2026-10-25 | Scope of the SDL2 sound backend: RAM voices (10..73) only; streamed voices (0..9), the reverb (wet) send and the master EQ/limiter DSP are deferred, so the mix is dry and `SD_PreUpdate` is empty. Sound turns on only when a device actually opens (`SD_Xaudio2CanInit` opens and closes one), else the engine runs as with `nosnd` | One unknown at a time: the decoders and matrices are verified offline, so the device + threading come up on their own. WSLg lists an ALSA device it cannot open, so the open probe is the real gate; a machine with no audio falls back to the state the engine already handles. Audible correctness, 3D and the non-unity resampler are verified on hardware, not on the 2 fps/no-audio desktop |
| 2026-10-25 | Turning sound on surfaced a 64-bit bug (not in the mixer): `SND_ALIAS_FIELDS` (`snd_db.cpp`), the field table `SND_PatchValue` writes sound-bank patches through, held hard-coded 32-bit struct offsets. In 64-bit the four pointers of `snd_alias_t` push its scalar fields down, so e.g. field 13 (`start_delay`, offset 38 in 32-bit, 58 in 64-bit) wrote onto `soundFile`, corrupting it; `SND_PlaySoundAlias` then crashed dereferencing it. Fixed by making every offset `offsetof(snd_alias_t, …)` | A hardware watchpoint on the corrupted slot caught the exact write at zone load (`SND_PatchValue(field=13, value=100)` → 0x64 into `soundFile+6`). `offsetof` equals the old literal on 32-bit MSVC, so win32 is byte-identical, and is correct on 64-bit. `snd_alias_t` is the only sound asset struct with pointers, so it was the only broken table (the other eight are scalar). Verified: headless Kino, sound forced active, runs the full session without the crash and shuts down clean |
| 2026-10-25 | ffconv's pointer-slot sentinels are guarded by a compile-time round-trip (`WidenPointer`/`NarrowPointer` are `constexpr`, with `static_assert`s in `ff_stream.cpp`) | The retail inline markers 0xFFFFFFFF/0xFFFFFFFE must widen to the 64-bit all-ones / all-ones-but-one the loader tests a slot against (`== (T*)-1`), not a zero-extended 32-bit value; the asserts pin the mapping, the round-trip and the low-32-bit preservation (pointer/data unions) at every ffconv build, so the encoding cannot silently drift |
| 2026-10-25 | Phase 5 started: the Switch (devkitA64 + libnx) build reuses the whole posix SDL2/Win32-shim layer and adds only `src/platform/switch/` (the libnx entry `switch_main.cpp` with romfs + applet-mode refusal + an SD `launch.cfg` for args, and `switch_libc_stubs.cpp` for the six POSIX functions newlib declares but does not implement: sysconf, pread, pwrite, posix_memalign, pause, getrusage). The deltas inside shared files are `#if defined(__SWITCH__)` | SDL2 is ported to Switch, so the window/input/audio/GL plumbing is real reuse, not wishful; a fresh layer would re-implement it for nothing (advisor, 2026-10-25). The bring-up is sequenced one unknown at a time: compiles, then links, then boot on the null backend, then GL, then audio |
| 2026-10-25 | aarch64 deltas that got the Switch build to compile+link+`.nro`: x86 SSE via sse2neon behind a shadow `xmmintrin.h` that suspends radbase.h's lowercase type macros (`u8..f64`) across the include; radbase.h gets a `__SWITCH__` branch (modern GCC/ELF model, `__RADX64__` for the 64-bit typedefs) so Bink's headers parse; the signal-based thread suspend, the crash handler, thread priority and CPU affinity are desktop-only (`#if !__SWITCH__`) with stubs; `/proc`, mmap/mprotect, dlfcn, sysinfo and timegm are replaced with memalign / mallinfo / fixed values / a portable timegm; `_GNU_SOURCE` on the command line (before the forced `-include compat.h`) exposes newlib's fopencookie, whose seek callback takes `off_t*` not `off64_t*` | Each was one devkitA64 build round trip. The crash handler, priority/affinity and a real thread-suspend (svcGetThreadContext3 / svcSetThreadCoreMask) are boot-milestone work, not yet wired; the memory and exe-path replacements are honest stubs (a fixed SD path, a 3 GiB application-mode budget) to be refined on the console. The `.nro` carries no romfs: the converted zones and the install's iwds live on the SD card (`sdmc:/switch/bo1zm/`), which still needs AArch64-ABI IWffs zones from ffconv |
| 2026-10-25 | First boot on Ryujinx (1.1.1375): the `.nro` loads as homebrew and runs. Two prerequisites were found and fixed. (1) GCC 16's libgcc carries guarded GCS/SME/SVE instructions in its unwinder (`chkfeat`, `mrs gcspr_el0`/`tpidr2_el0`, `gcspopm`, `smstop`, `str za`, …) that never run on the Tegra X1 (Cortex-A57, ARMv8.0) but abort Ryujinx's ARM JIT; `switch/tools/patch_ryujinx_mrs.py` NOPs all 38 sites on the linked ELF before `elf2nro` (POST_BUILD). (2) `romfsInit()` needs an embedded romfs: a homebrew NRO with none makes Ryujinx throw `KeyNotFoundException` in `VirtualFileSystem.GetRomFs` on `OpenDataStorageByCurrentProcess` and kills the emulator; a one-file `switch/romfs/version.txt` (`--romfsdir`) fixes it. With both, the boot reaches the engine's `WinMain` (SD `boot.log`: applet type = Application, romfs mounted, args, entering WinMain) and runs ~30 s with no crash | The 38 instructions are feature-guarded dead code on A57 (a `chkfeat`/`cbnz` pair that always skips, or an `__aarch64_sme_accessible` flag that is 0), so NOP is behaviour-neutral on the console while removing every instruction the JIT rejects; the patch finds sites by disassembly (not fixed offsets, which move each relink), byte-verifies each, refuses any site outside a libgcc unwinder symbol, and is idempotent. The engine produces no visible output yet — libnx does not route `printf`, `OutputDebugStringA` is a no-op stub, and no engine logfile was written — `Sys_Print`/`Sys_Error` are teed to the SD `boot.log` under `#if __SWITCH__` (`win_main.cpp`). With that the whole offline init is visible: the engine brings up the D3D9 null backend, Steam fails gracefully (offline), `FS_Startup` runs, `$init` queues the first fastfiles, then `Com_Error`s on the first missing zone (`...\zone\(null)\en_code_pre_gfx_mp.ff` — no data staged) and hangs in `Sys_Error`'s wait-for-quit `GetMessage` loop (alive, not crashed, not crawling). So the first wall is **data**. Next: ffconv AArch64 IWffs zones + the install's iwds staged on the SD; the `zone\(null)` path component (a null zone-dir string) and the `//main` search-path prefix to investigate; then GL (EGL on nwindow) |
| 2026-10-25 | ffconv needs **no separate AArch64 layout tables**: the Switch ABI (devkitA64 `aarch64-none-elf`, AAPCS64/LP64) lays out all 241 streamed IWffs types identically to the win64 reference (`x86_64-w64-mingw32`) the zones are written for, so the existing win64 zones are AArch64-valid as is. `layouts.py` gains a `--check-switch` mode (parses `db_load.cpp` as the switch build compiles it — aarch64 target, the devkitA64 newlib + libstdc++ and libnx/portlibs headers, no host system includes) wired into the switch CMake as `iwffs_layout_check_switch`, parallel to the x86-64 `--check-target64` | The win64 zones already lay out the same on x86-64 LP64 (the posix `--check-target64` passes), and AArch64 is LP64/AAPCS64 too, so the two ABI shifts from win64 (`long` 4→8, non-ms bitfields) were already validated there; confirmed empirically (241/241; clang's AAPCS64 model equals devkitA64 GCC's standardised ABI, and the real compiler never asserts the 64-bit sizes, so the clang check is the guard as on x86-64). No streamed struct uses a bare `long` or a `long double` (either would have diverged on size), so a later one that does now fails the switch build at the check instead of silently emitting zones the console cannot read |
| 2026-10-25 | The staged-data boot narrowed the `zone\(null)` wall: it is **language-init ordering, not data, format or case**. The full converted set (`build/zones64-check`, 65 zones) was staged to the Ryujinx SD at `zone/Common` + `zone/english` (engine casing) with `localization.txt`, and the engine still builds `...\zone\(null)\en_code_pre_gfx_mp.ff`. `en_code_pre_gfx_mp` is a *pre-gfx* code zone loaded before `FS_InitFilesystem` (which registers `loc_language` via `SEH_InitLanguage`), so `DB_BuildOSPath` takes its `else` branch (`Win_GetLanguage` → `localization.language`), and `Win_InitLocalization` (win_main.cpp:2425, pre-FS) had not loaded `localization.txt` either → null language | Case is not a factor: the shared `Bo1_PosixPath` shim case-folds `zone\Common`→`zone/common` and exact-case staging sidesteps it (that shim does have a real `sdmc:`/`device:`-prefix bug in its fallback, masked here by the exact-path fast path). The fix is engine code — make the language resolve before the pre-gfx zone load, mirroring Windows/posix — plus a `.nro` rebuild to verify, not more staging; deferred as its own step. `//main` (empty `fs_basepath`) and the iwds are the next wall, after zones resolve |

## Phases
| # | Phase | Status |
|---|---|---|
| 0 | Repo, toolchains, CMake presets, platform branches in `CMakeLists.txt` | done (2026-10-21) |
| 1 | Compiler compat layer (`src/platform/compat.h`), no behaviour change | done (2026-10-21) |
| 1c | `BO1_OFFLINE` build (Steam, DemonWare, voice, NVAPI, Bink, mjpeg/vpx, monkey, Tracy out; `src/platform/stubs/`) | done (2026-10-21) |
| 2 | `tools/ffconv`: PC fastfiles → native-64 `IWffs` + GLSL 4.60/UBO shaders | done (2026-10-21, win64 ABI). The AArch64 (Switch) ABI needs **no separate tables**: all 241 streamed types verified to lay out identically to the win64 reference, so the win64 zones are AArch64-valid; guarded at build time by `layouts.py --check-switch` (2026-10-25) |
| 3 | 64-bit clean engine on Windows x64 (still D3D9) | in progress: `win64-msvc` runs Kino as a dedicated server (log identical to the reference) and as a client (60 s, same pictures as the reference); 10 rounds next |
| 4 | Portable platform layer + D3D9-emu frontend + GL backend on Linux x86_64 | in progress: 4a, the Linux x86-64 dedicated server runs Kino and Moon headless with the win64 log (22 differing lines, all platform facts); 4b step 1, the D3D9 frontend on the null backend and the SDL2 window layer: the Linux headless client plays Kino until the game over, its log against win64's explained line by line; 4b step 2, keyboard, mouse and clipboard, checked by scripted input on an offscreen window; 4b step 3, the GL 4.3 backend: Kino renders on llvmpipe, checked against the contract by `gb_gl_test` and against win64 by screenshots; 4b step 4, sound: a software mixer on SDL2 (RAM voices; streaming/reverb deferred), its MS-ADPCM decoder and send matrices verified offline (`adpcm_test` vs ffmpeg, `snd_mix_test` vs a computed reference), the live device and threading smoke-tested headless with a dummy driver — audible playback is on hardware |
| 5 | Switch build (`.nro`), memory/threads/applet checks, profiling | in progress: the devkitA64 + libnx build compiles, links and packages `BO1Zombies.nro` (null backend), reusing the posix SDL2/Win32-shim layer with a thin `src/platform/switch/` and `#if defined(__SWITCH__)` deltas. **Boots on Ryujinx 1.1.1375**: the libnx entry runs (applet type = Application), romfs mounts, the engine's `WinMain` runs ~30 s with no crash (SD `boot.log`). Prereqs fixed: the libgcc GCS/SME NOP patch (`switch/tools/patch_ryujinx_mrs.py`, POST_BUILD) for Ryujinx's JIT, and a one-file embedded romfs (`switch/romfs`) so `romfsInit` does not crash the emulator. Engine output teed to the SD (`Sys_Print`/`Sys_Error` in `win_main.cpp`) shows it runs its whole offline init (D3D9 null backend, Steam fails gracefully, `FS_Startup`, `$init`) and `Com_Error`s on the first missing zone, then hangs in `Sys_Error`'s wait-for-quit loop — the first wall is data, not a crash. Next: ffconv AArch64 IWffs zones + iwds on the SD (and the `zone\(null)` path component); then GL (EGL on nwindow) and audio |
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
      - pointer-sized handles: `DynEntityClient::physObjId`, `PhysConstraint::constraintHandle` are 0 in the zone and
        written at runtime; now `intptr_t`, widened by `ffconv` (phase 4, 64-bit handle fields).
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
    - `cg_view_mp.cpp` `CG_OffsetThirdPersonView` keeps pointers in floats (`LODWORD(viewAngles[2]) =
      CG_GetEntity(...)`, then `+ 804`/`+ 488`): third-person view only, needs a rewrite with real locals;
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
- **4**: first step (4a): `linux64-clang` builds a dedicated server that loads a zombies map from the IWffs zones and
  runs its scripts headless, with the same log as the Windows x64 dedicated run (platform layer only, no renderer,
  sound or input). Then: `linux64-clang` plays 10 rounds of Kino. RenderDoc captures for each pass. No GL debug errors. The
  null-backend test of `gfx_d3d9emu` passes.
  - 4a step 1, every engine file parses for Linux (2026-10-23): `cmake --preset linux64-clang`, then
    `tools/syntax_check.py build/linux64-clang`. Win32 comes from the shim (`docs/ARCHITECTURE.md`, "Win32 on posix
    and switch"); the 13 Windows-only files of the decisions above are left out. Before the shim: 569 of 1183 files
    with errors. Now 12 of 1170:
    - 9 files, 16 errors: the phase 3 pointer truncations listed above (`DynEntity_client`, `g_mover`,
      `cl_console`, `r_add_staticmodel`, `rb_pixelcost`, `r_bsp_load_obj`, `g_missile`, `ui_feeders`,
      `ui_main_pc`), the same as `mingw64-clang-full`;
    - open decision: `r_material_load_obj.cpp` and `r_screenshot.cpp` use D3DX (`ID3DXBuffer`, the constant table
      and semantic parsers, `D3DXCompileShader`, `D3DXSaveSurfaceToFileA`), `r_cinematic.cpp` uses DirectSound and
      the Windows layout of Bink's `BINKTEXTURESET` (`binktextures.h` selects `__RADLINUX__` here, which has no
      `tex_draw` nor the lock/unlock calls). The SDK's `d3dx9*.h` and `dsound.h` need far more of Win32 than the
      engine does (`TEXTMETRIC`, `IStream`, GDI glyph metrics, `sal.h`). Options: the D3DX subset the engine uses
      declared in the shim for the D3D9 layer to implement, the SDK headers with a larger shim, or `_WIN32` guards.
  - `mingw32-gcc` and `mingw32-clang` (`--exclude sound/snd_driver_xaudio2`): 1248 files, 0 errors.
    `mingw64-clang-full`: 13 files, 422 errors, the same files and counts as before the shim.
  - `tools/codediff.py build/mingw32-gcc` on every engine `.cpp` changed for this step: identical.
    `r_model_skin.cpp` needs `--cflags=-mmmx` there (the HEAD version does not build with GCC otherwise); with
    `-D_DEBUG` its edited asserts, `phys_colgeom.cpp`'s and `snd_dsp.cpp`'s only move `.rodata` offsets (the
    stringized assert text).
  - 64-bit handle fields (2026-10-23): `DynEntityClient::physObjId` and `PhysConstraint::constraintHandle` are
    `intptr_t` (decision above), with `DynEntCl_CreatePhysObj`'s return and the locals that carry them
    (`GetTraceResultsRigidBody` also truncated `centity_s::pose.physObjId`). `DynEnt_LoadEntities` allocates the
    client lists with `sizeof(DynEntityClient)` (was 20, load_obj path). 64-bit sizes: `DynEntityClient` 24,
    `PhysConstraint` 200, `PhysConstraints` 3216.
    - `syntax_check.py build/linux64-clang --filter 'DynEntity|g_mover'`: 7 files, 0 errors (`DynEntity_client` and
      `g_mover` leave the 4a step 1 list); `mingw32-gcc` and `mingw32-clang`: 1248 files, 0 errors.
      `-Wshorten-64-to-32` over the 37 files that use these structs: no new narrowing. `codediff.py
      build/mingw32-gcc`: identical for the 7 edited files and 43 of the other 45 files that include the two
      headers; `g_missile.cpp` and `r_bsp_load_obj.cpp` differ only through their own pending edits
      (`StickMissile`'s corpse read, `R_CalculateVertexStream2Usage`).
    - `ffconv`: 18 handle leaves in 3 types (`DynEntityClient`, `PhysConstraint`, the 16 of `PhysConstraints`).
      `--verify` on the 65 zones: 0 failed, 237 handles widened and narrowed back in 13 zones (`code_post_gfx`,
      `code_post_gfx_mp` and 11 maps), all 0. All are `constraintHandle`s (`clipMap_t::constraints` and
      `PhysConstraints` assets): the dyn-ent client lists only reserve the runtime block, which grows by 4 bytes per
      entity (`frontend` too). `build/zones64` regenerated (416 s): those 14 zones changed, the other 51 and
      `shaders.pack` are byte-identical (13 704 shaders, 0 rejected by `check_shader_pack.py`).
  - 4a step 2, the dedicated server runs on Linux (2026-10-23):
    - Build: from an ext4 copy of the tree (compiling from `/mnt/c` waits on 9p for every header: half the speed),
      `cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++` (clang 22): every engine file
      compiles and `BO1Zombies` links (ELF x86-64). The build first runs the IWffs layout check (241 types, same as
      win64; decision above). The link showed three Itanium/MSVC differences, fixed with spellings MSVC compiles the
      same: two globals named `data` of different types (`rb_backend.cpp`, `r_water_sim.cpp`; MSVC's decorated
      names include the type), `rb_backend`'s now `static`; two interfaces whose virtual function had no definition
      (`bdDownloadInterceptor::handleDownload`, `broad_phase_terrain_query_callback::query`), now pure (Itanium
      emits the typeinfo with the first non-inline virtual function).
    - Run as `run64.sh` does: `fs_b` = `build/Release`, `fs_h` = a folder holding the exe, with `zone` linked to
      `build/zones64/zone` and `localization.txt` to the install's; `+set bo1_headless 1 ... +set bo1_autoquit 60000
      +devmap zombie_theater`. Exit 0: the zones load (`zombie_theater` in 857 ms, win64 915 ms), game
      initialization, scripts, autoquit, every zone unloaded.
    - Log against a win64 run of the same tree and zones, paths, the date and numbers masked: Kino 14 636 / 14 638
      lines, Moon 14 583 / 14 585, 22 differing lines each, all platform facts: no D3D9 runtime ("Unknown video
      card", `sys_gpu`, `sys_configSum`), the host name and addresses, no CPU set API (`bo1_mod_pcore`), two profile
      lines printed in another order by other threads, and `r_lightTweakSunColor` (upstream bug, below). With the
      numbers kept, only durations and the measured CPU speed differ; every PMem figure is the same.
    - Bugs the comparison showed:
      - 64-bit, in both 64-bit builds: `PMem_FreeIndex` walked down the allocation list with the 32-bit stride
        (`&prim->allocListCount + 2 * count` for `&allocList[count - 1]`), so it took another entry's position for a
        name and padding for a position: the low PMem position became garbage after some frees (1.4 GB in the 392 MB
        pool at Linux shutdown, 1.07 GB on win64, the low half of a string pointer). Now indexed; both runs end at
        `freed -146.55`. The inventory of field-address strides found one more, in `GlassShard`'s mesh build:
        `*(&renderer->maxNumGroupChanges + numVerts)` is `vertexList[numVerts - 1]`, read into an `int` (half a
        pointer on 64-bit), now indexed into a `uintptr_t`. The other strides step through `int`/`float` fields only
        (`actor_state`, `pathnode`, `ui_server`, `cg_visionsets`, physics vectors, BSP and exposure headers).
      - posix: `NET_GetLocalAddress` kept the IPv4 address in `u_long`, 8 bytes in glibc, so `HIBYTE` read byte 7
        (`IP: 0.0.1.1`); now `uint32_t`.
      - posix layer, before the first run: `VirtualAlloc(MEM_COMMIT)` without `MEM_RESERVE` (PMem's), CreateFile's
        `FILE_APPEND_DATA` (the emergency log), text mode (`localization.txt`), an include spelled `types.h` for
        `Types.h` (a check of every `#include` in `src/`, `tl/` and `DemonWare/` found no other that resolves only
        case-insensitively; `TlHelp32.h` and `DbgHelp.h` got `include_cased/` relays).
    - `codediff.py build/mingw32-gcc` on every engine `.cpp` changed in this step (`win_main`, `win_net`,
      `com_fileaccess`, `common`, `live_storage`, `phys_broad_phase`, `phys_collision`, `glass_shard`, CubeMapGenLib's
      `CCubeMapProcessor` and `CImageSurface`) and on the 23 files that include `demo_common.h` or
      `phys_broad_phase.h`: identical, except `rb_backend.cpp` (37 operands: the `.bss` offsets of the now `static`
      `data` and of the 32 variables after it) and `physicalmemory.cpp` (one loop's induction variable: the old and
      new expressions are the same address in the 32-bit layout, `allocListCount` at 4, `allocList` at 12, 8-byte
      entries).
    - `syntax_check.py` on `mingw32-gcc` and `mingw32-clang` (`--exclude sound/snd_driver_xaudio2`): 1248 files,
      0 errors each. `win64-msvc` builds (0 errors) and runs both maps (the references above).
    - Open: the D3D9 layer, SDL2 window and input and the sound backend (4b); the IWffs check or tables for aarch64
      (phase 5); `GetModuleFileNameA` is the only path the layer returns in Windows form, `SHGetFolderPathA` and
      `_getcwd` return POSIX paths the engine extends with `\` (they work, the log shows the mixed form).
  - 4b step 1, the D3D9 frontend on the null backend and the SDL2 window layer (2026-10-23):
    - New: the contract `src/gfx_backend/gfx_backend.h` (`docs/RENDERER.md`) with `gb_common.h`, the null backend
      (`src/gfx_backend/null/`), the frontend `src/gfx_d3d9emu/` (the 87 methods the posix build calls, inventoried
      with libclang; any other one ends the process naming itself), its host `posix_d3d9host.cpp`, and the window
      layer on SDL2 (`win32_gui.cpp`). `d3d9_absent.cpp` is gone.
    - `tests/gfx_d3d9emu` (`d3d9emu_test`, built with the posix target, and with g++ under ASan and UBSan): all
      checks pass, 107 backend calls (device and swap chain, resources and locks, passes and load operations, clears,
      pipelines and samplers, draws and UP draws, RESZ, queries, Reset).
    - `ffconv` after `shader_pack.h` (`ShaderPack_InputLocation`) and `db_load.cpp` (below) changed: `--verify --game`
      on the 65 zones, 0 failed, 13 704 shaders; `check_shader_pack.py`: 0 rejected; the 65 zones and `shaders.pack`
      are byte-identical to `build/zones64`.
    - Linux headless client (`+set bo1_headless_client 1` with `headless.ps1 -Client`'s settings, `+devmap
      zombie_theater`, autoquit 95 s), after the fixes below: `Video card is "no GPU (Direct3D 9 on null)"`, the hidden
      window is created as on Windows, the map loads, the player spawns with the M1911, round 1 starts, the zombies
      down the idle player (game over at level time 44 250), the end of game runs for 47 s, autoquit at level time
      91 200, every zone unloaded (`freed -146.55`, as the dedicated server). The frontend logged only its two start
      lines: no unemulated method, no contract breach (the null backend ends the process on one).
    - Against a win64 client of the same tree and zones (`build/run/win64c8`, 95 s; the real GPU spends its first
      seconds on shaders, so level time 78 700, game over at 54 300), numbers and paths masked: 54 kinds of lines
      differ, all explained. Platform facts: the video card and its memory, `sys_gpu`, `sys_configSum`, the host name
      and address, the CPU set API, DPI awareness, the mods folder the Windows run folder has. The null backend: the
      sun sprite occlusion calibration fails (its queries count 0). Timing: the null backend draws 937 loading-screen
      frames against 715 (each sets `ls_mapname` and `ls_gametype`), moving-entity notices per server frame, the
      first-draw technique warnings, one more AI path failure, the random loading tip, and when the game over comes.
    - Bugs the client run found, each with an inventory of the tree:
      - 64-bit, in both 64-bit builds: `Load_DynEntityClientArray` streamed `20 * count` bytes while `DynEntityClient`
        is 24 on 64-bit, so the server list streamed next overlapped the client list (`Phys_ObjDestroy` on
        `physObjId` 3 in `DynEntCl_InitEntities`; found with a hardware watchpoint). Now `sizeof`. All 78 type/size
        pairs of `db_load.cpp`'s `Load_Stream` calls, checked with `static_assert`s against each variable's type:
        no other. `ffconv` is 32-bit, so the zones do not change.
      - 64-bit: `Dvar_EnumToString` and `Dvar_IndexStringToEnumString` tested `domain.integer.max` for the string
        table, which it overlays on 32-bit only (padding on 64-bit). Now `domain.enumeration.strings`.
      - posix: glibc's 31-bit `rand()` hung `R_CreateParticleCloudBuffer` (decision above).
      - GNU compilers: `SV_BuildClientSnapshot` faulted on a `movaps` into the 4-aligned `MatchState` blocks
        (decision above). `tools/overaligned.py` over the 1 206 posix files lists 62 casts: the two `MatchState`
        blocks and `Z_Malloc` (`GlassPhysics`) needed a change; the others ask for 16 or 128, come from 16-byte pools
        (`MT_Alloc`, the fastfile stream) or point into aligned arrays. One upstream bug (below).
      - 64-bit, from the same list: `vehicle_cache_t` was allocated and freed as 1 928 bytes (3 568 on 64-bit, an
        overrun of 1 640 bytes in the script memory tree; MSVC x86's `sizeof` is 1 968, the same 128-node size class
        as 1 928, so win32 behaves the same); `LoadScriptFile`/`LoadScriptMemory` allocated `length + 1201` for a
        `script_s` followed by its text (1 240 on MSVC x64). Now `sizeof`.
      - 64-bit, in both 64-bit builds: `R_LightGridLookup` cleared its corner pointers with `memset(..., 4 * n)`, half
        of them on 64-bit; a pointer left with its low half zeroed faulted in `R_GetLightingAtPoint`. `litsizes.py`
        now also counts arrays of pointers and shifts (`x << 2`); on `rb_light.cpp` it also lists the light grid hash
        table (`0x40001C` bytes, 4 × `0x100007` pointers, used by `r_vc_makelog`), now `sizeof(htab *)`.
      - `litsizes.py` so extended, over the 764 `mingw64-clang-full` files: 430 calls, 289 on pointer elements. Most
        are element counts or buffer lengths passed next to an array (`String_Parse`, `_Dvar_RegisterEnum`,
        `AreaGlasses`, `FindEntryInList`, ...), out-parameter allocators whose element is a struct, or already
        `sizeof` (`cscr_compiler`'s `512 * sizeof(scr_block_s *)`). The real ones are now `sizeof`/`offsetof`, with
        identical 32-bit code: partial clears and moves of pointer arrays (`standCmds` in `cg_newDraw_mp`,
        `convArgs.args` in the UI and the expression evaluator (7), `newShards` in `glass_shard`, `uiDC.Menus` in
        `ui_shared`, `brush_edges.cpp` (5, one of them stepping `begina + 4` bytes to the next pointer),
        `fs_serverIwdNames`); three `fileShareWriteFileInfo` clears from `thumbData` that missed `numTags`; the
        script parser's hash tables (`l_precomp`, `l_script`: 1 024 and 256 pointers in 4 096 and 1 024 bytes, an
        overrun on 64-bit); `BG_RegisterSurfaceTypeSounds` (31 pointers, load_obj path); and the physics solver's
        `create_pulse_sum_normal`/`create_pulse_sum_wheel`, which allocated 160 and 192 bytes (their MSVC x86 sizes)
        for objects of 176 and 224 bytes on x64: every contact overran the next solver allocation. Left as is:
        `UI_ServersSort` sorts 4-byte elements from `&serverStatusInfo.lines[31][1]`, the decompiler's spelling of an
        `int` array (its comparator reads `int`s), which lands elsewhere on 64-bit; server browser only, unreachable
        offline, to check against the binary.
      - GNU compilers: `long double` (decision above).
      - posix: the word macros (decision above). A zombie's death animscript asserted in `Scr_GetAnims` (tree 0)
        after the game over. A trap in `HIGH_IND` (an error when its operand is a pointer, `long` or `unsigned long`)
        over the 1 206 posix files lists 5 uses: with the shim, `GScr_AnimHasNotetrack` (2) and `DB_GetXAsset`'s
        `s_inuseCache` tag (2, the pointer's bits 16-31) take windows.h's as on Windows; `q_shared.h`'s `SHIWORD` has
        no windows.h counterpart, and `MainWndProc` read `WM_MOVE`'s y from the top of the 64-bit `LPARAM` in both
        64-bit builds: now `SWORD1`, the same word as on 32-bit.
      - 64-bit, in both 64-bit builds: `Phys_CreateHinge` passed `(int)obj1` to `Phys_GetUserData`, truncating the
        handle (a crash after the game over). An explicit cast escapes `-Wshorten-64-to-32`; `ptrcasts.py` now lists
        them (`handle-narrow`). Over the 1 206 posix files: 175 casts of `intptr_t`/`uintptr_t`/`DWORD_PTR` values to
        4 bytes or less; this one is the only truncated handle. The rest keep low bits on purpose (alignment tests,
        hashes, debug prints), read counts the decompiler typed as pointers (`indices` in `fx_sprite`, the
        `animscriptfunc` offsets, the script VM's counts), or are expansions of windows.h's word macros.
    - `codediff.py build/mingw32-gcc --cflags=-mlong-double-64` over the 1 248 files (the 9 SSE/MMX files with
      `-msse2 -mmmx`): 1 158 identical; the 71 libtomcrypt files differ in `.rdata` offsets as before; the 19 edited
      files that differ are explained: `cg_vehicle`/`cg_snapshot_mp` (the `vehicle_cache_t` size), `sv_init_mp` and
      `com_memory` (the GNU branch of `BO1_ALLOC_ALIGN`/`Z_HEADER_SIZE`: 4 → 64, 164 → 176), `dvar` (block order at
      -O2; identical at -O0), `physicalmemory` and `rb_backend` (4a step 2), `cl_console`, `r_bsp_load_obj`,
      `rb_pixelcost`, `g_missile`, `ui_feeders`, `ui_main_pc` (their 64-bit edits of 4a step 1), `cm_mesh`,
      `l_precomp`, `l_script`, `eval` (only the mangled names of functions taking `long double`), `phys_util` and
      `rbc_def_vehicle` (x87 register and stack-slot choices around former `long double` locals). The 16 files of
      the pointer-array fixes, after that run: identical, `l_precomp`, `l_script` and `rbc_def_vehicle` keeping only
      the differences above.
    - The same `long double` change compiled for MSVC x86 (`clang++ --target=i686-pc-windows-msvc`, the phase 3
      MSVC-ABI command with `-O2 -c`), HEAD against the worktree, instructions compared: 67 of the 70 `.cpp` files
      identical (`phys_util` and `rbc_def_vehicle` included); `cg_vehicle` differs by the `vehicle_cache_t` size
      only; `rb_pixelcost` differs by its 4a edit only (with that edit kept and `long double` restored: identical);
      `cg_scr_main` does not compile in that mode at HEAD either (a cast clang refuses), and is identical with MinGW.
    - `syntax_check.py` on `mingw32-gcc` and `mingw32-clang`: 1 248 files, 0 errors each. `win64-msvc` builds
      (0 errors). The posix build links and runs the IWffs layout check (241 types).
    - Open (rest of 4b): keyboard and mouse (step 2, below), fullscreen, the GL 4.3 backend (step 3, with
      `D3DXSaveSurfaceToFileA` for the screenshots the comparisons use), SDL2 audio, the gamepad.
  - 4b step 2, keyboard, mouse and clipboard (2026-10-24, `win32_input.cpp`, decisions above):
    - A windowed client (`r_fullscreen 0`, the same window settings as the headless client) under
      `SDL_VIDEODRIVER=offscreen`, `+devmap zombie_theater`, autoquit 58 s, with `BO1_INPUT_SCRIPT` from 42 s (after
      the map load, which the engine's own input drop requires): the console opens on the key at scan code 0x29;
      typed `/viewpos` prints `(-99 -1256 144) : 61 0`; two 200-pixel moves right and one 100 up turn the view to
      `17 349` (yaw down 44 degrees, pitch up 11); holding W for 2 s moves the player to `(269 -1138 147)`, 385 units
      along yaw 17; `/echoo`, Backspace, ` Hello_World! backspace ok`, Enter runs `/echo Hello_World! backspace ok`
      (shifted characters, Backspace through `WM_CHAR` 8); `/viewpos` put on the clipboard and pasted with Ctrl+V
      (`WM_CHAR` 22, `Field_Paste`, `Sys_GetClipboardData`) runs; a left click fires (the impact effect appears).
      In a run whose events came before the map load (they reach `Sys_QueEvent`, then the load drops them), gdb on
      `Sys_QueEvent` shows the other mappings: the wheel (206 up, 205 down, one event per notch),
      the right, middle and X1 buttons (201, 202, 203), F1 167, keypad 8 without Num Lock 183, Up 154, Delete 162,
      keypad Enter 191, Shift 160 with `a` 97, Right Ctrl 159, `-` 45, Escape 27, Tab 9, Space 32.
    - Bugs the runs found in the layer itself, fixed before the results above: `TranslateMessage` read the modifiers
      when the render thread pumped, so a Ctrl released before that lost its Ctrl+V (now decided at key-down); a
      paste that waited for the video thread stalled it and the main thread for a second (now the clipboard copy).
    - The headless client run is unchanged (Kino 60 s, game over, autoquit, `freed -146.55`); `d3d9emu_test` passes.
    - Not tested by machine: a real keyboard and mouse under WSLg (X11), and SDL's relative mode there; the offscreen
      driver has none, so the captured cursor's virtual motion is what the runs exercised.
  - 4b step 3, the GL backend (2026-10-24, `src/gfx_backend/gl`, `docs/RENDERER.md`, decisions above):
    - `gb_gl_test` (SDL offscreen, llvmpipe, Mesa 26.1.6, OpenGL 4.6 core): 46 checks pass. Each convention check was
      seen failing first: the front face (3 checks failed with `GL_CCW`), the pixel centres (2 failed without
      `bo1_pos_fixup`), the 4-sample edges (2 failed with the old half-pixel viewport), the upload after a draw
      (2 failed without `EditTexture`); an exact half-pixel fixup failed 4 checks on ties at edges through pixel
      centres, hence 63/128.
    - `d3d9emu_test` passes (115 backend calls; the constant blocks are 4 112 bytes, a viewport change uploads only
      the vertex block; `D3DXSaveSurfaceToFileA` writes the exact TGA bytes, a sub-rect, a JPG, and refuses DDS).
    - `ffconv --verify --game build\Release` after each normaliser change (vPos, `bo1_pos_fixup`, shadow samplers):
      65 zones, 0 failed, 13 704 shaders, 0 rejected by `check_shader_pack.py`; the 65 zones identical byte for byte
      to the previous ones (`diff -rq`); against the previous pack only the expected lines changed (356 vertex
      shaders with the fixup, 300 pixel shaders with the new `vPos`, `bo1_vpos_flip` gone).
    - The game on GL: the windowed client under `SDL_VIDEODRIVER=offscreen` and the headless client with
      `BO1_GFX_BACKEND=gl` load Kino and draw it: the theater from the spawn, the HUD, the round text, upright, the
      13 704 shaders compiled and linked without an error, no GL debug message (`BO1_GL_DEBUG=1`); against the
      win64 shot from the spawn, the same scene: theater, lit furniture, pistol and hands, HUD. Bugs the runs
      found, fixed before the results above (found with a per-draw pixel watch on the backend's thread, since
      removed): the input pump never ran (SDL video started on the main thread for `Sys_DetectVideoCard`); a line
      of clear colour along the first column and row with 4 samples (the half-pixel viewport); every model drawn
      black (uploads after a draw went to the wrong texture unit, so the model lighting atlas never changed:
      `glTexSubImage3D` reported `GL_INVALID_OPERATION`); shadow map samplers read as plain samplers.
    - `gl perf:` on llvmpipe with other sessions loading the machine (load 3-8 on 8 threads): about 3.5 fps in
      game for 5 400 draws a frame, the backend's thread busy the whole frame (software rasterising), the frontend
      side waiting for it; about 40 fps on the loading screen. The client uploads about 14 MB a second in game
      (4 MB a frame), to profile on the console (phase 5).
    - The headless client on the null backend is unchanged (Kino 95 s, autoquit, `freed -146.55`); `win64-msvc`
      builds.
    - COLOR clamp (2026-10-24): the normaliser clamps COLOR outputs to [0, 1] (decision above); `ffconv --verify`
      reconverts, 65 zones 0 failed, the zones identical, only `shaders.pack` changed (356 vertex shaders gain the
      clamp), 0 rejected by `check_shader_pack.py`. `gb_gl_test` grows a check (46) that fails without it.
    - Gamepad (2026-10-24): `SDL_GameController` behind the XInput API (decision above); the mapping is unit-tested
      (`gamepad_test`, fail-first confirmed) and a headless GL run opens the subsystem without a crash and reports no
      device on a machine with no pad. Real-controller input needs a pad on a display (hand-to-user).
    - Fullscreen (2026-10-24): `r_fullscreen 1` becomes SDL desktop full screen (decision above). Under the
      offscreen driver it is a no-op, but a windowed client with `r_fullscreen 1 r_borderless 0` reaches the
      full-screen window path, creates the window and renders Kino without a crash; the actual display coverage
      needs a real display (hand-to-user). The borderless and headless paths are unchanged.
    - Open (rest of 4b): SDL2 audio (PCM + MS-ADPCM only, no ffmpeg -- inventory above); for GL, the GLSL version
      for Mesa 20.1 on the Switch (`docs/RENDERER.md`).
    - Case-sensitive filesystems (found testing on ext4 2026-10-25, fixed same day): the engine opens zones with
      fixed casing (`DB_BuildOSPath_Unlocalized` -> `zone\Common\`, `DB_BuildOSPath` -> `zone\<language>\`) while
      ffconv writes `zone/common` and `zone/English`. On /mnt/c (9p, case-insensitive) it worked; on ext4 (and the
      Switch SD) it did not. Fixed in `Bo1_PosixPath` (`win32_files.cpp`): every file op resolves each existing path
      component to its real case (the exact path is tried first, so a matching case costs one `stat`); a component
      that does not exist is kept, so writes keep their intended case. Verified on the ext4 run with no symlinks:
      `common_zombie` loads (it had failed before).
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
- `src/universal/dvar.cpp` (`_Dvar_RegisterLinearRGB`, `_Dvar_RegisterColorXYZ`): x, y and z are declared
  `unsigned int` instead of `float` and stored in the first three bytes of an uninitialised `DvarValue`, so
  `r_lightTweakSunColor`'s default reads back as a float built from stack bytes (`2.00006` on win64, `3.6e-43` on
  Linux). Renderer only (`r_bsp.cpp` overwrites it when a world loads); fixing it changes the 32-bit build.
- `src/EffectsCore/fx_draw.cpp` (`FX_DrawElem_Cloud`): `_InterlockedExchangeAdd((volatile unsigned long long *)
  &system->gfxCloudCount, 1u)` on a 4-byte field. winnt.h's C++ overload makes it a 64-bit add, which also rewrites
  the next 4 bytes (`visState` on 32-bit, padding on 64-bit; the field is 8-aligned there, so AArch64 can run it).
  The posix shim reproduces the overload.
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
  `useFastFile 0`. With MSVC the `Eval` layout is the same on x86 and x64; `EvalValue::u.d` is a `double` on every
  compiler since phase 4b (decision 2026-10-23, `long double`).
- `src/physics/phys_colgeom.cpp` (`gjk_double_sphere_t` query creation): `allocate(sizeof(gjk_double_sphere_t), 0,
  0)` passes alignment 0, for which `phys_transient_allocator::allocate` computes address 0 and asserts; the other
  shapes pass 16. Same on every build; unreached so far (found by `tools/overaligned.py`).
- `src/physics/xdoll.cpp` (`XDoll_CreatePhysObj`, the AABB case of a model without `collSurfs`): `surfFlags = (int)"t"` takes
  the address of a string as surface flags, then their bits 20-25 as the surface type: a decompiled constant that
  happened to match an address. The type is address-dependent on every build.
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
