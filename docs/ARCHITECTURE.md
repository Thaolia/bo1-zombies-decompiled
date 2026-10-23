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
| `win64-msvc` | 64-bit engine with D3D9 still in place, IWffs zones (always `BO1_OFFLINE`) |
| `mingw64-clang` | Phase 3 diagnostics only (`tools/syntax_check.py`): every pointer-width assumption is a clang error |
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
  each `Load_Stream` passes the `var*` global it streams from.
  `ff_stream.cpp` implements the engine's stream API (`db_stream.h`, `db_stream_load.h`) with the same 32-bit
  positions as the engine, and tracks the 64-bit positions alongside. `ff_stubs.cpp` replaces registration, D3D,
  sound and mod hooks.
- **Layouts**: `gen/layouts.py` (libclang) parses `db_load.cpp` for the 32-bit and the 64-bit target and writes, per
  streamed type, its leaf fields with both offsets: pointers, unions of pointers, unions of pointers and wider data
  (converted with the member the loader walks), and bytes. 32-bit padding is copied where the 64-bit layout has room.
- **Value fixups**: an integer whose value is a byte distance inside an array of a struct that grows is rescaled,
  not copied. `layouts.py`'s `VALUE_FIXUPS` lists `(record, field, unit)`; the generated layout carries `ValueFixup`
  entries (`ff_layout.h`) with both offsets and both unit sizes. `ff_stream.cpp` writes `value / unit32 * unit64`
  when widening and the inverse when narrowing, and fails the zone if the value is not a whole number of units or
  points outside the array. One entry so far: `GfxAabbTree::childrenOffset` (40 → 48 bytes per node). The audit of
  the other integer fields is in `docs/PORTING.md` (phase 3).
- **Shared targets**: where identical bytes were already streamed, the retail linker writes an offset instead of a
  copy, whatever type those bytes had: a weapon's all-null `XModel *[16]` points at 64 zero bytes of a script string
  array, a `BrushWrapper::planes` array at the planes its sides streamed one by one (8-aligned apart on 64-bit). The
  transform passes each `DB_ConvertOffsetToPointer` the pointee type and count of its inline branch (`READ_COUNTS`
  where the engine reads another count), and the branch itself. `ff_stream.cpp` checks that every leaf of the pointee
  lands on a leaf of the same kind at the 64-bit position the loader reads; where not, the IWffs slot says "follows"
  and the inline branch streams a widened copy of the target's retail bytes, so the 64-bit loader walks it like any
  inline object. Only targets whose pointers and unions are zero qualify. The copy is a separate object (no engine
  code compares those addresses). `--verify` gets each such slot's retail offset from the widening pass, skips the
  copy with the same branch, requires its narrowed bytes to equal the retail target, and fails on any pointee
  mismatch left in the 64-bit zone.
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

## 64-bit engine (phase 3)
The same sources build 32-bit (reference) and 64-bit. Pointer-width fixes are retypings that leave the 32-bit code
as it was:
- **Loader**: a 64-bit build reads `IWffs` zones only. `DB_ReadIWffsHeader` (`db_file_load.cpp`) checks magic,
  version and ABI against `DB_FF64_BUILD_ABI`; `DB_OffsetToAddress` (`db_stream_load.cpp`) decodes 64-bit pointer
  slots; `DB_AllocStreamPos` aligns to at least 8. `db_load.cpp`'s `Load_Stream` sizes are `sizeof`/`offsetof`.
- **Layout checks**: `src/platform/arch_size.h` gives `BO1_ARCH_SIZE(n32, n64)` for struct-size `static_assert`s,
  `BO1_ABI_SIZE` for classes with a base class (MSVC and Itanium lay them out differently), and
  `BO1_SAVE_IMAGE_ASSERT` for the SP save image, which binds 32-bit builds only: 64-bit builds skip saves.
- **Script bytecode**: operands that hold a pointer (code positions, `case` tables, thread-call parameter counts,
  animation slots) are `uintptr_t` wide, written by `EmitCodepos` and read by `Scr_ReadCodePos`/`Scr_ReadUnsigned`;
  integers and jump offsets stay 4 bytes (`EmitInt`/`Scr_ReadInt`). Function handles are `programBuffer` offsets.
- **Decompiled views of 32-bit layouts** keep their shape but name what they mean: `centity_s::flagWord` and
  `flameGeneric_s::flagWord` (unions over the bitfield words read as `+ 201` / `+ 23` dwords),
  `phys_free_list<T>::data_of(node)`/`node_of(data)` (was `(T *)&node[2]`, `node[2].m_prev_T_internal`), `Float4Load`/`Float4Store` (`com_math.h`, a `float4`
  view of unaligned storage), `IK_STATE_ALLOC_SIZE`, `DObj objBuf[3072]`. Stack buffers and streamed arrays that the
  SSE code expects 16-aligned (`GfxSurface`) get `alignas` on 64-bit only.
- **Lock-free physics pools** (`phys_mem_new.h`): the free-list head is a tagged pointer, a `{pointer, tag}` pair on
  32-bit and a 48-bit pointer with a 16-bit tag in one word on 64-bit, both swapped with the 64-bit CAS.
- **Tools** (`tools/`):
  - `syntax_check.py build/mingw64-clang`: lists every pointer cast to a smaller integer and every failing assert;
  - `codediff.py build/mingw32-gcc FILE...`: compares the 32-bit `.text` of each file with its version at a git ref;
  - `litsizes.py build/mingw64-clang`: lists calls passing a literal byte count for a struct that grows on 64-bit
    (including out-parameter allocators such as `R_DebugAlloc((void **)&p, N, ...)`);
  - `ptrcasts.py build/mingw64-clang`: lists casts that read 64-bit data through a 4-byte view (pointer arrays and
    pointer-holding structs cast to `int *`, byte offsets into them, `qsort`/`bsearch` element sizes, `4 * n`
    pointer-array allocations), which compile without a warning;
  - `stackviews.py build/mingw64-clang-full`: lists casts that view a local or a parameter (`&x`, `x[N]`,
    `&x.member`) as a type bigger than what is left of it, the trace of a stack object Hex-Rays split into several
    locals (`make_rotate((phys_mat44 *)v3, ...)` on a 12-byte `v3`), parsed for both i686 and x86_64;
  - `headless.ps1 -ExeDir build\win64-msvc\Release -RunDir <dir> -ZoneDir build\iwffs-full\zone`: runs the 64-bit
    exe on the IWffs zones (a run folder whose `zone` is a junction to `ZoneDir`, without the 32-bit DLLs of
    `build\Release`).

## Runtime data on the Switch
`sdmc:/switch/bo1zm/`: converted `zone/` from `ffconv`, `main/*.iwd` from the user's install, and the shader
cache. The app must run in application mode (title takeover). Applet mode only gives about 400 MB.
