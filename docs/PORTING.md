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
| 2026-10-25 | The staged-data boot narrowed the `zone\(null)` wall: it is **language-init ordering, not data, format or case**. The full converted set (`build/zones64-check`, 65 zones) was staged to the Ryujinx SD at `zone/Common` + `zone/english` (engine casing) with `localization.txt`, and the engine still builds `...\zone\(null)\en_code_pre_gfx_mp.ff`. `en_code_pre_gfx_mp` is a *pre-gfx* code zone loaded before `FS_InitFilesystem` (which registers `loc_language` via `SEH_InitLanguage`), so `DB_BuildOSPath` takes its `else` branch (`Win_GetLanguage` → `localization.language`), and `Win_InitLocalization` (win_main.cpp:2425, pre-FS) had not loaded `localization.txt` either → null language | Case is not a factor: the shared `Bo1_PosixPath` shim case-folds `zone\Common`→`zone/common` and exact-case staging sidesteps it (that shim does have a real `sdmc:`/`device:`-prefix bug in its fallback, masked here by the exact-path fast path). Root cause = **working directory**: `Win_InitLocalization` reads `localization.txt` via `FileWrapper_Open` → `bo1_fopen(name, "rt")` relative to the cwd, and `fs_basepath` defaults to `Sys_Cwd()` (`getcwd`); the desktop builds run with cwd = the install folder (set by their launcher), the console has none, so both the language and `fs_basepath` come out empty. Fix: `chdir(kDataDir)` in `switch_main.cpp` before WinMain (the launcher analog; logs its return + `getcwd`) — one change for both symptoms; the `TextRead` shim already collapses the file's CRLF so the language parses clean. `//main`/`fs_basepath` and the iwds are the next wall, after zones resolve |
| 2026-10-25 | The converted zones now **load on Ryujinx**, which proves the ffconv AArch64 work end to end. Three walls fell in a chain, each by one targeted fix. (1) `chdir(kDataDir)` in `switch_main.cpp` (the desktop launcher's cwd=install analog): resolves the language and `fs_basepath` (search path is now `sdmc:/switch/bo1zm/main`, not `//main`). (2) The zone read failed with ERROR_INVALID_PARAMETER on the 0x40000 **read-ahead past the end of a small fastfile**: libnx's fsdev `lseek(SEEK_SET)` past EOF returns EINVAL, where POSIX `pread` reads 0 (EOF); the `pread` stub (`switch_libc_stubs.cpp`) now returns 0 for an offset at/past the file size. (3) The IWffs ABI tag: ffconv stamps zones `DB_FF64_ABI_WIN64`, the Switch build read `DB_FF64_ABI_AARCH64` and rejected them — but AArch64 is layout-identical to win64 (241/241, `--check-switch`), so the Switch build now reads `DB_FF64_ABI_WIN64` like the posix x86-64 build (`db_ff64.h`) | Diagnosed with a `Com_Printf` trace in `DB_ReadData` (a per-call SD `fopen` logger is blind: `win_main.cpp`'s `Switch_DebugLog` holds `boot.log` open, and libnx rejects a second write handle, so only the engine's own log channel reaches the SD). `boot.log` now shows `Loaded fastfile 'en_code_pre_gfx_mp'` + `'code_pre_gfx_mp'`, then `FS_Startup`, then `Couldn't load fileSysCheck.cfg` — the next wall is the **iwds** (the Steam install's `main/*.iwd`, 57 of them) not staged on the SD, as expected |
| 2026-10-25 | With the 57 iwds staged, the engine boots through to a **map load**: FS_Startup indexes 103814 iwd files, R_Init comes up (null backend), the frontend/menu zones load, and `+devmap zombie_theater` loads `common_zombie` (78 MB), `zombie_theater_patch`, then `en_zombie_theater` — Kino der Toten. One more switch fix got the iwds seen: `Bo1_PosixPath` (win32_files.cpp) now treats a devoptab `device:` prefix (`sdmc:`) as the root. Its case-insensitive fallback assumed a `/`-rooted or relative path, so a `sdmc:`-prefixed path with a wildcard (the pattern `Sys_ListFiles` builds for the iwd scan) resolved from `"."` and mangled the device segment — `FS_Startup` found `0 files in iwd files`. For a plain file open the exact path exists and the fallback never runs, which is why zones loaded but the iwd listing did not | The map load then **crashes** part way through `en_zombie_theater`: Ryujinx reports an `InvalidMemoryRegionException` (guest invalid access at a high address), with script/config strings (`"restart\n"`, `"set next…"`) in the registers — a 64-bit memory bug in the zombie map asset stream (a likely GSC VM pointer/codepos width area). Reproduced on the posix x86-64 build (headless, under gdb): it does **not** crash — that build loads `en_zombie_theater` + `zombie_theater` and runs the map (`sv_running 1`), from zone files byte-identical (md5) to the SD's. So the zones and the IWffs loader are correct on 64-bit; the crash is specific to the Switch runtime (libnx/AArch64). Next: map the Ryujinx fault PC (`rtld:0x613a10`, `.text` base 0x0) to a function with `aarch64-none-elf-addr2line`/`nm`, and look at Switch-runtime causes — the worker (DB) thread's stack size (the `CreateThread` shim passes 0, the devkitA64 pthread default, far below Linux's 8 MB; the map's deeper `Load_*` traversal could overflow) or a switch memory-layout pointer — not the data path, now proven clean |
| 2026-10-25 | The crash was **not** the loader: the Ryujinx fault PC maps to `GPad_GetStick` (win_gamepad.cpp:575), called from `IN_GamepadsMove` — a gamepad-input 64-bit bug the headless posix/win64 builds never hit (no controller polled). `GamePadStick` carries `GPAD_STICK_MASK` (0x40000000) in its values (`GPAD_LX..GPAD_RY` = 0x40000000..3); `GPad_GetStick` indexed `sticks[stick]` with the mask still set. On 32-bit the mask fell off the `* sizeof(double)` offset by truncation (→ sticks[0..3]); on 64-bit `sticks[0x40000000]` is a ~4 GB out-of-bounds read. Fixed by masking the index (`stick & 0xBFFFFFFF`), as the sibling `GPad_GetStickChangedToPressedState` already does — byte-identical on MSVC-32, correct on 64-bit, no `#if` | With it, **Kino loads fully on Ryujinx**: `common_zombie`, `en_zombie_theater`, `zombie_theater` (105 MB) all load and the engine reaches `Game Initialization`. The next crash is in the **GSC script compiler** (`Scr_CalcLocalVarsFormalParameterListInternal`, cscr_compiler.cpp:1297 — a NULL deref during script compile), again Switch-specific (the posix x86-64 build compiled the same scripts and reached `sv_running 1`). Cause found 2026-10-25 (two rows down): uninitialised parse-node memory — **not** codepos width and **not** stack depth |
| 2026-10-25 | The GL (Mesa) backend is enabled on the Switch build and **renders on Ryujinx**: a real GL 4.3 core context (NV120 / Mesa 20.1 / nouveau), "Direct3D 9 on the gl backend", frames presented and textures uploaded. ffconv now emits GLSL 4.30 (the Switch Mesa caps there; 4.60 was rejected) and the staged `shaders.pack` is patched 4.60→4.30 in place. A `gfx.cfg` file picks the backend without a rebuild. **Shader cache: not feasible on GL/Mesa** — `GL_NUM_PROGRAM_BINARY_FORMATS` is 0 (nouveau advertises `GL_ARB_get_program_binary` but provides no format) and the portlibs' Mesa has no transparent disk cache, so neither glGetProgramBinary nor an env-var cache works. The compile of 13704 shaders makes the first boot very slow on Ryujinx's emulated Mesa | The perf path for shaders is therefore the **deko3d + uam** route (phase 7: offline compilation to DKSH, no runtime compile), or cutting the shader count, not a GL cache. ⚠ The GSC-crash re-diagnosis attempted here (libnx `armGetTls`, stack overflow, runaway recursion, "the parser builds a malformed AST") was **wrong** and is superseded by the next row: SP was constant across runs (not ~0) and a depth/cycle instrumentation of `Scr_CalcLocalVars*` never fired, so it is not recursion at all — it is a NULL deref from uninitialised parse-node memory |
| 2026-10-25 | **GSC compiler crash fixed — uninitialised parse-node memory, not recursion.** `sval_u` (cscr_debugger.h) is a union whose largest member is `sval_u *node` (8 bytes on 64-bit; the `// sizeof=0x4` comment is the stale 32-bit size). The parse-tree builders write 32-bit fields into 8-byte slots and rely on the rest being zero: a list terminator is `node[1].stringValue = 0` (`linked_list_end`/`append_node`, cscr_parsetree.cpp), which zeroes only the low 4 bytes of the slot and leaves the high 4 whatever `Hunk_UserAlloc` last held. `Scr_CalcLocalVarsFormalParameterListInternal` walks the list reading that slot as `node[1].node` (8 bytes): on x86-64 fresh hunk pages read back zero → `next` is NULL → the walk ends; on AArch64 reused node memory left the high half non-zero → `next` came back `0x2'00000000` → the walk dereferenced NULL (fault at 0x0). **Fix:** `Scr_AllocNode` zeroes the node block, guarded `if (sizeof(sval_u) > sizeof(unsigned int))` so the branch folds away on the 32-bit reference | Diagnosed by instrumenting the formal-parameter walk (per-cell pointer log + null guards) on the null backend: the culprit was the 2nd function parameter list in `codescripts/struct.gsc`, `next = 0x200000000` — the exact signature of a 4-byte-low-zeroed 8-byte slot (`0x2 << 32`). Verified on the null backend **with and without** the unrelated `-fsigned-char` probe (which was my first, wrong hypothesis and was reverted): no `InvalidMemoryRegionException`, the compile completes, `zombie_theater` reaches `Game Initialization` and runs zombies rounds (`mus_zombie_round_start`, perks revive jingle, `mus_zombie_game_over`, 130+ s of server sim time). `codediff` on cscr_parsetree.cpp vs mingw32-gcc is **identical** (the guard removes the fix from the 32-bit reference codegen). Family note: the same 4-byte-into-8-byte pattern also appears as full-`sval_u` copies of partially-constructed temporaries (`node2_`, `prepend_node`, `append_node`, `node[k] = val`) — benign by type-consistency (a value stored is read back as its own type, never reinterpreted as `.node`) and not observed on `zombie_theater`, but unverified on the other five maps. **Verified null-backend only**; the fix is in the backend-independent compiler so it carries to GL. Next: GL smoke test of the fixed build + the "AI couldn't find path to goal" question (likely no player target in a headless run, unconfirmed) |
| 2026-10-26 | Decompiled byte shuffles read their bytes through `Float4Byte(v, i)` (`com_math.h`): 84 reads in `fx_convert.cpp`, `fx_beam.cpp` and `r_water_sim.cpp` that were `v.unitVec[0].array[i]` with a computed `i` up to 15 (fx_beam's `beamDot` reads, upstream, already index a separate vector with `i - 16`) | `PackedUnitVec::array` is 4 bytes, so GCC takes any index above 3 as out of bounds. In `Byte4PackVertexColor` devkitA64 GCC then computed and stored only the red channel and packed stack garbage, usually 0, as G, B and alpha, so every UI and HUD vertex colour had alpha 0 (invisible menus and HUD, on GL and deko3d alike). Disassembly after the fix shows all four channels stored. `codediff.py` on mingw32-gcc: `fx_beam.cpp` identical; in `fx_convert.cpp` only `Byte4PackVertexColor` (i686 GCC also dropped channels 1-3: the base loads one float, the new code four), `FX_SampleVisualState` (it called the full function twice instead of inlining the truncated one) and one constant-pool offset in `FX_ConvertElemDef`; in `r_water_sim.cpp`, `ReadValue` now copies all 48 bytes of its `v5[3]` instead of 4. MSVC never exploited the bound, so the reference is unchanged. No other `.array[<computed>]` read is left in `src/` (grep) |
| 2026-10-26 | Every GCC/clang build of the engine passes `-fsigned-char` (`CMakeLists.txt`, with the other MSVC-semantics flags) | MSVC's `char` is signed; the AArch64 ABI makes it unsigned in GCC, so each negative `char` of the engine read back above 127 on the Switch. Found through the HUD: `Glyph::y0` (`r_font.h`) is a `char`, glyph tops are negative, so every line of text was drawn 256 × font scale lower than on PC (the score text at y 830-849 of a 720-line frame, under the screen, where the win64 reference draws it at ~677). With the flag the score `500` sits on its brush and the round tally shows, as in the win64 reference. x86 targets already default to signed `char`, so the flag changes nothing there; it was first tried (and reverted, as unrelated) for the GSC crash (2026-10-25 row) |
| 2026-10-26 | **The loading-screen checkerboard is fixed** (64-bit builds): `Window_Paint` draws the map's `loadscreen_<map>` material where a window's background is `$levelbriefing`, black when the map has none (`ui_shared.cpp`, under `#if defined(_WIN64) \|\| !defined(_WIN32)`) | Traced on Ryujinx/deko3d by logging every large 2D quad with its caller chain: the full-screen checkerboard is the window background of the `connect` menu (`UI_DrawMapLevelshot` → `Menu_Paint`), the material `$levelbriefing`, whose only image is `default` — `images/default.iwi` in `iw_04.iwd`, the 16x16 checkerboard. Nothing in the decompiled engine puts a level image into it, and the menu has no `WINDOW_STYLE_LOADSCREEN` (7) item, the path that draws the material `UI_SetLoadingScreenMaterial` resolves (`loadscreen_zombie_theater` is found, not default). The fix shows that material, the Kino artwork, instead. The 32-bit reference runs the same code on the same data, so it should show the checkerboard too (not observed); it keeps the decompiled behaviour (`codediff.py`: `ui_shared.cpp` identical). Not a ffconv or backend problem: the earlier "missing materials" verdict (deko3d notes) had the right screen but the wrong asset |
| 2026-10-26 | A dedicated-server project starts on branch `server` (worktree `../BO-Zombies_switch_server`), phase 8 below: a dedicated zombies server (Linux and Windows, curses console), win64 and Switch clients joining over UDP by IP from `launch.cfg`, client logs sent to the server, admin commands, join in progress, then more than 4 players | The engine is the MP build with the SP zombies grafted on, so the Quake 3 UDP connect path (`connect`, `getchallenge`, `SV_DirectConnect`, netchan) is already there without DemonWare; only a client of a *remote* server had never been run |
| 2026-10-26 | Fixes that make a path work which was broken in the reference too (a remote `connect` from the menu) may change `win32-msvc` behaviour, without a guard | Agreed with the user. 64-bit fixes and port code still keep the 32-bit code identical (`codediff`) |
| 2026-10-27 | The message Huffman tree is built with a total order: equal weights fall back to node creation order (`huffman.cpp`, `nodeCmp`) | `qsort` leaves the order of equal elements to each C library, so MSVC, glibc and newlib built different trees and a client decoded garbage from a server of another platform (a win64 client of the Linux server misread a script mover's origin, then crashed on a dynent id). It changes the compressed bits on every build, `win32-msvc` included; both ends run the same code, nothing else reads them |

## Phases
| # | Phase | Status |
|---|---|---|
| 0 | Repo, toolchains, CMake presets, platform branches in `CMakeLists.txt` | done (2026-10-21) |
| 1 | Compiler compat layer (`src/platform/compat.h`), no behaviour change | done (2026-10-21) |
| 1c | `BO1_OFFLINE` build (Steam, DemonWare, voice, NVAPI, Bink, mjpeg/vpx, monkey, Tracy out; `src/platform/stubs/`) | done (2026-10-21) |
| 2 | `tools/ffconv`: PC fastfiles → native-64 `IWffs` + GLSL 4.60/UBO shaders | done (2026-10-21, win64 ABI). The AArch64 (Switch) ABI needs **no separate tables**: all 241 streamed types verified to lay out identically to the win64 reference, so the win64 zones are AArch64-valid; guarded at build time by `layouts.py --check-switch` (2026-10-25) |
| 3 | 64-bit clean engine on Windows x64 (still D3D9) | in progress: `win64-msvc` runs Kino as a dedicated server (log identical to the reference) and as a client (60 s, same pictures as the reference); 10 rounds next |
| 4 | Portable platform layer + D3D9-emu frontend + GL backend on Linux x86_64 | in progress: 4a, the Linux x86-64 dedicated server runs Kino and Moon headless with the win64 log (22 differing lines, all platform facts); 4b step 1, the D3D9 frontend on the null backend and the SDL2 window layer: the Linux headless client plays Kino until the game over, its log against win64's explained line by line; 4b step 2, keyboard, mouse and clipboard, checked by scripted input on an offscreen window; 4b step 3, the GL 4.3 backend: Kino renders on llvmpipe, checked against the contract by `gb_gl_test` and against win64 by screenshots; 4b step 4, sound: a software mixer on SDL2 (RAM voices; streaming/reverb deferred), its MS-ADPCM decoder and send matrices verified offline (`adpcm_test` vs ffmpeg, `snd_mix_test` vs a computed reference), the live device and threading smoke-tested headless with a dummy driver — audible playback is on hardware |
| 5 | Switch build (`.nro`), memory/threads/applet checks, profiling | in progress: the devkitA64 + libnx build compiles, links and packages `BO1Zombies.nro` (null backend), reusing the posix SDL2/Win32-shim layer with a thin `src/platform/switch/` and `#if defined(__SWITCH__)` deltas. **Boots on Ryujinx 1.1.1375**: the libnx entry runs (applet type = Application), romfs mounts, the engine's `WinMain` runs ~30 s with no crash (SD `boot.log`). Prereqs fixed: the libgcc GCS/SME NOP patch (`switch/tools/patch_ryujinx_mrs.py`, POST_BUILD) for Ryujinx's JIT, and a one-file embedded romfs (`switch/romfs`) so `romfsInit` does not crash the emulator. With the converted zones staged on the SD, the engine **loads the pre-gfx code zones** (`en_code_pre_gfx_mp`, `code_pre_gfx_mp`) — the ffconv AArch64 path proven end to end — and runs `FS_Startup`. Four fixes got there: the GCS/SME NOP patch and embedded romfs (boot), `chdir` to the install dir (language + `fs_basepath`), a `pread` EOF fix for libnx's past-EOF `lseek` (zone read-ahead), and reading the win64 IWffs ABI tag (layout-identical, `--check-switch`). With the iwds staged and a `device:`-prefix fix in `Bo1_PosixPath`, it then boots through FS_Startup (103814 iwd files), R_Init, the frontend, and `+devmap zombie_theater` into a **map load** (common_zombie 78 MB, the Kino zones) — before crashing part way through `en_zombie_theater` on a guest invalid memory access (a 64-bit bug in the zombie map asset stream, to debug on the posix x86-64 build). Then GL (EGL on nwindow) and audio |
| 6 | Stabilisation, all base maps, docs | todo |
| 7 | (optional) deko3d backend behind the same `gfx_backend.h` | in progress: opt-in on the Switch build (`gfx.cfg` = deko3d, `shaders.dksh` from uam); renders the main menu's 3D scene and Kino in game with its HUD on Ryujinx (2026-10-26, see the deko3d notes below); not yet run on the console in game |
| 8 | Dedicated server: curses console, UDP clients from `launch.cfg`, client logs, admin commands, join in progress (branch `server`) | in progress: step 0 done (2026-10-26), a win64 client of a remote win64 dedicated server plays Kino over UDP; step 1 done (2026-10-27), `bo1_server` on Windows and Linux, LAN clients of both, `getstatus`; step 2 done for PC clients (2026-10-27: `launch.cfg`, `cl_autoConnect` with reconnection, `cl_allowWanConnect`), its Switch part deferred until `_new` and `_server` are merged into `switch`; step 3 done (2026-10-27: curses console, Linux and Windows); step 4 done on win64 (2026-10-27: `zm_*` admin commands, mods/admin); see "Dedicated server (phase 8)" |

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

## Map-load crash on console = runtime shader-compile storm, OOM-killed (2026-10-25)
`+devmap zombie_theater` on hardware does not hard-crash on a bad asset: it dies of OOM during the GL backend's
shader compile. Instrumenting the build lane (`gb_gl_device.cpp` `Compile`/`Link`/`ServeBuilds`, logged as
`gl build:`) showed, over one run:
- shader count climbed to **7207 and was still climbing** when the console killed the process (the zone streams
  thousands of techset shader variants; `CreateShader` compiles each one eagerly and never dedups);
- per-compile cost **uniform ~20 ms, worst single only 56 ms** -> no pathological shader, no hang, no loop;
- total compile time **143 s and not finished**; the render thread meanwhile only drew the 8-draw loading plaque
  (`0 new pipelines, 0 links`), so the frontend perf counters hid the whole storm;
- **heap grew monotonically 580 -> 2537 MB (~0.27 MB/shader)**; projected to ~10 000 shaders that is ~3.3 GB,
  past the application-mode budget -> the console kills it. That kill is the "erreur switch" the user saw.

Root cause is eager runtime GLSL compilation of the map's entire shader database on nouveau, in both time and
memory. `CreateShader` (`gb_gl.cpp`) does `new Shader` + eager `QueueBuild`; the zone references the whole pack and
every referenced shader is compiled at load, drawn or not.

Dedup ruled out by a static measurement of `shaders.pack` (`BO1GLSL1`, 13704 entries = **356 vertex, 13348
pixel**): **13682 of 13704 sources are byte-distinct** (only 22 duplicates), and two same-size pixel shaders differ
in constants, samplers, inputs and body -- genuinely different shaders, not trivial variants. So the 13704 are
real and unique; dedup by source (or trivial normalisation) saves nothing. The only lever is **lazy compilation**:
compile a shader only when a drawn pipeline first needs it, so only the on-screen working set compiles, not all
13704. Caveats: lazy compile defers memory rather than bounding it (a full match may still climb toward the
ceiling -> may need LRU eviction of compiled programs later), and the first gameplay frame triggers a burst of
`ProgramFor` links on the render thread (a multi-second first-frame stall instead of an upfront OOM). `DestroyShader`
has no refcount, which a shader cache would have to respect.

Kill mechanism pinned by the Atmosphere crash reports (2026-10-25, build 23:10): two `BO1Zombies` Data Aborts,
Fault Address **0x0**, both **inside Mesa's GLSL compiler on the backend thread**, exactly in the storm --
`DeviceMain -> ServeBuilds -> _mesa_glsl_compile_shader -> glcpp_preprocess -> _mesa_string_buffer_append_len ->
memcpy` and `_mesa_glsl_compile_shader -> ... -> _mesa_set_clone`. Mesa does not check its `ralloc` returns; under
heap pressure (the ~2.5 GB of already-compiled shaders plus a shader's transient compile peak) a `ralloc` returned
NULL and the next deref faulted at a near-null offset. So it is **CPU-heap OOM**, not GPU memory and not a
watchdog -- fragmentation and Mesa's transient compile allocations put the real ceiling below 3.2 GB. Lazy compile
is the fix: keeping the resident compiled set to the drawn working set leaves Mesa headroom on every compile.
Interim fix is lazy compile (Phase 5 "shader cache"); the definitive answer is Phase 7 deko3d with offline DKSH.

**Lazy compile implemented (2026-10-26, build 00:07):** `CreateShader` (`gb_gl.cpp`) now stores the GLSL in the
`Shader` instead of queueing an eager compile; the backend compiles a shader on demand inside `Link` (`gb_gl_device.cpp`),
the first time a drawn program needs it, and frees the source afterwards. `Build` carries only a program now. So
only the on-screen working set compiles, not all ~13k. Trade-offs as noted: ~126 MB of un-drawn shader sources sit
resident until `DestroyShader`, and the first gameplay frame serialises a burst of compile+link on the render
thread (one-time multi-second stall instead of an OOM). No change to the win32/D3D9 reference (GL backend is
posix/switch only). Awaiting a hardware run to confirm the map loads and the `gl build:` count stays in the hundreds.


## deko3d port started (2026-10-26)
Pre-scaffold gate (advisor): does `uam` (deko3d's offline compiler, `/opt/devkitpro/tools/bin/uam`) accept the exact
GLSL ffconv emits? **Yes.** Extracted shaders straight from `shaders.pack` (`#version 430 core`, std140 UBO at
binding 0, explicit `layout(location/binding)`): a 202-shader even sample across the pack compiled **202/202** to
valid `DKSH` (vert 1024 B, a pixel 3584 B). So the GLSL-with-explicit-bindings groundwork pays off: the deko3d
shader path needs no translator rework, only an `ffconv --dksh` step that runs this same `uam` offline.

Plan (vertical slice first, "never debug two unknowns at once"): 1) `src/gfx_backend/deko3d/` skeleton filling the
`Backend` table (gb_backend.h), wired into the dispatcher (`BO1_GFX_BACKEND=deko3d`) and the switch CMake only;
2) device -> queue -> memblock pools -> swapchain -> BeginPass/Clear -> Present with a hand-compiled test shader,
mirroring `/opt/devkitpro/examples/switch/graphics/deko3d`; 3) only then the uam/ffconv DKSH *pack* pipeline (the
big piece). deko3d shrinks the shader memory footprint (no runtime Mesa compile, lean DKSH) but does not zero it:
13704 DKSH still have a code-size cost, so lazy *loading* of DKSH will still matter. The null backend (Phase 4
harness) is the correctness oracle. Still want the lazy-compile hardware boot.log: its drawn working-set count
scopes whether deko3d is the Phase-7 perf project or more urgent.

**Scaffold compiles and links (2026-10-26):** `src/gfx_backend/deko3d/{gb_deko3d.h,gb_deko3d.cpp}` fills the `Backend` table, wired into the
dispatcher (`BO1_GFX_BACKEND=deko3d`, guarded by `BO1_GB_DEKO3D`) and the switch CMake (links `libdeko3d`). `Init`
brings up the real `DkDevice` + graphics `DkQueue` and fills `GetCaps`; every rendering/resource call is a no-op /
returns null for now (device-init milestone -- draws nothing yet). Verified: the switch build compiles `gb_deko3d.o`
and links the ELF with `dkDeviceCreate`/`dkQueueCreate` resolved, NRO packaged; default backend stays GL, deko3d is
opt-in. Next: the device->swapchain(nwindowGetDefault)->BeginPass/Clear->Present slice with a hand-compiled DKSH,
then the uam/ffconv DKSH pack pipeline and lazy DKSH loading.

**Lazy compile confirmed on hardware (2026-10-26):** `+devmap zombie_theater` now **loads and reaches gameplay** -- no crash, no OOM.
The lazy compile collapsed the storm to its real drawn working set: **413 shaders compiled / 263 programs linked**
(not 13704), compile 5.3 s, heap ~1.7 GB stable (well under the ceiling). `CL_InitCGame: 1.76 s` and
`playsound mus_zombie_round_start` prove it is in-map at round start. This also rescopes deko3d: the GL path is
playable, so deko3d is the Phase-7 perf project (the remaining ~16.8 s of link time at load + GL driver CPU cost),
not an emergency. Open: ~1.7 GB heap for menu+413 shaders is high (links hold compiled programs); the asset-miss
flood (menu_map_zombie_theater, hud_zombie_theater.txt, ...) persists but does not stop the load.

**deko3d render slice #1 (2026-10-26):** `gb_deko3d.cpp` now implements memory (per-resource DkMemBlock), real `CreateBuffer`/
`CreateTexture` (so the frontend's device init succeeds and reaches Present), and a swapchain on
`nwindowGetDefault()` whose `Present` clears the acquired framebuffer to teal and shows it (pre-recorded per-slot
command lists, deko_basic pattern -- no per-frame command memory rewrite). Draws stay skipped (`GetPipeline` null,
guarded by the frontend), so with `BO1_GFX_BACKEND=deko3d` the engine runs and the screen is a flipping solid teal:
proof the device->swapchain->cmdbuf->queue->present chain works on hardware, independent of the engine's rendering.
Window ownership: `switch_main.cpp` sets `SDL_VIDEODRIVER=dummy` when deko3d is selected so SDL does not grab
`nwindowGetDefault()` (GL path unaffected). Compiles and links. Untested on hardware yet -- the risk is exactly
that SDL/nwindow coexistence; the log (`device up` / `swapchain WxH` / `dkSwapchainCreate failed`) will say. Next
after it presents: the pass/pipeline path (DKSH from an `ffconv --dksh` pack, lazy-loaded) and Present copying the
engine's real image onto the framebuffer.

**deko3d slice test #1 result (2026-10-26):** On hardware the deko3d backend **selected and its device came up** (`d3d9emu: Direct3D 9 on
the deko3d backend (deko3d (Tegra X1, Maxwell))`), proving the backend plugs in. But R_Init aborted: `No valid
resolutions of 800 x 600 or above found`. Cause: `SDL_VIDEODRIVER=dummy` (set so deko3d can own nwindow) publishes
no display modes, and `host::DisplayModes`/`DesktopMode` (posix_d3d9host.cpp) read them from SDL. Fix: when SDL has
no modes, report the console's fixed modes (1280x720, 1920x1080) -- both >= 800x600, so R_Init proceeds. GL path
unaffected (SDL gives it real modes). Re-test pending to reach the teal present.

**deko3d slice test #2 (2026-10-26):** Resolution fix worked -- R_Init advanced to `Pixel shader version is 3.0`, then
`Com_ERROR: r_rendertarget.cpp(1821) CreateDepthStencilSurface failed HRESULT 0x8876086c` (D3DERR_INVALIDCALL). No
new Atmosphere creport, so it is the engine's own error, not a fault. INVALIDCALL comes from CreateStorage's
validation (a null CreateTexture would be OUTOFVIDEOMEMORY), and that validation's only backend-specific input is
GetCaps -- GL passes the same engine code. My deko3d caps were hand-guessed: depth formats lacked FORMAT_SAMPLED
(GL sets it) and maxSamples was 8 though the slice has no MSAA. Fix: deko3d GetCaps now mirrors GL's formatFlags and
reports maxSamples=1. Also instrumented CreateStorage to log any RT/DS rejection with its inputs + caps
(d3d9emu_resources.cpp), so if caps was not the cause the next log names the exact failing check. Re-test pending.

**deko3d present slice WORKS (2026-10-26):** `BO1_GFX_BACKEND=deko3d` now boots to a flipping teal screen on both Ryujinx and the
user's console. The whole chain is validated: device, memory, buffers/textures, swapchain on nwindowGetDefault,
command buffer, queue, present -- and SDL/nwindow coexistence (SDL on its dummy video driver, deko3d owns nwindow).
The 0x0 depth-stencil crash was a cascade from that dummy driver: SDL_INIT_VIDEO has no usable driver, so
Bo1_VideoReady() is false and every SDL-backed display query returned nothing, leaving the engine's render
resolution 0 (r_init.cpp derives it from the monitor rect, clamped to the adapter-native size). Fix: on __SWITCH__
the display shims report the console's fixed 1280x720 regardless of SDL -- DisplayCount() returns 1, DisplayBounds()
and GetSystemMetrics() give 1280x720, plus the earlier DisplayModes/DesktopMode fallbacks (posix_d3d9host.cpp). The
GL path has a real SDL display and is untouched. Iterated entirely on Ryujinx (~20-60s/run, no Mesa shader compile
on deko3d), confirmed on hardware by the user. Next: the real render path -- pass/pipeline state from DKSH (an
`ffconv --dksh` pack run through uam), then Present copying the engine's frame onto the framebuffer, so the game
draws instead of a clear colour.

**deko3d: DKSH pack + present-copy (2026-10-26):** `ffconv --dksh` = `tools/ffconv/gen/make_dksh_pack.py`: reads the BO1GLSL1 pack, runs uam
on each shader, writes a `BO1DKSH1` pack (same entry layout, DKSH payload + a NUL byte so the existing reader's
check passes) -- **13692/13704 compiled**, 70.6 MB, 12 vertex shaders rejected by uam (their draws skip). d3d9emu
loads `shaders.dksh` instead of `shaders.pack` on deko3d (ShaderPack accepts the second magic; host::ShaderPackPath
picks by backend). The deko3d backend now: loads DKSH into a GPU code pool (CreateShader), records passes with their
load-clears into a per-frame command buffer (BeginPass/Clear/EndPass, fence-recycled), and Present blits the
engine's finished image onto the swapchain framebuffer (no more hardcoded teal). Verified on Ryujinx: the DKSH pack
loads (`shaders.pack holds 13692 shaders`), CL_InitCGame reached, no GPU fault from the frame/present path. Draws
are still skipped (GetPipeline null), so the screen shows the passes' clears, not geometry. Next: the draw path --
pipelines bound from the DKSH shaders (vertex attrib state, UBOs at bindings 0/1, texture+sampler descriptor sets),
so geometry renders. The `missing TECHNIQUE_DEPTH_PREPASS` spam is that unbuilt draw path, not a missing asset.

**Damier (missing materials) verdict (2026-10-26):** The map-load "Could not load" flood is 3537 misses: 3233 xanim (MP animations, no
checkerboard), 116 material, 91 image, the rest fx/rawfile/menu. Of the 116 missing materials, ~114 are MP / HUD /
customization / code assets solo zombies references but never draws (`$..._donotremove`, `$ps3_*`, `ammo_counter_*`,
`compass*`/`compassping_*`, `killicon*`/`headicon*`, `mtl_c_gen_facepaint_0..24`, `demo_*`, `lagometer`, ...): the
same flood retail produces without the MP fastfiles, not a ffconv gap, and not rendered so no visible damier. The
user confirmed the checkerboard is **only on the loading screen**, not the world or HUD -- so the real culprits are
the loading-screen cosmetics (`loadscreen_frontend`, `menu_map_zombie_theater`), plus two genuinely-zombies
full-screen overlays that would show during events if hit (`zombie_electric_shock_overlay`,
`zombie_transporter_overlay`). World surfaces load fine. Fix is scoped to converting/loading those loadscreen (and
overlay) assets, or a plain-background mitigation for the load screen -- cosmetic, lower priority than the deko3d
draw path.

**deko3d draw pipeline (2026-10-26, in progress):** Implemented the full draw path in `gb_deko3d.cpp`: `CreateShader` loads DKSH into a GPU
code pool; `GetPipeline` builds and caches a pipeline from a `PipelineDesc` (vertex attrib state, blend/color/
depth-stencil/rasterizer state, all translated from the contract enums); `AllocUniforms` is a per-frame bump ring
wrapped in one Buffer; `Draw`/`DrawIndexed` bind shaders + states + vertex/index buffers + the two UBOs (vertex at
binding 0, pixel at binding 1) + textures and issue the draw; passes bind the engine's real targets and `Present`
blits the finished image. Textures are bound to a single placeholder image+sampler for now (geometry/lighting render;
per-material texture descriptor sets are next). Compiles and links.

Debugged on Ryujinx with the **debug deko3d lib (libdeko3dd)** -- it validates state and names problems via
`svcOutputDebugString` (which survives an abort that loses buffered virtual-SD writes; the guest's stdout is not
captured). Two bugs fixed: (1) sparse vertex-attribute locations left gaps at `DkVtxAttribSize` 0 (invalid) -- fill
gaps with a valid fixed attribute; (2) **Maxwell/deko3d cannot render to the packed 16-bit colour formats**
(`DkImageFormat_RGB565_Unorm`, a B5G6R5 1024x2048 render target here) -- `dk_image.cpp:353 "specified format does not
support DkImageFlags_UsageRender"`; `CreateTexture` now substitutes `RGBA8_Unorm` for a render target in those
formats. After both fixes all R_Init render targets create cleanly (BGRA8, RGBA16, Z24S8, downsampled bloom RTs).

Still open: the draw path reaches R_Init material registration but does not reach `CL_InitCGame` (no assert, no
Atmosphere fatal) -- likely a draw in an R_Init loading frame faults the GPU, so the next frame's `dkFenceWait(-1)`
hangs. Next: give `dkFenceWait` a timeout (so a GPU hang does not freeze the app) and/or log around the first Draw/
Present to find the faulting draw; then per-material texture descriptor sets for correct colours. Default backend
stays GL (playable); deko3d is opt-in and in progress.

**deko3d draw-fault cracked + pipeline proven end-to-end (2026-10-26):** The "hang after the first draw" was never a GPU
fault. The engine busy-waits on `IDirect3DQuery9::GetData` for occlusion and timestamp queries (`rb_sky.cpp:128`,
`rb_corona.cpp:552`, superflare, pixelcost: `while (hr == S_FALSE) Sleep(0)`), and the deko3d backend returned
`CreateQuery = nullptr` / `QueryResult = false`, so `GetData` stayed `S_FALSE` forever -- the render thread spun in
engine code the moment the world's sky/coronas drew (no backend log, no fence wait, which is how it was diagnosed:
zero "fence wait" lines in an 18 s freeze = stuck mid-frame in the frontend, not on a GPU fence). Fix: queries now
report *ready* (a real `Query` handle storing its type; `QueryResult` returns a nonzero occlusion count so flares read
as visible, or `armTicksToNs` for timestamps). Not real `DkCounter`s yet -- TODO. After the fix the full game runs on
Ryujinx: menu presents at ~23 fps (1 draw/frame), the world loads and presents continuously (~860 draws/frame,
present index climbs with the cumulative draw counter -- confirmed candidate "many frames present", not "one frame
stuck"); one first-world-frame fence timeout was a one-off GPU warmup on the emulator (none on reruns).

The present/blit path is correct (presented image 1280x720 BGRA8, swapchain 1280x720, 1:1 blit). Verified by a
backend framebuffer dump (`Present` copies the presented image to a buffer and writes `sdmc:/switch/bo1zm/frame.ppm`,
since Ryujinx's GL/Vulkan surface is not host-screenshottable -- CopyFromScreen grabbed the wrong window, PrintWindow
gave black on the GPU surface). The dump showed **pure black**. Root cause of the black screen: **`UploadTexture` is a
no-op and every texture binding points at a single uninitialised (black) 1x1 placeholder**, so every textured sample
is black -- including the world's final composite pass, which samples a scene RT *through* a texture binding. Proof
the rest of the pipeline is sound: filling the placeholder **white** turned the dump **red** with a white UI bar, and
the user watching live saw the **loading screen render correctly (white loading bar on a blue background)**. So
depth/blend/raster/viewport/present all work; simple screens (solid-colour quads) render right, the world is a
full-screen quad sampling the uniform-white placeholder (-> red) hiding the real scene underneath. Next (the real
remaining renderer work): per-draw texture binding (a descriptor pool -- each `Texture`/`Sampler` gets a persistent
descriptor id, handles via `dkMakeTextureHandle`, bound per draw with `dkCmdBufBindTextures`, un-uploaded textures
falling back to the white placeholder) and a working `UploadTexture` (staging buffer + `dkCmdBufCopyBufferToImage`,
block-aligned for BCn -- `MapFormat` already maps BC1/2/3). Debug scaffolding still in `gb_deko3d.cpp` (PPM dump,
white placeholder, per-draw/present logs, 2 s fence timeout) -- remove once geometry is correct. Default backend
stays GL (playable); deko3d is opt-in and in progress.

**deko3d renders the main-menu 3D scene end to end on Ryujinx (2026-10-26).** With `launch.cfg` = `+set bo1_zombies 1`
the game sits at the main menu, whose background is the 3D interrogation room of the `frontend` zone (no
`zombie_theater` is loaded -- an earlier note here called this Kino; it is not). That scene now reaches the screen
with real textures, lighting, sun/spot shadows, the depth prepass, transparent effects and the bloom/composite chain
(captured with the backend frame dump, `frame_*.ppm`). What it took, each found by a per-pass trace of one frame
(target, draws, first draw's state, bound textures, clears, blits -- `s_frameTrace`) and per-pass PPM captures:

- **`UploadTexture`** is real: the pixels are repacked tight into a 32 MB staging block (as the GL backend does) and
  copied with `dkCmdBufCopyBufferToImage` on a dedicated command buffer, submit + wait per upload (load is slower;
  batch later). Its memory is added **once**: `dkCmdBufClear` keeps it (deko3d's console example adds memory once).
- **Ryujinx DMA overrun**: an upload crashed Ryujinx with `InvalidMemoryRegionException: Not mapped:
  va=0xFFFFFFFFFFFFFFFF` (the PTE-unmapped sentinel) in `DmaClass.DmaCopy`, a few hundred uploads in, at a point that
  moved with timing. Its DMA copy into a block-linear image read-modify-writes a GOB-rounded destination range, which
  for a small mip in the mip tail runs past a tightly sized memory block into an unmapped page. Every texture block now
  has 64 KiB of slack (`kTexSlack`; ~2600 textures here, so ~160 MB -- trim once measured, it is for Ryujinx).
- **Descriptor pools**: each texture and sampler gets a persistent descriptor id at creation; a draw binds handles from
  them (`dkMakeTextureHandle`). A render target's descriptor points at its own image (post passes sample it); a sampled
  texture points at a white 1x1 placeholder until its first upload, then at its image.
- **Formats**: sampled BGRA8/BGRX8 textures are stored as `DkImageFormat_BGRA8/BGRX8_Unorm` (D3D's byte order; render
  targets stay RGBA8). `TextureDesc::swizzle` is applied in the image view (L8 -> RRR1, A8 -> 000R, A8L8 -> RRRG, depth
  -> RRRR, as GL's `GL_TEXTURE_SWIZZLE_RGBA`); without it luminance textures read (L,0,0) and the scene had a red cast.
- **Caches**: each frame starts with an Image + Descriptors + Shader + L2 invalidate (descriptors are rewritten by the
  CPU, the uniform ring and dynamic buffers are rewritten), uploads invalidate L2 before copying from the reused
  staging block, and `EndPass` invalidates the image cache (a later pass samples what this one rendered), as deko3d's
  deferred-shading example does before reading its G-buffer.
- **`Clear`** honours its rects (each becomes the scissor) and every colour target of the pass; a pass's own size, not
  the swapchain's, sets its load-clear viewport. **Scissor**: with the pipeline's scissor test off (`RasterState::
  scissor`), the draw scissor covers the pass -- deko3d always scissors, and the frontend fills the rect from D3D9's
  scissor rect regardless, as GL ignores it then.
- **`Blit` (D3D9 StretchRect)** was a no-op: the engine resolves the scene (colour and depth) into the textures its
  post passes sample, 8 blits a frame, so every post pass read an empty texture and the frame came out black although
  the lit pass held the full scene. Now `dkCmdBufCopyImage` (depth, or equal size and format) or `dkCmdBufBlitImage`
  (scaling / conversion), recorded in order with full barriers around it.
- **Buffer renaming**: a frame is recorded whole and submitted at `Present`, so `UploadBuffer`'s in-place `memcpy`
  let later uploads overwrite data earlier draws of the same frame still needed. The engine's dynamic tess ring (1 MB,
  NOOVERWRITE appends) wraps ~3 times a frame with DISCARD, and everything drawn before the last wrap read the wrong
  vertices -- large stretched orange triangles across the screen. Each buffer now has a small pool of stores: a draw
  marks the current store read by this frame (frame serials, `s_frameSerial` / `s_completedSerial`), and Discard or
  Ordered on a store recorded or running work reads switches to a free store (Ordered copies the old contents first);
  NoOverwrite writes in place. ~2-4 renames a frame here. Destroyed buffers and textures free their memory once the
  GPU is past the frame (`s_pendingFree`, drained after BeginFrame's fence wait or by `WaitIdle`), as the contract asks.

Still open then: the menu's 2D UI (6 draws in the last pass: text and images over the scene) did not show although
its draws ran with their data in the right store and a full scissor (the loading screen's 2D did show) -- solved
below: an engine bug, not the backend. Not done yet: depth bias (`RasterState::depthBias`), the packed 16-bit formats' channel order, `ReadTexture`,
real occlusion/timestamp queries, batched uploads. Debug scaffolding to remove before shipping: frame dumps, the
one-frame trace, per-upload logs, the 2 s fence timeout. Default backend stays GL.

**deko3d runs Kino der Toten in game, HUD included, on Ryujinx (2026-10-26).** With `launch.cfg` = `+set bo1_zombies 1
+devmap zombie_theater` the theater lobby, its staircase and banners, the lighting, the post-processing chain, the
zombies (glowing eyes) and the M1911 viewmodel reach the screen at ~10.8 fps on Ryujinx (~3300 draws a frame), with no
crash in 5 minutes. Frames are checked against the win64 D3D9 reference (`x64_kino_20s.jpg`: crosshair, score brush at
the bottom right, round tally at the bottom left). Found on the way:

- **The invisible UI was an engine bug** (affecting GL too): `Byte4PackVertexColor` packed every UI/HUD vertex colour
  with alpha 0, because devkitA64 GCC exploited an out-of-bounds byte read of the decompiled shuffle. Fixed with
  `Float4Byte` (decision row of 2026-10-26). The trace of the last pass's raw vertices (`ff790100` for the score
  brush) is how it was read.
- **Text 256 scaled pixels too low**: `char` is unsigned on AArch64 and `Glyph::y0` is a negative `char`; the build now
  passes `-fsigned-char` (decision row). After it, the score `500` sits on its brush and the round tally shows.
- **Ryujinx swaps red and blue in D3DCOLOR vertex colours**: `UByte4NBGRA` maps to `DkVtxAttribState::isBgra`, which
  libdeko3d copies unchanged into the Maxwell vertex-attribute register (bit 31, the bit nouveau sets for B8G8R8A8
  vertex formats). Ryujinx 1.1.1375 ignores that bit: its `VertexAttribState` only unpacks the buffer index, constant
  flag, offset and format (`UnpackBufferIndex`/`UnpackIsConstant`/`UnpackOffset`/`UnpackFormat`, no BGRA accessor in
  `Ryujinx.exe`). So on the emulator the red score brush, the round tally and the zombies' eye glow come out blue. The
  backend is kept as the hardware expects it; **to confirm on the console**.
- **Depth bias** is implemented: a biased pipeline sets `depthBiasEnableMask` and each of its draws records
  `dkCmdBufSetDepthBias(depthBias × 2^24 (2^16 for D16), 0, slopeScaledDepthBias)`, the units of GL's
  `glPolygonOffset` (libdeko3d doubles the constant factor, as nouveau does for GL's units: `fadd s8, s8, s8` in
  `dkCmdBufSetDepthBias`).
- **SD writes fail on Ryujinx once a map is loaded**: every write returns EIO (errno 5), the engine's own `boot.log`
  stops there too. The backend's frame dumps therefore go out through `svcOutputDebugString` as hex lines
  (`IMG WxH label` / `IMGD` / `IMGEND`, the frame 4× downscaled plus the two bottom corners at full resolution) and are
  rebuilt into PPMs on the PC. The GL runs on the console had in-map `boot.log` lines, so this looks emulator-only.

- **Loading screens and the main menu**: with the `$levelbriefing` fix (decision row of 2026-10-26) the Kino load
  shows its artwork and the boot load a black background under the tips box; the main menu ("Press Any Key", the
  title art, the copyright line) renders whole on deko3d.

Open: the ammo counter and the weapon d-pad icon at the bottom right, which the win64 reference draws (`8 / 24` at
55 s), do not show on deko3d; the materials they need are missing on win64 too, so that is not the cause (not
investigated further yet). Not done yet: the packed 16-bit formats' channel order (B5G6R5 is used, as shadow-pass
colour targets and one sampled 512x2048 texture), `ReadTexture`, real occlusion/timestamp queries, batched uploads,
descriptor-id reuse (the pools leak ids of destroyed textures), trimming `kTexSlack` (a Ryujinx need; done
below: one tail per image block). Debug
scaffolding to remove before shipping: the timed frame dumps and the one-frame trace (they only run when
`sdmc:/switch/bo1zm/deko3d_debug` exists, so the console build skips them), per-upload logs, the 2 s fence timeout.
Default backend stays GL; `gfx.cfg` = `deko3d` selects deko3d.

**First console run of deko3d: the process dies right after init (2026-10-26, NRO `e402e66b`).** The user's
`boot.log` (menu launch, `gfx.cfg` = `deko3d`) matches the Ryujinx one of the same NRO line for line, numbers masked,
up to `--- Common Initialization Complete ---`, and stops there. On Ryujinx the next line is `Loaded fastfile
'patch_mp'`, about 5 s later, while the render thread draws the first menu frames: the console dies within those.
No Atmosphère crash report yet. What the log could not show, and what changed to show it:

- **None of the backend's lines reached the SD.** The engine opens `boot.log` once and keeps the handle; the console's
  FS gives a file one write handle at a time, so the backend's per-line `fopen("ab")` failed from the engine's first
  print on. Ryujinx behaves the same: 1 `[gb-deko3d]` line in its SD log, 13 663 on `svcOutputDebugString`. Every
  writer now goes through one process-wide `Switch_BootLogWrite` (`switch_main.cpp`: one `FILE *`, a mutex, `fflush`
  per write), used by `BootLog`, the engine's `Switch_DebugLog` (`win_main.cpp`, `#if __SWITCH__`) and the backend.
  Checked on Ryujinx: 270 backend lines in the SD log of a menu run.
- **The build stamp lied**: `built Oct 26 2026 06:57:49` was `switch_main.cpp`'s `__DATE__`, for an NRO linked at
  08:05:48. The packaging step now writes the link time to `romfs:/build.txt` (`CMakeLists.txt`) and the first lines
  log it (`build 2026-10-26 12:38:04 +0100`).
- **Memory figures**: `device up`, `swapchain` and the `Present` lines log the heap in use (`mallinfo`; deko3d allocates
  its blocks there) and the live `DkMemBlock`s. On Ryujinx: heap 401 MB at device creation, 990 MB at the first
  Present, 1193 MB on the menu after 82 s; **1384 live blocks at the first Present, 2860 (437 MB) on the menu, 11 210
  (636 MB) in Kino**. Each block is an nvmap handle and a GPU mapping that nvservices books in the 8 MiB of transfer
  memory libnx gives it (`__nx_nv_transfermem_size` = `0x00800000` in the ELF), a limit Ryujinx does not emulate.
  A failed `dkMemBlockCreate` never returns null in the release library: it aborts (below).
- **A GPU fault would hang silently**: each texture upload ended in `dkQueueWaitIdle`, which never returns once the
  queue faults, and the loader thread holds the frontend lock meanwhile. Uploads and `WaitIdle` now wait on a fence in
  2 s steps (`WaitQueue`), log each step with `dkQueueIsInErrorState`, and give up only on a faulted queue. Gotcha met
  on the way: `dkQueueSignalFence`'s `flush` flushes the GPU caches and does not submit; without a `dkQueueFlush` the
  first version timed out on every upload with an idle GPU. With the flush, Kino loads and plays on Ryujinx (2544
  frames in 184 s, no stall logged).

Also seen, not known to be linked to the crash: with `SDL_VIDEODRIVER=dummy` nothing calls `appletMainLoop()` (only
SDL's Switch video driver does, `SDL_switchvideo.o`), so on the deko3d path the applet messages (HOME, exit requests)
go unserviced; and the posix window layer ignores `SDL_QUIT`, so a console exit request reaches the engine on neither
backend. The console run of the diagnostic NRO came next.

**Second console run (NRO `ff02e8a5`, build 12:38:04): the first frame is presented, then the process dies.** The
backend's lines now reach the SD, and they match the Ryujinx run of the same NRO line for line: the same 2000+
uploads, the same render targets, `GetPipeline #0`, the loading screen's pass and its one draw, then `Present #0` (1332
live blocks, 296 MB; heap 970.5 MB). On Ryujinx `BeginPass #2` and `Present #1` follow; on the console the log ends.
No 2 s GPU-wait line, no failed block: a fast death, not a hang.

Why nothing is logged: **the release `libdeko3d.a` never calls `cbDebug`** (only `libdeko3dd` does). Every error goes
to `dk::detail::RaiseError(DkResult)`, which is `diagAbortWithResult(MAKERESULT(359, result))`: an immediate
`svcBreak`, error `2359-<DkResult>` on screen. Its callers (relocations in the archive): `dkMemBlockCreate` and
`Device::allocMem`, `CmdBuf::requestCmdMem`, `dkQueueSubmitCommands`, `Queue::flush` (which also aborts once
`Queue::checkError` has seen a GPU channel error notification, the queue's state 2), `Queue::waitFence`,
`dkQueueAcquireImage`/`dkQueuePresentImage`/`Swapchain::acquireImage`/`presentImage`, `dkSwapchainCreate`. Right after
the `Present #0` line the frame calls `dkQueueAcquireImage`, the blit, `dkQueueSubmitCommands` and
`dkQueuePresentImage`; the next frame flushes the queue again. So the console run died in one of those or on the
first flush after a GPU fault of frame 0.

`diagAbortWithResult` is weak in libnx, so `switch_main.cpp` now defines it: it writes `*** abort: result 0x..
(module 359, description N), frames: ...` (the frame-record chain as ELF offsets) to `boot.log` and to
`svcOutputDebugString`, then breaks as libnx does. Checked on Ryujinx with a temporary 4 GB `MakeBlock` at the 100th
Present: `result 0x767 (module 359, description 3)` (`DkResult_OutOfMemory`) and 10 frames that `addr2line` turns into
`RaiseError` < `Device::allocMem` < `MemBlock::initialize` < `dkMemBlockCreate` < `MakeBlock` < `Present` <
`d3d9emu::Device::PresentFrame` < `RB_SwapBuffers` < `RB_EndFrame` < `R_IssueRenderCommands`, the same as Ryujinx's own
guest stack trace. (Ryujinx itself dies on the break and drops its unflushed SD writes, hence the debug-string copy; the
console kept every line written before its abort.) Next: the console run of that NRO (`c2e15a43`, build 12:55:23); its
abort line names the failing call. Whatever it is, textures and buffer stores should come from large suballocated
blocks instead of one block each.

**Third console run (NRO `c2e15a43`, build 12:55:23): `dkMemBlockCreate` fails at ~1332 live blocks.** The log ends
with `*** abort: result 0x367 (module 359, description 1)`, error `2359-0001` (`DkResult_Fail`); its frames, and the
Atmosphère report's stack (`crash/02582020888_...log`, the same return addresses), resolve to `RaiseError` <
`dkMemBlockCreate` (`dk_memblock.cpp:167`) < `MakeBlock` < `gb::deko3d::CreateTexture` < `d3d9emu::CreateStorage` <
`Device::CreateTexture` < `Image_Create2DTexture_PC` < `Image_Setup` < `Image_SetupFromFile` < `Image_LoadDxtc`: the
loader thread creating a DXT texture while the first menu frame is presented, with 1331 blocks (296 MB) live. Both
console runs died at that count (1332 and 1331) with only 296 MB in blocks, so the console limits the *number* of
blocks (each `MemBlock::initialize` is an `nvMapCreate` and an `nvAddressSpaceMap` in nvservices), not their size.

**Fix: textures and buffer stores are suballocated** (`gb_deko3d.cpp`, `Allocate`/`Release`), as deko3d's own sample
framework (`CMemPool`: images `GpuCached | Image` in 16 MiB blocks, data in 8 MiB) does. An image pool of 64 MiB
chunks and a data pool (`CpuUncached | GpuCached`) of 16 MiB chunks, first fit at the layout's alignment
(`dkImageLayoutGetAlignment`, `DK_UNIFORM_BUF_ALIGNMENT` for buffers) over a coalesced free list per chunk; a range
larger than half a chunk gets its own block; an emptied chunk goes back unless it is the pool's last; ranges still go
through `s_pendingFree`, so memory is reused only after the GPU finished the frame that read it. Ryujinx's 64 KiB
image slack (its GOB-rounded DMA read-modify-write) moves from every texture to the end of each image block. On
Ryujinx: **14 live blocks at the first Present instead of 1384, 17 on the menu after 57 s instead of 2860, 30 in Kino
after 207 s instead of 11 210**; block memory in Kino 488 MB instead of 636 MB, heap 1343 MB instead of 1462 MB; the
loading artwork, the in-game frame and the HUD captured at 30 s and 150 s render as before. Next: the console run of
this NRO (`9e340f07`, build 13:07:47).

**Fourth console run (NRO `9e340f07`, build 13:07:47, `+devmap zombie_theater`): Kino plays on the console.** No
abort in 120 s: 29 live blocks (484 MB), heap 1339 MB, about 40 fps on the loading screen (8 draws) and **about 20
fps in game** (frames 1728 to 2896 between 62 s and 119 s, ~5700 draws a frame); the cap is 60 (the swapchain's
default swap interval 1 on the 60 Hz screen; `com_maxfps` is 85). The user's photo shows the score `500` in **red**,
so the vertex-attribute BGRA bit works on the hardware (Ryujinx alone shows it blue), and the ammo counter `8 / 24`
with the d-pad weapon icon, which Ryujinx did not draw. Two problems:

- **No controller input, only HOME and capture** (which the system handles). SDL2-switch has no dummy video driver:
  `libSDL2.a` holds only `SWITCH_bootstrap`, so with `SDL_VIDEODRIVER=dummy` (`switch_main.cpp`, deko3d path)
  `SDL_InitSubSystem(SDL_INIT_VIDEO)` fails, the window layer has no video thread and its `Pump` returned at once:
  no SDL events, no `Bo1_GamepadPump`, and no `appletMainLoop` (the Switch video driver's pump called it). Fix
  (`win32_gui.cpp`, `#if __SWITCH__`): without video, the first thread that pumps calls `appletMainLoop` (never again
  once it returned false; the exit request becomes a `WM_CLOSE` to the active window, which the engine quits on),
  drains SDL's events and refreshes the controller. `win32_gamepad.cpp` now logs `gamepad: opened '...'`, a detach,
  or `no game controller (N joysticks)` once. On Ryujinx: `gamepad: opened 'Switch Controller'`.
- **Purple-blue specks over the 3D scene** (short vertical dashes, spread over walls, floor and ceiling; the HUD is
  clean). Ryujinx's captures show none. Not explained yet. The `deko3d_debug` captures were emulator-only (`LogImage`
  wrote its pixel lines to `svcOutputDebugString`, which reaches nothing on a console without a debugger): they now go
  to the SD `boot.log` as well, every frame dump adds a full-resolution centre crop, and the one-frame trace runs at
  100 s (in game on a `+devmap` console launch) and copies a full-resolution centre crop of every 16:9 pass (scene
  and post-processing chain, 24 passes on Kino) so the pass that adds the specks can be named. Checked on Ryujinx:
  the crops decode (`log_images.py`) and show the scene and the final frame. Next: the console `boot.log` of NRO
  `42628491` (build 13:25:31) with `deko3d_debug` on the SD.

**Fifth console run (NRO `42628491`, build 13:25:31, `deko3d_debug` on): the controller works, the specks are tiles.**
`gamepad: opened 'Switch Controller'`, and the pad drives the game, except inside the menu START opens. The user saw
no specks with the GL backend, and they flicker and move with the view. The trace's full-resolution crops place them
in **pass 04, the first pass that draws the lit scene**: solid blue rectangles of 4x8, 8x8 and 4x16 pixels
(`con1img/09_pass_04`), the same in every later pass; the 150 s frame also has dark olive ones. Whole small tiles of
the lit pass missing, showing what the target held before, only on the GPU (Ryujinx's crops are clean) and only with
deko3d: the signature of Zcull, Maxwell's coarse per-tile depth cull, culling tiles on a summary that does not match
the depth buffer being tested (BO1 renders to several depth buffers: scene, viewmodel, shadow maps). Ryujinx does not
emulate Zcull and nouveau's GL does not enable it; deko3d's queue defaults enable it. **Fix, to confirm on the
console: the queue is made with `DkQueueFlags_DisableZcull`** (`gb_deko3d.cpp`, `Init`); per-pixel early depth still
runs after the depth prepass. If the frame rate suffers, the alternative is Zcull on with
`DkInvalidateFlags_Zcull` whenever the depth target changes. (The trace frame's last passes, 21 to 23, came out white
on the console: a white flash in game at that moment, or another bug; the 150 s dump is a normal frame.)

**Menus with a pad.** The PC menus step between items on the arrow keys (`Menu_HandleKey`: 154/155 previous/next,
A/Start/Enter accept, B/Escape back) and have no case for the d-pad (`K_DPAD_*`, 20-23) or the left stick
(`K_APAD_*`, 28-31), which only the console menus handled; item focus follows the mouse. `CL_GamepadButtonEvent`
(`cl_gamepad.cpp`) now hands the UI the arrow key for each pad direction, `#if !defined(_WIN32)` only (an identity on
Windows: `codediff` vs mingw32-gcc identical). In game the pad keys keep their bindings; only `UI_KeyEvent` sees the
arrows. To confirm on the console with the NRO `16809ec3` (build 13:51:05).

**Sixth console run (NRO `16809ec3`, build 13:51:05, Zcull off): the specks remain, so not Zcull.** The trace's pass 04
crop still has them. Measured on the two console crops: in every speck the red and green channels hold the scene's
own value (~30-38) and **only blue is 255**, so these pixels are drawn, with one wrong channel (pass 02 cleared the
target to (128, 191, 255), a different colour); and of the 56 blobs of the second crop, **50 are solid 4x8
rectangles at x and y multiples of 4 on the screen**: one Maxwell warp's 32 pixels. A whole warp gets a wrong blue,
which changes from frame to frame. Ruled out on the way: temporaries read before they are written (a scan of the
13 704 shaders that merges if/else branches finds none, and every pixel shader writes all of `oC0`). Every draw of
the lit pass samples the shadow map (`1024x2048` Z24S8 through a compare sampler, `sampler2DShadow`), and in a dark
theatre lit by blue moonlight, a shadow test that says "lit" adds mostly blue: the next run turns the shadow maps off
(`+set sm_enable 0`, checked on Ryujinx: the shadow passes go and the lit pass becomes pass 02) to tell a shadow
fault from a general one. The deko3d queue keeps Zcull off for now.

**Controller face buttons by their labels.** SDL2-switch names the face buttons by position as on an Xbox pad (its
A is the console's bottom B, its X the console's left Y). The game binds `BUTTON_X` to `+usereload` (use, rebuild,
buy) and `BUTTON_Y` to `weapnext` (`buttons_default.cfg`), and its prompts name the letter: the player pressed the
Joy-Con's X to rebuild and got a weapon switch, while the left Y rebuilt. On the Switch, `Bo1_MapGamepadState`
(`win32_gamepad.h`) now maps by the printed letters (Joy-Con A, B, X, Y to the game's A, B, X, Y), which also puts
menu confirm on the right-hand A as on Nintendo's consoles. The desktop mapping is unchanged (`gamepad_test`
passes). NRO `a6a8da49`, build 14:29:35.

**Seventh console run (NRO `a6a8da49`, build 14:29:35, `+set sm_enable 0`).** The START menu works with the pad;
the specks remain **without shadow maps**, so the shadow test is ruled out too; the user asked for Y = reload and
L = switch weapon; and the game froze at the end of round 1.

- **The freeze is a 64-bit bug in the physics.** At `mus_zombie_round_over` the log has `contact manifold intersect
  poly poly failed`, then 25 403 `contact manifold failure` and no frame after: `intersect_poly_poly`
  (`phys_contact_manifold.cpp`) counted its bridges as `((char *)b - (char *)list_bridge) >> 4` and advanced its
  allocator by `16 * num_bridges`. A `bridge` holds a `phys_vec2` and two pointers: 16 bytes on 32 bits, **24** on 64
  (clang, x86-64 and AArch64 alike), so the count came out 1.5 times too large, odd counts took the "failed" branch,
  and the arc walk looped over garbage bridges for ever. Now `b - list_bridge` and `sizeof(bridge)`. Same build on
  win64 and Linux: the bug was there too, only never reached in their runs. `codediff` vs mingw32-gcc: same
  subtraction and `sar $4`, only the register and stack-slot choice and the jump offsets after it change.
- **The same family elsewhere** (a pointer difference divided by a 32-bit element size, which `tools/litsizes.py`
  cannot see because it is not a call argument; inventoried with a regex over `src/` and `tl/`, 22 hits, each element
  measured): `r_staticmodelcache.cpp` divided by 8 a difference of `static_model_leaf_t`, a union with two pointers
  (16 bytes on 64 bits), and `cm_trace.cpp` divided by 96 a difference of `cbrush_t` (104 bytes on 64 bits), the
  `index_hint` a trace then uses as `&brushes[*index_hint]`, i.e. another brush. Both now divide by
  `(ptrdiff_t)sizeof(...)`, which keeps the signed division: `codediff` identical. The other hits are elements without
  pointers (`FxTrail` 8, `FxTrailElem` 32, `FxElemContainer` 48, `FxUniqueHandleDb::Node` 16, `gitem_s` 4, `mnode_t`
  4) or byte offsets by design (`FX_EffectToHandle`'s `/ 4`).
- **Controls**: `Cmd_ExecFromFastFile` (`cmd.cpp`, `#if __SWITCH__`) lays the console's bindings over every
  `buttons_*.cfg` it runs (the profile and the menus run `buttons_default.cfg` again during play): `BUTTON_Y` =
  `+reload`, `BUTTON_LSHLDR` = `weapnext`, and the tactical grenade (`+smoke`, monkey bombs) that L threw goes to
  `DPAD_DOWN`; X keeps `+usereload`. `codediff` identical.
- **Specks, next experiment** (`deko3d_debug` only): the one-frame trace now runs four times (100, 112, 124, 136 s),
  the last three with a family of sampled textures (never a render target) replaced for that frame by a white
  placeholder of the same type (1x1 RGBA8 2D, cube or volume): cubes, volumes, then every sampled texture. The pass
  crops of each trace show whether the specks follow a texture family or none. Also checked and ruled out: deko3d
  forces 6 layers for a `DkImageType_Cubemap` whatever `dimensions[2]` says (`dkImageLayoutInitialize`), so the
  cube textures are whole; and the Maxwell code uam makes for a lit shader (`uam -r`) turns the D3D9 `rsq`/`rcp`
  ternaries into selects and waits on every MUFU result and on all barriers at `EXIT`.

**Eighth console run (NRO `5bafdabc`, build 14:57:27): the specks do not follow the cube or 3D textures.** Three of
the four traces ran (normal, no cube, no 3D); the lit pass (pass 02 here, no shadow pass in that frame) has its blue
4x8 specks in all three (3113, 3492 and 3227 blue pixels in the 320x180 crop) although the neutralisation worked (the
viewmodel goes white without its cube reflection). The user saw a freeze on a white screen with the HUD: the traces'
pixel lines, one SD flush per 480 characters for ~20 crops a trace, stalled the game, and the traced frame's
post-processing comes out white on the console (as in the earlier captures; not seen in normal frames). Next build:
the traces are cumulative (100, 106, 112, 118 s: normal; no sampled texture; and no sampled colour render target;
and the default sampler everywhere), capture only the screen-sized passes up to pass 6, write 7680 hex characters a
line to the SD (16 times fewer flushes) and 480 a line to the debug string, and use **dark grey** (0x40) placeholders:
white ones shade the world magenta, whose saturated blue would hide a blue speck. Checked on Ryujinx: four traces,
no saturated blue in the neutralised crops (means 31-62 per channel). NRO `51814624`, build 15:17:41.

**Ninth console run (NRO `51814624`, build 15:17:41): the specks do not follow the textures or the samplers.** The
four traces ran. Blue pixels in the 320x180 crop of the lit pass: 825 (normal), 785 (no sampled texture), 640 (and no
sampled colour target), 62 (and the default sampler everywhere). The last count misleads: that crop has **light grey
4x8 tiles** where the others have blue ones. Whole warps still compute a wrong colour; only the colour changes with
what they sample. The round-end freeze is gone, but a ~3.5 s pause remains at the end of a round (cause unknown, not
the streamed sounds, which do not play yet). The user also reports missing gunfire sounds and asked again for the pad
layout, which had not taken.

Ruled out since, offline on `shaders.dksh` (scripts decode the DKSH: header `<6I>` magic, header size, control size,
code size, programs offset, count; code after the control section at the program's entrypoint; Maxwell control words
of 21 bits per instruction: stall, yield, write barrier, read barrier, wait mask, reuse; operand fields from envytools'
`gm107.c`):
- **Scoreboard hazards in uam's code.** A first scan reported 11 280 pixel shaders reading a texture result before
  waiting on its barrier. That was the scan's own decoding: a `TEXS` writes 1 to 4 registers depending on its
  component selector (bits 50-52) and on whether its second destination (bits 28-35) is RZ, and a `TEX` writes one
  register per enabled component (mask at bits 31-34), not always 4. With exact destinations, exact `TEXS`/`TEX`
  source registers (yuzu's assignment by texture type, lod and depth-compare), conversions that read Rb only (`RRO`,
  `F2F`, `I2F`...) and `SHFL` writing one register, a linear scan of all 13 692 shaders finds **no** read-after-write,
  write-after-read or write-after-write hazard on any variable-latency result, and no barrier pending at `EXIT`.
  Every opcode family that takes a write barrier takes one every time.
- **Scratch memory.** Every DKSH program header has `per_warp_scratch_sz` 0: no shader spills to local memory, so the
  queue's per-warp scratch size (2 KiB by default, never checked by deko3d for graphics) cannot overflow.
- **Uniform branches.** No `BRA.U` in any shader; 1 889 pixel shaders branch on a per-thread predicate, which is
  correct per thread.
- **Stale caches.** The frame's first barrier drops the texture, descriptor, shader and constant caches and L2
  (`DkInvalidateFlags_Shader` includes the constant cache, deko3d `gpu_base.cpp`), and the CPU only writes uniforms,
  descriptors and dynamic buffers while the GPU is idle (the frame waits for the previous one's fence). Descriptor
  ids are never reused. Images are not hardware-compressed; the tiled cache is never enabled.
- **Interpolants the vertex shader does not write.** Stages are compiled apart, so a pixel shader reading an
  attribute its vertex shader leaves unwritten would read whatever the SM's attribute storage holds (GL's linker
  turns it into a constant). `GetPipeline` now compares the program headers' attribute masks (a vertex shader's
  output map, a pixel shader's input map) and logs `attribute gap: ...` once per pair. On Ryujinx, 200 s of Kino
  (3.39 M draws, the same pairs as on the console): masks read correctly (vertex `f` per attribute, pixel `3`/`f`),
  **no gap**.

**Controls, second try.** The first hook never ran: no start-up config binds the pad (`default_mp_controls.cfg` only
sets the `gpad_*` dvars). `buttons_default.cfg` runs when the profile is saved from the dvars (`Updating profile 0
from dvars`, e.g. after the in-game menu), and the game then writes the bindings into `players/config.cfg`, which
the next launches exec from disk. `cmd.cpp` (`#if __SWITCH__`) now lays the console's layout over any exec, from a
fastfile or from disk, whose text binds a pad key (`BUTTON_`), and logs `pad layout after <file>: ...`. Checked on
Ryujinx with pad bindings added to its `config.cfg`: `pad layout after config.cfg: Y +reload, L weapnext, X
+usereload, d-pad down +smoke`, and the game wrote these bindings back into `config.cfg`. `codediff` vs mingw32-gcc:
`cmd.cpp` identical.

**Specks, next experiment** (`deko3d_debug` only). The traces now test two families of causes instead of texture
families: 100 s normal; 106 s **serialised** (before each draw, a barrier that waits for the previous primitives and
drops the texture, descriptor, shader, constant and L2 caches); 112 s **low occupancy** (each pixel shader is bound
through a CPU copy of its DKSH control section whose program header declares 255 registers, which leaves room for ~8
warps per SM instead of ~60; same code, deko3d's separate `control` pointer); 118 s both. If the tiles vanish when
serialised, something crosses draws; if they vanish at low occupancy, warps of one draw interfere (a hardware
resource sized by state uam or deko3d programs, e.g. the fragment register `0x0d8` that uam itself does not fully
understand). The placeholder textures are gone. Checked on Ryujinx: the four traces run, no fence timeout, the crops
show the scene. NRO `b496c377`, build 16:17:20.

**Tenth console run (NRO `b496c377`, build 16:17:20): the tiles come from warps of one draw sharing an SM.** The
user confirms the pad layout (Y reload, L switch weapon, d-pad down monkey bombs, X use); the log has `pad layout
after config.cfg: Y +reload, L weapnext, X +usereload, d-pad down +smoke`. The player stood still through the four
traces, so the lit-pass crops (320x180) compare pixel for pixel:

| trace | blue pixels | pixels off the cleanest crop by more than 30 |
|---|---|---|
| normal | 168 | 207 |
| serialised | 182 | 221 |
| low occupancy (255 registers declared) | 0 | 28 |
| serialised and low occupancy | 0 | 0 (the reference) |

Serialising draws changes nothing, so no draw disturbs a later one; fewer warps per SM removes the tiles. Every
shader's code stays within its declared register count (a scan of the highest register each program touches: equal
to `num_gprs - 1` for 13 657 programs, `num_gprs - 2` for 35), so warps do not overwrite each other's registers. What is
left is per-SM state programmed for fragment programs. uam writes `0x087F6080` to 3D register `0x0360` (the DKSH
fragment header's `param_d8`, method `0x0d8` in deko3d's `cmd_bind_common.cpp`) for all 13 348 pixel shaders, a
value its source marks "TODO: figure out"; Mesa 20.1's `nvc0_fragprog_validate` writes `0x20164010` (and `0x20` to
`0x0364`, as deko3d does) for every fragment program, and the GL path never showed the tiles on this console. The
backend now binds every pixel shader through a CPU copy of its DKSH control section with `param_d8 = 0x20164010`
(`InitPatchedShader`, `gb_deko3d.cpp`) and logs `pixel shaders bind with 0x0360 = 0x20164010` once. The traces test
it: 100 s normal (nouveau's value), 106 s uam's value, 112 s low occupancy, 118 s normal again. The serialised
experiment is gone. Checked on Ryujinx (which never showed the tiles): the scene renders, the four traces run. NRO `c2a5a3ae`, build 16:42:06.

**Eleventh console run (NRO `c2a5a3ae`, build 16:42:06): register `0x0360` is not the cause.** Blue pixels in the
lit-pass crop: 541 with nouveau's `0x20164010`, 790 with uam's `0x087F6080`, **2** at low occupancy, 562 with
nouveau's value again (the view moved a little between the first trace and the others). The value is back to uam's
(no patch). Ruled out on the way, offline: every `IPA` of every pixel shader reads an attribute component its input
map declares, with the declared interpolation mode (531 144 `IPA`; the others read position, `0x70`-`0x7c`); every
program header's local-memory and call-return-stack sizes are 0, and branch nesting is at most 3.

**The schedules differ.** uam's Maxwell emitter is Mesa's with `fincs-edit`s in `SchedDataCalculatorGM107` and
`TargetGM107::canDualIssue`. Mesa 20.1 (the GL path) never dual-issues (`canDualIssue`: "TODO, return false"); uam
pairs an ALU instruction with the next TEX/IPA/MUFU/branch/memory instruction (stall 0 + yield): 522 536 pairs in the
pack, mostly `FFMA`/`FMUL` + `IPA`, at all three positions of a control group. uam also takes the next instruction in
layout order across basic blocks to decide whether a barrier-setting instruction needs 2 cycles; Mesa takes 2 at
every block end. `CreateShader` now rewrites each DKSH's control words before placing the code
(`UseMesaSchedule`, `gb_deko3d.cpp`): stall 0 becomes 1 without the yield (no dual issue), and any instruction that
sets a dependency barrier gets at least 2. This only delays issue. Offline twin over the pack: 784 970 control words
change, none keeps a stall of 0 or a barrier under 2 cycles. Traces: 100 s normal (rescheduled), 106 s uam's own
schedule (a second copy of the code, `deko3d_debug` only), 112 s low occupancy, 118 s normal again. Checked on
Ryujinx: the scene renders, the traces run. NRO `20bb6871`, build 16:56:49.

**Twelfth console run (NRO `20bb6871`, build 16:56:49): uam's dual issue was the cause; the specks are gone.** The
user saw no speck in play. Blue pixels in the lit-pass crop: **0** rescheduled (the 16 white ones are a zombie's eyes),
**1 109** with uam's own schedule, 0 at low occupancy, 0 rescheduled again. Whole warps computed a wrong colour when a
dual-issued pair (mostly `FFMA`/`FMUL` + `IPA`) ran on a busy SM; fewer warps per SM hid it, and neither Ryujinx
(no dual issue) nor GL (Mesa never dual-issues) could show it. The experiments are gone from the backend (no second
code copy, no 255-register header, one world trace at 100 s with `deko3d_debug`); `UseMesaSchedule` stays for every
DKSH, vertex shaders included, and the `attribute gap` check stays. Cost not measured yet: the log has no frame-rate
line. Next: an on-screen frame rate and battery level (asked by the user), measure the cost of the rescheduling, then
re-enable Zcull and measure (it is still off from the investigation).

**On-screen frame rate and battery (asked by the user).** On the Switch, the `bo1_fpscounter` mod counter
(`cl_scrn_mp.cpp`, averaged over 500 ms) is on by default and draws `<fps> FPS  <battery>%` at the top right in
fluorescent green (0.22, 1, 0.08), in the console font, over game and menus alike; PC keeps the mod's top-left yellow
counter, off by default (`codediff` identical). The text is drawn twice: the small font's strokes are mostly
antialiased edge texels, which left the green at half strength (measured (8, 128, 24) in a capture) and now reach 3/4
((8, 160, 32) to (16, 208, 40)). The battery comes from `Switch_BatteryPercent` (`switch_main.cpp`): `psmInitialize`
at the first call (logged), the charge re-read every 5 s (an IPC call), `psmExit` after `WinMain`. The `deko3d_debug`
frame dumps add a full-resolution top-right crop. Checked on Ryujinx: `51 FPS  100%` at the top right.

**Aim assist settings from `launch.cfg` (asked by the user).** `+set aim_slowdown_enabled 1 +set aim_lockon_enabled
1` had no effect. The three aim-assist switches are registered with flags `0x1080`:
- `0x80`, cheat-protected: refused without `sv_cheats`, and reset by `Dvar_SetCheatState`;
- `0x1000`: `G_InitGame` resets every such dvar at each game start (`Dvar_ResetDvars(0x1000u)`), after the
  command line ran.

Lock-on was therefore always back to 0 in game. Slowdown was already on by default (pitch and yaw scaled to 0.4 over
a target).

On the Switch, `aim_slowdown_enabled`, `aim_lockon_enabled` and `aim_autoaim_enabled` are now registered without these
flags (`kAimSwitchFlags`, `aim_assist.cpp`); PC keeps `0x1080` (`codediff` identical). Checked on Ryujinx with the
user's `launch.cfg`: after the map loads, `slowdown 1 lockon 1 autoaim 0`.

The rest of the chain is wired: zombies are targets (eType 17 with `EF_AIM_ASSIST`), the target lists are filled
each frame, and the pad path calls `AimAssist_UpdateGamePadInput`. That path does not run on Ryujinx without input,
so a console run checks the assist itself, through a diagnostic line every 2 s of pad input (`aim: weap ... range
... targets ... on screen ... slowdown ... lockon ...`).

**Aim assist: the retail PC build hard-wires it off.** The console run's diagnostic lines (build 17:48:28) show the
chain working up to the last gate:
- `targets 2 on screen 2`, with the nearest zombie at 0.07 from the crosshair;
- the weapon's assist range is 3200, and the three switches are at 1;
- but `assistOff 1`: `AimAssistPlayerState::targetAssistDisabled` comes from `AimAssist_PlayerDisabledAutoAim`,
  which the PC exe compiles to `Demo_IsPlaying(); return 1;`. That one result blocks slowdown, lock-on and (line
  1650) auto aim.

Now a mod dvar decides: `bo1_aimassist` (on by default on the Switch, off on PC, where `+set bo1_aimassist 1` turns
it on). Since the user asked for PC too, the three switches lose their `0x1080` flags on every platform, so a PC
player can set them without cheats.

`codediff` against mingw32-gcc, function by function: 6 of 49 functions differ.
- Intended: `RegisterDvars` (the new dvar, flags `0x1080 → 0`), `PlayerDisabledAutoAim` (reads the dvar) and
  `BackupPlayerState` (stores its result instead of the constant 1).
- `ApplyAutoAim`: GCC had removed the whole auto-aim body as dead under the constant; it is back, and still skipped
  while `bo1_aimassist` is 0.
- `AddToTargetList` and `ApplyAutoMelee`: only jump offsets, alignment padding and global addresses moved by the new
  global.

With default settings PC behaves as retail. win64-msvc builds. NRO `46a17975`, build 18:08:35, for the console check.

**Aim assist works on the console; a new 1 fps stretch, and a profiler crash.** With build 18:08:35 the diagnostic
shows `assistOff 0` and slowdown catching zombies (`active 1 ent 747`); the user feels slowdown, lock-on and ADS
snap. But from 137 s to ~200 s the game ran at ~1 fps (16 frames per 11-16 s of `Present` lines), around the end of
round 1 (`mus_zombie_round_over` at 150 s), with the GPU idle by the user's overlay: CPU-bound. The run before the
aim-assist change (build 17:10) ended round 1 at 151 s with no slow stretch. Cause unknown so far.

`+set bo1_frameperf 200` (the engine's frame profiler, `BO1_FRAMEHITCH` lines per slow frame) crashed the console
right after `Com_Init` ("Huffman Took", the first frame), with no deko3d abort line, while it ran on Ryujinx. The
profiler's zones read `__rdtsc`, which `posix/msvc_intrinsics.h` implemented on AArch64 as `mrs cntvct_el0`:
Horizon lets user code read the physical counter only (libnx's `armGetSystemTick` reads `cntpct_el0`), so the virtual
one traps on hardware. The crashing ELF held 2 030 such reads, all in `PROF_SCOPED` zones, which only run with the
profiler on. The Switch now reads `cntpct_el0` (none of `cntvct_el0` left in the ELF); Ryujinx still reports
`BO1_FRAMEHITCH` lines. NRO build 19:06:39.

**The 1 fps stretch is the impact-mark system.** With build 19:06:39 and `bo1_frameperf 200`, round 1 ended at
115 s with no slow frame. In round 2, at level time 99.9 s, one bullet impact took 988 ms to generate:
`FX_SpawnEffect → FX_CreateImpactMark → R_MarkFragments_Go → markfragments worldbrushes`, a scope that includes the
mark's allocation callback. From the next frame on, `fx_marks_draw` (`FX_GenerateMarkVertsForWorld`, on a worker or on
the main thread when it assists) took 820-920 ms every frame for ~25 s of level time (~1 fps), then 680 ms. When it
dropped to 0, `Mark verts` (marks on static and dynamic models) took ~190 ms per frame instead. Generation against door
brushes (`markfragments entbrushes`) cost 163 ms for 4 calls, and one impact took 4.9 s. So the slowness follows
particular marks from list to list, through generation and drawing alike. Every loop in these paths is bounded
(256 marks, 255 tris and 765 points per mark, a 6144-vertex mark buffer), so reading the code has not explained a
cost in the hundreds of ms.

Ruled out by reading:
- the asserts: a fired one breaks into the debugger, it cannot slow the game down;
- lock contention: the `CRITSECT_ALLOC_MARK` users never overlap the draw (`g_markThread` would assert);
- an invalid `std::sort` comparator;
- 32-bit sizes in the worker commands (`sizeof(FxCmd)`);
- uncached memory: the world vertices come from the zone and the mark vertex buffer is `Z_VirtualAlloc`'d;
- starvation of one core: the main thread runs the draw just as slowly when it assists, while its other scopes stay
  normal.

Found on the way, a 64-bit bug: the tri- and point-group handles are byte offsets into pools whose stride is a union
with a pointer member. That stride is 24 bytes for tri groups on both widths, and 68 → 72 bytes for point groups.
`FX_PointGroupFromHandle`/`ToHandle` still checked the 32-bit limit `0x19800` (1536 × 68), so a point group past index
1450 would have broken into the debugger. The four checks now compare against `sizeof(marksSystem->triGroups)` and
`sizeof(marksSystem->pointGroups)`, which equal the old constants in 32-bit (`codediff` identical on `fx_marks.cpp`
and `r_marks.cpp`).

To catch the slow marks, a TEMP Switch-only instrumentation logs `markdiag:` lines (at most 400):
- the world-mark list when it takes over 20 ms: visits, drawn, cull time, mark-buffer use;
- any single mark draw over 2 ms, split into reserve/tris and lights/scene/expand, with the mark's fields and
  material;
- `FX_AllocAndConstructMark` over 5 ms, split into sort, lock, tri and point allocation, LRU frees, link and copy;
- `worldbrushes`/`entbrushes` over 20 ms: box query, surface loop, callback, surface and triangle counts;
- any `R_MarkFragments_Go` over 50 ms.

NRO build 19:57:43 (`c648113a`) for the console run.

**The instrumented run lost its end; thread stacks smaller than on Windows.** The user's run of build 19:57:43 lagged
in round 2, then crashed, but its SD log stops at 180 s, ~40 s into round 2, at ~30 fps. It holds no `markdiag:` line,
no slow frame and no abort line. `Switch_BootLogWrite` flushes each line, but `fflush` only hands the bytes to the FS
service, which commits them on `fsFileFlush` or close. The writer now calls `fsync` (libnx `fsdev_fsync` →
`fsFileFlush`) at most once a second.

The crash's cause is not known yet (it needs Atmosphère's crash report). A difference found while looking for it:
- **Windows**: `CreateThread`'s `dwStackSize` is the stack's initial commit, and the thread grows up to the exe's
  reserve (1 MiB, the MSVC default; this tree sets no `/STACK`).
- **posix layer, before**: it gave that size to `pthread_attr_setstacksize` as the whole stack. The job-queue workers
  (`0x10000`) had 64 KiB, and the threads created with 0 had devkitA64's default, which libnx's
  `__syscall_thread_create` sets to 128 KiB.
- **Largest frames in the ELF**: `Sys_ListFiles` takes 132 KiB of frame alone, `BG_LoadWeaponVariantDefFile` 83 KiB
  and `Demo_WriteServerCommands` 70 KiB. Gameplay frames stay under 4 KiB (`G_RunFrame` 3.8 KiB).
- **Now**: every posix thread gets at least the Windows reserve, 1 MiB, or the size rounded up to a MiB multiple, or
  the platform default when larger (glibc's 8 MiB).

**The crash: shooting a ragdoll truncated its physics id.** Atmosphère's report for the run of build 19:57:43 shows a
data abort at `0x4bc89360` on the main thread, with SP near the top of a 1 MiB stack, so not an overflow. Heap
addresses there are `0x3d4bc8xxxx`: this one lost its high 32 bits. The chain, symbolised with that build's ELF:
`CG_FireWeapon → BulletTrace_Start → FireBulletPenetrate → DynEntCl_EntityImpactEvent → Phys_ObjAddForce`
(`phys_main.cpp:1961`, the read of `Phys_GetUserData(id)->body`). In `DynEntCl_EntityImpactEvent`, a bullet that hits
a ragdoll (a dead zombie's body) asks `Ragdoll_GetRBForBone` for the bone's rigid body. `RagdollBone::rigidBody` is an
`intptr_t` holding the `PhysObjUserData` pointer, but the function returned `int` into an `int id`. Both are now
`intptr_t`. `syntax_check` is clean on mingw32-gcc and mingw32-clang for the header's 11 includers, and `codediff`
is identical on `ragdoll_update.cpp` and `DynEntity_client.cpp`.

Inventory of the family: the `mingw64-clang` compile commands were replayed from the ext4 mirror with `-fsyntax-only
-Wno-everything -Wshorten-64-to-32` over the 761 engine files, keeping the conversions whose source is
`intptr_t`/`uintptr_t`. That is 12 sites, the ragdoll one included, which proves the filter catches the bug:
- **`live_stats.cpp:1527`**, a real one: `v17` (`unsigned int`) took `(uintptr_t)&name[strlen(name) + 1]`, then
  `memcpy(v14, v15, v17 - (uintptr_t)v15)` took a ~2^64 length. It is a milestone ticker on rank-up, never seen in
  zombies. Now `uintptr_t`; `codediff` identical.
- **Benign**:
  - the four hunk `commitSize` values in `com_memory.cpp` (differences of addresses inside one hunk);
  - the frame count in `snd_radverb.cpp:338`;
  - `track_userhunk_alloc`'s position argument (its body is commented out);
  - the three `Scr_ReadUnsigned` reads into `unsigned int` counters and case values in `cscr_vm.cpp`, which the
    emitter wrote from 32-bit values.

The replay's other errors are the two XAudio2 files (posix and Switch have no XAudio2) and the known
`cl_keys.cpp:1241` (see phase 3). In the new ELF, `Ragdoll_GetRBForBone` loads its result with `ldr x0` where build
19:57:43 had `ldr w0`. win64-msvc builds. NRO build 20:57:15 (`4f24887a`): this fix, the posix stack reserve, the log
`fsync`, and the `markdiag` instrumentation for the lag.

Console run of build 20:57:15 (user: "ça semble bon"): Kino from boot to level time 137 s, 25-30 fps throughout, the
round-2 start at 106 s included. No slow frame after loading, no `markdiag:` line, no crash, no crash report. The log
ends at 190 s of uptime without "Shutdown complete", which suggests the game was closed from HOME. The lag has not
come back in the ~30 s of round 2 this log covers. The instrumentation stays until a longer run either shows it or
rules it out.

The user shot corpses in that run without a crash. Committed without the temporary diagnostics. The aim-assist lines
are removed: the assist is confirmed on the console. The `markdiag` instrumentation stays out of the commits, as a
working-tree change in `fx_marks.cpp` and `r_marks.cpp`, until a longer console run shows the lag or rules it out.
Before the merge: win32-msvc and win64-msvc build, and the Switch builds in the committed state (no `markdiag:` or
`aim:` string, no `cntvct_el0`).

**Start menu on the Switch (asked by the user: start a game without the official menu).** Before the engine,
`switch_main.cpp` shows a libnx text-console menu (`switch_launcher.cpp`). It offers:
- the map: the 10 zombies maps, labelled as in `tools/mod-launcher.hta`;
- the aim assist (`bo1_aimassist` and the three `aim_*_enabled`);
- god mode (`bo1_testclient_god`, which puts `FL_GODMODE` back around every server frame: the `god` command is a
  toggle that `ClientSpawn` clears);
- the renderer (`gfx.cfg`, deko3d or GL);
- the frame-rate cap (`com_maxfps` 30 or 60), shadows (`sm_enable`) and texture filtering (`r_texFilterAnisoMax`
  16/8/4/2, or 1 with `r_texFilterMipMode` trilinear or bilinear);
- the FPS/battery counter (`bo1_fpscounter`);
- the controllers: the Npad styles every 0.5 s, and A opens the system controller screen (`hidLaShowControllerSupport`,
  single mode).

Every graphics dvar is written explicitly, because they are archived in `config.cfg`. The choices persist in
`menu.cfg`. Start (**+**, or A on DEMARRER) writes `launch.cfg` (`... +devmap <map>`, with the optional
`launch_extra.cfg` before `+devmap`) and `gfx.cfg`. An empty `skip_menu` file boots on those two files as they are.

`launcher_config.cpp` holds the settings and the line and has no libnx; `launcher_config_test` checks it on the host
(defaults, the exact line, round-trips, invalid values, `+devmap` last): all checks pass. `d3d9emu_test`,
`gb_gl_test` (46 checks) and `gamepad_test` still pass, and win64-msvc builds.

Ryujinx, build 22:06:32, with Player 1 mapped to the keyboard for the test (config backed up and restored, same md5).
The menu shows with battery, build and "Joueur 1 : Manette Pro". Down then Right picks Five, and + starts. The log has
the generated line with the extra `+set bo1_frameperf 200` before `+devmap zombie_pentagon`, and Five loads and
renders on deko3d.

The first try crashed at the first `BootLog` after Start: `printf → consolePrintChar → framebufferBegin` read address
0. libnx's `consoleExit` closes the framebuffer but leaves stdout and stderr on the console device, which starts a
new frame on the next character. The launcher now saves `devoptab_list[STD_OUT]`/`[STD_ERR]` before `consoleInit`
and restores them after `consoleExit` (rule in `switch-homebrew.md`).

`skip_menu` run, with a hand-written `launch.cfg` (Kino, `bo1_frameperf`):
- the log says `skip_menu present`, has no `[launcher]` line and uses exactly that line;
- `menu.cfg`, `launch.cfg` and `gfx.cfg` keep their md5;
- Kino loads.

The emulator's SD keeps `skip_menu` for scripted runs. Console check pending: the controller screen, god mode, 30 fps
and the filters on hardware.

**Console test of the menu (build 22:06:32).** The menu works. A on "Manettes" did nothing: `hidLaShowControllerSupport`
returned `0x5d59` (libnx 46, `LibAppletBadExit`) each time. That call only shows its screen when the connected pads
fall short of the request, and the attached Joy-Cons satisfy a single-player request. The menu now calls
`hidLaShowControllerSupportForSystem(&info, &arg, false)`, which always shows the game's controller screen (SDL2-switch
uses it too). On the console the controller screen now opens and returns `0x0`.

**Asserts do not stop on the Switch: probable cause of the round-2 lag.** On the console, `__debugbreak()` is
`raise(SIGTRAP)` (`msvc_intrinsics.h`), which newlib/libnx ignore, so a failed assert logs and the game goes on. The log
is `Sys_HeadlessLogAssert`, which opens, appends and closes two SD files (`main/console_mp.log`,
`main/bo1_emergency.log`).

The point-group check fixed in `1c216da` held the 32-bit limit, so it failed on every access past group 1450. That is
dozens of accesses a frame once round 2 filled the pool, each one paying two SD writes. This fits the
`fx_marks_draw`/`Mark verts`/`markfragments` costs of 200-900 ms per frame, which followed particular marks and
stopped as marks were freed, and the lag has not been seen since build 20:57. It stays "probable": `bo1_emergency.log`
is deleted at each start, so the lag runs' asserts are gone.

A Call of the Dead game logged 1011 `CG_EntityContact` asserts (`!cent->pose.general.mins[2]`). `actor.height` and
`general.mins[2]` share offset 16 on both widths, so this is the map's data, not a layout bug; the same assert stops the
Linux client, where SIGTRAP kills. On the Switch (`__SWITCH__` only), `Assert_MyHandler` now logs each assert site
three times, then once per thousand, with the count. Ryujinx, Call of the Dead: occurrences 1, 2, 3, 1000, 2000 and
3000 logged instead of more than 3000 double writes. The `markdiag` instrumentation is removed (never committed).

**`bo1_clientCommands` on the Switch.** The console and Ryujinx have no headless client, so `bo1_clientCommands`
(`win_main.cpp`, `Sys_HeadlessClientCommandsFrame`) runs in a normal game there (`__SWITCH__` only, empty by default):
a scripted emulator run puts it in `launch.cfg` or `launch_extra.cfg` to act at a set time after spawning.

**Call of the Dead rendering: the snow shrubs (fixed 2026-10-27, a stale shader pack on the SD).**
- **Symptom.** The "flickering walls" were the snow shrubs: alpha-tested cards drawn as solid light-blue or orange
  triangles, on the console and on Ryujinx, with shadows on or off and filtering 2x or 16x. Der Riese showed whole walls
  in the fog colour the same way.
- **Placing it.**
  - The Linux client on GL drew the shrubs right. Gdb swallowed the asserts' SIGTRAP:
    `handle SIGTRAP nostop noprint nopass`. So the zone data, the GLSL translation and the frontend were fine.
  - The deko3d bindings were right too: BC2 to `DkImageFormat_RGBA_BC2`, the sampler3D in slot 11, the blend state.
  - The deko3d frame dump showed the texture's dark branches *inside* the solid triangles. So the colour was read,
    and only the cut-out was missing.
- **Cause.** Both SD cards held the `shaders.pack` of 2026-10-22 (`build/iwffs-v2`) and a `shaders.dksh` compiled from
  it (uam output of that GLSL, byte for byte).
  - That GLSL still declares `vec4 bo1_vpos_flip;` before `bo1_alpha_test`, so it reads the alpha test at `c[257]`.
  - The frontend writes it at `c[256]`, so the alpha test was off in every shader.
  - With the alpha test off, the depth prepass wrote whole cards and the fog showed through them.
  - Nothing warned: a pack entry's hash is the D3D9 bytecode's, so the stale and the current packs hold the same
    13 704 hashes.
- **Fix.** Rebuilt `shaders.dksh` from the current `build/zones64/shaders.pack` with `make_dksh_pack.py` (13 692 DKSH,
  the same 12 vertex shaders rejected by uam), and copied both packs to the SD cards. Ryujinx then showed no solid
  triangles at the same 150 s frame.
- **Rule.** Redeploy `shaders.pack` and `shaders.dksh` together after any change to the shader interface. Before
  chasing a rendering bug that only the console shows, check `grep -ac bo1_vpos_flip shaders.pack` gives 0 on the SD.

**Der Riese rendering: flickering walls (fixed 2026-10-27, two shader-interface bugs).**
- **Symptom.** With the current packs, whole façade walls drew as solid light blue or orange, changing from frame to
  frame, and a band of lower walls stayed light blue. Seen on the console and on Ryujinx.
- **Placing it.** A temporary deko3d probe read two pixels of the lit pass (pass 2) after each draw of the traced
  frame. The first draw wrote the orange, and none of the 338 draws after it touched those pixels: the wall was not
  drawn there at all. With `+set r_depthPrepass 0` the façade drew right.
- **Cause 1: no position invariance.** The engine's depth prepass and its lit passes compare equal depths computed by
  different vertex shaders. uam fuses their multiply-adds differently, the positions differ in the last bits, and the
  lit pass fails its `LessEqual` test on whole walls (z-fighting). Mesa on llvmpipe happened to agree with itself.
  - Fix: `ff_glsl.cpp` declares `invariant gl_Position;` in every vertex shader. uam then stops fusing them: a test
    shader grows from 3072 to 3328 bytes of DKSH.
- **Cause 2: 12 vertex shaders never compiled.** `ShaderPack_InputLocation` put BLENDWEIGHT at location 19, the
  index of its material stream destination. GL and uam accept 16 locations, so uam rejected those 12 shaders (GL
  failed to link them) and the frontend skipped their draws ("is not in shaders.pack"): the band of light-blue walls.
  - Fix: BLENDWEIGHT takes location 15, and TEXCOORD10-13 (destinations 15-18) take none. No vertex shader in the
    zombies zones reads them: 356 vertex shaders checked, and ffconv rejects a shader that would.
  - The frontend and the packs must change together: an NRO built before this table reads the wrong inputs with the
    new packs, and the reverse.
- **Verified.**
  - `ffconv --verify --game`: 65 zones, 0 failed; the zones are byte-identical to the previous `build/zones64`.
  - `check_shader_pack.py`: 13 704 shaders, 0 rejected. `make_dksh_pack.py`: 13 704 DKSH, 0 rejected by uam
    (12 before).
  - The intermediate pack (invariance only) differs from the previous one only by `#version 430` and the
    `invariant` line in the 356 vertex shaders.
  - `d3d9emu_test`, `gb_gl_test` (46 checks), `launcher_config_test` and `gamepad_test` pass.
  - Ryujinx, NRO of 01:43:34 with the new packs: the 150 s and 210 s frames of Der Riese show the façade, the
    scaffolding and the lower stone walls, with no solid fill.
  - Console (the user, same NRO and packs): Der Riese draws right, and no texture problem is left.
- **Not done.** The Linux GL client could not be compared on Der Riese: it stops early with "Field loopSoundFade
  changed for archived eType Player Entity" (a separate bug, open).

**Call of the Dead rendering, still open.**
- **After the player dies (Ryujinx).** Seen with the stale packs; the console run with the current ones showed no
  texture problem, Ryujinx is still to check again. The world turns flat grey and light blue. The pass trace shows every lit draw
  binding the same 128x128 BC3 texture with 8 mips in slots 0 and 1 (the engine's default image, by its size), with only
  the lightmaps differing. The real textures are back at the 150 s dump.
- **Linux GL comparison.** The Linux client stops on the `CG_EntityContact` assert while Call of the Dead loads
  unless gdb swallows the SIGTRAP: run it under `/usr/bin/gdb -batch -ex "handle SIGTRAP nostop noprint nopass"
  -ex "handle SIG35 nostop noprint pass" -ex run --args ...`.

## Dedicated server (phase 8)

Plan (steps): 0 prove that a client of a remote server plays; 1 a `bo1_server` mode bound to the network (not
loopback), `server.cfg`, `getstatus`; 2 clients: `launch.cfg` on PC and Switch, Switch sockets, connect beyond the LAN,
silent reconnect; 3 the curses console (ncurses on Linux, PDCurses on Windows) inside the server process; 4 admin
commands (`zm_*`: god, revive, respawn, points, perks, starting points, reset, map and mods) through the engine and a
`mods/admin` script mod; 5 client logs sent to the server over UDP, one file per player; 6 join in progress;
7 more than 4 players. Work happens in `../BO-Zombies_switch_server` (branch `server`); before every rebuild the test
servers and clients are stopped (`.claude/rules/server-test-processes.md`).

**Step 0, a remote client plays (2026-10-26).** Setup, all win64-msvc: a dedicated server
(`tools/headless.ps1 -Zombies -Commands "+set sv_maxclients 4 +devmap zombie_theater"`, slot 0, `127.0.0.1:29060`)
and a headless client in another process (`-Client`, slot 1) that boots to the main menu and then runs
`connect 127.0.0.1:29060` (`+set bo1_clientCommands "2000:connect 127.0.0.1:29060"`). `127.0.0.1` is a real UDP
socket (`NA_IP`), not the in-process loopback. Five fixes got the client in game:

- **The menu's local server swallowed the reply.** The zombies main menu runs a listen server (`frontend`), and
  while one runs `Com_ServerPacketEvent` hands every UDP packet to `SV_PacketEvent`, which drops the remote server's
  `challengeResponse`: the client sat in `CA_CONNECTING`. `CL_Connect_f` now kills the local server for any address
  (it did only for `localhost`), as the party connect path already did (`cl_main_pc_mp.cpp`).
- **`zombiemode` was 0 while the client parsed the gamestate.** Both ends pick the zombies netfield tables (actor,
  script mover, origin bits) from `zombiemode`, which `Com_LoadLevelFastFiles` sets from the map name. A remote
  server's client loads the map only after the gamestate, so it read the baselines with the MP tables: `bad command
  byte 134` after the 881 configstrings (all identical on both sides), 30 times out of 30. The map-name logic is now
  `Com_SetGameModeDvarsForMap` (`common.cpp`), also called by `CL_ParseGamestate` when no local server runs.
- **The client compiled the MP server scripts.** Without a local server, `CG_Init` compiles the server's scripts
  itself (`CGScr_LoadScriptsAndAnims`) so that the animtree indices in the snapshots resolve; that path named the MP
  scripts (`maps/mp/_destructible`, the MP gametype) and failed. For an SP level it now compiles what the server's
  `GScr_LoadScripts` does, in the same order (codescripts, then `GScr_SP_LoadScripts`), then `BG_LoadAnim`,
  `G_SP_FindActorAnimTree`, `BG_PostLoadAnim`: the actors get the client's own `generic_human` tree. The SP list's
  aitype and traverse scripts are compiled in a new `compileOnly` mode that keeps no `AITypeScript` and writes no hunk
  file data (that table belongs to a local server).
- **64-bit: `Hunk_AddData` allocated its node with the 32-bit size.** `alloc(9)` is `offsetof(fileData_s, name)` on
  32-bit (17 on 64-bit), so the next allocation overwrote the node's `next`: `Hunk_Clear` then followed a pointer made of
  two 32-bit halves (`0x1CF2A1201C0C65E0`, found with a temporary check of every node) and crashed at quit and at
  disconnect. Every remote client hit it (the client-side animtrees go through `Hunk_AddData`). Now
  `alloc(offsetof(fileData_s, name))`; `codediff` identical.
- **Same family, inventoried.** The struct-cast allocations with a literal size were measured with clang for
  `i686-` and `x86_64-pc-windows-msvc` (method of phase 3): `cgScriptMover_s` 96, `cLinkInfo_s` 72,
  `ClientTagCache` 96, `AimTargetCache` 8, `GfxLight` 368 are the same on both; `XModel` (380 for 252/328) and the
  path buffers are large enough. `GfxImage` is 52 on 32-bit and 72 on 64-bit: `ui_screenshot.cpp` allocated 52, now
  `sizeof(GfxImage)` (`codediff` identical). `ui_shared_obj.cpp` allocates 8 bytes for a `focusItemDef_s` (24 on
  32-bit already) in the raw `.menu` loader that the fastfile build never runs: left as retail, noted here.
  `tools/litsizes.py` misses this family (it looks for multiples of a 32-bit size; 9 and 52 are an `offsetof` and a
  non-pointer struct).

Result: the client goes `CA_SENDINGSTATS` → `CA_LOADING` → `CA_ACTIVE` on `zombie_theater` with the world, view
model and HUD (500 points). With the player in god mode on the server (`bo1_testclient_god 1`) zombies walk up and
attack, drawn and animated on the client (screenshots). Without it the idle player dies, the server shows the game
over, restarts the map, and the client follows each `map_restart` back to `CA_ACTIVE` (3 times in 240 s); the server
log has no script error, and the client quits normally (exit 0). Not covered yet: the inactivity kick a server
gives an idle client (it crashed in `Hunk_Clear` before the `Hunk_AddData` fix, not rerun since), a client on another machine of
the LAN (the dedicated mode still binds `127.0.0.1`: step 1), and a log comparison against a listen-server run.

Merged into `switch` with phase 5's work (2026-10-26): `win32-msvc` and `win64-msvc` build; the same client run on
the merged tree reaches `CA_ACTIVE` 4 times in 240 s (3 game overs) and quits normally. That run's server log shows 3
script runtime errors in retail `maps/_zombiemode.gsc` (`chalk_one_up`, line 3785: `huds[0]` removed before
`MoveOverTime`), a race between the game over and the round change printed with `developer 1`; the earlier run (2
game overs) had none. To compare against a listen-server run before blaming the remote path.

**Step 1, the `bo1_server` mode (2026-10-27).** Operator's guide: `docs/SERVER.md`.

- `+set bo1_server 1` (`win_main.cpp`) runs with every `bo1_headless` guard (no window, dialog or sound; fatal errors
  exit) but serves the network: it does not force `net_ip`, whose default `localhost` `NET_IPSocket` binds as
  `INADDR_ANY`. It starts with `+set bo1_zombies 1 +set sv_maxclients 4 +exec server.cfg` before the caller's line
  (the caller's `+set` values override the defaults; `server.cfg`, an exec, runs after every startup `+set` and wins
  over both) and ends with `+set dedicated 1 +set logfile 2`. `onlinegame` stays 0 (headless zombies).
- `getstatus` (`sv_main_mp.cpp`, `SV_StatusRequest`): Quake 3's text reply, serverinfo plus one line per client
  (number, points from `G_GetClientScore`, ping, state, name), at most one answer per 50 ms. `tools/server_status.py`
  sends it.
- 64-bit: `NET_IPSocket` and `NET_TCPIPSocket` returned the socket as `unsigned int`; now `SOCKET` (`codediff`
  identical).
- Test only: `+set bo1_headless_lan 1` keeps a headless client's `net_ip` as given, so it can reach a LAN address (a
  socket bound to 127.0.0.1 gets `WSAENETUNREACH` there).
- Verified: the Windows server binds `0.0.0.0:29060` (`netstat`), execs `server.cfg`, answers `getstatus` from Windows
  and from WSL (another host for it); a win64 client joins it through `192.168.1.194` and shows in `getstatus` as
  `#0 Player: 500 points, ping 51, active`, quits normally. The Linux server (`linux64-clang`, ext4 mirror, data on
  ext4) binds `0.0.0.0:29070`, answers from WSL and Windows, and a win64 client joins it through `172.23.3.156`:
  active, in game (screenshot), quits normally.
- That last case first crashed the client in `DynEnt_GetClientEntity` (a dynent id past the model list). Byte dumps of
  the gamestate showed both servers' messages identical up to the failing entity, and per-field traces showed the
  client reading a script mover's origin wrongly only from the Linux server: the Huffman tree (decision above). The
  netfield tables (2249 rows) and the map center are identical on both servers. The other `qsort` calls are local to
  one process (server snapshot order, client HUD, physics, stats), so they cannot break the protocol.
- Known, not fixed yet: on Linux `console_mp.log` stops mid-line after the first client's gamestate (the rest stays
  buffered); stdout keeps the lines. The curses console (step 3) will take the server's output.
- Rule kept from the run: before each rebuild, stop the test servers and clients, Linux ones by their executable
  (`.claude/rules/server-test-processes.md`): three stale Linux servers once kept port 29070.

**Step 2, PC clients (2026-10-27).** The Switch part (sockets, `nifm`, the shared `launch.cfg` reader) waits until the
`_new` and `_server` worktrees are both merged into `switch` (user's order).

- `launch.cfg` next to the program (`win_main.cpp`, `Sys_PrependLaunchCfg`, not on the Switch, which reads its own
  from the same folder in `switch_main.cpp`): its text, newlines read as blanks, goes before the command line, so the
  command line's values win. Read before the `bo1_*` flags, so it may hold them.
- `cl_autoConnect <address>` (`cl_main_mp.cpp`, `CL_AutoConnectFrame`): a `+connect` on the command line is undone by
  the menu's own load (phase 0), so the client connects once the menu's level is up, and again whenever it is back at
  the menu or disconnected, after 2 s, then 5 s doubled per failed attempt up to 60 s. A short outage needs nothing:
  the netchan rides it until `cl_timeout`.
- `cl_allowWanConnect` (`cl_main_pc_mp.cpp`): `CL_Connect_f` now refuses a non-LAN address first, with a message, before
  it tears anything down. Without that, step 0's kill of the menu's local server ran even for a refused address and left
  the client disconnected with no menu level.
- Verified (win64 server and client, `192.168.1.194`): with only `launch.cfg` (`developer 1`, `cl_autoConnect`,
  `cl_timeout 15`, three lines) the client joins within 40 s of the server being up; the server killed in game, the
  client times out (`EXE_ERR_SERVER_TIMEOUT`), retries, and is active again 35 s after the server restarted, without
  any input; it keeps retrying at growing intervals once the server is gone, and quits normally (exit 0). `connect
  203.0.113.1` is refused with the message and the menu stays up; with `cl_allowWanConnect 1` it goes `CA_CONNECTING`.

**Step 3, the server's curses console (2026-10-27).** Operator's view in `docs/SERVER.md` ("The console").

- `src/server_tui/sv_tui.cpp` is the curses side (status line, player slots, a 5000-line log, the command line with a
  100-entry history, F2/F3 selection, F8/F10 fill the command line, PgUp/PgDn/End). It includes no engine header:
  curses.h defines `move`, `clear`, `refresh`, `timeout`, `scroll`. `sv_tui_engine.cpp` fills a plain status struct
  from `svs.clients`, `sv_hostname`, `mapname`, `net_port`, `sv_maxclients` and `G_GetClientScore`.
- `win_main.cpp`: started after the headless set-up when `bo1_server` and `bo1_tui` (default 1); `ServerTui_Start`
  refuses without a terminal (`isatty` on posix; on Windows it opens a console with `AllocConsole`). `Sys_Print`
  feeds it instead of the plain console (any thread, under a mutex); `Sys_GetEvent` takes its command line instead of
  `Sys_ConsoleInput` and runs its frame (keys, redraw at most 10 times a second). `Sys_Quit`, `Sys_Error` (which ends a
  headless process with `TerminateProcess`, so no `atexit`) and the headless exception filter restore the terminal.
- Build: PDCurses 3.9 (public domain) vendored in `third_party/pdcurses` (core, wincon port, `common/acs437.h`) as its
  own static library on Windows, so the engine's forced includes stay out of it, with the engine's CRT (the engine's
  project is `MultiThreadedDLL`: CMake 3.16's `MSVC_RUNTIME_LIBRARY` wins over the `/MT` in `CMAKE_CXX_FLAGS`; forcing
  `/MT` on PDCurses gave `LNK4098`). posix links the system ncurses; the Switch has neither.
- Command lines end with `\n`, as `win_syscon.cpp` returns them: `Sys_GetEvent` copies `strlen - 1` characters. The
  TUI's first command arrived as `statu`. `posix_syscon.cpp` had the same bug (it stripped the newline, so a typed
  `status` ran as `statu`): fixed the same way.
- Verified on Linux in a 120x40 `tmux` pane: the screen above with a win64 client joined (selected with F3, F8 filled
  `clientkick 0`, Esc cleared it), `status` run from the command line printed the server's table into the log, `quit`
  ended the server with code 0 and gave the terminal back. The Windows console window (`AllocConsole`) was checked
  by the user, with a current exe: it shows the same screen and takes commands.

**Start scripts (2026-10-27).** `Server.bat` / `Server.sh` at the root start a server on `0.0.0.0` (map and port as
arguments, the rest passed on); `Play.bat net <address[:port]> [name]` (alias `Launch.bat`) starts the 64-bit client
with `cl_autoConnect` and `name`. Usage in `docs/SERVER.md` ("Start scripts", "Joining from a client").

- `Server.bat` starts the program with `start /D build\Release`: the engine reads `localization.txt` from its working
  folder, and without it the server stopped on the `localization.language` assert (`win_localize.cpp`).
- `Server.sh` runs a copy of the program in the server folder, next to links to `zone`, `shaders.pack` and
  `localization.txt`: the program loads `zone/` from its own folder.
- `Play.bat net` leaves `net_ip` at its default: the other modes bind `127.0.0.1`, from which a LAN address cannot be
  reached.
- `Server.bat` and `Play.bat` take `BO1_INSTALL` / `BO1_ZONES` as `Server.sh` does: the `server` worktree has
  neither the game files in `build\Release` nor `build\zones64` (the user's first `Server.bat` stopped on "IWffs
  zones not found"). Checked: `Server.bat zombie_cosmodrome` with both set loaded Ascension (`admin: loaded on
  zombie_cosmodrome`, `getstatus`), and `Play.bat net`'s dry copy took the worktree's exe with the other folder's files.
- Verified: `Server.bat` bound `0.0.0.0:3074` (netstat, `Opening IP socket`), answered `getstatus` from Windows and
  WSL, and a win64 client with `name Player2` showed up as `#0 Player2 ... active`; `Server.sh` on the ext4 data bound
  `0.0.0.0:3074` and a win64 client `Player3` joined it over the WSL address (`172.23.3.156`). `Play.bat net`'s command
  lines were checked with a dry copy (`start` replaced by `echo`) for an address alone, with a name, and without an
  address (usage message).

**Step 4, admin commands (2026-10-27).** Operator's view in `docs/SERVER.md` ("Admin commands"); script side in
`mods/admin/README.md`.

- `src/server_mp/sv_zm_admin.cpp` registers the `zm_*` commands only with `bo1_server`, as the operator commands are
  (`Cmd_AddCommandInternal` + `Cbuf_AddServerText_f`, so they run on the server thread). god / demigod / notarget /
  noclip are the retail cheats' entity and client flags, kept per slot and set again before and after `G_RunFrame`
  (a spawn inside the frame rebuilds the flags, as `bo1_testclient_god` found); switched off, a flag is cleared once,
  so the retail cheats still work afterwards. A slot's states end when it is freed, not on a map load.
- Everything that needs the game's scripts (points, perks, revive, kill, respawn, round) is queued and handed to
  mods/admin as a `"zm_admin", cmd, arg` notify on the player's entity (`Scr_AddString` ×2 + `Scr_Notify`, the
  `menuresponse` path, values pushed last to first), at most one per player and frame: a notify finds only the
  threads already in their `waittill`, and the script's loop is back there one frame later.
- mods/admin is stacked on every map by `FS_ModStackNames` when `Sys_IsDedicatedZombiesServer()` (new, `win_main`),
  after the `fs_mods` names, and the server also searches `fs_homepath/mods/<name>` (after `fs_basepath`'s, so it
  wins): `Server.bat` / `Server.sh` link the repository's `mods/` there, since the install has none and
  `build\Release\mods` is only refreshed by the win32 build. A map load (`SV_SpawnServer` → `FS_Restart`) reads
  `fs_mods` again, so `zm_mods` is `fs_mods` + `devmap <current map>`.
- The script's replies are `LogPrint("admin: ...")`; `G_LogPrintf` also prints such lines to the console on the
  server (`SV_ZmAdmin_LogLine`), so they reach the curses console. rcon output (`Com_Printf` while a command sent
  with rcon runs) goes back to the rcon client only, not to `console_mp.log`.
- Each command calls the retail function found in the zones' script sources (`auto_revive`, `give_perk`,
  `add_to_player_score`, `spectator_respawn` through `level.spawnPlayer`, the devgui's goto-round). `zm_kill` first
  went through the engine's `player_die` (the kill cheat's body): the player stayed `playing` at 0 health and the MP
  engine respawned it a few seconds later, skipping the zombies death. It now does what `maps\_zombiemode` does to a
  player outside the playable area (`lives = 0`, `DoDamage(health + 1000)`, `bleedout_time = 0`): down, bled out at
  once, spectator.
- `g_inactivity 0` joins the server defaults: a dedicated config sets 120 (`g_inactivity "120"` in the dvar dump), and
  the idle headless test clients were dropped with `GAME_DROPPEDFORINACTIVITY` after two minutes, ending the game.
- `zm_admin` and the TUI show each player's place in the game (`playing`, `down` from `gclient_s::lastStand`, `dead`
  at 0 health, `spectator`) and the zm states (G/D/N/C); F4-F7 fill `zm_god`, `zm_revive`, `zm_respawn`, `zm_points`,
  F10 now `zm_reset`. `tools/server_rcon.py` sends a command over rcon.
- Verified on win64 (Kino, a `bo1_server` server and two headless clients over the LAN address, commands over rcon):
  `zm_startpoints 5000` / `zm_startperks jugg` gave both players 5000 points and Juggernog (health 250); `zm_points`
  (+1234, -2000), `zm_setpoints 7777`, `zm_perk Bravo all` (Quick Revive, Speed Cola, Double Tap, Mule Kick: Kino's
  other machines), `zm_perk 0 speed doubletap`, an unknown perk refused; `zm_round 5` and `zm_round 3` ("round 3 from
  1", then "round 3 started"); `zm_revive` on a player downed in round 5 ("revived"), refused on a standing one;
  `zm_kill 0` → `spectator` within a second, `zm_respawn 0` → `playing`; `zm_mods zinfo` reloaded Kino with
  `fs_mods: hooks of mods/admin` and `mods/zinfo`, both players back with their start points and perks; `zm_god`
  kept a player alive through rounds the other one was downed in. Not run: `zm_noclip` / `zm_notarget` on a moving
  player (headless clients do not move).
- Verified on Linux (`Server.sh` in a 120x40 `tmux` pane, ext4 data, a win64 client over the WSL address): the
  `mods` link to the repository, `fs_mods: hooks of mods/admin`, `admin: loaded on zombie_theater`; the TUI row
  `0 Alpha ... playing 2500 G` (`zm_startpoints 2500`, `zm_god` over `tools/server_rcon.py`), the script's `admin:`
  replies in the curses log; F7 with nothing selected filled `zm_points 0 ` (the first player is selected), `500`
  Enter gave 3000 points, F4 Enter `zm_god 0`. `server_rcon.py` with a wrong password prints `Invalid password.`
  (BO1's rcon replies carry a 0x01 byte before `print`). `linux64-clang` and `switch` (`.nro`) build;
  `tools/syntax_check.py` (`win_main.h` changed): 1298 files, 0 errors with `mingw32-gcc` and `mingw32-clang`.
- Known: a Quick Revive given while one player is in takes the solo path (`give_perk`: `players.size == 1`), and its
  retail `solo_revive_buy_trigger_move` thread then logs `self.lives` errors once a second player is in, as a solo
  purchase followed by a join would. A joiner can come in as a spectator until the round ends (step 6); with only
  that player alive, a down ends the game.
